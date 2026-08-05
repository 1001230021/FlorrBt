#include "opencontroller.h"
#include "open_reward_service.h"
#include "../../HotReload/snapshot_archive.h"
#include "../../../Shared/game_config.h"
#include "../entities/flower.h"
#include "../gamecontext.h"
#include "../gameworld.h"
#include "../player.h"
#include <algorithm>
#include <unordered_set>

void COpenController::CaptureSnapshot(CSnapshotWriter& writer) const
{
    m_spawn_director.CaptureSnapshot(writer);

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
    PruneSquads(world);
    m_spawn_director.Tick(world, dt, &COpenRewardService::OnMobSpawned);
}

void COpenController::SpawnMobs(CGameWorld& world)
{
    m_spawn_director.SpawnMobs(world, &COpenRewardService::OnMobSpawned);
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
