#pragma once
#include "../../../Shared/shared.h"
#include "../entities/flower.h"
#include "../state.h"
#include <cstdint>

class CPoisonState : public CState
{
  public:
    CPoisonState(CMobBase* owner, float timer, float basic_dmg, ERarity rarity, CEntity* applier);

    void Tick(float dt) override;

    bool IsValid() const { return m_is_valid; }
    float GetBasicDmg() const { return m_basic_dmg; }

  private:
    float m_basic_dmg = 0.f;
    int m_applier_id = -1;
    std::uint64_t m_applier_generation = 0;
    bool m_is_valid = false;
};

class CBanSlotState : public CState
{
  public:
    CBanSlotState(CMobBase* owner, float timer, int slot, ERarity rarity);
    ~CBanSlotState() override;
    void Tick(float dt) override;

  private:
    int m_slot_index = -1;
    CFlower* m_p_flower = nullptr;
};

class CPincerSpeedReduceState : public CState
{
  public:
    CPincerSpeedReduceState(CMobBase* owner, float timer, ERarity rarity);

    void Tick(float dt) override { m_timer -= dt; }
    bool IsValid() const { return m_is_valid; }

  private:
    bool m_is_valid = false;
};

class CWebSpeedReduceState : public CState
{
  public:
    CWebSpeedReduceState(CMobBase* owner, float timer, float desired_multiplier, float reference_mass);

    void Tick(float dt) override { m_timer -= dt; }
    bool IsValid() const { return m_is_valid; }
    float GetMultiplier() const { return m_multiplier; }

  private:
    float m_multiplier = 1.f;
    bool m_is_valid = false;
};

class CAntiHealState : public CState
{
  public:
    CAntiHealState(CMobBase* owner, float timer, ERarity rarity, float medic_mult);

    void Tick(float dt) override { m_timer -= dt; }
    void Refresh(float timer, ERarity rarity, float medic_mult);
    float GetMedicMultiplier() const { return m_medic_mult; }

    float m_medic_mult = 1.f;
};

class CNullificationState : public CState
{
  public:
    CNullificationState(CMobBase* owner, float timer, ERarity rarity) : CState(owner, timer, rarity) {}

    void Tick(float dt) override { if (m_timer != endless) m_timer -= dt; }
};

class CUndeadState : public CState
{
  public:
    CUndeadState(CMobBase* owner, float timer, ERarity rarity, int source_slot);
    ~CUndeadState() override;

    void Tick(float dt) override { m_timer -= dt; }
    int SourceSlot() const { return m_source_slot; }
    void CancelDeathOnDestroy() { m_kill_on_destroy = false; }

  private:
    int m_source_slot = -1;
    bool m_kill_on_destroy = true;
};

class CCorruptionState : public CState
{
  public:
    CCorruptionState(CMobBase* owner, float timer, ERarity rarity);
    ~CCorruptionState() override;

    void Tick(float dt) override { if (m_timer != endless) m_timer -= dt; }

  private:
    int m_old_team = 0;
};

class CNoReviveState : public CState
{
  public:
    CNoReviveState(CMobBase* owner, float timer, ERarity rarity) : CState(owner, timer, rarity) {}

    void Tick(float dt) override { m_timer -= dt; }
};

class CInvincibleState : public CState
{
  public:
    CInvincibleState(CMobBase* owner, float timer, ERarity rarity);

    void Tick(float dt) override;

  private:
    bool m_marked_for_destroy = false;
    float m_hp = 0.f;
};

class CDiggingState : public CState
{
  public:
    CDiggingState(CMobBase* owner, float timer, ERarity rarity) : CState(owner, timer, rarity) {}
    ~CDiggingState() override;

    void Tick(float dt) override { if (m_timer != endless) m_timer -= dt; }
};

class CPsionicConnectionState : public CState
{
  public:
    CPsionicConnectionState(CMobBase* owner, float timer, ERarity rarity);

    void Tick(float dt) override { if (m_timer != endless) m_timer -= dt; }
    bool IsValid() const { return m_is_valid; }

  private:
    bool m_is_valid = false;
};

bool BlocksNullifiedInteraction(const CEntity* lhs, const CEntity* rhs);
bool IsDiggingEntity(const CEntity* entity);
bool ShouldSkipDiggingCollision(const CEntity* lhs, const CEntity* rhs);
float GetPincerSpeedMultiplier(const CMobBase* mob);
float GetMedicMultiplier(const CMobBase* mob);
void ApplyDandelionAntiHeal(CMobBase* mob, ERarity rarity);
bool TrySharePsionicDamage(CMobBase* receiver, float dmg, CEntity* attacker, EDamageType dmg_type);
float GetBandageUndeadDuration(ERarity rarity);
