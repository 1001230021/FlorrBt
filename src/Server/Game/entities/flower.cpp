#include "flower.h"
#include "petals/petal.h"
#include <algorithm>
#include <cmath>

namespace
{
struct yin_yang_layout
{
    int columns = 1;
    float direction = 1.f;
};

yin_yang_layout GetYinYangLayout(int count)
{
    if (count <= 0) return {};

    static constexpr yin_yang_layout layouts[] = {
        {1, -1.f}, {6, 1.f}, {6, -1.f}, {4, 1.f}, {4, -1.f},
        {3, 1.f}, {2, -1.f}, {2, 1.f}, {1, -1.f}, {1, 1.f},
    };
    return layouts[std::clamp(count, 1, 10) - 1];
}

int CountBonusCopies(const CPetalSlot& slot)
{
    return slot.GetBonusCopyCount();
}
}

void CFlower::Tick(float dt)
{
    CMob::Tick(dt);
    RebuildFinalStats();

    float direction = 1.f;
    if (m_final_stats.petal_rotation_mode == EPetalRotationMode::YinYang) direction = GetYinYangLayout(m_yinyang_count).direction;
    m_petal_rotation_angle += m_final_stats.petal_rotation_speed * direction * dt;
    m_petal_rotation_angle = std::fmod(m_petal_rotation_angle, 2.f * game_config::pi);

    m_total_copies = 0;
    m_yinyang_count = 0;
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
        if (slot.m_p_proto && slot.m_p_proto->m_type == EPetalType::YinYang) m_yinyang_count += CountBonusCopies(slot);
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

float CFlower::GetPetalLayerDistance() const
{
    if (m_final_stats.petal_rotation_mode != EPetalRotationMode::YinYang || m_yinyang_count <= 0) return 0.f;

    int columns = GetYinYangColumnCount();
    if (columns <= 0) return 0.f;

    int max_column_size = (m_total_copies + columns - 1) / columns;
    int layers = std::max(1, max_column_size);
    return static_cast<float>(std::max(0, layers - 1)) * game_config::default_petal_orbit_radius;
}

int CFlower::GetYinYangColumnCount() const
{
    if (m_final_stats.petal_rotation_mode != EPetalRotationMode::YinYang || m_yinyang_count <= 0) return std::max(1, m_total_copies);
    if (!HasNonYinYangPetals()) return 1;
    return GetYinYangLayout(m_yinyang_count).columns;
}

int CFlower::GetPetalColumnIndex(const CPetal* petal) const
{
    if (!petal) return 0;

    int start_index = GetStartCopyIndex(petal->m_slot_index);
    if (start_index < 0) return 0;
    int absolute_index = start_index + petal->m_copy_index;
    int columns = GetYinYangColumnCount();
    if (columns <= 0) return 0;
    return absolute_index % columns;
}

int CFlower::GetPetalLayerIndex(const CPetal* petal) const
{
    if (!petal) return 0;

    int start_index = GetStartCopyIndex(petal->m_slot_index);
    if (start_index < 0) return 0;
    int absolute_index = start_index + petal->m_copy_index;
    int columns = GetYinYangColumnCount();
    if (columns <= 0) return 0;
    return absolute_index / columns;
}

bool CFlower::HasNonYinYangPetals() const
{
    for (const auto& slot : m_slots)
    {
        if (!slot.m_available || slot.m_banned || !slot.m_p_proto) continue;
        if (slot.m_p_proto->m_type != EPetalType::YinYang && CountBonusCopies(slot) > 0) return true;
    }
    return false;
}
