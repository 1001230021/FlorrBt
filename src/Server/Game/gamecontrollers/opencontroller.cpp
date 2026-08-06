#include "opencontroller.h"
#include "open_reward_service.h"
#include "../../../Engine/logger.h"
#include "../../HotReload/snapshot_archive.h"
#include "../../../Shared/game_config.h"
#include "../entities/flower.h"
#include "../entities/mob.h"
#include "../gamecontext.h"
#include "../gameworld.h"
#include "../player.h"
#include "../zone_mob_tools.h"
#include <algorithm>
#include <cmath>
#include <cctype>
#include <string>
#include <unordered_set>

namespace
{
std::string LowerOpenControllerText(std::string_view text)
{
    std::string result;
    result.reserve(text.size());
    for (char ch : text)
        result.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
    return result;
}

std::optional<ERarity> ParseOpenControllerRarity(std::string_view text)
{
    const std::string target = LowerOpenControllerText(text);
    for (int value = static_cast<int>(ERarity::Common); value <= static_cast<int>(ERarity::Exotic); ++value)
    {
        const ERarity rarity = static_cast<ERarity>(value);
        if (LowerOpenControllerText(GetRarityName(rarity)) == target) return rarity;
    }
    if (target == "ex") return ERarity::Exotic;
    return std::nullopt;
}

bool HasPreciseSpawnMob(CGameWorld& world, const FlorrBtMap::PreciseSpawn& spawn, EMobType type, ERarity rarity)
{
    constexpr float position_epsilon = 0.5f;
    bool found = false;
    world.ForEachEntity([&](CEntity* entity) {
        if (found || !entity || entity->m_is_marked_for_des) return;
        const auto* mob = dynamic_cast<const CMobBase*>(entity);
        if (!mob || mob->IsDead() || mob->GetMobType() != type || mob->GetRarity() != rarity) return;
        if (DistanceSq(mob->m_pos, { spawn.x, spawn.y }) <= position_epsilon * position_epsilon) found = true;
    });
    return found;
}
} // namespace

void COpenController::OnActivate(CGameWorld& world)
{
    InitializePreciseSpawns(world);
}

void COpenController::InitializePreciseSpawns(CGameWorld& world)
{
    if (m_precise_spawns_initialized) return;
    const FlorrBtMap* map = world.GetMap();
    if (!map || map->precise_spawns.empty())
    {
        m_precise_spawns_initialized = true;
        return;
    }

    // Worlds are constructed before RegisterMobs runs. Defer until the first tick in that case.
    if (!FindMobPrototype(EMobType::NormalFlower)) return;

    for (const FlorrBtMap::PreciseSpawn& spawn : map->precise_spawns)
    {
        EMobType mob_type = EMobType::None;
        if (!TryParseZoneMobType(spawn.type, mob_type))
        {
            LOG_WARN("opencontroller", "Invalid precise_spawn mob type '" + spawn.type + "' in " +
                                             world.GetMapPath());
            continue;
        }

        const std::optional<ERarity> rarity = ParseOpenControllerRarity(spawn.rarity);
        if (!rarity)
        {
            LOG_WARN("opencontroller", "Invalid precise_spawn rarity '" + spawn.rarity + "' in " +
                                             world.GetMapPath());
            continue;
        }
        if (HasPreciseSpawnMob(world, spawn, mob_type, *rarity)) continue;

        auto mob = CreateMob(mob_type, &world, { spawn.x, spawn.y }, *rarity, false);
        if (!mob)
        {
            LOG_WARN("opencontroller", "Failed to create precise_spawn " + std::string(GetRarityName(*rarity)) +
                                             " " + std::string(GetMobTypeName(mob_type)));
            continue;
        }
        CMobBase* raw_mob = dynamic_cast<CMobBase*>(world.InsertEntity(std::move(mob)));
        if (raw_mob) COpenRewardService::OnMobSpawned(world, *raw_mob);
    }
    m_precise_spawns_initialized = true;
}

void COpenController::UpdateSpawnWave(float dt)
{
    const float period = game_config::open_spawn_wave_period;
    const float minimum = std::clamp(game_config::open_spawn_wave_min_multiplier, 0.f, 1.f);
    const float maximum = std::clamp(game_config::open_spawn_wave_max_multiplier, minimum, 1.f);

    if (std::isfinite(dt) && dt > 0.f) m_spawn_wave_time += dt;
    if (!std::isfinite(period) || period <= game_config::entity_collision_epsilon)
    {
        m_spawn_wave_time = 0.f;
        m_spawn_density_multiplier = (minimum + maximum) * 0.5f;
        return;
    }

    if (!std::isfinite(m_spawn_wave_time)) m_spawn_wave_time = 0.f;
    m_spawn_wave_time = std::fmod(m_spawn_wave_time, period);
    if (m_spawn_wave_time < 0.f) m_spawn_wave_time += period;

    const float phase = 2.f * game_config::pi * m_spawn_wave_time / period;
    const float midpoint = (minimum + maximum) * 0.5f;
    const float amplitude = (maximum - minimum) * 0.5f;
    m_spawn_density_multiplier = std::clamp(midpoint + amplitude * std::sin(phase), minimum, maximum);
}

void COpenController::CaptureSnapshot(CSnapshotWriter& writer) const
{
    m_spawn_director.CaptureSnapshot(writer);
    writer.Field("spawn_wave_time", m_spawn_wave_time);
    writer.Field("spawn_density_multiplier", m_spawn_density_multiplier);

    CJsonOwner squads = MakeJsonArray();
    for (const auto& [player_id, parent_id] : m_squad_parent)
    {
        CJsonOwner entry = MakeJsonObject();
        CSnapshotWriter entry_writer(entry.get());
        entry_writer.Field("player_id", player_id);
        entry_writer.Field("parent_id", parent_id);
        AppendJson(squads.get(), entry.release());
    }
    writer.Node("squads", squads.release());
}

bool COpenController::RestoreSnapshot(const CSnapshotReader& reader, std::uint32_t version, std::string& error)
{
    if (version > SnapshotVersion())
    {
        error = "Unsupported open controller snapshot version " + std::to_string(version);
        return false;
    }

    if (!m_spawn_director.RestoreSnapshot(reader, error)) return false;

    m_spawn_wave_time = reader.Float("spawn_wave_time", 0.f);
    m_spawn_density_multiplier = reader.Float("spawn_density_multiplier", 0.6f);
    UpdateSpawnWave(0.f);

    m_squad_parent.clear();
    m_pruned_squad_player_ids.clear();
    m_has_pruned_squads = false;
    if (const json_t* entries = reader.Node("squads"); json_is_array(entries))
    {
        const size_t count = json_array_size(entries);
        for (size_t i = 0; i < count; ++i)
        {
            CSnapshotReader entry(json_array_get(entries, i));
            const std::uint32_t player_id = entry.UInt32("player_id");
            const std::uint32_t parent_id = entry.UInt32("parent_id");
            if (player_id != 0 && parent_id != 0) m_squad_parent[player_id] = parent_id;
        }
    }
    return true;
}

void COpenController::OnTick(CGameWorld& world, float dt)
{
    InitializePreciseSpawns(world);
    PruneSquads(world);
    UpdateSpawnWave(dt);
    m_spawn_director.Tick(world, dt, m_spawn_density_multiplier, &COpenRewardService::OnMobSpawned);
}

void COpenController::SpawnMobs(CGameWorld& world)
{
    UpdateSpawnWave(0.f);
    m_spawn_director.SpawnMobs(world, m_spawn_density_multiplier, &COpenRewardService::OnMobSpawned);
}

std::optional<sf::Vector2f> COpenController::SelectPlayerSpawn(CGameWorld& world, CPlayer& player,
                                                               EPlayerSpawnReason reason)
{
    return m_spawn_director.SelectPlayerSpawn(world, player.GetUseNewPlayerSpawn(), reason);
}

void COpenController::ModifyTalentContext(CGameWorld&, CPlayer*, ETalentEvent, STalentContext&) {}

bool COpenController::IsSquadPlayerInWorld(const CGameWorld& world, const CPlayer& player) const
{
    if (!player.IsAuthenticated() || !player.IsConnected()) return false;
    const CEntity* entity = player.GetEntity();
    return entity && entity->GameWorld() == &world;
}

void COpenController::EnsureSquadPlayer(std::uint32_t player_id) { m_squad_parent.try_emplace(player_id, player_id); }

std::uint32_t COpenController::FindSquadRoot(std::uint32_t player_id)
{
    auto node = m_squad_parent.find(player_id);
    if (node == m_squad_parent.end()) return player_id;

    std::vector<std::uint32_t> path;
    std::uint32_t current = player_id;
    while (true)
    {
        path.push_back(current);
        auto current_node = m_squad_parent.find(current);
        if (current_node == m_squad_parent.end()) break;

        const std::uint32_t parent = current_node->second;
        if (parent == current) break;
        if (std::find(path.begin(), path.end(), parent) != path.end())
        {
            current = player_id;
            m_squad_parent[player_id] = player_id;
            break;
        }
        current = parent;
    }

    for (std::uint32_t id : path)
    {
        if (auto it = m_squad_parent.find(id); it != m_squad_parent.end()) it->second = current;
    }
    return current;
}

std::vector<std::uint32_t> COpenController::GetSquadMemberIds(std::uint32_t root_id)
{
    std::vector<std::uint32_t> members;
    for (const auto& [player_id, _] : m_squad_parent)
    {
        if (FindSquadRoot(player_id) == root_id) members.push_back(player_id);
    }
    std::sort(members.begin(), members.end());
    return members;
}

void COpenController::PruneSquads(CGameWorld& world)
{
    CGameContext* context = world.GameContext();
    if (!context)
    {
        m_squad_parent.clear();
        m_pruned_squad_player_ids.clear();
        m_has_pruned_squads = true;
        return;
    }

    if (m_has_pruned_squads)
    {
        size_t active_index = 0;
        bool unchanged = true;
        for (const auto& player : context->Players())
        {
            if (!player || !IsSquadPlayerInWorld(world, *player)) continue;
            if (active_index >= m_pruned_squad_player_ids.size() ||
                m_pruned_squad_player_ids[active_index] != player->GetId())
            {
                unchanged = false;
                break;
            }
            ++active_index;
        }
        if (unchanged && active_index == m_pruned_squad_player_ids.size()) return;
    }

    m_pruned_squad_player_ids.clear();
    m_pruned_squad_player_ids.reserve(context->Players().size());
    std::unordered_set<std::uint32_t> active_id_set;
    active_id_set.reserve(context->Players().size());
    for (const auto& player : context->Players())
    {
        if (!player || !IsSquadPlayerInWorld(world, *player)) continue;
        m_pruned_squad_player_ids.push_back(player->GetId());
        active_id_set.insert(player->GetId());
        EnsureSquadPlayer(player->GetId());
    }

    std::unordered_map<std::uint32_t, std::vector<std::uint32_t>> components;
    components.reserve(m_pruned_squad_player_ids.size());
    for (std::uint32_t player_id : m_pruned_squad_player_ids)
        components[FindSquadRoot(player_id)].push_back(player_id);

    m_squad_parent.clear();
    for (auto& [old_root, members] : components)
    {
        if (members.empty()) continue;
        std::uint32_t new_root =
            active_id_set.contains(old_root) ? old_root : *std::min_element(members.begin(), members.end());
        for (std::uint32_t player_id : members)
            m_squad_parent.emplace(player_id, new_root);
    }
    m_has_pruned_squads = true;
}

std::uint32_t COpenController::GetSquadRootId(CGameWorld& world, CPlayer& player)
{
    PruneSquads(world);
    if (!IsSquadPlayerInWorld(world, player)) return player.GetId();
    EnsureSquadPlayer(player.GetId());
    return FindSquadRoot(player.GetId());
}

std::vector<CPlayer*> COpenController::GetSquadPlayerList(CGameWorld& world, CPlayer& player)
{
    PruneSquads(world);
    if (!IsSquadPlayerInWorld(world, player)) return {};

    EnsureSquadPlayer(player.GetId());
    const std::vector<std::uint32_t> member_ids = GetSquadMemberIds(FindSquadRoot(player.GetId()));
    CGameContext* context = world.GameContext();
    if (!context) return {};

    std::unordered_map<std::uint32_t, CPlayer*> players_by_id;
    for (const auto& candidate : context->Players())
    {
        if (candidate && IsSquadPlayerInWorld(world, *candidate))
            players_by_id.emplace(candidate->GetId(), candidate.get());
    }

    std::vector<CPlayer*> players;
    players.reserve(member_ids.size());
    for (std::uint32_t member_id : member_ids)
    {
        if (auto it = players_by_id.find(member_id); it != players_by_id.end()) players.push_back(it->second);
    }
    return players;
}

bool COpenController::TrySquad(CGameWorld& world, CPlayer& joining_player, CPlayer& target_player)
{
    PruneSquads(world);
    if (&joining_player == &target_player || !IsSquadPlayerInWorld(world, joining_player) ||
        !IsSquadPlayerInWorld(world, target_player))
        return false;

    EnsureSquadPlayer(joining_player.GetId());
    EnsureSquadPlayer(target_player.GetId());

    const std::uint32_t joining_root = FindSquadRoot(joining_player.GetId());
    const std::uint32_t target_root = FindSquadRoot(target_player.GetId());
    if (joining_root == target_root || GetSquadMemberIds(joining_root).size() != 1) return false;

    const size_t max_squad_size = std::max<size_t>(1, game_config::open_controller_max_squad_size);
    if (GetSquadMemberIds(target_root).size() >= max_squad_size) return false;

    m_squad_parent[joining_player.GetId()] = target_root;
    return true;
}

bool COpenController::TryLeaveSquad(CGameWorld& world, CPlayer& player)
{
    PruneSquads(world);
    if (!IsSquadPlayerInWorld(world, player)) return false;

    EnsureSquadPlayer(player.GetId());
    const std::uint32_t root = FindSquadRoot(player.GetId());
    const std::vector<std::uint32_t> members = GetSquadMemberIds(root);
    if (members.size() <= 1) return false;

    if (player.GetId() != root)
    {
        m_squad_parent[player.GetId()] = player.GetId();
        return true;
    }

    auto successor = std::find_if(members.begin(), members.end(),
                                  [&player](std::uint32_t member_id) { return member_id != player.GetId(); });
    if (successor == members.end()) return false;

    for (std::uint32_t member_id : members)
        m_squad_parent[member_id] = member_id == player.GetId() ? player.GetId() : *successor;
    return true;
}

void COpenController::OnEntityRemoved(CGameWorld&, CEntity& entity, EEntityRemovalReason)
{
    m_spawn_director.OnEntityRemoved(entity);
}

void COpenController::OnMobDefeated(CGameWorld& world, CMobBase& mob)
{
    COpenRewardService::OnMobDefeated(*this, world, mob);
}
