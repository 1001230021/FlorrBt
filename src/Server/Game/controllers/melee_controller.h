#pragma once
#include "../controller.h"
#include "../entities/flower.h"
#include "../../../Shared/game_config.h"

class CMeleeController : public IController
{
  public:
    void OnTick(CMobBase* mob, float dt) override;

  protected:
    void PickRandomTargetPos(CMobBase* mob, const SMobStats& stats);

    CEntity* m_p_target = nullptr;
    sf::Vector2f m_target_pos = {0.f, 0.f};
    float m_change_target_count = game_config::melee_target_time;
    bool m_has_random_target_pos = false;
};

class CSummonedMeleeController : public CMeleeController
{
  public:
    explicit CSummonedMeleeController(int owner_id) : m_owner_id(owner_id) {}

    void OnTick(CMobBase* mob, float dt) override;

  private:
    int m_owner_id = -1;
};
