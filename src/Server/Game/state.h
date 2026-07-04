#pragma once
#include "../../Shared/shared.h"

class CMobBase;

constexpr float endless = -1.f;

class CState
{
  public:
    CState(CMobBase* owner, float timer, ERarity rarity) : m_p_owner(owner), m_timer(timer), m_rarity(rarity) {}
    virtual ~CState() = default;

    virtual void Tick(float dt) = 0;

    CMobBase* m_p_owner = nullptr;
    float m_timer = endless;
    ERarity m_rarity = ERarity::Null;
};
