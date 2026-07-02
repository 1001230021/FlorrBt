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