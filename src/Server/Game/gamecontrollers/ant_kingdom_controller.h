#pragma once
#include "../tile_board.h"
#include "../wall_board.h"
#include "eou_slot_proxy.h"
#include "eou_team.h"
#include "mutable_terrain_controller.h"
#include "pvp_controller.h"
#include <SFML/System/Vector2.hpp>
#include <cstdint>
#include <unordered_map>
#include <vector>

// Ant-kingdom world controller (eou). Merges the mutable-terrain rules (dirt
// layer + CWall markers on every in-rule tile) with the pvp team system
// (fixed 2-player cap, one team per player, spectators beyond the cap).
//
// Ownership chain: controller -> teams -> pheromone groups -> (decisions)
// mobs (the world owns the mob entities; groups only hold {id, generation}
// handles). The controller owns team lifecycles, teams own pheromone
// lifecycles, pheromones decide mob goals, players decide pheromones.
//
// Commands:
//   /mark_wall             - toggle this team's flag on the cell under you
//   /mark_wall [x,y]       - toggle this team's flag on the cell at [x,y]
//   /pheromone [x,y]       - move this team's primary group target to [x,y]
//   /build [storage|hatchery] - report or switch the team's build mode (what
//                            a defend-thrown pheromone petal builds)
// Only players with a match team (>= start_team) may use them.
//
// Construction parameters (overridable from the waiting room /setting):
//   pheromone_group_count  - pheromone groups per team and pheromone petal
//                            slots per player (default eou_pheromone_group_count,
//                            integer in [0, 10]). Slot 0 stays reserved; slots
//                            1..N hold one pheromone petal each, bound to
//                            group 1..N.
class CAntKingdomController : virtual public CMutableTerrainController,
                              virtual public CAntHelPvpController
{
  public:
    static constexpr int kMaxPheromoneGroups = 10;

    explicit CAntKingdomController(CControllerParams params = {});

    void OnActivate(CGameWorld& world) override;
    void OnTick(CGameWorld& world, float dt) override;
    void OnMobDefeated(CGameWorld& world, CMobBase& mob) override;
    void OnEntityRemoved(CGameWorld& world, CEntity& entity, EEntityRemovalReason reason) override;
    void OnPlayerEntityReady(CGameWorld& world, CPlayer& player, CPlayerFlower& flower,
                             EPlayerSpawnReason reason) override;
    void OnPlayerEnteredWorld(CGameWorld& world, CPlayer& player) override;
    void OnPlayerLeftWorld(CGameWorld& world, CPlayer& player) override;
    bool HandlePlayerCommand(CGameWorld& world, CPlayer& player,
                             const std::vector<std::string>& args) override;
    std::string_view SnapshotKey() const override { return "world_controller.ant_kingdom"; }
    std::uint32_t SnapshotVersion() const override { return 1; }
    void CaptureSnapshot(CSnapshotWriter& writer) const override;
    bool RestoreSnapshot(const CSnapshotReader& reader, std::uint32_t version, std::string& error) override;
    std::vector<SControllerParamInfo> ParamSchema() const override;
    // Diamond resolution: the mutable-terrain base wins.
    bool ForcesPrimordialNullification() const override
    {
        return CMutableTerrainController::ForcesPrimordialNullification();
    }
    // The pheromone petals are the only equippable loadout; the system owns
    // them, so they can never be unequipped back into the backpack.
    bool CanEquipPetalType(EPetalType type) const override
    {
        return type == ToPetalType(EModPetalType::EouPheromone);
    }
    bool CanUnequipPetalType(EPetalType type) const override
    {
        return type != ToPetalType(EModPetalType::EouPheromone);
    }

    // Diggable wall markers for every in-rule dirt tile (see InsertWallsForTerrain).
    CWall& WallBoard() { return m_wall_board; }
    const CWall& WallBoard() const { return m_wall_board; }

    // Team registry (controller owns team lifecycles).
    CTeam* FindTeam(int team_id);
    const CTeam* FindTeam(int team_id) const;
    std::size_t TeamCount() const { return m_teams.size(); }

    // Spawns a registered eou mob for a team into the world (nullptr on
    // failure). The mob is inserted into the world and added to the team's
    // group; the world owns the entity, the group only holds its handle.
    CEntity* SpawnEouMob(CGameWorld& world, int team_id, EMobType mob_type, florrbt::Vector2f pos);

    // Skill slots disguised as vanilla petal slots for the client (see
    // eou_slot_proxy.h).
    const CEouSlotProxy* SlotProxy() const override { return &m_slot_proxy; }
    CEouSlotProxy& SlotProxyMut() { return m_slot_proxy; }

    // Pheromone petal stopped: attack throw retargets the bound group,
    // defend throw toggles a cell at the resting point.
    void OnPheromonePetalStopped(CGameWorld& world, int team_id, int group_id, florrbt::Vector2f pos,
                                 bool defend) override;

    // Worker deposit: ferry food into the nearest storage cell (overflow and
    // storage-less colonies fall back to the team stash).
    void DepositWorkerFood(CGameWorld& world, int team_id, florrbt::Vector2f pos, float amount);

    // A worker finished digging at `pos`: removes the wall cell there when it
    // is still flagged by `team` and still standing. Returns false when the
    // job vanished under the worker (already dug or the flag was withdrawn).
    bool WorkerDigWall(CGameWorld& world, int team, florrbt::Vector2f pos);

  private:
    // Scans the dirt layer and binds a cell wall to every in-rule tile.
    void InsertWallsForTerrain(CGameWorld& world);
    // Neutral food: the initial food field and the corpses every eou mob
    // leaves behind (SpawnMobDefeated corpse path wakes through OnMobDefeated).
    void SpawnInitialFood(CGameWorld& world);
    CEntity* SpawnFoodEntity(CGameWorld& world, florrbt::Vector2f pos, float amount, int food_type);
    // Cells: a defense pheromone drop toggles the cell under the point.
    CEntity* SpawnCellEntity(CGameWorld& world, int team_id, ECouCellType type, florrbt::Vector2f pos,
                             EMobType egg_type = EMobType::None);
    void DestroyCellEntity(CGameWorld& world, CEntity* cell, int team_id);
    // Hatchery egg: creates the egg the cell binds to (retry-driven).
    CEntity* SpawnOrStandbyEgg(CGameWorld& world, CEouCellMob& cell, CTeam* team);
    // Drives hatchery egg lifecycle (retry spawn + hatch when grown & fed).
    void TickTeamCells(CGameWorld& world, CTeam& team, float dt);
    // Hooks a freshly created eou mob into its colony: team id, home point
    // and membership in the team's primary pheromone group (which also wires
    // the board). Used for both spawned and hatched mobs.
    void AdoptEouMob(CGameWorld& world, CTeam& team, CEntity& entity);
    // The queen: spawned at the team's home on team creation; her death loses
    // the match for the team.
    CEntity* SpawnQueen(CGameWorld& world, CTeam& team, florrbt::Vector2f pos);
    void OnQueenSlain(CGameWorld& world, int team_id);
    void SendTeamMessage(CGameWorld& world, int team_id, const std::string& message);
    void BroadcastMessage(CGameWorld& world, const std::string& message);
    bool HandleMarkWallCommand(CGameWorld& world, CPlayer& player, const std::vector<std::string>& args);
    bool HandlePheromoneCommand(CGameWorld& world, CPlayer& player, const std::vector<std::string>& args);
    bool HandleBuildCommand(CGameWorld& world, CPlayer& player, const std::vector<std::string>& args);
    void SendSystemMessage(CGameWorld& world, CPlayer& player, const std::string& message) const;
    void TickSlotProxies(float dt);
    void EquipPheromonePetals(CPlayerFlower& flower);
    void SyncBuildModeCard();
    ECouCellType TeamBuildType(int team_id) const;
    static bool ValidatePheromoneGroupCount(const std::string& value);

    int m_pheromone_group_count = 6;
    CWall m_wall_board;
    std::unordered_map<int, CTeam> m_teams;
    // What a defend-thrown pheromone petal builds for each team (default
    // Storage; switched with /build).
    std::unordered_map<int, ECouCellType> m_team_build_type;
    CEouSlotProxy m_slot_proxy;
    // Live food entities ({id, generation} handles; erased on removal).
    std::unordered_map<int, std::uint64_t> m_food_entities;
};