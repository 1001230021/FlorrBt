#pragma once

#include "entity.h"
#include <algorithm>
#include <cstdint>
#include <string>

class CGameWorld;

class CChainEntity;

// A Linker petal implements this lease. The chain never stores a raw pointer
// to it; it resolves the id/generation pair and calls this interface once when
// the chain is broken.
class IChainLease
{
  public:
    virtual ~IChainLease() = default;
    virtual void OnChainBroken(CChainEntity& chain) = 0;
};

struct SChainEndpointRef
{
    int id = -1;
    std::uint64_t generation = 0;

    bool IsValid() const { return id >= 0 && generation != 0; }
};

class CChainEntity final : public CEntity
{
  public:
    CChainEntity(CGameWorld* world, CEntity& first, CEntity& second, float max_length, float integrity,
                 CEntity* lease_owner);

    void Tick(float) override {}
    bool CanCollide() const override { return false; }
    bool CollidesWithWalls() const override { return false; }
    bool IsVisible() const override { return !IsMarkedForDes(); }
    void TakeDamage(float, CEntity*, EDamageType) override {}
    void OnBeforeRemoved(EEntityRemovalReason) override;

    void ApplyConstraint(float dt);
    void Break();
    bool ConvertEndpoint(CEntity& from, CEntity& replacement);
    bool HasEndpoint(const CEntity& entity) const;
    bool HasLease(const CEntity& entity) const;

    bool IsBroken() const { return IsMarkedForDes(); }
    float MaxLength() const { return m_max_length; }
    float MaxIntegrity() const { return m_max_integrity; }
    float Integrity() const { return std::max(0.f, Health()); }
    float Tension() const { return m_tension; }
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
    float m_tension = 0.f;
    bool m_lease_notified = false;
};
