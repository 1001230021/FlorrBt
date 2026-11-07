#pragma once
#include "../gamecontroller.h"
#include <string>
#include <string_view>
#include <vector>

class CPlayer;

// 死亡竞赛使用 team 0 自由混战，允许自定义装备与天赋；地图参数可在开局前调整。
class CDeathmatchController : public IGameController
{
  public:
    CDeathmatchController(CControllerParams params);
    void OnTick(CGameWorld&, float) override {}
    void OnPlayerArrivalCommitted(CGameWorld&, CPlayer&) override;
    void OnPlayerEntityReady(CGameWorld&, CPlayer&, CPlayerFlower&, EPlayerSpawnReason) override;
    void OnPlayerDepartureCommitted(CGameWorld&, CPlayer&, CEntity*) override;
    std::string_view SnapshotKey() const override { return "world_controller.deathmatch"; }
    std::uint32_t SnapshotVersion() const override { return 1; }
    bool LocksPetalLoadout() const override { return false; }
    std::vector<SControllerParamInfo> ParamSchema() const override;

  private:
    static bool ValidateMapPath(const std::string& value);
    bool HandlePlayerCommand(CGameWorld&, CPlayer&, const std::vector<std::string>&) override
    {
        return false;
    }

    std::string m_map_path;
};
