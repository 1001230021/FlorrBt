#include "ant_kingdom_controller.h"
#include "../../../Engine/logger.h"
#include "../../../Shared/tools.h"
#include "../../Module/network_module.h"
#include "../../server.h"
#include "../entities/eou_mob.h"
#include "../entities/mob.h"
#include "../entities/petals/petal.h"
#include "../gamecontext.h"
#include "../gameworld.h"
#include "../player.h"
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <sstream>
#include <string>

namespace
{
constexpr int kEouSlotPheromone = 1;
constexpr int kEouSlotMarkWall = 2;
constexpr int kEouSlotBuildMode = 3;
constexpr float kEouPheromoneCooldown = 5.f;
constexpr float kEouMarkWallCooldown = 3.f;

int ParseIntDefault(std::string_view value, int fallback)
{
    std::stringstream stream{std::string(value)};
    int parsed = 0;
    stream >> parsed;
    return stream.fail() || !stream.eof() ? fallback : parsed;
}
}

bool CAntKingdomController::ValidatePheromoneGroupCount(const std::string& value)
{
    int count = 0;
    std::stringstream stream{value};
    stream >> count;
    return !stream.fail() && stream.eof() && count >= 0 && count <= kMaxPheromoneGroups;
}

CAntKingdomController::CAntKingdomController(CControllerParams params)
    : CAntHelPvpController(std::move(params))
{
    if (params.Contains("pheromone_group_count"))
        m_pheromone_group_count = std::clamp(params.GetInt("pheromone_group_count", 6), 0, kMaxPheromoneGroups);
    else
        m_pheromone_group_count = std::clamp(
            ParseIntDefault(ResolveControllerParamDefault("eou_pheromone_group_count"), 6), 0, kMaxPheromoneGroups);
}

void CAntKingdomController::OnActivate(CGameWorld& world)
{
    CMutableTerrainController::OnActivate(world);
    // Bind wall markers to every in-rule dirt tile; map walls are registered
    // by the board itself. Safe to run again after a hot-reload restore
    // (InsertCell is idempotent per cell).
    m_wall_board.Reload(world);
    InsertWallsForTerrain(world);

    // Skill cards disguised as vanilla petal slots for the client. The build
    // card mirrors the build mode (Soil = storage, AntEgg = hatchery).
    if (m_slot_proxy.SlotCount() == 0)
    {
        m_slot_proxy.SetSlotCount(3);
        SEouSlotProxySlot& pheromone = m_slot_proxy.Slot(0);
        pheromone.skill_id = kEouSlotPheromone;
        pheromone.visual_petal_type = EPetalType::Bubble;
        pheromone.visual_rarity = ERarity::Epic;
        SEouSlotProxySlot& mark_wall = m_slot_proxy.Slot(1);
        mark_wall.skill_id = kEouSlotMarkWall;
        mark_wall.visual_petal_type = EPetalType::Rock;
        mark_wall.visual_rarity = ERarity::Rare;
        SEouSlotProxySlot& build_mode = m_slot_proxy.Slot(2);
        build_mode.skill_id = kEouSlotBuildMode;
        build_mode.visual_petal_type = EPetalType::Soil;
        build_mode.visual_rarity = ERarity::Rare;
    }

    // Neutral starting food: scattered over walkable cells (team 0 = no
    // owner: either team can harvest it).
    SpawnInitialFood(world);
}

void CAntKingdomController::SpawnInitialFood(CGameWorld& world)
{
    const int count = std::max(0, game_config::eou_initial_food_count);
    if (count <= 0) return;

    const int width = m_wall_board.Width();
    const int height = m_wall_board.Height();
    if (width <= 0 || height <= 0) return;

    int placed = 0;
    for (int attempt = 0; attempt < count * 40 && placed < count; ++attempt)
    {
        const int col = static_cast<int>(GetLimitedRng(0.f, static_cast<float>(width)));
        const int row = static_cast<int>(GetLimitedRng(0.f, static_cast<float>(height)));
        if (row < 0 || row >= height || col < 0 || col >= width) continue;
        if (m_wall_board.InBounds(row, col) && m_wall_board.TileSolid(row, col)) continue;
        const florrbt::Vector2f pos{ (static_cast<float>(col) + 0.5f) * m_tile_size,
                                (static_cast<float>(row) + 0.5f) * m_tile_size };
        if (SpawnFoodEntity(world, pos, game_config::eou_initial_food_amount, 0)) ++placed;
    }
    LOG_INFO("ant_kingdom", "initial food placed: " + std::to_string(placed));
}

CEntity* CAntKingdomController::SpawnFoodEntity(CGameWorld& world, florrbt::Vector2f pos, float amount, int food_type)
{
    std::unique_ptr<CMobBase> food = CreateMob(ToMobType(EModMobType::EouFood), &world, pos, ERarity::Common);
    if (!food) return nullptr;
    auto* eou_food = dynamic_cast<CEouFoodMob*>(food.get());
    if (!eou_food)
    {
        LOG_WARN("ant_kingdom", "eou_food is not a CEouFoodMob");
        return nullptr;
    }
    eou_food->SetFoodTotal(std::max(0.f, amount));
    eou_food->SetFoodType(food_type);

    CEntity* entity = world.InsertEntity(std::move(food));
    if (!entity) return nullptr;
    entity->Team() = 0; // neutral: anybody may harvest
    m_food_entities.emplace(entity->Id(), entity->Generation());
    return entity;
}

void CAntKingdomController::OnMobDefeated(CGameWorld& world, CMobBase& mob)
{
    // A slain queen loses the match for her team (despawns via Dissolve do
    // not run through the defeat path, so this only fires on real kills).
    for (auto& [team_id, team] : m_teams)
    {
        if (!team.IsQueen(mob.Id(), mob.Generation())) continue;
        OnQueenSlain(world, team_id);
        break;
    }

    // Every eou mob leaves its corpse: a neutral food entity with the mob's
    // biomass as food total.
    if (IEouMob* eou = dynamic_cast<IEouMob*>(&mob))
    {
        const float biomass = eou->EouBiomass();
        if (biomass > 0.f) SpawnFoodEntity(world, mob.Pos(), biomass, 0);
    }
    COpenController::OnMobDefeated(world, mob);
}

void CAntKingdomController::OnEntityRemoved(CGameWorld& world, CEntity& entity, EEntityRemovalReason reason)
{
    if (const auto it = m_food_entities.find(entity.Id());
        it != m_food_entities.end() && it->second == entity.Generation())
        m_food_entities.erase(it);
    CAntHelPvpController::OnEntityRemoved(world, entity, reason);
}

void CAntKingdomController::OnTick(CGameWorld& world, float dt)
{
    CMutableTerrainController::OnTick(world, dt);
    CAntHelPvpController::OnTick(world, dt);
    // After a hot-reload restore the world's map (and wall grid) is rebuilt:
    // lazily rebuild the board and replay its recorded cells/removals.
    if (!m_wall_board.IsLoaded()) m_wall_board.Reload(world);

    for (auto& [team_id, team] : m_teams)
    {
        team.OnTick(world, dt);
        TickTeamCells(world, team, dt);
    }
    TickSlotProxies(dt);
}

void CAntKingdomController::OnPlayerEntityReady(CGameWorld& world, CPlayer& player, CPlayerFlower& flower,
                                                 EPlayerSpawnReason reason)
{
    CAntHelPvpController::OnPlayerEntityReady(world, player, flower, reason);
    // Pheromone petal slots (slot 0 stays reserved; slots 1..N hold one
    // pheromone petal each, bound to pheromone group 1..N).
    if (HasMatchTeam(player.GetId())) EquipPheromonePetals(flower);
}

void CAntKingdomController::OnPlayerEnteredWorld(CGameWorld& world, CPlayer& player)
{
    CMutableTerrainController::OnPlayerEnteredWorld(world, player);
    CAntHelPvpController::OnPlayerEnteredWorld(world, player);

    // A match team gets its eou team (owning its pheromone groups).
    const int team_id = PlayerTeam(player.GetId());
    if (HasMatchTeam(player.GetId()) && m_teams.find(team_id) == m_teams.end())
    {
        m_teams.emplace(team_id, CTeam(team_id, m_wall_board));
        // The team starts with pheromone groups 1..N (slot-bound petals bind
        // to group == slot index; group 1 is the primary /pheromone target).
        florrbt::Vector2f origin = player.GetEntity() ? player.GetEntity()->Pos() : florrbt::Vector2f{};
        CTeam& team = m_teams.at(team_id);
        for (int i = 1; i <= m_pheromone_group_count; ++i) team.EnsureGroup(i, origin);
        // The team's home is the first player's spawn: the queen stands here
        // and workers ferry food here. Home must exist before the workers
        // spawn so adoption hands them the right ferry point.
        if (!team.HasHomePos()) team.SetHomePos(origin);
        SpawnQueen(world, team, origin);
        const int workers = std::max(0, game_config::eou_initial_worker_count);
        for (int i = 0; i < workers; ++i)
            SpawnEouMob(world, team_id, ToMobType(EModMobType::EouWorker), origin);
        LOG_INFO("ant_kingdom", "team " + std::to_string(team_id) + " created for player " +
                                    std::to_string(player.GetId()) + " with " +
                                    std::to_string(m_pheromone_group_count) + " pheromone groups, " +
                                    std::to_string(workers) + " workers and a queen");
    }
}

void CAntKingdomController::OnPlayerLeftWorld(CGameWorld& world, CPlayer& player)
{
    // Dissolve the player's team before the pvp base forgets the assignment.
    const int team_id = PlayerTeam(player.GetId());
    const auto it = m_teams.find(team_id);
    if (it != m_teams.end())
    {
        it->second.Dissolve(world);
        m_teams.erase(it);
        m_team_build_type.erase(team_id);
        LOG_INFO("ant_kingdom", "team " + std::to_string(team_id) + " dissolved");
    }
    CAntHelPvpController::OnPlayerLeftWorld(world, player);
}

bool CAntKingdomController::HandlePlayerCommand(CGameWorld& world, CPlayer& player,
                                                 const std::vector<std::string>& args)
{
    if (!args.empty())
    {
        std::string name = args[0];
        if (!name.empty() && name.front() == '/') name.erase(name.begin());
        std::transform(name.begin(), name.end(), name.begin(),
                       [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
        if (name == "mark_wall") return HandleMarkWallCommand(world, player, args);
        if (name == "pheromone") return HandlePheromoneCommand(world, player, args);
        if (name == "build") return HandleBuildCommand(world, player, args);
    }
    return COpenController::HandlePlayerCommand(world, player, args);
}

ECouCellType CAntKingdomController::TeamBuildType(int team_id) const
{
    const auto it = m_team_build_type.find(team_id);
    return it == m_team_build_type.end() ? ECouCellType::Storage : it->second;
}

bool CAntKingdomController::HandleBuildCommand(CGameWorld& world, CPlayer& player,
                                               const std::vector<std::string>& args)
{
    if (!HasMatchTeam(player.GetId()))
    {
        SendSystemMessage(world, player, "build: only players with a match team can build");
        return true;
    }
    const int team_id = PlayerTeam(player.GetId());

    const auto type_name = [](ECouCellType type) {
        return type == ECouCellType::Hatchery ? "hatchery" : "storage";
    };

    if (args.size() < 2)
    {
        SendSystemMessage(world, player,
                          std::string("build: current mode is ") + type_name(TeamBuildType(team_id)) +
                              " - usage /build storage|hatchery (defend-throw a pheromone petal to build)");
        return true;
    }

    std::string mode = args[1];
    std::transform(mode.begin(), mode.end(), mode.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    if (mode == "storage" || mode == "s")
        m_team_build_type[team_id] = ECouCellType::Storage;
    else if (mode == "hatchery" || mode == "h")
        m_team_build_type[team_id] = ECouCellType::Hatchery;
    else
    {
        SendSystemMessage(world, player, "build: usage /build storage|hatchery");
        return true;
    }
    SyncBuildModeCard();
    SendSystemMessage(world, player,
                      std::string("build: mode set to ") + type_name(TeamBuildType(team_id)));
    return true;
}

void CAntKingdomController::SyncBuildModeCard()
{
    if (m_slot_proxy.SlotCount() <= 2) return;
    // The card can only show one team's mode; with the two-player room both
    // players share the display, so it mirrors the most recent /build.
    ECouCellType shown = ECouCellType::Storage;
    for (const auto& [team_id, type] : m_team_build_type)
    {
        (void)team_id;
        shown = type;
    }
    SEouSlotProxySlot& card = m_slot_proxy.Slot(2);
    card.visual_petal_type = shown == ECouCellType::Hatchery ? EPetalType::AntEgg : EPetalType::Soil;
}

bool CAntKingdomController::WorkerDigWall(CGameWorld& world, int team, florrbt::Vector2f pos)
{
    if (m_tile_size <= 0.f) return false;
    const int col = static_cast<int>(std::floor(pos.x / m_tile_size));
    const int row = static_cast<int>(std::floor(pos.y / m_tile_size));
    if (!m_wall_board.InBounds(row, col)) return false;
    // The job may have vanished under the worker: the wall already dug (by
    // either team) or the flag withdrawn mid-dig.
    if (!m_wall_board.TileSolid(row, col)) return false;
    if (!m_wall_board.IsDugByTeam(row, col, team)) return false;
    if (!m_wall_board.CommitRemove(row, col, team)) return false;
    // The walls are gone (removed + broadcast by CommitRemove); erase the
    // dirt tile so the client visual matches and the four-tile rule
    // re-normalizes the tunnel edge.
    EraseTerrain(world, m_wall_board.CellCenter(row, col), 1.f);
    return true;
}

CTeam* CAntKingdomController::FindTeam(int team_id)
{
    const auto it = m_teams.find(team_id);
    return it == m_teams.end() ? nullptr : &it->second;
}

const CTeam* CAntKingdomController::FindTeam(int team_id) const
{
    const auto it = m_teams.find(team_id);
    return it == m_teams.end() ? nullptr : &it->second;
}

void CAntKingdomController::AdoptEouMob(CGameWorld& world, CTeam& team, CEntity& entity)
{
    (void)world;
    entity.Team() = team.TeamId();
    // Workers know where to ferry food: the team's home (the queen's spot).
    if (auto* eou = dynamic_cast<IEouMob*>(&entity))
        if (team.HasHomePos()) eou->SetHomePos(team.HomePos());
    // Membership in the primary group (id 1) wires the board and group id;
    // create the group on demand when the team has none yet.
    CEouPheromoneGroup* group = team.GroupCount() > 0 ? team.FindGroup(1) : nullptr;
    if (!group) group = team.CreateGroup(entity.Pos());
    group->AddMember(&entity);
}

CEntity* CAntKingdomController::SpawnEouMob(CGameWorld& world, int team_id, EMobType mob_type, florrbt::Vector2f pos)
{
    CTeam* team = FindTeam(team_id);
    if (!team) return nullptr;

    std::unique_ptr<CMobBase> mob = CreateMob(mob_type, &world, pos, ERarity::Common);
    if (!mob) return nullptr;

    CEntity* entity = world.InsertEntity(std::move(mob));
    if (!entity) return nullptr;
    AdoptEouMob(world, *team, *entity);
    return entity;
}

CEntity* CAntKingdomController::SpawnQueen(CGameWorld& world, CTeam& team, florrbt::Vector2f pos)
{
    std::unique_ptr<CMobBase> queen_mob = CreateMob(ToMobType(EModMobType::EouQueen), &world, pos, ERarity::Common);
    if (!queen_mob) return nullptr;
    if (!dynamic_cast<CEouQueenMob*>(queen_mob.get()))
    {
        LOG_WARN("ant_kingdom", "eou_queen is not a CEouQueenMob");
        return nullptr;
    }

    CEntity* entity = world.InsertEntity(std::move(queen_mob));
    if (!entity) return nullptr;
    entity->Team() = team.TeamId();
    team.SetQueen({ entity->Id(), entity->Generation() });
    LOG_INFO("ant_kingdom", "team " + std::to_string(team.TeamId()) + " queen spawned at " +
                                std::to_string(pos.x) + "," + std::to_string(pos.y));
    return entity;
}

void CAntKingdomController::OnQueenSlain(CGameWorld& world, int team_id)
{
    // Name the winner: with the fixed two-team cap the only other match team
    // still standing takes the match.
    int winner_team = 0;
    for (const auto& [other_id, other_team] : m_teams)
    {
        (void)other_team;
        if (other_id != team_id)
        {
            winner_team = other_id;
            break;
        }
    }

    std::string message = "The queen of team " + std::to_string(team_id) + " has fallen!";
    if (winner_team > 0) message += " Team " + std::to_string(winner_team) + " wins the match!";
    BroadcastMessage(world, message);
    LOG_INFO("ant_kingdom", message);

    const auto it = m_teams.find(team_id);
    if (it != m_teams.end())
    {
        it->second.Dissolve(world);
        m_teams.erase(it);
    }
    m_team_build_type.erase(team_id);
}

bool CAntKingdomController::HandleMarkWallCommand(CGameWorld& world, CPlayer& player,
                                                  const std::vector<std::string>& args)
{
    const int team = PlayerTeam(player.GetId());
    if (!HasMatchTeam(player.GetId()))
    {
        SendSystemMessage(world, player, "mark_wall: only players with a match team can mark walls");
        return true;
    }

    int target_row = -1;
    int target_col = -1;
    if (args.size() == 1)
    {
        // Cell under the player.
        if (CEntity* entity = player.GetEntity())
        {
            const florrbt::Vector2f pos = entity->Pos();
            target_col = static_cast<int>(std::floor(pos.x / m_tile_size));
            target_row = static_cast<int>(std::floor(pos.y / m_tile_size));
        }
    }
    else if (args.size() == 2)
    {
        // "[x,y]" world coordinates.
        const std::string& token = args[1];
        if (token.size() < 5 || token.front() != '[' || token.back() != ']')
        {
            SendSystemMessage(world, player, "mark_wall: usage /mark_wall or /mark_wall [x,y]");
            return true;
        }
        const std::string inner = token.substr(1, token.size() - 2);
        const size_t comma = inner.find(',');
        if (comma == std::string::npos)
        {
            SendSystemMessage(world, player, "mark_wall: usage /mark_wall or /mark_wall [x,y]");
            return true;
        }
        const float x = std::stof(inner.substr(0, comma));
        const float y = std::stof(inner.substr(comma + 1));
        target_col = static_cast<int>(std::floor(x / m_tile_size));
        target_row = static_cast<int>(std::floor(y / m_tile_size));
    }
    else
    {
        SendSystemMessage(world, player, "mark_wall: usage /mark_wall or /mark_wall [x,y]");
        return true;
    }

    if (!m_wall_board.InBounds(target_row, target_col))
    {
        SendSystemMessage(world, player, "mark_wall: target cell is out of bounds");
        return true;
    }

    if (m_wall_board.IsDugByTeam(target_row, target_col, team))
    {
        m_wall_board.ClearTeamFlag(target_row, target_col, team);
        SendSystemMessage(world, player, "mark_wall: removed your flag at [" +
                                             std::to_string(target_col) + "," + std::to_string(target_row) + "]");
    }
    else
    {
        m_wall_board.SetTeamFlag(target_row, target_col, team);
        SendSystemMessage(world, player, "mark_wall: flagged [" + std::to_string(target_col) + "," +
                                             std::to_string(target_row) + "] for team " + std::to_string(team));
    }
    if (m_slot_proxy.SlotCount() > 1)
    {
        SEouSlotProxySlot& slot = m_slot_proxy.Slot(1);
        slot.cooldown_progress = 0.f;
        slot.available = false;
    }
    return true;
}

bool CAntKingdomController::HandlePheromoneCommand(CGameWorld& world, CPlayer& player,
                                                   const std::vector<std::string>& args)
{
    if (!HasMatchTeam(player.GetId()))
    {
        SendSystemMessage(world, player, "pheromone: only players with a match team can place pheromones");
        return true;
    }
    const int team_id = PlayerTeam(player.GetId());
    CTeam* team = FindTeam(team_id);
    if (!team)
    {
        SendSystemMessage(world, player, "pheromone: your team does not exist yet");
        return true;
    }

    florrbt::Vector2f target;
    if (args.size() == 1)
    {
        if (CEntity* entity = player.GetEntity()) target = entity->Pos();
    }
    else if (args.size() == 2)
    {
        const std::string& token = args[1];
        if (token.size() < 5 || token.front() != '[' || token.back() != ']')
        {
            SendSystemMessage(world, player, "pheromone: usage /pheromone or /pheromone [x,y]");
            return true;
        }
        const std::string inner = token.substr(1, token.size() - 2);
        const size_t comma = inner.find(',');
        if (comma == std::string::npos)
        {
            SendSystemMessage(world, player, "pheromone: usage /pheromone or /pheromone [x,y]");
            return true;
        }
        target.x = std::stof(inner.substr(0, comma));
        target.y = std::stof(inner.substr(comma + 1));
    }
    else
    {
        SendSystemMessage(world, player, "pheromone: usage /pheromone or /pheromone [x,y]");
        return true;
    }

    // The primary group (first created) receives the pheromone target.
    CEouPheromoneGroup* group = team->GroupCount() > 0 ? team->FindGroup(1) : nullptr;
    if (!group) group = team->CreateGroup(target);
    else group->SetPheromoneTarget(target);
    if (m_slot_proxy.SlotCount() > 0)
    {
        SEouSlotProxySlot& slot = m_slot_proxy.Slot(0);
        slot.cooldown_progress = 0.f;
        slot.available = false;
    }
    SendSystemMessage(world, player, "pheromone: team " + std::to_string(team_id) + " target set to [" +
                                         std::to_string(target.x) + "," + std::to_string(target.y) + "]");
    return true;
}

void CAntKingdomController::TickSlotProxies(float dt)
{
    for (int i = 0; i < m_slot_proxy.SlotCount(); ++i)
    {
        SEouSlotProxySlot& slot = m_slot_proxy.Slot(i);
        if (slot.available) continue;
        const float duration = slot.skill_id == kEouSlotPheromone ? kEouPheromoneCooldown : kEouMarkWallCooldown;
        slot.cooldown_progress = std::min(1.f, slot.cooldown_progress + dt / std::max(duration, 0.001f));
        if (slot.cooldown_progress >= 1.f)
        {
            slot.cooldown_progress = 1.f;
            slot.available = true;
        }
    }
}

void CAntKingdomController::EquipPheromonePetals(CPlayerFlower& flower)
{
    const CPetalPrototype* proto = FindPetalPrototype(ToPetalType(EModPetalType::EouPheromone));
    if (!proto) return;
    const int count = m_pheromone_group_count;
    if (count <= 0) return;

    // Slot 0 stays reserved (empty / proxy skill card); slots 1..N carry the
    // pheromone petals in rarity order Common..Mythic (then Ultra/Super/
    // Eternal/Unique for larger N). Two more empty slots trail the petals so
    // the mark-wall and build-mode proxy cards have somewhere to show (the
    // snapshot service maps proxy cards onto empty slots in order).
    static constexpr ERarity kPheromoneRarities[kMaxPheromoneGroups] = {
        ERarity::Common,  ERarity::Unusual, ERarity::Rare,      ERarity::Epic,
        ERarity::Legendary, ERarity::Mythic, ERarity::Ultra,    ERarity::Super,
        ERarity::Eternal, ERarity::Unique,
    };
    flower.SetPetalSlotCapacity(count + 3);
    flower.SetPetalSlotCount(count + 3);
    for (int i = 1; i <= count; ++i)
        flower.LoadPetalSlot(i, proto, kPheromoneRarities[i - 1]);
}

void CAntKingdomController::OnPheromonePetalStopped(CGameWorld& world, int team_id, int group_id,
                                                     florrbt::Vector2f pos, bool defend)
{
    CTeam* team = FindTeam(team_id);
    if (!team) return;

    if (!defend)
    {
        // Attack release: the resting point guides the bound pheromone group.
        if (group_id <= 0) return;
        CEouPheromoneGroup* group = team->FindGroup(group_id);
        if (!group) return;
        group->SetPheromoneTarget(pos);
        return;
    }

    // Defend release: toggle the cell under the resting point.
    if (CEouCellMob* cell = team->GetCellAt(world, pos))
    {
        DestroyCellEntity(world, cell, team_id);
        return;
    }
    // Cells live in dug-out space: refuse to build inside a standing wall.
    if (m_tile_size > 0.f)
    {
        const int col = static_cast<int>(std::floor(pos.x / m_tile_size));
        const int row = static_cast<int>(std::floor(pos.y / m_tile_size));
        if (m_wall_board.InBounds(row, col) && m_wall_board.TileSolid(row, col))
        {
            SendTeamMessage(world, team_id, "cannot build inside a wall - dig it open first");
            return;
        }
        // Snap to the cell center so the grid connectivity reads cleanly.
        if (m_wall_board.InBounds(row, col)) pos = m_wall_board.CellCenter(row, col);
    }
    const ECouCellType build_type = TeamBuildType(team_id);
    const EMobType egg_type = build_type == ECouCellType::Hatchery ? ToMobType(EModMobType::EouWorker) : EMobType::None;
    if (SpawnCellEntity(world, team_id, build_type, pos, egg_type) != nullptr)
        SendTeamMessage(world, team_id,
                        build_type == ECouCellType::Hatchery ? "hatchery built" : "storage built");
}

void CAntKingdomController::DepositWorkerFood(CGameWorld& world, int team_id, florrbt::Vector2f pos, float amount)
{
    CTeam* team = FindTeam(team_id);
    if (!team || amount <= 0.f) return;
    // Storage cells first; whatever they cannot take (no cells yet, or the
    // nearest is full) lands on the queen's stash so no carried food is lost.
    const float stored = team->DepositToNearestStorage(world, pos, amount);
    if (stored < amount) team->DepositFood(amount - stored);
}

std::vector<SControllerParamInfo> CAntKingdomController::ParamSchema() const
{
    std::vector<SControllerParamInfo> schema = CAntHelPvpController::ParamSchema();
    schema.push_back({
        "pheromone_group_count",
        EControllerParamType::Int,
        "eou_pheromone_group_count",
        "pheromone groups per team and pheromone petal slots per player (0..10)",
        &CAntKingdomController::ValidatePheromoneGroupCount,
    });
    return schema;
}

void CAntKingdomController::SendSystemMessage(CGameWorld& world, CPlayer& player, const std::string& message) const
{
    CGameContext* context = world.GameContext();
    if (!context) return;

    CServer::SChatEntry entry;
    entry.flag = EChatFlag::Server;
    entry.player_id = 0;
    entry.target_player_id = static_cast<int>(player.GetId());
    entry.player_name = "Server";
    entry.message = message;
    entry.system_time = static_cast<uint32_t>(
        std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch())
            .count());
    context->Network().SendChatToPlayer(player, entry);
}

void CAntKingdomController::InsertWallsForTerrain(CGameWorld& world)
{
    (void)world;
    if (!m_terrain_loaded) return;

    int inserted = 0;
    for (int row = 0; row < m_height; ++row)
    {
        for (int col = 0; col < m_width; ++col)
        {
            // In-rule tile: a dirt variant the four-tile rule can express.
            if (CurrentDirtMask(row, col) < 0) continue;
            if (m_wall_board.InsertCell(row, col)) ++inserted;
        }
    }
    LOG_INFO("ant_kingdom", "bound " + std::to_string(inserted) + " wall markers to in-rule dirt tiles");
}

void CAntKingdomController::CaptureSnapshot(CSnapshotWriter& writer) const
{
    CMutableTerrainController::CaptureSnapshot(writer);
    CAntHelPvpController::CaptureSnapshot(writer);
    m_wall_board.CaptureSnapshot(writer);

    writer.Field("eou_team_count", static_cast<std::uint32_t>(m_teams.size()));
    std::uint32_t index = 0;
    for (const auto& [team_id, team] : m_teams)
    {
        writer.Field("eou_team_id_" + std::to_string(index), team_id);
        CJsonOwner team_object = MakeJsonObject();
        CSnapshotWriter team_writer(team_object.get());
        team.CaptureSnapshot(team_writer);
        writer.Node("eou_team_" + std::to_string(team_id), team_object.release());
        writer.Field("eou_build_type_" + std::to_string(team_id),
                     static_cast<int>(TeamBuildType(team_id)));
        ++index;
    }
}

bool CAntKingdomController::RestoreSnapshot(const CSnapshotReader& reader, std::uint32_t version, std::string& error)
{
    if (!CMutableTerrainController::RestoreSnapshot(reader, version, error)) return false;
    if (!CAntHelPvpController::RestoreSnapshot(reader, version, error)) return false;
    if (version > SnapshotVersion())
    {
        error = "Unsupported ant-kingdom controller snapshot version " + std::to_string(version);
        return false;
    }
    m_wall_board.RestoreSnapshot(reader, version, error);

    m_teams.clear();
    m_team_build_type.clear();
    const std::uint32_t count = reader.UInt32("eou_team_count", 0);
    for (std::uint32_t i = 0; i < count; ++i)
    {
        const int team_id = reader.Int("eou_team_id_" + std::to_string(i), 0);
        const json_t* node = reader.Node("eou_team_" + std::to_string(team_id));
        if (!node || !json_is_object(node) || team_id <= 0) continue;
        CSnapshotReader team_reader(node);
        auto team = std::make_unique<CTeam>(team_id, m_wall_board);
        if (!team->RestoreSnapshot(team_reader, version, error)) return false;
        m_teams.emplace(team_id, std::move(*team));
        const int build_type = reader.Int("eou_build_type_" + std::to_string(team_id),
                                          static_cast<int>(ECouCellType::Storage));
        m_team_build_type[team_id] =
            build_type == static_cast<int>(ECouCellType::Hatchery) ? ECouCellType::Hatchery
                                                                   : ECouCellType::Storage;
    }
    SyncBuildModeCard();
    return true;
}
CEntity* CAntKingdomController::SpawnCellEntity(CGameWorld& world, int team_id, ECouCellType type,
                                                florrbt::Vector2f pos, EMobType egg_type)
{
    CTeam* team = FindTeam(team_id);
    if (!team) return nullptr;

    std::unique_ptr<CMobBase> cell_mob = CreateMob(ToMobType(EModMobType::EouCell), &world, pos, ERarity::Common);
    if (!cell_mob) return nullptr;
    auto* cell = dynamic_cast<CEouCellMob*>(cell_mob.get());
    if (!cell)
    {
        LOG_WARN("ant_kingdom", "eou_cell is not a CEouCellMob");
        return nullptr;
    }
    cell->SetCellType(type);
    cell->SetEggType(egg_type);
    cell->SetTeamId(team_id);

    CEntity* entity = world.InsertEntity(std::move(cell_mob));
    if (!entity) return nullptr;
    entity->Team() = team_id;

    // Join the connectivity set: unifies with same-type neighbours.
    auto* placed = dynamic_cast<CEouCellMob*>(entity);
    team->AddCell(world, *placed);
    if (!team->HasHomePos()) team->SetHomePos(pos);

    if (type == ECouCellType::Hatchery) SpawnOrStandbyEgg(world, *placed, team);
    LOG_INFO("ant_kingdom", "team " + std::to_string(team_id) + " cell " +
                                (type == ECouCellType::Hatchery ? "hatchery" : "storage") + " at " +
                                std::to_string(pos.x) + "," + std::to_string(pos.y));
    return entity;
}

void CAntKingdomController::DestroyCellEntity(CGameWorld& world, CEntity* cell, int team_id)
{
    CTeam* team = FindTeam(team_id);
    if (!cell || !team) return;
    if (auto* cell_mob = dynamic_cast<CEouCellMob*>(cell))
        team->RemoveCell(world, *cell_mob);
    cell->MarkForDestroy(EEntityRemovalReason::Despawned);
    SendTeamMessage(world, team_id, "cell removed");
}

CEntity* CAntKingdomController::SpawnOrStandbyEgg(CGameWorld& world, CEouCellMob& cell, CTeam* team)
{
    if (!team) return nullptr;
    EMobType egg_type = cell.EggType();
    if (egg_type == EMobType::None) egg_type = ToMobType(EModMobType::EouWorker);

    std::unique_ptr<CMobBase> egg_mob = CreateMob(ToMobType(EModMobType::EouEgg), &world, cell.Pos(), ERarity::Common);
    if (!egg_mob) return nullptr;
    auto* egg = dynamic_cast<CEouEggMob*>(egg_mob.get());
    if (!egg)
    {
        LOG_WARN("ant_kingdom", "eou_egg is not a CEouEggMob");
        return nullptr;
    }
    const int team_id = team->TeamId();
    egg->SetHatchType(egg_type);
    egg->SetTeamId(team_id);
    egg->SetBinding(cell.Id(), cell.Generation());

    CEntity* entity = world.InsertEntity(std::move(egg_mob));
    if (!entity) return nullptr;
    entity->Team() = team_id;
    if (auto* placed = dynamic_cast<CEouEggMob*>(entity)) cell.SetEgg(placed->Id(), placed->Generation());
    return entity;
}

void CAntKingdomController::TickTeamCells(CGameWorld& world, CTeam& team, float dt)
{
    (void)dt;
    const int team_id = team.TeamId();
    for (const SEouEntityHandle& handle : team.Cells().AllCells())
    {
        CEntity* entity = world.GetEntity(handle.id, handle.generation);
        auto* cell = dynamic_cast<CEouCellMob*>(entity);
        if (!cell) continue;

        if (cell->EggType() == EMobType::None) continue; // storage cells

        // Hatchery: keep an egg alive.
        CEouEggMob* egg = nullptr;
        if (cell->HasEgg())
        {
            CEntity* egg_entity = world.GetEntity(cell->EggId(), cell->EggGeneration());
            egg = dynamic_cast<CEouEggMob*>(egg_entity);
            if (!egg || egg->IsMarkedForDes())
            {
                cell->ClearEgg();
                egg = nullptr;
            }
        }
        if (!egg)
        {
            cell->TickEggRetry(dt);
            if (cell->EggRetryTimer() <= 0.f)
            {
                cell->ResetEggRetry();
                CEntity* fresh = SpawnOrStandbyEgg(world, *cell, &team);
                if (fresh) cell->SetEgg(fresh->Id(), fresh->Generation());
            }
            continue;
        }

        // Egg grown? Hatch once the team can pay the food cost.
        if (!egg->Grown()) continue;
        const float cost = game_config::eou_egg_hatch_food_cost;
        if (team.TryConsumeFood(world, cost))
        {
            std::unique_ptr<CMobBase> hatch = CreateMob(egg->HatchType(), &world, egg->Pos(), ERarity::Common);
            if (hatch)
            {
                // Adoption gives the hatchling its colony: team, home point
                // and primary-group membership (which wires the path board).
                if (CEntity* hatched = world.InsertEntity(std::move(hatch)))
                    AdoptEouMob(world, team, *hatched);
            }
            SendTeamMessage(world, team_id, "egg hatched");
            cell->ClearEgg();
            egg->MarkForDestroy(EEntityRemovalReason::Despawned);
        }
    }
}

void CAntKingdomController::SendTeamMessage(CGameWorld& world, int team_id, const std::string& message)
{
    CGameContext* context = world.GameContext();
    if (!context) return;
    for (const auto& player : context->Players())
    {
        if (!player) continue;
        if (PlayerTeam(player->GetId()) != team_id) continue;
        SendSystemMessage(world, *player, message);
    }
}

void CAntKingdomController::BroadcastMessage(CGameWorld& world, const std::string& message)
{
    CGameContext* context = world.GameContext();
    if (!context) return;
    for (const auto& player : context->Players())
    {
        if (!player) continue;
        SendSystemMessage(world, *player, message);
    }
}
