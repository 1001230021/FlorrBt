#pragma once
#include "../controller.h"
#include "../entities/flower.h"

constexpr float target_time = 2.5f;

class CMeleeController : public IController
{
  public:
    void OnTick(CMobBase* mob, float dt) override;

    CEntity* m_pTarget;
    sf::Vector2f m_TargetPos;
    float m_ChangeTargetCount = 0.f;
};
