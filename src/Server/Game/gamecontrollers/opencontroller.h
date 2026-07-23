#pragma once
#include "../gamecontroller.h"
#include "../../../Shared/game_config.h"
#include <cstddef>
#include <cstdint>
#include <vector>

class COpenController : public IGameController
{
  public:
    COpenController() = default;
    void OnTick(CGameWorld& world, float dt) override;
    void OnPlayerConnect(CGameWorld& world, CPlayer* player) override;
    void OnPlayerSpawn(CGameWorld& world, CPlayer* player, CEntity* entity) override;
    void OnEntityDie(CGameWorld& world, CEntity* entity) override;
    void ModifyTalentContext(CGameWorld& world, CPlayer* player, ETalentEvent event, STalentContext& ctx) override;
    void SpawnMobs(CGameWorld& world);

  private:
    struct single_spawn_zone_mob
    {
        int id = -1;
        std::uint64_t generation = 0;
    };

    bool SingleSpawnZoneMobAlive(CGameWorld& world, size_t zone_index) const;
    void RememberSingleSpawnZoneMob(size_t zone_index, const CEntity* entity);
    void ForgetSingleSpawnZoneMob(const CEntity* entity);

    float m_count = game_config::open_initial_spawn_delay;
    size_t m_spawn_zone_cursor = 0;
    size_t m_idle_spawn_zone_cursor = 0;
    std::vector<single_spawn_zone_mob> m_single_spawn_zone_mobs;
};
