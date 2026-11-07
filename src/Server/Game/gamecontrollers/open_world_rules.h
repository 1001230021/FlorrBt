#pragma once
#include "open_spawn_director.h"
#include "../gamecontroller.h"

// 可组合的开放世界规则，不承担玩法控制器身份或生命周期。
class COpenWorldRules
{
  public:
    void OnTick(CGameWorld& world, float dt);
    void InitializePreciseSpawns(CGameWorld& world);
    void SpawnMobs(CGameWorld& world);
    std::optional<florrbt::Vector2f> SelectPlayerSpawn(CGameWorld&, CPlayer&, EPlayerSpawnReason);
    EMobType PickHabitatMobType(CGameWorld&, florrbt::Vector2f);
    void OnEntityRemoved(CGameWorld&, CEntity&, EEntityRemovalReason);
    void OnMobDefeated(CGameWorld&, CMobBase&);
    void CaptureSnapshot(CSnapshotWriter&) const;
    bool RestoreSnapshot(const CSnapshotReader&, std::uint32_t version, std::string& error);
    void ModifyTalentContext(CGameWorld&, CPlayer*, ETalentEvent, STalentContext&);
    COpenSpawnDirector& SpawnDirector() { return m_spawn_director; }
    const COpenSpawnDirector& SpawnDirector() const { return m_spawn_director; }
  private:
    void UpdateSpawnWave(float dt);
    COpenSpawnDirector m_spawn_director;
    bool m_precise_spawns_initialized = false;
    float m_spawn_wave_time = 0.f;
    float m_spawn_density_multiplier = 0.6f;
};
