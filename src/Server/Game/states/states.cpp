#include "states.h"
#include <cmath>

CPoisonState::CPoisonState(CMobBase* owner, float timer, float basic_dmg, ERarity rarity, CEntity* applier)
    : CState(owner, timer, rarity), m_basic_dmg(basic_dmg), m_p_applier(applier)
{
    auto existing_states = owner ? owner->FindStates<CPoisonState>() : std::vector<CPoisonState*>{};
    CPoisonState* existing = existing_states.empty() ? nullptr : existing_states[0];

    if (!existing)
    {
        m_is_valid = true;
        return;
    }

    bool should_replace = false;
    if (GetLevel(rarity) > GetLevel(existing->m_rarity))
    {
        should_replace = true;
    } else if (GetLevel(rarity) == GetLevel(existing->m_rarity)) {
        if (basic_dmg > existing->m_basic_dmg) should_replace = true;
        else if (basic_dmg == existing->m_basic_dmg && timer > existing->m_timer) should_replace = true;
    }

    if (should_replace)
    {
        owner->RemoveState(existing);
        m_is_valid = true;
    }
}

void CPoisonState::Tick(float dt)
{
    if (!m_is_valid || !m_p_owner) return;

    m_p_owner->TakeDamage(m_basic_dmg * dt * std::pow(3.f, static_cast<float>(GetLevel(m_rarity) - 1)), m_p_applier,
                          EDamageType::Poison);
    m_timer -= dt;
}

CBanSlotState::CBanSlotState(CMobBase* owner, float timer, int slot, ERarity rarity)
    : CState(owner, timer, rarity), m_slot_index(slot)
{
    if (auto* flower = dynamic_cast<CFlower*>(owner))
    {
        flower->SetBanned(true, slot);
        m_p_flower = flower;
    }
}

CBanSlotState::~CBanSlotState()
{
    if (m_p_flower) m_p_flower->SetBanned(false, m_slot_index);
}

void CBanSlotState::Tick(float dt)
{
    m_timer -= dt;
}
