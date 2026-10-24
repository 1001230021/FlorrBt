#pragma once
#include "open_spawn_director.h"
#include "../../../Shared/math_types.h"
#include "../gamecontroller.h"
#include <cstdint>
#include <vector>

class CPlayer;
struct SWeatherDiurnalSnapshot;

class COpenController : public IGameController
{
  public:
    COpenController() = default;
    void OnActivate(CGameWorld& world) override;
    void OnTick(CGameWorld& world, float dt) override;
    std::optional<florrbt::Vector2f> SelectPlayerSpawn(CGameWorld& world, CPlayer& player,
                                                  EPlayerSpawnReason reason) override;
    void OnEntityRemoved(CGameWorld& world, CEntity& entity, EEntityRemovalReason reason) override;
    void OnMobDefeated(CGameWorld& world, CMobBase& mob) override;
    EMobType PickHabitatMobType(CGameWorld& world, florrbt::Vector2f pos) override;
    std::string_view SnapshotKey() const override { return "world_controller.open"; }
    bool CanRestoreSnapshotKey(std::string_view key) const override
    { return key == SnapshotKey() || (SnapshotKey() == "world_controller.open" && key == "world_controller.weather_diurnal_open"); }
    std::uint32_t SnapshotVersion() const override { return 2; }
    void CaptureSnapshot(CSnapshotWriter& writer) const override;
    bool RestoreSnapshot(const CSnapshotReader& reader, std::uint32_t version, std::string& error) override;
    void ModifyTalentContext(CGameWorld& world, CPlayer* player, ETalentEvent event, STalentContext& ctx) override;
    void SpawnMobs(CGameWorld& world);

  protected:
    // Sandbox and derived controllers may keep spawn-director bookkeeping
    // in sync while overriding the reward path.
    COpenSpawnDirector& SpawnDirector() { return m_spawn_director; }
    const COpenSpawnDirector& SpawnDirector() const { return m_spawn_director; }

  private:
    void InitializePreciseSpawns(CGameWorld& world);
    void UpdateSpawnWave(float dt);

    COpenSpawnDirector m_spawn_director;
    bool m_precise_spawns_initialized = false;
    float m_spawn_wave_time = 0.f;
    float m_spawn_density_multiplier = 0.6f;
};
