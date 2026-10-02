#pragma once
#include "../../Shared/shared.h"
#include "../../Shared/math_types.h"

class CMobBase;

constexpr float endless = -1.f;

class CState
{
  public:
    CState(CMobBase* owner, float timer, ERarity rarity, EStateType type)
        : m_p_owner(owner), m_timer(timer), m_rarity(rarity), m_type(type)
    {
    }
    virtual ~CState() = default;

    virtual void Tick(float dt) = 0;
    virtual bool AffectsMovementTarget() const { return false; }
    virtual florrbt::Vector2f ModifyMovementTarget(const CMobBase&, const florrbt::Vector2f& target_pos) const
    {
        return target_pos;
    }
    EStateType GetType() const { return m_type; }

    CMobBase* m_p_owner = nullptr;
    float m_timer = endless;
    ERarity m_rarity = ERarity::Null;

  private:
    EStateType m_type = EStateType::None;
};
