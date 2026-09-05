#pragma once
#include "opencontroller.h"
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

class CPlayer;
class CPlayerFlower;
class CMobBase;

// Sandbox world: inherits the open controller but the room maintains its
// own player inventory/loadout copy (in-world changes never write back to
// the persistent account), exposes the server-side admin commands to
// players (player argument optional; defaults to the sender), and gates
// those commands behind a permission system when the room enables it.
class CSandboxController : public COpenController
{
  public:
    CSandboxController(CControllerParams params);
    std::string_view SnapshotKey() const override { return "world_controller.sandbox"; }
    std::uint32_t SnapshotVersion() const override { return 1; }
    std::vector<SControllerParamInfo> ParamSchema() const override;
    bool HandlePlayerCommand(CGameWorld& world, CPlayer& player, const std::vector<std::string>& args) override;
    void OnPlayerEntityReady(CGameWorld& world, CPlayer& player, CPlayerFlower& flower,
                             EPlayerSpawnReason reason) override;
    void OnPlayerLeftWorld(CGameWorld& world, CPlayer& player) override;
    void OnMobDefeated(CGameWorld& world, CMobBase& mob) override;
    void OnTick(CGameWorld& world, float dt) override;
    void CaptureSnapshot(CSnapshotWriter& writer) const override;
    bool RestoreSnapshot(const CSnapshotReader& reader, std::uint32_t version, std::string& error) override;

  private:
    bool HandlePermCommand(CGameWorld& world, CPlayer& player, const std::vector<std::string>& args);
    bool HandleSandboxCommand(CGameWorld& world, CPlayer& player, const std::vector<std::string>& args);
    CPlayer* FindSandboxPlayer(CGameWorld& world, std::string_view name) const;
    bool IsAdmin(uint32_t player_id) const { return m_admin_ids.contains(player_id); }
    void SendSystem(CGameWorld& world, CPlayer& player, const std::string& message) const;
    static bool ValidateBool(const std::string& value);
    static bool ValidateMapPath(const std::string& value);

    bool m_enable_permission = false;
    bool m_enable_spawn = true;
    bool m_enable_loot = true;
    std::string m_map_path;
    std::unordered_set<uint32_t> m_admin_ids;
    std::unordered_map<std::uint32_t, std::string> m_original_accounts;
};
