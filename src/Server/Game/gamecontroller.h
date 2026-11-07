#pragma once
#include "../../Shared/talent_type.h"
#include "controller_params.h"
#include "../../Shared/math_types.h"
#include "../../Shared/mob_type.h"
#include "world_transition.h"
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

class CGameWorld;
class CEntity;
class CMobBase;
class CPlayer;
class CPlayerFlower;
class IGameWorldResolver;
class IPlayerSceneWriter;
enum class EEntityRemovalReason : std::uint8_t;
enum class EPetalType : int;
struct STalentContext;
class CSnapshotReader;
class CSnapshotWriter;
class CEouSlotProxy;

enum class EPlayerSpawnReason : std::uint8_t
{
    Login,
    Respawn,
};

class IGameController
{
  public:
    virtual ~IGameController() = default;
    virtual bool PrepareActivation(CWorldPreparation&, std::string&) const { return true; }
    virtual void OnActivate(CGameWorld&) {}
    virtual void OnDeactivate(CGameWorld&) {}
    virtual void OnTick(CGameWorld& world, float dt) = 0;
    virtual std::optional<florrbt::Vector2f> SelectPlayerSpawn(CGameWorld&, CPlayer&, EPlayerSpawnReason)
    {
        return std::nullopt;
    }
    virtual void OnPlayerEntityReady(CGameWorld&, CPlayer&, CPlayerFlower&, EPlayerSpawnReason) {}
    virtual bool ValidatePlayerTransition(const SPlayerTransitionView&, EPlayerTransitionSide,
                                          std::string&) const { return true; }
    // 提交通知不能否决结果，也不能重入迁移或销毁参与者。
    virtual void OnPlayerArrivalCommitted(CGameWorld&, CPlayer&) {}
    // 场景数据只能在 Welcome 暂存后写入，进入通知不能提前广播地图状态。
    virtual bool WritePlayerScene(const CGameWorld&, const CPlayer&, IPlayerSceneWriter&) const { return true; }
    virtual void OnPlayerDepartureCommitted(CGameWorld&, CPlayer&, CEntity*) {}
    virtual void OnEntityRemoved(CGameWorld&, CEntity&, EEntityRemovalReason) {}
    virtual void OnMobDefeated(CGameWorld&, CMobBase&) {}
    // 无栖息地规则时返回 None，由技能选择替代生物。
    virtual EMobType PickHabitatMobType(CGameWorld&, florrbt::Vector2f) { return EMobType::None; }
    virtual std::string_view SnapshotKey() const = 0;
    virtual bool CanRestoreSnapshotKey(std::string_view key) const { return SnapshotKey() == key; }
    virtual std::uint32_t SnapshotVersion() const { return 1; }
    virtual void CaptureSnapshot(CSnapshotWriter&) const {}
    virtual bool RestoreSnapshot(const CSnapshotReader&, std::uint32_t version, std::string& error)
    {
        if (version <= SnapshotVersion()) return true;
        error = "Unsupported world controller snapshot version";
        return false;
    }
    virtual bool ResolveReferences(CGameWorld&, const IGameWorldResolver&, std::string&) { return true; }
    // 使玩家花朵恒定穿越生物和墙体。
    virtual bool ForcesPrimordialNullification() const { return false; }
    virtual bool LocksPetalLoadout() const { return false; }
    // 沙盒只切换临时进度，不改登录身份。
    virtual bool UsesIsolatedProgress() const { return false; }
    // 先检查整套装备锁定，再检查具体花瓣的装备和卸下权限。
    virtual bool CanEquipPetalType(EPetalType type) const { return true; }
    virtual bool CanUnequipPetalType(EPetalType type) const { return true; }
    // 等待房间通过 /setting 设置构造参数，创建玩法前完成校验。
    virtual std::vector<SControllerParamInfo> ParamSchema() const { return {}; }
    virtual void ModifyTalentContext(CGameWorld& world, CPlayer* player, ETalentEvent event, STalentContext& ctx)
    {
        (void)world;
        (void)player;
        (void)event;
        (void)ctx;
    }
    // args[0] 保留原命令前缀；返回 true 表示已处理。
    virtual bool HandlePlayerCommand(CGameWorld& world, CPlayer& player, const std::vector<std::string>& args)
    {
        (void)world;
        (void)player;
        (void)args;
        return false;
    }
    // EOU 逻辑技能暂映射到空花瓣槽，由快照服务提供显示数据。
    virtual const CEouSlotProxy* SlotProxy(int team_id) const { return nullptr; }
    // 攻击投掷更新信息素目标；防御投掷操作落点下的巢室。
    virtual void OnPheromonePetalStopped(CGameWorld&, int team_id, int group_id, florrbt::Vector2f pos, bool defend)
    {
        (void)team_id;
        (void)group_id;
        (void)pos;
        (void)defend;
    }
    // 除自身权威状态外，所有实体先检查可见权限，再分配快照优先级。
    virtual bool SnapshotEntityVisibleToPlayer(CGameWorld& world, const CPlayer& player, const CEntity& entity) const
    {
        (void)world;
        (void)player;
        (void)entity;
        return true;
    }
};
