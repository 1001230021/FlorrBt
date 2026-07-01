#pragma once
#include "../controller.h"
#include "../../../Shared/shared.h"
#include <queue>

class CFlower;

class CPlayerController : public IController
{
  public:
    void OnTick(CMobBase* mob, float dt) override;

    void PushOperate(const ClientOperate& op);

  private:
    sf::Vector2f m_MoveDir = { 0.f, 0.f };

    std::queue<ClientOperate> m_OpQueue;

    void ExecuteOperate(const ClientOperate& op, CMobBase* mob);
};