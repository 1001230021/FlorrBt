#pragma once
#include "../gamecontroller.h"
#include "../../../Shared/game_config.h"
#include <cstddef>

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
    float m_count = game_config::open_initial_spawn_delay;
    size_t m_idle_spawn_zone_cursor = 0;
};
