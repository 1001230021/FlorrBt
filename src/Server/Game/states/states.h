#pragma once
#include "../../../Shared/shared.h"
#include "../entities/flower.h"
#include "../state.h"

class CPoisonState : public CState
{
  public:
    CPoisonState(CMobBase* owner, float timer, float basic_dmg, ERarity rarity, CEntity* applier);

    void Tick(float dt) override;

    bool IsValid() const { return m_is_valid; }
    float GetBasicDmg() const { return m_basic_dmg; }

  private:
    float m_basic_dmg = 0.f;
    CEntity* m_p_applier = nullptr;
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
    CInvincibleState(CMobBase* owner, float timer, ERarity rarity) : CState(owner, timer, rarity) {}

    void Tick(float dt) override { m_timer -= dt; }
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
float GetPincerSpeedMultiplier(const CMobBase* mob);
bool TrySharePsionicDamage(CMobBase* receiver, float dmg, CEntity* attacker, EDamageType dmg_type);
float GetBandageUndeadDuration(ERarity rarity);
