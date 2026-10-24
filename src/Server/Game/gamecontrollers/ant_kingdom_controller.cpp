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

bool ParseWorldPoint(const std::string& token, florrbt::Vector2f& point)
{
    std::istringstream input(token);
    char open = 0, comma = 0, close = 0;
    if (!(input >> open >> point.x >> comma >> point.y >> close)) return false;
    input >> std::ws;
    return open == '[' && comma == ',' && close == ']' && input.eof() &&
           std::isfinite(point.x) && std::isfinite(point.y);
}

void ConfigureColonyEntity(CEntity& entity)
{
    entity.TickMode() = EEntityTickMode::Always;
    entity.AddTag(EEntityTag::NoDefeatRewards);
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
    : CAntHelPvpController(params)
{
    if (params.Contains("pheromone_group_count"))
        m_pheromone_group_count = std::clamp(params.GetInt("pheromone_group_count", 6), 0, kMaxPheromoneGroups);
    else
        m_pheromone_group_count = std::clamp(
            ParseIntDefault(ResolveControllerParamDefault("eou_pheromone_group_count"), 6), 0, kMaxPheromoneGroups);
    InitializeSlotProxies();
}

void CAntKingdomController::OnActivate(CGameWorld& world)
{
    CMutableTerrainController::OnActivate(world);
    // Bind wall markers to every in-rule dirt tile; map walls are registered
    // by the board itself. Safe to run again after a hot-reload restore
    // (InsertCell is idempotent per cell).
    m_wall_board.Reload(world);
    InsertWallsForTerrain(world);

    SpawnInitialFood(world);
    m_food_replenish_timer = std::max(1.f, game_config::eou_food_replenish_interval);
}

void CAntKingdomController::OnDeactivate(CGameWorld& world)
{
    m_wall_board.DetachWorld(world);
    CMutableTerrainController::OnDeactivate(world);
}

void CAntKingdomController::InitializeSlotProxies()
{
    // Skill cards disguised as vanilla petal slots for the client. The build
    // card mirrors the build mode (Soil = storage, AntEgg = worker hatchery,
    // Stinger = soldier hatchery).
    if (m_slot_proxy.GetSlotCount() == 0)
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

}

void CAntKingdomController::SpawnInitialFood(CGameWorld& world)
{
    const int count = std::clamp(game_config::eou_initial_food_count, 0, 128);
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
    if (!std::isfinite(amount) || amount <= 0.f) return nullptr;
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
    ConfigureColonyEntity(*food);

    CEntity* entity = world.InsertEntity(std::move(food));
    if (!entity) return nullptr;
    entity->Team() = 0; // neutral: anybody may harvest
    m_food_entities.insert(entity->GetHandle());
    return entity;
}

int CAntKingdomController::SpawnColonyFood(CGameWorld& world, const CTeam& team, int count)
{
    if (!team.HasHomePos() || !m_wall_board.IsLoaded()) return 0;
    count = std::clamp(count, 0, 8);
    const float tile = m_wall_board.TileSize();
    const float radius = std::max(0.f, game_config::eou_food_mob_radius);
    const float min_distance = std::max(100.f, game_config::eou_queen_radius + radius + 32.f);
    const float max_distance = std::max(min_distance, std::min(480.f, game_config::default_horizon * 0.8f));
    int placed = 0;
    for (int attempt = 0; attempt < 80 && placed < count; ++attempt)
    {
        const float angle = 2.f * game_config::pi * static_cast<float>(attempt) / 17.f;
        const float distance = min_distance + (max_distance - min_distance) * static_cast<float>(attempt % 5) / 4.f;
        const florrbt::Vector2f pos = team.HomePos() + florrbt::Vector2f{ std::cos(angle), std::sin(angle) } * distance;
        bool clear = tile > 0.f;
        for (int dy = -1; dy <= 1 && clear; ++dy)
        for (int dx = -1; dx <= 1 && clear; ++dx)
        {
            const int row = static_cast<int>(std::floor((pos.y + dy * (radius + 8.f)) / tile));
            const int col = static_cast<int>(std::floor((pos.x + dx * (radius + 8.f)) / tile));
            clear = m_wall_board.InBounds(row, col) && !m_wall_board.TileSolid(row, col);
        }
        if (!clear || !eou_has_line_of_sight(m_wall_board, team.HomePos(), pos)) continue;
        for (const SEntityHandle& food_ref : m_food_entities)
        {
            const auto* food = world.ResolveEntity(food_ref);
            if (food && !food->IsMarkedForDes() && EouDistance(pos, food->Pos()) < radius * 2.f + 40.f)
            { clear = false; break; }
        }
        if (clear && SpawnFoodEntity(world, pos, game_config::eou_initial_food_amount, 0)) ++placed;
    }
    return placed;
}

void CAntKingdomController::ReplenishFood(CGameWorld& world)
{
    std::erase_if(m_food_entities, [&world](const SEntityHandle& food_ref) {
        auto* food = dynamic_cast<CEouFoodMob*>(world.ResolveEntity(food_ref));
        return !food || food->IsMarkedForDes() || food->IsDead() || food->RemainingFood() <= 0.f;
    });
    const int near_cap = std::clamp(game_config::eou_colony_food_count, 0, 8);
    const size_t total_cap = static_cast<size_t>(std::clamp(game_config::eou_initial_food_count, 0, 128)) +
                             m_teams.size() * static_cast<size_t>(near_cap);
    for (const auto& [team_id, team] : m_teams)
    {
        if (m_food_entities.size() >= total_cap) break;
        int nearby = 0;
        for (const SEntityHandle& food_ref : m_food_entities)
        {
            const auto* food = world.ResolveEntity(food_ref);
            if (food && EouDistance(team.HomePos(), food->Pos()) <= std::max(512.f, game_config::default_horizon) &&
                eou_has_line_of_sight(m_wall_board, team.HomePos(), food->Pos())) ++nearby;
        }
        if (nearby < near_cap) SpawnColonyFood(world, team, 1);
    }
}

void CAntKingdomController::OnMobDefeated(CGameWorld& world, CMobBase& mob)
{
    // A slain queen loses the match for her team (despawns via Dissolve do
    // not run through the defeat path, so this only fires on real kills).
    for (auto& [team_id, team] : m_teams)
    {
        if (!team.IsQueen(mob.GetHandle())) continue;
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
    if (auto* cell = dynamic_cast<CEouCellMob*>(&entity))
    {
        ReleaseCellContents(world, *cell);
        if (CTeam* team = FindTeam(cell->TeamId())) team->RemoveCell(world, *cell);
    }
    m_food_entities.erase(entity.GetHandle());
    CAntHelPvpController::OnEntityRemoved(world, entity, reason);
}

void CAntKingdomController::OnTick(CGameWorld& world, float dt)
{
    CMutableTerrainController::OnTick(world, dt);
    CAntHelPvpController::OnTick(world, dt);
    // After a hot-reload restore the world's map (and wall grid) is rebuilt:
    // lazily rebuild the board and replay its recorded cells/removals.
    if (!m_wall_board.IsLoaded()) m_wall_board.Reload(world);
    m_food_replenish_timer -= dt;
    if (m_food_replenish_timer <= 0.f)
    {
        m_food_replenish_timer = std::max(1.f, game_config::eou_food_replenish_interval);
        if (game_config::eou_food_replenish_interval > 0.f) ReplenishFood(world);
    }

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

void CAntKingdomController::OnPlayerArrivalCommitted(CGameWorld& world, CPlayer& player)
{
    CMutableTerrainController::OnPlayerArrivalCommitted(world, player);
    CAntHelPvpController::OnPlayerArrivalCommitted(world, player);

    // A match team gets its eou team (owning its pheromone groups).
    const int team_id = PlayerTeam(player.GetId());
    if (HasMatchTeam(player.GetId()))
    {
        m_team_slot_proxies.try_emplace(team_id, m_slot_proxy);
        SyncBuildModeCard(team_id);
        if (auto* flower = dynamic_cast<CPlayerFlower*>(player.GetEntity())) EquipPheromonePetals(*flower);
    }
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
        team.DepositFood(game_config::eou_initial_stash);
        SpawnQueen(world, team, origin);
        SpawnColonyFood(world, team, game_config::eou_colony_food_count);
        const int workers = std::max(0, game_config::eou_initial_worker_count);
        for (int i = 0; i < workers; ++i)
            SpawnEouMob(world, team_id, ToMobType(EModMobType::EouWorker), origin);
        LOG_INFO("ant_kingdom", "team " + std::to_string(team_id) + " created for player " +
                                    std::to_string(player.GetId()) + " with " +
                                    std::to_string(m_pheromone_group_count) + " pheromone groups, " +
                                    std::to_string(workers) + " workers and a queen");
    }
    if (auto* entity = player.GetEntity(); entity && dynamic_cast<IEouMob*>(entity))
        if (auto* team = FindTeam(team_id)) AdoptEouMob(world, *team, *entity);
    SendSystemMessage(world, player, "EOU: /colony = food/army status; /colony help = controls. Collect food, build nurseries, protect your queen.");
}

bool CAntKingdomController::WritePlayerScene(const CGameWorld& world, const CPlayer& player, IPlayerSceneWriter& writer) const
{
    return CMutableTerrainController::WritePlayerScene(world, player, writer) && m_wall_board.WritePlayerScene(writer);
}

void CAntKingdomController::OnPlayerDepartureCommitted(CGameWorld& world, CPlayer& player, CEntity* departing)
{
    // Dissolve the player's team before the pvp base forgets the assignment.
    const int team_id = PlayerTeam(player.GetId());
    const auto it = m_teams.find(team_id);
    if (it != m_teams.end())
    {
        it->second.Dissolve(world);
        m_teams.erase(it);
        m_team_build_type.erase(team_id);
        m_team_slot_proxies.erase(team_id);
        LOG_INFO("ant_kingdom", "team " + std::to_string(team_id) + " dissolved");
    }
    CAntHelPvpController::OnPlayerDepartureCommitted(world, player, departing);
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
        if (name == "colony" || name == "food") return HandleColonyCommand(world, player, args);
    }
    return COpenController::HandlePlayerCommand(world, player, args);
}

EEouBuildMode CAntKingdomController::TeamBuildMode(int team_id) const
{
    const auto it = m_team_build_type.find(team_id);
    return it == m_team_build_type.end() ? EEouBuildMode::Storage : it->second;
}

namespace
{
const char* EouBuildModeName(EEouBuildMode mode)
{
    switch (mode)
    {
    case EEouBuildMode::WorkerHatchery: return "worker hatchery";
    case EEouBuildMode::SoldierHatchery: return "soldier hatchery";
    case EEouBuildMode::Storage: break;
    }
    return "storage";
}
} // namespace

bool CAntKingdomController::HandleBuildCommand(CGameWorld& world, CPlayer& player,
                                               const std::vector<std::string>& args)
{
    if (!HasMatchTeam(player.GetId()))
    {
        SendSystemMessage(world, player, "build: only players with a match team can build");
        return true;
    }
    const int team_id = PlayerTeam(player.GetId());

    if (args.size() < 2)
    {
        SendSystemMessage(world, player,
                          std::string("build: current mode is ") + EouBuildModeName(TeamBuildMode(team_id)) +
                              " - usage /build storage|worker|soldier (defend-throw a pheromone petal to build)");
        return true;
    }

    std::string mode = args[1];
    std::transform(mode.begin(), mode.end(), mode.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    if (mode == "storage" || mode == "s")
        m_team_build_type[team_id] = EEouBuildMode::Storage;
    else if (mode == "worker" || mode == "w" || mode == "hatchery" || mode == "h")
        m_team_build_type[team_id] = EEouBuildMode::WorkerHatchery;
    else if (mode == "soldier" || mode == "sol")
        m_team_build_type[team_id] = EEouBuildMode::SoldierHatchery;
    else
    {
        SendSystemMessage(world, player, "build: usage /build storage|worker|soldier");
        return true;
    }
    SyncBuildModeCard(team_id);
    if (CGameContext* context = world.GameContext()) context->Network().QueueOwnerStateUpdate(player);
    const EEouBuildMode chosen = TeamBuildMode(team_id);
    const float cost = chosen == EEouBuildMode::SoldierHatchery ? game_config::eou_soldier_cell_build_cost
                       : chosen == EEouBuildMode::WorkerHatchery ? game_config::eou_worker_cell_build_cost
                                                                 : game_config::eou_storage_build_cost;
    SendSystemMessage(world, player,
                      std::string("build: mode set to ") + EouBuildModeName(chosen) + " (costs " +
                          std::to_string(static_cast<int>(std::lround(cost))) + " food)");
    return true;
}

// `/colony` (alias `/food`): the colony readout. Without a HUD this is the
// only way to see the food economy, so it reports stock, nurseries and the
// live population in one message.
bool CAntKingdomController::HandleColonyCommand(CGameWorld& world, CPlayer& player,
                                                const std::vector<std::string>& args)
{
    (void)args;
    if (!HasMatchTeam(player.GetId()))
    {
        SendSystemMessage(world, player, "colony: only players with a match team have a colony");
        return true;
    }
    CTeam* team = FindTeam(PlayerTeam(player.GetId()));
    if (!team)
    {
        SendSystemMessage(world, player, "colony: no colony yet");
        return true;
    }

    int storage_cells = 0;
    int worker_cells = 0;
    int soldier_cells = 0;
    int eggs = 0;
    int grown_eggs = 0;
    for (const SEouEntityHandle& handle : team->Cells().AllCells())
    {
        auto* cell = dynamic_cast<CEouCellMob*>(world.ResolveEntity(handle));
        if (!cell || cell->IsMarkedForDes() || cell->IsDead()) continue;
        if (cell->CellType() == ECouCellType::Storage) ++storage_cells;
        else if (cell->EggType() == ToMobType(EModMobType::EouSoldier)) ++soldier_cells;
        else ++worker_cells;
        if (!cell->HasEgg()) continue;
        auto* egg = dynamic_cast<CEouEggMob*>(world.ResolveEntity(cell->EggRef()));
        if (!egg || egg->IsMarkedForDes()) continue;
        ++eggs;
        if (egg->Grown()) ++grown_eggs;
    }

    // Population: ants counted from the world (group membership can lag a
    // tick behind a hatch), carried cargo included so ferried food is visible.
    int workers = 0;
    int soldiers = 0;
    float carried = 0.f;
    const int team_id = team->TeamId();
    world.ForEachEntity([&](CEntity* entity) {
        auto* mob = dynamic_cast<CMobBase*>(entity);
        if (!mob || mob->IsMarkedForDes() || mob->IsDead() || mob->Team() != team_id) return;
        if (const auto* eou = dynamic_cast<const IEouMob*>(entity)) carried += eou->EouCarry();
        if (mob->GetMobType() == ToMobType(EModMobType::EouSoldier)) ++soldiers;
        else if (mob->GetMobType() == ToMobType(EModMobType::EouWorker)) ++workers;
    });

    const auto round1 = [](float value) {
        return std::to_string(static_cast<int>(std::lround(value)));
    };
    SendSystemMessage(world, player,
                      "colony: food " + round1(team->StorageFoodTotal(world)) + "/" +
                          round1(team->FoodCapacity(world)) + " (stash " + round1(team->StashFood()) +
                          ", carried " + round1(carried) + ")");
    SendSystemMessage(world, player,
                      "colony: cells storage " + std::to_string(storage_cells) + ", worker " +
                          std::to_string(worker_cells) + ", soldier " + std::to_string(soldier_cells) +
                          " | eggs " + std::to_string(eggs) + " (" + std::to_string(grown_eggs) +
                          " waiting on food)");
    SendSystemMessage(world, player,
                      "colony: ants worker " + std::to_string(workers) + ", soldier " +
                          std::to_string(soldiers) + " | hatch cost worker " +
                          round1(game_config::eou_egg_hatch_food_cost) + ", soldier " +
                          round1(game_config::eou_soldier_hatch_food_cost));
    return true;
}

const CEouSlotProxy* CAntKingdomController::SlotProxy(int team_id) const
{
    const auto it = m_team_slot_proxies.find(team_id);
    return it == m_team_slot_proxies.end() ? nullptr : &it->second;
}

void CAntKingdomController::SyncBuildModeCard(int team_id)
{
    auto it = m_team_slot_proxies.find(team_id);
    if (it == m_team_slot_proxies.end() || it->second.GetSlotCount() <= 2) return;
    const EEouBuildMode shown = TeamBuildMode(team_id);
    SEouSlotProxySlot& card = it->second.Slot(2);
    card.visual_petal_type = shown == EEouBuildMode::SoldierHatchery ? EPetalType::Stinger
                             : shown == EEouBuildMode::WorkerHatchery ? EPetalType::AntEgg
                                                                      : EPetalType::Soil;
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
    ConfigureColonyEntity(entity);
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
    ConfigureColonyEntity(*entity);
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
        florrbt::Vector2f target;
        if (!ParseWorldPoint(token, target))
        {
            SendSystemMessage(world, player, "mark_wall: usage /mark_wall or /mark_wall [x,y]");
            return true;
        }
        target_col = static_cast<int>(std::floor(target.x / m_tile_size));
        target_row = static_cast<int>(std::floor(target.y / m_tile_size));
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
    if (auto it = m_team_slot_proxies.find(team); it != m_team_slot_proxies.end())
    {
        SEouSlotProxySlot& slot = it->second.Slot(1);
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
        if (!ParseWorldPoint(token, target))
        {
            SendSystemMessage(world, player, "pheromone: usage /pheromone or /pheromone [x,y]");
            return true;
        }
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
    if (auto it = m_team_slot_proxies.find(team_id); it != m_team_slot_proxies.end())
    {
        SEouSlotProxySlot& slot = it->second.Slot(0);
        slot.cooldown_progress = 0.f;
        slot.available = false;
    }
    SendSystemMessage(world, player, "pheromone: team " + std::to_string(team_id) + " target set to [" +
                                         std::to_string(target.x) + "," + std::to_string(target.y) + "]");
    return true;
}

void CAntKingdomController::TickSlotProxies(float dt)
{
    for (auto& [team_id, proxy] : m_team_slot_proxies)
    for (int i = 0; i < proxy.GetSlotCount(); ++i)
    {
        SEouSlotProxySlot& slot = proxy.Slot(i);
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
    {
        const auto& slot = flower.GetSlots()[i];
        if (slot.m_p_proto != proto || slot.m_stored_rarity != kPheromoneRarities[i - 1] ||
            slot.m_banned || !slot.m_available)
            flower.LoadPetalSlot(i, proto, kPheromoneRarities[i - 1]);
    }
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
    const EEouBuildMode build_mode = TeamBuildMode(team_id);
    // Cell count is capped so a colony cannot carpet the map with nurseries.
    if (static_cast<int>(team->Cells().AllCells().size()) >= std::max(1, game_config::eou_max_cells_per_team))
    {
        SendTeamMessage(world, team_id, "cell limit reached - demolish one first");
        return;
    }
    const ECouCellType build_type =
        build_mode == EEouBuildMode::Storage ? ECouCellType::Storage : ECouCellType::Hatchery;
    const EMobType egg_type = build_mode == EEouBuildMode::SoldierHatchery ? ToMobType(EModMobType::EouSoldier)
                              : build_mode == EEouBuildMode::WorkerHatchery ? ToMobType(EModMobType::EouWorker)
                                                                            : EMobType::None;
    // Building costs food (soldier nurseries cost the most). Payment is
    // atomic: it only leaves the stores once the cell actually exists.
    const float cost = build_mode == EEouBuildMode::SoldierHatchery ? game_config::eou_soldier_cell_build_cost
                       : build_mode == EEouBuildMode::WorkerHatchery ? game_config::eou_worker_cell_build_cost
                                                                     : game_config::eou_storage_build_cost;
    if (!team->TryConsumeFood(world, std::max(0.f, cost)))
    {
        SendTeamMessage(world, team_id,
                        std::string(EouBuildModeName(build_mode)) + " needs " +
                            std::to_string(static_cast<int>(std::lround(cost))) + " food");
        return;
    }
    if (SpawnCellEntity(world, team_id, build_type, pos, egg_type) != nullptr)
        SendTeamMessage(world, team_id, std::string(EouBuildModeName(build_mode)) + " built");
    else
        team->RefundFood(world, std::max(0.f, cost));
}

float CAntKingdomController::DepositWorkerFood(CGameWorld& world, int team_id, florrbt::Vector2f pos, float amount)
{
    CTeam* team = FindTeam(team_id);
    if (!team || amount <= 0.f) return 0.f;
    // Storage cells first; whatever they cannot take (no cells yet, or the
    // nearest is full) lands on the queen's stash. Both are capped, so the
    // worker keeps whatever neither could accept and retries later.
    const float stored = team->DepositToNearestStorage(world, pos, amount);
    const float stashed = stored < amount ? team->DepositFood(amount - stored) : 0.f;
    return stored + stashed;
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

    SChatEntry entry;
    entry.flag = EChatFlag::Server;
    entry.player_id = 0;
    entry.target_player_id = static_cast<int>(player.GetId());
    entry.player_name = "Server";
    entry.message = message;
    entry.system_time = static_cast<uint32_t>(
        std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch())
            .count());
    context->Extensions().Globals().Find<IChatService>()->SendTo(player, entry);
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
                     static_cast<int>(TeamBuildMode(team_id)));
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
    if (!m_wall_board.RestoreSnapshot(reader, version, error)) return false;

    m_teams.clear();
    m_team_build_type.clear();
    constexpr std::uint32_t max_restore_teams = 4096;
    const std::uint32_t count = std::min(reader.UInt32("eou_team_count", 0), max_restore_teams);
    for (std::uint32_t i = 0; i < count; ++i)
    {
        const int team_id = reader.Int("eou_team_id_" + std::to_string(i), 0);
        const json_t* node = reader.Node("eou_team_" + std::to_string(team_id));
        if (!node || !json_is_object(node) || team_id <= 0) continue;
        CSnapshotReader team_reader(node);
        auto team = std::make_unique<CTeam>(team_id, m_wall_board);
        if (!team->RestoreSnapshot(team_reader, version, error)) return false;
        m_teams.emplace(team_id, std::move(*team));
        // Old snapshots stored ECouCellType (0 storage / 1 hatchery); the
        // build-mode values keep those meanings (1 = worker hatchery).
        const int build_mode = reader.Int("eou_build_type_" + std::to_string(team_id),
                                          static_cast<int>(EEouBuildMode::Storage));
        m_team_build_type[team_id] = build_mode == static_cast<int>(EEouBuildMode::SoldierHatchery)
                                         ? EEouBuildMode::SoldierHatchery
                                     : build_mode == static_cast<int>(EEouBuildMode::WorkerHatchery)
                                         ? EEouBuildMode::WorkerHatchery
                                         : EEouBuildMode::Storage;
    }
    m_team_slot_proxies.clear();
    for (const auto& [team_id, team] : m_teams)
    {
        m_team_slot_proxies.emplace(team_id, m_slot_proxy);
        SyncBuildModeCard(team_id);
    }
    return true;
}

bool CAntKingdomController::ResolveReferences(CGameWorld& world, const IGameWorldResolver& resolver, std::string& error)
{
    if (!CMutableTerrainController::ResolveReferences(world, resolver, error)) return false;
    m_wall_board.Reload(world);
    for (auto& [team_id, team] : m_teams)
    {
        if (team.TeamId() != team_id)
        {
            error = "EOU team map key does not match the restored team id";
            return false;
        }
        team.ResolveReferences(world);
    }

    // The neutral food index holds {id, generation} handles that only make
    // sense against live entities, so it is deliberately not snapshotted:
    // after a restore we rebuild it by scanning the world once. Without this
    // the replenisher would see an empty field and re-seed on top of the
    // food that actually survived.
    m_food_entities.clear();
    world.ForEachEntity([&](CEntity* entity) {
        if (auto* egg = dynamic_cast<CEouEggMob*>(entity))
        {
            auto* cell = dynamic_cast<CEouCellMob*>(world.ResolveEntity(egg->CellRef()));
            if (!cell || cell->IsMarkedForDes() || cell->IsDead() || cell->TeamId() != egg->TeamId() ||
                !cell->EggRef().RefersTo(egg->GetHandle()) || !FindTeam(egg->TeamId()))
                egg->MarkForDestroy(EEntityRemovalReason::Despawned);
        }
        const auto* food = dynamic_cast<const CEouFoodMob*>(entity);
        if (!food || food->IsMarkedForDes() || food->IsDead()) return;
        m_food_entities.insert(entity->GetHandle());
    });
    return true;
}

// A cell is going away (demolished, despawned or dissolved): its egg dies
// with it and its stored food returns to the team so a rebuild does not
// silently burn the colony's reserves.
void CAntKingdomController::ReleaseCellContents(CGameWorld& world, CEouCellMob& cell)
{
    if (cell.HasEgg())
    {
        if (CEntity* egg = world.ResolveEntity(cell.EggRef()))
            egg->MarkForDestroy(EEntityRemovalReason::Despawned);
        cell.ClearEgg();
    }
    // The bound ant outlives its nursery; it just loses its tile binding.
    cell.ClearBoundAnt();

    const float stored = cell.StoredFood();
    if (stored <= 0.f) return;
    cell.TakeFood(stored);
    // Hand it back through the team so remaining storage cells get first
    // refusal and only the overflow lands in the queen's stash.
    if (CTeam* team = FindTeam(cell.TeamId())) team->RefundFood(world, stored);
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
    egg->SetBinding(cell.GetHandle());

    CEntity* entity = world.InsertEntity(std::move(egg_mob));
    if (!entity) return nullptr;
    entity->Team() = team_id;
    if (auto* placed = dynamic_cast<CEouEggMob*>(entity)) cell.SetEgg(placed->GetHandle());
    return entity;
}

void CAntKingdomController::TickTeamCells(CGameWorld& world, CTeam& team, float dt)
{
    const int team_id = team.TeamId();
    for (const SEouEntityHandle& handle : team.Cells().AllCells())
    {
        CEntity* entity = world.ResolveEntity(handle);
        auto* cell = dynamic_cast<CEouCellMob*>(entity);
        if (!cell) continue;

        if (cell->EggType() == EMobType::None) continue; // storage cells

        // One tile, one ant (the EotU rule): while the tile's bound ant is
        // alive no egg is laid; its death starts the respawn cycle (retry
        // timer -> egg -> grow -> hatch for the food cost).
        if (cell->HasBoundAnt())
        {
            CEntity* ant_entity = world.ResolveEntity(cell->BoundAntRef());
            auto* ant = dynamic_cast<CMobBase*>(ant_entity);
            if (ant && !ant->IsMarkedForDes() && !ant->IsDead()) continue;
            cell->ClearBoundAnt();
        }

        // Hatchery: keep an egg alive.
        CEouEggMob* egg = nullptr;
        if (cell->HasEgg())
        {
            CEntity* egg_entity = world.ResolveEntity(cell->EggRef());
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
                if (fresh) cell->SetEgg(fresh->GetHandle());
            }
            continue;
        }

        // Egg grown? Hatch once the team can pay the food cost (soldiers
        // cost more than workers).
        if (!egg->Grown()) continue;
        const bool soldier = egg->HatchType() == ToMobType(EModMobType::EouSoldier);
        const float cost = soldier ? game_config::eou_soldier_hatch_food_cost
                                   : game_config::eou_egg_hatch_food_cost;
        if (!team.TryConsumeFood(world, cost)) continue;

        // Payment is only final once the ant exists: a factory/insertion
        // failure refunds the food and keeps the egg for the next tick.
        std::unique_ptr<CMobBase> hatch = CreateMob(egg->HatchType(), &world, egg->Pos(), ERarity::Common);
        CEntity* hatched = hatch ? world.InsertEntity(std::move(hatch)) : nullptr;
        if (!hatched)
        {
            team.RefundFood(world, cost);
            continue;
        }
        // Adoption gives the hatchling its colony: team, home point and
        // primary-group membership (which wires the path board). The tile
        // binds its ant: no new egg until this one dies.
        AdoptEouMob(world, team, *hatched);
        cell->SetBoundAnt(hatched->GetHandle());
        SendTeamMessage(world, team_id, soldier ? "soldier hatched" : "worker hatched");
        cell->ClearEgg();
        egg->MarkForDestroy(EEntityRemovalReason::Despawned);
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
