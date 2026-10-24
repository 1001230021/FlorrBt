#pragma once
#include "entity.h"
#include "../../Shared/shared.h"
#include "../../Shared/math_types.h"

class CMobBase;
class CGameWorld;

constexpr float endless = -1.f;

class CState
{
  public:
    CState(CMobBase* owner, float timer, ERarity rarity, EStateType type);
    virtual ~CState() = default;

    virtual void Tick(float dt) = 0;
    virtual void RemapTransferredReferences(TEntityHandleRemap) noexcept {}
    virtual bool AffectsMovementTarget() const { return false; }
    virtual florrbt::Vector2f ModifyMovementTarget(const CMobBase&, const florrbt::Vector2f& target_pos) const
    {
        return target_pos;
    }
    EStateType GetType() const { return m_type; }

    // The state does not own its mob.  Resolve the owner through a qualified
    // entity handle instead of keeping an unchecked long-lived mob pointer.
    // The world pointer is only a resolver context and is cleared together
    // with the handle before the owning mob starts destruction.
    CMobBase* ResolveOwner() const;
    void RebindOwner(CMobBase* owner);
    void DetachOwner();

    float m_timer = endless;
    ERarity m_rarity = ERarity::Null;

  private:
    CGameWorld* m_p_owner_world = nullptr;
    SEntityHandle m_owner_ref;
    EStateType m_type = EStateType::None;
};
