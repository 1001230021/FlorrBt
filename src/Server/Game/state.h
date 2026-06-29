#pragma once
#include "../../Shared/shared.h"

class CMobBase;

constexpr float Endless = -1.f;

class CState {
public:
	CState(CMobBase* owner, float timer, ERarity rarity)
		: m_pOwner(owner), m_Timer(timer), m_Rarity(rarity) {}
	~CState() = default;
	virtual void Tick(float dt) = 0;

	CMobBase* m_pOwner;
	float m_Timer;
	ERarity m_Rarity;
};