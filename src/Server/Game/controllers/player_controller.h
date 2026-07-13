#pragma once
#include "../../../Shared/shared.h"
#include "../controller.h"
#include <queue>

class CFlower;

class CPlayerController : public IController
{
  public:
    void OnTick(CMobBase* mob, float dt) override;

    void PushOperate(const ClientOperate& op);
    void ResetOperate();

  private:
    void ExecuteOperate(const ClientOperate& op, CMobBase* mob);
    void TryManualAttack(CMobBase* mob);

    sf::Vector2f m_move_dir = {0.f, 0.f};
    sf::Vector2f m_aim_dir = {1.f, 0.f};
    bool m_has_aim_dir = false;
    std::queue<ClientOperate> m_op_queue;
};
