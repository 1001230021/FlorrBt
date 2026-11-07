#pragma once

#include "entity.h"
#include <algorithm>
#include <cstdint>
#include <string>

class CGameWorld;

class CChainEntity;

// 链条按完整句柄解析 Linker；断链时通过租约接口通知一次，不长期借用花瓣指针。
class IChainLease
{
  public:
    virtual ~IChainLease() = default;
    virtual void OnChainBroken(CChainEntity& chain) = 0;
};

using SChainEndpointRef = SEntityHandle;

class CChainEntity final : public CEntity
{
  public:
    CChainEntity(CGameWorld* world, CEntity& first, CEntity& second, float max_length, float integrity,
                 float elasticity, CEntity* lease_owner);

    void OnTickActions(float) override {}
    bool CanCollide() const override { return false; }
    bool CollidesWithWalls() const override { return false; }
    bool IsVisible() const override { return !IsMarkedForDes(); }
    void OnTakeDamage(float, CEntity*, EDamageType) override {}
    void OnBeforeRemoved(EEntityRemovalReason) override;

    void ApplyConstraint(float dt);
    void ValidateReferences();
    void Break();
    bool ConvertEndpoint(CEntity& from, CEntity& replacement);
    bool HasEndpoint(const CEntity& entity) const;
    bool HasLease(const CEntity& entity) const;

    bool IsBroken() const { return IsMarkedForDes(); }
    float MaxLength() const { return m_max_length; }
    float MaxIntegrity() const { return m_max_integrity; }
    float Integrity() const { return std::max(0.f, Health()); }
    float Tension() const { return m_tension; }
    float Elasticity() const { return m_elasticity; }
    bool HasLease() const { return m_lease_ref.IsValid(); }
    std::string SnapshotName() const;
    void RestoreState(float integrity, float tension)
    {
        Health() = std::clamp(integrity, 0.f, m_max_integrity);
        m_tension = std::clamp(tension, 0.f, 1.f);
    }
    SChainEndpointRef FirstRef() const { return m_first; }
    SChainEndpointRef SecondRef() const { return m_second; }
    SChainEndpointRef LeaseRef() const { return m_lease_ref; }

  private:
    CEntity* Resolve(const SChainEndpointRef& ref) const;
    void UpdateGeometry(const CEntity* first, const CEntity* second);
    void NotifyLeaseOnce();

    SChainEndpointRef m_first;
    SChainEndpointRef m_second;
    SChainEndpointRef m_lease_ref;
    float m_max_length = 0.f;
    float m_max_integrity = 1.f;
    // 零为定长约束，正值为弹簧，负值为单向绳索。
    float m_elasticity = -1.f;
    float m_tension = 0.f;
    bool m_lease_notified = false;
};
