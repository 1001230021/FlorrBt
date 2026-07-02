#include "flower.h"
#include "petals/petal.h"

void CFlower::Tick(float dt)
{
    CMob::Tick(dt);
    RebuildFinalStats();

    m_total_copies = 0;
    for (auto& slot : m_slots)
    {
        if (!slot.m_available || slot.m_banned)
        {
            for (CPetal*& petal : slot.m_p_petals)
            {
                if (petal) petal->m_is_marked_for_des = true;
                petal = nullptr;
            }
            slot.m_start_copy_index = -1;
            continue;
        }

        int copies = slot.GetCurrentCopyCount();
        if (copies > 0)
        {
            slot.m_start_copy_index = m_total_copies;
            m_total_copies += copies;
        } else {
            slot.m_start_copy_index = -1;
        }
        slot.Tick(dt, this);
    }
}

void CFlower::TakeDamage(float dmg, CEntity* attacker, EDamageType damage_type)
{
    for (auto& slot : GetSlots())
    {
        if (!slot.m_p_proto || !slot.m_p_proto->m_p_behavior) continue;

        for (auto* petal : slot.m_p_petals)
        {
            if (petal && !petal->m_is_marked_for_des)
            {
                slot.m_p_proto->m_p_behavior->OnFlowerTakeDamage(petal, slot.m_stored_rarity, this, dmg, damage_type,
                                                                attacker);
            }
        }
    }
    CMob::TakeDamage(dmg, attacker, damage_type);
}

int CFlower::GetStartCopyIndex(int slot_index) const
{
    if (slot_index >= 0 && slot_index < static_cast<int>(m_slots.size())) return m_slots[slot_index].m_start_copy_index;
    return -1;
}

void CFlower::RebuildFinalStats()
{
    float old_max = m_final_stats.max_health;

    m_final_stats = m_base_stats;
    for (const auto& slot : m_slots)
    {
        slot.ApplyStatsTo(m_final_stats);
    }

    float new_max = m_final_stats.max_health;
    if (old_max > 0.f && new_max != old_max) m_health = m_health * new_max / old_max;
}

void CFlower::EquipPetal(int slot_index, const CPetalPrototype* proto, ERarity rarity)
{
    if (slot_index < 0 || slot_index >= static_cast<int>(m_slots.size())) return;
    if (!m_slots[slot_index].m_available) return;
    if (!proto || !proto->m_p_behavior) return;

    m_slots[slot_index].SetPetal(proto, slot_index, rarity);

    if (!proto->m_p_behavior->GetPetalStats(rarity).stack) ApplyExclusivity(proto->m_type);
}

void CFlower::UnequipPetal(int slot_index)
{
    if (slot_index < 0 || slot_index >= static_cast<int>(m_slots.size())) return;

    CPetalSlot& target_slot = m_slots[slot_index];
    EPetalType old_type = EPetalType::None;
    bool was_exclusive = false;

    if (target_slot.m_p_proto && target_slot.m_p_proto->m_p_behavior)
    {
        if (!target_slot.m_p_proto->m_p_behavior->GetPetalStats(target_slot.m_stored_rarity).stack)
        {
            old_type = target_slot.m_p_proto->m_type;
            was_exclusive = true;
        }
    }

    target_slot.ClearPetal();
    target_slot.m_p_proto = nullptr;
    target_slot.m_stored_rarity = ERarity::Null;
    target_slot.m_available = true;

    if (!was_exclusive) return;

    for (auto& slot : m_slots)
    {
        if (slot.m_p_proto && slot.m_p_proto->m_type == old_type) slot.m_available = true;
    }
    ApplyExclusivity(old_type);
}

void CFlower::ApplyExclusivity(EPetalType type)
{
    CPetalSlot* p_best_slot = nullptr;
    int best_rarity = -1;

    for (auto& slot : m_slots)
    {
        if (!slot.m_p_proto || slot.m_p_proto->m_type != type) continue;
        if (!slot.m_p_proto->m_p_behavior) continue;
        if (slot.m_p_proto->m_p_behavior->GetPetalStats(slot.m_stored_rarity).stack) continue;

        int rarity_level = static_cast<int>(slot.m_stored_rarity);
        if (rarity_level > best_rarity)
        {
            best_rarity = rarity_level;
            p_best_slot = &slot;
        }
    }

    for (auto& slot : m_slots)
    {
        if (!slot.m_p_proto || slot.m_p_proto->m_type != type) continue;
        if (!slot.m_p_proto->m_p_behavior) continue;
        if (slot.m_p_proto->m_p_behavior->GetPetalStats(slot.m_stored_rarity).stack) continue;
        slot.m_available = (&slot == p_best_slot);
    }

    if (p_best_slot) return;

    for (auto& slot : m_slots)
    {
        if (slot.m_p_proto && slot.m_p_proto->m_type == type) slot.m_available = true;
    }
}

void CFlower::SetBanned(bool banned, int slot_index)
{
    if (slot_index < 0 || slot_index >= static_cast<int>(m_slots.size())) return;
    m_slots[slot_index].m_banned = banned;
}

void CFlower::InitSlots()
{
    size_t old_size = m_slots.size();
    m_slots.resize(m_petal_num_max);

    for (size_t i = old_size; i < m_slots.size(); ++i)
    {
        m_slots[i].m_slot_index = static_cast<int>(i);
    }
}