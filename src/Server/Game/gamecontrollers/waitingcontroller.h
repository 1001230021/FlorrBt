#pragma once
#include "../controller_params.h"
#include "../../../Shared/math_types.h"
#include "../gamecontroller.h"
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

enum class EWaitingReleasePolicy : std::uint8_t
{
    Disabled,
    MinimumPlayers,
};

struct SWaitingControllerConfig
{
    static constexpr std::uint32_t invalid_world_id = std::numeric_limits<std::uint32_t>::max();

    EWaitingReleasePolicy release_policy = EWaitingReleasePolicy::Disabled;
    std::uint32_t minimum_player_count = 1;
    // 游戏世界在开局时按需创建，届时填入目标身份。
    std::uint32_t destination_world_id = invalid_world_id;
    // 房间开局时按蓝图创建地图与玩法。
    std::string game_map_path;
    std::string game_controller_key;
    // /setting 提供构造参数，并按目标控制器的 ParamSchema 校验。
    CControllerParams game_controller_params;
    // 普通等待世界没有房间编号，以 -1 表示。
    int room_code = -1;
};

class CWaitingController : public IGameController
{
  public:
    CWaitingController() = default;
    explicit CWaitingController(SWaitingControllerConfig config);

    void OnDeactivate(CGameWorld& world) override;
    void OnTick(CGameWorld& world, float dt) override;
    std::optional<florrbt::Vector2f> SelectPlayerSpawn(CGameWorld& world, CPlayer& player,
                                                  EPlayerSpawnReason reason) override;
    void OnPlayerEntityReady(CGameWorld& world, CPlayer& player, CPlayerFlower& flower,
                             EPlayerSpawnReason reason) override;
    void OnPlayerArrivalCommitted(CGameWorld& world, CPlayer& player) override;
    void OnPlayerDepartureCommitted(CGameWorld& world, CPlayer& player, CEntity* departing) override;
    // 等待区封锁全部换装入口，主槽和副槽遵循同一规则。
    bool LocksPetalLoadout() const override { return true; }
    bool HandlePlayerCommand(CGameWorld& world, CPlayer& player,
                             const std::vector<std::string>& args) override;
    std::string_view SnapshotKey() const override { return "world_controller.waiting"; }
    void CaptureSnapshot(CSnapshotWriter& writer) const override;
    bool RestoreSnapshot(const CSnapshotReader& reader, std::uint32_t version, std::string& error) override;
    bool ResolveReferences(CGameWorld& world, const IGameWorldResolver& worlds, std::string& error) override;

    const SWaitingControllerConfig& GetConfig() const { return m_config; }

    // 标记开局请求，在下一次规则更新时释放等待玩家。
    bool RequestStart() { m_start_requested = true; return true; }
    bool IsStartRequested() const { return m_start_requested; }

  private:
    bool ShouldRelease(size_t player_count) const;
    CGameWorld* ResolveDestinationWorld(CGameWorld& world) const;
    CGameWorld* ResolveOrCreateDestinationWorld(CGameWorld& world);
    bool HandleSettingCommand(CGameWorld& world, CPlayer& player, const std::vector<std::string>& args);
    size_t CountWaitingPlayers(CGameWorld& world) const;
    std::vector<CPlayer*> CollectWaitingPlayers(CGameWorld& world) const;
    void SetPlayerPetalsBanned(CPlayer* player, bool banned) const;
    void MaintainPetalBans(CGameWorld& world) const;
    void ReleasePlayers(CGameWorld& world, CGameWorld& destination_world);
    void SendSystemMessage(CGameWorld& world, CPlayer& player, const std::string& message) const;

    SWaitingControllerConfig m_config;
    bool m_start_requested = false;
};
