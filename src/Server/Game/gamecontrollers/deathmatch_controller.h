#pragma once
#include "../gamecontroller.h"
#include <string>
#include <string_view>
#include <vector>

class CPlayer;

// Deathmatch world: every player is teamless (team 0), free loadouts and
// talents, everyone fights everyone. The room params mirror the waiting
// room map setting so the map can be swapped before /start.
class CDeathmatchController : public IGameController
{
  public:
    CDeathmatchController(CControllerParams params);
    void OnTick(CGameWorld&, float) override {}
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
