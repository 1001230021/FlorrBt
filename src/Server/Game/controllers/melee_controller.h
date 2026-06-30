#pragma once
#include "../controller.h"
#include "../entities/flower.h"

constexpr float target_time = 8.f;

class CMeleeController : public IController
{
  public:
    virtual void OnTick(CMobBase* mob, float dt) override;

    float m_Angle = 0;
    float m_VelAngle = 0;

    CEntity* m_pTarget;
    sf::Vector2f m_TargetPos;
    float m_ChangeTargetCount = 0.f;
};
