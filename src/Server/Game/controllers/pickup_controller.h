#pragma once
#include "../../../Shared/game_config.h"
#include "../entity.h"
#include <cstdint>

class CMobBase;
class CSnapshotReader;
class CSnapshotWriter;

// 携带行为共用活跃登记、前方位置约束、质量加权墙体处理和转向限制。
// 派生控制器通过钩子定义拾取资格、范围及携带状态变化。
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
    virtual bool CanCarry(const CMobBase* mob, const CEntity* carried) const = 0;
    virtual float CarryContactRadius(const CMobBase* mob, const CEntity* carried) const;
    virtual float CarryPickupCooldown() const;
    virtual void OnCarryChanged(CMobBase* mob, CEntity* carried, bool now_carrying);
    void ApplyCarryingTurn(CMobBase* mob, float previous_angle, bool had_facing, float dt) const;

    // 旧快照采用延迟句柄补全，因此只读查询也可能修复缓存身份。
    mutable SEntityHandle m_carried_ref;
    float m_carry_pickup_timer = 0.f;
};
