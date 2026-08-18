#pragma once
#include "../controller_params.h"
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
    // Filled in once the room's game world has been created (lazily on start).
    std::uint32_t destination_world_id = invalid_world_id;
    // Game world blueprint used by the room system: the game world is created
    // with this map and controller when the waiting room starts.
    std::string game_map_path;
    std::string game_controller_key;
    // Construction-time overrides for the destination world controller, set
    // through /setting and validated against its declared ParamSchema.
    CControllerParams game_controller_params;
    // The room code this waiting world belongs to (-1 for plain waiting worlds).
    int room_code = -1;
};

class CWaitingController : public IGameController
{
  public:
    CWaitingController() = default;
    explicit CWaitingController(SWaitingControllerConfig config);

    void OnActivate(CGameWorld& world) override;
    void OnDeactivate(CGameWorld& world) override;
    void OnTick(CGameWorld& world, float dt) override;
    std::optional<sf::Vector2f> SelectPlayerSpawn(CGameWorld& world, CPlayer& player,
                                                  EPlayerSpawnReason reason) override;
    void OnPlayerEntityReady(CGameWorld& world, CPlayer& player, CPlayerFlower& flower,
                             EPlayerSpawnReason reason) override;
    void OnPlayerEnteredWorld(CGameWorld& world, CPlayer& player) override;
    void OnPlayerLeftWorld(CGameWorld& world, CPlayer& player) override;
    bool HandlePlayerCommand(CGameWorld& world, CPlayer& player,
                             const std::vector<std::string>& args) override;
    std::string_view SnapshotKey() const override { return "world_controller.waiting"; }
    void CaptureSnapshot(CSnapshotWriter& writer) const override;
    bool RestoreSnapshot(const CSnapshotReader& reader, std::uint32_t version, std::string& error) override;
    bool ResolveReferences(CGameWorld& world, const IGameWorldResolver& worlds, std::string& error) override;

    const SWaitingControllerConfig& GetConfig() const { return m_config; }

    // Marks the waiting room as "start requested". The controller releases the
    // waiting players into the destination world on the next tick.
    bool RequestStart() { m_start_requested = true; return true; }
    bool IsStartRequested() const { return m_start_requested; }

  private:
    bool ShouldRelease(size_t player_count) const;
    CGameWorld* ResolveDestinationWorld(CGameWorld& world) const;
    // Resolves the game world, creating it from the room blueprint on demand.
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
