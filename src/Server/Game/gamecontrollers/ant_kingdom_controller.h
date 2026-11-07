#pragma once
#include "../tile_board.h"
#include "../wall_board.h"
#include "eou_slot_proxy.h"
#include "eou_team.h"
#include "open_world_rules.h"
#include "match_rules.h"
#include "../../../Shared/math_types.h"
#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// EOU 显式组合刷怪、比赛与地形规则；模式拥有团队，世界拥有实体和地形。
// 建筑类型写入快照，不能重编号。
enum class EEouBuildMode : std::uint8_t
{
    Storage = 0,
    WorkerHatchery = 1,
    SoldierHatchery = 2,
};

class CWorldTerrain;
class CAntKingdomController : public IGameController
{
  public:
    static constexpr int kMaxPheromoneGroups = 10;

    explicit CAntKingdomController(CControllerParams params = {});
    int PlayerTeam(std::uint32_t id) const { return m_match.PlayerTeam(id); }
    bool HasMatchTeam(const CPlayer& player) const { return m_match.HasMatchTeam(player); }
    std::optional<florrbt::Vector2f> SelectPlayerSpawn(CGameWorld& world, CPlayer& player, EPlayerSpawnReason reason) override
    { return m_open.SelectPlayerSpawn(world, player, reason); }
    EMobType PickHabitatMobType(CGameWorld& world, florrbt::Vector2f pos) override
    { return m_open.PickHabitatMobType(world, pos); }

    bool PrepareActivation(CWorldPreparation& preparation, std::string& error) const override;
    void OnActivate(CGameWorld& world) override;
    void OnDeactivate(CGameWorld& world) override;
    void OnTick(CGameWorld& world, float dt) override;
    void OnMobDefeated(CGameWorld& world, CMobBase& mob) override;
    void OnEntityRemoved(CGameWorld& world, CEntity& entity, EEntityRemovalReason reason) override;
    void OnPlayerEntityReady(CGameWorld& world, CPlayer& player, CPlayerFlower& flower,
                             EPlayerSpawnReason reason) override;
    void OnPlayerArrivalCommitted(CGameWorld& world, CPlayer& player) override;
    void OnPlayerDepartureCommitted(CGameWorld& world, CPlayer& player, CEntity* departing) override;
    bool HandlePlayerCommand(CGameWorld& world, CPlayer& player,
                             const std::vector<std::string>& args) override;
    std::string_view SnapshotKey() const override { return "world_controller.ant_kingdom"; }
    std::uint32_t SnapshotVersion() const override { return 1; }
    void CaptureSnapshot(CSnapshotWriter& writer) const override;
    bool RestoreSnapshot(const CSnapshotReader& reader, std::uint32_t version, std::string& error) override;
    bool ResolveReferences(CGameWorld& world, const IGameWorldResolver&, std::string& error) override;
    std::vector<SControllerParamInfo> ParamSchema() const override;
    bool ForcesPrimordialNullification() const override { return true; }
    // 信息素花瓣由模式提供，不能卸入玩家背包。
    bool LocksPetalLoadout() const override { return true; }
    bool CanEquipPetalType(EPetalType type) const override
    {
        return type == ToPetalType(EModPetalType::EouPheromone);
    }
    bool CanUnequipPetalType(EPetalType type) const override
    {
        return type != ToPetalType(EModPetalType::EouPheromone);
    }

    CWall& WallBoard();
    const CWall& WallBoard() const;

    CTeam* FindTeam(int team_id);
    const CTeam* FindTeam(int team_id) const;
    std::size_t TeamCount() const { return m_teams.size(); }

    // 实体归世界所有，团队仅登记其句柄。
    CEntity* SpawnEouMob(CGameWorld& world, int team_id, EMobType mob_type, florrbt::Vector2f pos);

    // 逻辑技能暂借用花瓣槽显示，不属于玩家库存。
    const CEouSlotProxy* SlotProxy(int team_id) const override;

    // 攻击投掷更新目标，防御投掷操作落点巢室。
    void OnPheromonePetalStopped(CGameWorld& world, int team_id, int group_id, florrbt::Vector2f pos,
                                 bool defend) override;

    // 优先存入最近的储藏室，溢出部分尝试放入团队库存。
    float DepositWorkerFood(CGameWorld& world, int team_id, florrbt::Vector2f pos, float amount);

    // 墙已挖开或标记撤销时，原任务不再生效。
    bool WorkerDigWall(CGameWorld& world, int team, florrbt::Vector2f pos);

  private:
    void SpawnInitialFood(CGameWorld& world);
    int SpawnColonyFood(CGameWorld& world, const CTeam& team, int count);
    void ReplenishFood(CGameWorld& world);
    CEntity* SpawnFoodEntity(CGameWorld& world, florrbt::Vector2f pos, float amount, int food_type);
    CEntity* SpawnCellEntity(CGameWorld& world, int team_id, ECouCellType type, florrbt::Vector2f pos,
                             EMobType egg_type = EMobType::None);
    void DestroyCellEntity(CGameWorld& world, CEntity* cell, int team_id);
    void ReleaseCellContents(CGameWorld& world, CEouCellMob& cell);
    CEntity* SpawnOrStandbyEgg(CGameWorld& world, CEouCellMob& cell, CTeam* team);
    void TickTeamCells(CGameWorld& world, CTeam& team, float dt);
    // 出生和孵化共用归队流程：设置阵营、巢穴及主信息素组。
    void AdoptEouMob(CGameWorld& world, CTeam& team, CEntity& entity);
    // 蚁后在巢穴生成，真实死亡判定该队失败。
    CEntity* SpawnQueen(CGameWorld& world, CTeam& team, florrbt::Vector2f pos);
    void OnQueenSlain(CGameWorld& world, int team_id);
    void SendTeamMessage(CGameWorld& world, int team_id, const std::string& message);
    void BroadcastMessage(CGameWorld& world, const std::string& message);
    bool HandleMarkWallCommand(CGameWorld& world, CPlayer& player, const std::vector<std::string>& args);
    bool HandlePheromoneCommand(CGameWorld& world, CPlayer& player, const std::vector<std::string>& args);
    bool HandleBuildCommand(CGameWorld& world, CPlayer& player, const std::vector<std::string>& args);
    bool HandleColonyCommand(CGameWorld& world, CPlayer& player, const std::vector<std::string>& args);
    void SendSystemMessage(CGameWorld& world, CPlayer& player, const std::string& message) const;
    void TickSlotProxies(float dt);
    void InitializeSlotProxies();
    void EquipPheromonePetals(CPlayerFlower& flower);
    void SyncBuildModeCard(int team_id);
    EEouBuildMode TeamBuildMode(int team_id) const;
    static bool ValidatePheromoneGroupCount(const std::string& value);

    int m_pheromone_group_count = 6;
    CWorldTerrain* m_terrain = nullptr;
    COpenWorldRules m_open;
    CMatchRules m_match;
    bool m_initial_food_pending = false;
    std::unordered_map<int, CTeam> m_teams;
    std::unordered_map<int, EEouBuildMode> m_team_build_type;
    CEouSlotProxy m_slot_proxy;
    std::unordered_map<int, CEouSlotProxy> m_team_slot_proxies;
    // 完整句柄避免实体 ID 复用后继承旧食物索引。
    std::unordered_set<SEntityHandle, SEntityHandleHash> m_food_entities;
    float m_food_replenish_timer = 0.f;
};
