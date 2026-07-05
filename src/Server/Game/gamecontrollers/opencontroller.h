#pragma once
#include "../gamecontroller.h"
#include "../../../Shared/game_config.h"

class COpenController : public IGameController
{
  public:
    COpenController() = default;
    void OnTick(CGameWorld& world, float dt) override;
    void OnPlayerConnect(CGameWorld& world, CPlayer* player) override;

  private:
    float m_count = game_config::open_initial_spawn_delay;
};
