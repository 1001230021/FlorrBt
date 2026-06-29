#pragma once 
#include "../controller.h"
#include "../entities/flower.h"

class CMeleeController : public IController {
public:
	void OnTick(CMobBase* mob, float dt) override;

	CEntity* m_pTarget;
	sf::Vector2f m_TargetPos;
};
