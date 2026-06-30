#include "flower.h"
#include "../../../Shared/shared.h"
#include "petals/petal.h"

void CFlower::Tick(float dt)
{
    CMob::Tick(dt);
    RebuildFinalStats();

    m_TotalCopies = 0;
    for (auto& slot : m_Slots)
    {
        if (!slot.m_Available || slot.m_Banned)
        {
            for (auto* cp : slot.m_pPetals)
                if (cp)
                    cp->m_IsMarkedForDes = true;
            continue;
        }

        int copies = slot.GetCurrentCopyCount();
        if (copies > 0)
        {
            slot.m_StartCopyIndex = m_TotalCopies;
            m_TotalCopies += copies;
        }
        else
            slot.m_StartCopyIndex = -1;
        slot.Tick(dt, this);
    }
}

void CFlower::TakeDamage(float dmg, CEntity* attacker, EDamageType damage_type)
{
    for (auto& slot : this->GetSlots())
    {
        if (!slot.m_pProto || !slot.m_pProto->m_pBehavior)
            continue;
        for (auto* petal : slot.m_pPetals)
        {
            if (petal && !petal->m_IsMarkedForDes)
            {
                slot.m_pProto->m_pBehavior->OnFlowerTakeDamage(petal, slot.m_StoredRarity, this, dmg, damage_type,
                                                               attacker);
            }
        }
    }
    CMob::TakeDamage(dmg, attacker, damage_type);
}

int CFlower::GetStartCopyIndex(int slotIndex) const
{
    if (slotIndex >= 0 && slotIndex < (int)m_Slots.size())
        return m_Slots[slotIndex].m_StartCopyIndex;

    return -1;
}

void CFlower::RebuildFinalStats()
{
    int oldMax = m_FinalStats.max_health;

    m_FinalStats = m_BaseStats;
    for (const auto& slot : m_Slots)
        slot.ApplyStatsTo(m_FinalStats);

    int newMax = m_FinalStats.max_health;
    if (oldMax > 0 && newMax != oldMax)
        m_Health = m_Health * newMax / oldMax;
}

void CFlower::EquipPetal(int slotIdx, const CPetalPrototype* proto, ERarity rarity)
{
    if (slotIdx < 0 || slotIdx >= static_cast<int>(m_Slots.size()))
        return;
    if (!m_Slots[slotIdx].m_Available)
        return;
    if (!proto || !proto->m_pBehavior)
        return;

    m_Slots[slotIdx].SetPetal(proto, slotIdx, rarity);

    if (!proto->m_pBehavior->GetPetalStats(rarity).stack)
        ApplyExclusivity(proto->m_Type);
}

void CFlower::UnequipPetal(int slotIdx)
{
    if (slotIdx < 0 || slotIdx >= static_cast<int>(m_Slots.size()))
        return;

    EPetalType oldType = EPetalType::None;
    bool isExclusive = false;

    if (m_Slots[slotIdx].m_pProto && m_Slots[slotIdx].m_pProto->m_pBehavior)
    {
        if (!m_Slots[slotIdx].m_pProto->m_pBehavior->GetPetalStats(m_Slots[slotIdx].m_StoredRarity).stack)
        {
            oldType = m_Slots[slotIdx].m_pProto->m_Type;
            isExclusive = true;
        }
    }

    m_Slots[slotIdx].ClearPetal();

    if (isExclusive)
    {
        for (auto& slot : m_Slots)
        {
            if (slot.m_pProto && slot.m_pProto->m_Type == oldType)
            {
                slot.m_Available = true;
                slot.SetPetal(slot.m_pProto, slot.m_SlotIndex, slot.m_StoredRarity);
            }
        }
    }
}
void CFlower::ApplyExclusivity(EPetalType type)
{
    CPetalSlot* best_slot = nullptr;
    int best_rarity = -1;

    for (auto& slot : m_Slots)
    {
        if (!slot.m_pProto || slot.m_pProto->m_Type != type)
            continue;
        if (!slot.m_pProto->m_pBehavior)
            continue;
        if (slot.m_pProto->m_pBehavior->GetPetalStats(slot.m_StoredRarity).stack)
            continue;

        int rarityLevel = static_cast<int>(slot.m_StoredRarity);
        if (rarityLevel > best_rarity)
        {
            best_rarity = rarityLevel;
            best_slot = &slot;
        }
    }

    for (auto& slot : m_Slots)
    {
        if (!slot.m_pProto || slot.m_pProto->m_Type != type)
            continue;
        if (!slot.m_pProto->m_pBehavior)
            continue;
        if (slot.m_pProto->m_pBehavior->GetPetalStats(slot.m_StoredRarity).stack)
            continue;
        slot.m_Available = (&slot == best_slot);
    }

    if (!best_slot)
    {
        for (auto& slot : m_Slots)
        {
            if (!slot.m_pProto || slot.m_pProto->m_Type != type)
                continue;
            slot.m_Available = true;
        }
    }
}

void CFlower::SetBanned(bool banned, int slotIdx)
{
    if (slotIdx < 0 || slotIdx >= static_cast<int>(m_Slots.size()))
        return;
    m_Slots[slotIdx].m_Banned = banned;
}

void CFlower::InitSlots()
{
    size_t oldSize = m_Slots.size();
    m_Slots.resize(m_PetalNumMax);

    for (size_t i = oldSize; i < m_Slots.size(); i++)
        m_Slots[i].m_SlotIndex = static_cast<int>(i);

    for (size_t i = 0; i < m_Slots.size(); i++)
    {
        m_Slots[i].m_SlotIndex = static_cast<int>(i);
        // 寫好背包記得更改這裏：卸下的花瓣返回背包。
    }
}
