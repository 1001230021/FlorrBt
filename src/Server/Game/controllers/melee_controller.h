#pragma once
#include "../controller.h"
#include "../entities/flower.h"

constexpr float target_time = 16.f;

class CMeleeController : public IController
{
  public:
    void OnTick(CMobBase* mob, float dt) override;

  private:
    void PickRandomTargetPos(CMobBase* mob, const SMobStats& stats);

    CEntity* m_p_target = nullptr;
    sf::Vector2f m_target_pos = {0.f, 0.f};
    float m_change_target_count = target_time;
    bool m_has_random_target_pos = false;
};
