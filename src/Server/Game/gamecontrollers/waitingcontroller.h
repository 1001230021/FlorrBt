#pragma once
#include "../gamecontroller.h"
#include <cstddef>
#include <cstdint>
#include <limits>
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
    std::uint32_t destination_world_id = invalid_world_id;
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
    std::string_view SnapshotKey() const override { return "world_controller.waiting"; }
    void CaptureSnapshot(CSnapshotWriter& writer) const override;
    bool RestoreSnapshot(const CSnapshotReader& reader, std::uint32_t version, std::string& error) override;
    bool ResolveReferences(CGameWorld& world, const IGameWorldResolver& worlds, std::string& error) override;

    const SWaitingControllerConfig& GetConfig() const { return m_config; }

  private:
    bool ShouldRelease(size_t player_count) const;
    CGameWorld* ResolveDestinationWorld(CGameWorld& world) const;
    size_t CountWaitingPlayers(CGameWorld& world) const;
    std::vector<CPlayer*> CollectWaitingPlayers(CGameWorld& world) const;
    void SetPlayerPetalsBanned(CPlayer* player, bool banned) const;
    void MaintainPetalBans(CGameWorld& world) const;
    void ReleasePlayers(CGameWorld& world, CGameWorld& destination_world);

    SWaitingControllerConfig m_config;
};
