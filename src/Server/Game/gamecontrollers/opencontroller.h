#pragma once
#include "open_world_rules.h"

class COpenController : public IGameController
{
  public:
    void OnTick(CGameWorld& world, float dt) override;
    std::optional<florrbt::Vector2f> SelectPlayerSpawn(CGameWorld&, CPlayer&, EPlayerSpawnReason) override;
    void OnEntityRemoved(CGameWorld&, CEntity&, EEntityRemovalReason) override;
    void OnMobDefeated(CGameWorld&, CMobBase&) override;
    EMobType PickHabitatMobType(CGameWorld&, florrbt::Vector2f) override;
    std::string_view SnapshotKey() const override { return "world_controller.open"; }
    bool CanRestoreSnapshotKey(std::string_view key) const override
    { return key == SnapshotKey() || (SnapshotKey() == "world_controller.open" && key == "world_controller.weather_diurnal_open"); }
    std::uint32_t SnapshotVersion() const override { return 2; }
    void CaptureSnapshot(CSnapshotWriter&) const override;
    bool RestoreSnapshot(const CSnapshotReader&, std::uint32_t version, std::string& error) override;
    void SpawnMobs(CGameWorld& world);
  protected:
    COpenWorldRules m_open_rules;
    COpenSpawnDirector& SpawnDirector() { return m_open_rules.SpawnDirector(); }
    const COpenSpawnDirector& SpawnDirector() const { return m_open_rules.SpawnDirector(); }
};
