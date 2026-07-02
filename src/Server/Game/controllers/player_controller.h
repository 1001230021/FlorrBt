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

  private:
    void ExecuteOperate(const ClientOperate& op, CMobBase* mob);

    sf::Vector2f m_move_dir = {0.f, 0.f};
    std::queue<ClientOperate> m_op_queue;
};