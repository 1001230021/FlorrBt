#pragma once
#include "../../../Shared/game_config.h"
#include "../entity.h"
#include <cstdint>

class CMobBase;
class CSnapshotReader;
class CSnapshotWriter;

// Pickup behavior base (可拾取基类): a mob controller may forcibly control
// another entity by "carrying" it. The shared mechanics are transplanted from
// the leafcutter soldier: world-registry keep-alive, forward position lock,
// mass-weighted wall resolution and turn limiting. Derived controllers (the
// leafcutter soldier and the special hornet) override the virtual hooks
// (虚标) for eligibility, reach and carry state transitions.
class CPickupController
{
  public:
    virtual ~CPickupController() = default;

    CEntity* GetCarried(const CMobBase* mob) const;
    bool IsCarrying(const CMobBase* mob, const CEntity* carried) const;
    bool TryPickup(CMobBase* mob, CEntity* candidate);
    void DropCarried(CMobBase* mob);
    void DropCarriedFromWorld(CMobBase* mob, CGameWorld& source, const SEntityHandle& old_carrier);
    void TickCarried(CMobBase* mob, float dt);
    void SyncCarriedTransform(CMobBase* mob);
    virtual void ResolveCarriedConstraint(CMobBase* mob);
    void CaptureCarriedSnapshot(CSnapshotWriter& writer) const;
    void RestoreCarriedSnapshot(const CSnapshotReader& reader);

  protected:
    // 虚标: eligibility, reach and carry-transition hooks.
    virtual bool CanCarry(const CMobBase* mob, const CEntity* carried) const = 0;
    virtual float CarryContactRadius(const CMobBase* mob, const CEntity* carried) const;
    virtual float CarryPickupCooldown() const;
    virtual void OnCarryChanged(CMobBase* mob, CEntity* carried, bool now_carrying);
    void ApplyCarryingTurn(CMobBase* mob, float previous_angle, bool had_facing, float dt) const;

    // Lazy canonicalization is performed by GetCarried() for legacy
    // snapshots, so const inspection may still repair the cached handle.
    mutable SEntityHandle m_carried_ref;
    float m_carry_pickup_timer = 0.f;
};
