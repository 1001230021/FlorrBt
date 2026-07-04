#pragma once
#include "../gamecontroller.h"

class COpenController : public IGameController
{
  public:
    COpenController() = default;
    void OnTick(CGameWorld& world, float dt) override;
    void OnPlayerConnect(CGameWorld& world, CPlayer* player) override;

  private:
    float m_count = 5.f;
};
