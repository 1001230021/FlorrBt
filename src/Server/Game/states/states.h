#pragma once
#include "../../../Shared/shared.h"
#include "../state.h"
#include "../entities/flower.h"

// ============ Posion ============
class CPoisonState : public CState {
public:
    CPoisonState(CMobBase* owner, float timer, float basicDmg, ERarity rarity, CEntity* applier);

    void Tick(float dt) override;

    bool IsValid() const { return m_IsValid; }
    float GetBasicDmg() const { return m_BasicDmg; }

private:
    float m_BasicDmg;
    CEntity* m_pApplier;
    bool m_IsValid = false;
};

// ============ BanSlot ============
class CBanSlotState : public CState {
public:
    CBanSlotState(CMobBase* owner, float timer, int slot, ERarity rarity);
    ~CBanSlotState();
    void Tick(float dt) override;
private:
    int m_SlotIdx;
    CFlower* m_pFlower;
};