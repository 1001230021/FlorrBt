#include "states.h"

// =========== Posion ===========
CPoisonState::CPoisonState(CMobBase* owner, float timer, float basicDmg, ERarity rarity, CEntity* applier)
    : CState(owner, timer, rarity), m_BasicDmg(basicDmg), m_pApplier(applier)
{
    auto existingVec = owner->FindStates<CPoisonState>();
    CPoisonState* existing = existingVec.empty() ? nullptr : existingVec[0];

    if (existing) {
        bool shouldReplace = false;
        if (GetLevel(rarity) > GetLevel(existing->m_Rarity)) shouldReplace = true;
        else if (GetLevel(rarity) == GetLevel(existing->m_Rarity))
        {
            if (basicDmg > existing->m_BasicDmg) shouldReplace = true;
            else if (basicDmg == existing->m_BasicDmg)
            {
                if (timer > existing->m_Timer) shouldReplace = true;
            }
        } if (shouldReplace) {
            owner->RemoveState(existing);
            m_IsValid = true;
        } else {
            m_IsValid = false;
        }
    } else {
        m_IsValid = true;
    }
}

void CPoisonState::Tick(float dt)
{
    if (!m_IsValid || !m_pOwner) return;
    m_pOwner->TakeDamage(m_BasicDmg * dt * pow(3, GetLevel(m_Rarity) - 1), m_pApplier, EDamageType::Poison);
    m_Timer -= dt;
}

// =========== BanSlot ===========
CBanSlotState::CBanSlotState(CMobBase* owner, float timer, int slot, ERarity rarity)
    : CState(owner, timer, rarity), m_SlotIdx(slot)
{
    if (CFlower* flower = dynamic_cast<CFlower*>(owner))
    {
        flower->SetBanned(true, slot);
        m_pFlower = flower;
    } else {
        m_pFlower = nullptr;
    }
}

CBanSlotState::~CBanSlotState()
{
    if(m_pFlower)
        m_pFlower->SetBanned(false, m_SlotIdx);
}

void CBanSlotState::Tick(float dt) { m_Timer -= dt; }