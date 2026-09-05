#pragma once
#include "../controller_params.h"
#include "../gamecontroller.h"
#include "opencontroller.h"
#include <cstdint>
#include <unordered_map>

class CGameWorld;
class CPlayer;
class CPlayerFlower;

// PvP controller for the ant-kingdom room worlds. Players released from the
// waiting room each get their own team (2, 3) up to the fixed 2-player cap;
// anyone joining beyond the cap is assigned to the spectator team (1 by
// default). The petal loadout is stripped on entry and equip/unequip is
// rejected for the whole world.
//
// Construction parameters (overridable from the waiting room /setting):
//   start_team       - first player team (default pvp_start_team, >= 2)
//   spectator_team   - over-cap join team (default pvp_spectator_team, >= 1)
//   lock_loadout     - strip and lock petal loadouts (default pvp_lock_loadout)
//
// Virtually inherits COpenController so a diamond child (e.g. the ant-kingdom
// controller) can combine this base with other open-world bases.
class CAntHelPvpController : virtual public COpenController
{
  public:
    static constexpr int kSpectatorTeam = 1;
    // Hard-coded room cap: two teams / two players.
    static constexpr int kMaxPlayers = 2;

    explicit CAntHelPvpController(CControllerParams params = {});

    void OnTick(CGameWorld& world, float dt) override;
    void OnPlayerEntityReady(CGameWorld& world, CPlayer& player, CPlayerFlower& flower,
                             EPlayerSpawnReason reason) override;
    void OnPlayerEnteredWorld(CGameWorld& world, CPlayer& player) override;
    void OnPlayerLeftWorld(CGameWorld& world, CPlayer& player) override;
    std::string_view SnapshotKey() const override { return "world_controller.pvp"; }
    std::uint32_t SnapshotVersion() const override { return 2; }
    void CaptureSnapshot(CSnapshotWriter& writer) const override;
    bool RestoreSnapshot(const CSnapshotReader& reader, std::uint32_t version, std::string& error) override;
    // No petals in the ant kingdom: equip and unequip are both rejected while
    // the loadout is locked.
    bool LocksPetalLoadout() const override { return m_lock_loadout; }
    std::vector<SControllerParamInfo> ParamSchema() const override { return ParamSchemaStatic(); }

    // The team assigned to a player (0 = not assigned, kSpectatorTeam for
    // over-cap joins). Teams of "in the match" players are >= start_team.
    int PlayerTeam(std::uint32_t player_id) const;
    bool HasMatchTeam(std::uint32_t player_id) const;

  private:
    static std::vector<SControllerParamInfo> ParamSchemaStatic();
    static bool ValidateStartTeam(const std::string& value);
    static bool ValidateSpectatorTeam(const std::string& value);
    static bool ValidateLockLoadout(const std::string& value);

    int AssignTeam(CPlayer& player);
    void StripPlayerPetals(CPlayer& player);
    void MaintainStrip(CGameWorld& world) const;

    bool m_started = false;
    int m_start_team = 2;
    int m_next_team = 2;
    int m_spectator_team = kSpectatorTeam;
    bool m_lock_loadout = true;
    std::unordered_map<std::uint32_t, int> m_player_teams;
};