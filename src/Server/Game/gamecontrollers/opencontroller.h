#pragma once
#include "../gamecontroller.h"
#include "../../../Shared/game_config.h"

class COpenController : public IGameController
{
  public:
    COpenController() = default;
    void OnTick(CGameWorld& world, float dt) override;
    void OnPlayerConnect(CGameWorld& world, CPlayer* player) override;
    void OnPlayerSpawn(CGameWorld& world, CPlayer* player, CEntity* entity) override;
    void OnEntityDie(CGameWorld& world, CEntity* entity) override;
    void SpawnMobs(CGameWorld& world);

  private:
    float m_count = game_config::open_initial_spawn_delay;
};
