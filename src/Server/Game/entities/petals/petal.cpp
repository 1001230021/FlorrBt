#include "petal.h"
#include "petal_slot.h"
#include "../../gameworld.h"
#include "../../../../Shared/shared.h"

CPetal::CPetal(float r, CFlower* owner, int slot, SPetalStats petalStats)
    : CProjectile(owner->m_Pos.x, owner->m_Pos.y, r, owner),
    m_BasePetalStats(petalStats),
    m_FinalPetalStats(petalStats),
    m_MaxSlotNum(petalStats.copy)
{
    m_SkipWorldTick = true;
}

void CPetal::Tick(float dt)
{
    CFlower* flower = static_cast<CFlower*>(m_pOwner);
    if (flower)
    {
        m_FinalPetalStats = m_BasePetalStats;
        m_FinalPetalStats.ActedOn(*flower->GetFinalStats());
    }
    CProjectile::Tick(dt);
}

void CPetal::TakeDamage(float dmg, CEntity* attacker, EDamageType damage_type)
{   
    
    m_Health -= dmg;
    if (m_Health <= 0)
    {
        m_Health = 0;
        m_IsMarkedForDes = true;
    }
}

void CPetalSlot::SpawnCopy(int copyIndex, CFlower* flower)
{
    if (!m_pProto || !m_pProto->m_Factory)
        return;
    if (copyIndex < 0 || copyIndex >= static_cast<int>(m_pPetals.size()))
        return;

    if (m_pPetals[copyIndex] != nullptr) return;
    if (m_Banned) return;

    CPetal* pPet = m_pProto->m_Factory(flower, m_SlotIndex, m_StoredRarity);
    if (pPet)
    {
        pPet->m_CopyIndex = copyIndex;
        m_pPetals[copyIndex] = pPet;
        flower->GameWorld()->InsertNonOwningEntity(pPet);
        m_pProto->m_pBehavior->OnPetalSpawned(pPet, m_StoredRarity, flower);
    }
}

void CPetalSlot::SetPetal(const CPetalPrototype* proto, int slotIndex, ERarity rarity)
{
    ClearPetal();

    m_pProto = proto;
    m_StoredRarity = rarity;
    m_SlotIndex = slotIndex;

    if (!m_pProto || !m_pProto->m_pBehavior) return;

    int copies = m_pProto->m_pBehavior->GetPetalStats(rarity).copy;
    if (copies <= 0) copies = 0;

    m_pPetals.resize(copies, nullptr);
    m_ReloadTimers.assign(copies, proto->m_pBehavior->GetPetalStats(rarity).preload);

}

void CPetalSlot::ClearPetal()
{
    for (CPetal* cp : m_pPetals)
    {
        if (cp)
            cp->m_IsMarkedForDes = true;
    }
    m_pPetals.clear();
    m_ReloadTimers.clear();
}


void CPetalSlot::KillCopy(int copyIndex)
{
    if (copyIndex < 0 || copyIndex >= static_cast<int>(m_ReloadTimers.size())) return;
    CPetal* pp = m_pPetals[copyIndex];
    if (pp) pp->m_Health = 0;
}

void CPetalSlot::Tick(float dt, CFlower* flower)
{
    if (m_Banned) return;
    if (!m_pProto) return;

    for (size_t i = 0; i < m_ReloadTimers.size(); ++i)
    {
        if (m_ReloadTimers[i] > 0.0f)
        {
            float mul = flower->GetFinalStats()->detection_multiplier;
            if (mul < 0.001f) mul = 0.001f;
            m_ReloadTimers[i] -= dt / mul;
            if (m_ReloadTimers[i] <= 0.0f)
            {
                m_ReloadTimers[i] = 0.0f;
                SpawnCopy(static_cast<int>(i), flower);
            }
        }
        else if (m_pPetals[i] == nullptr)
        {
            SpawnCopy(static_cast<int>(i), flower);
        }
    }

    for (size_t i = 0; i < m_pPetals.size(); ++i)
    {
        CPetal* cp = m_pPetals[i];
        if (!cp) continue;

        cp->Tick(dt);
        if (m_pProto->m_pBehavior)
            m_pProto->m_pBehavior->OnTick(cp, m_StoredRarity, flower, dt);

        if (cp->m_Health <= 0.f || cp->m_IsMarkedForDes)
        {
            cp->m_IsMarkedForDes = true;
            if (m_pProto->m_pBehavior)
                m_pProto->m_pBehavior->OnPetalDestroyed(cp, m_StoredRarity, flower);
            m_pPetals[i] = nullptr;
            m_ReloadTimers[i] = cp->m_FinalPetalStats.reload;
        }
    }
}

int CPetalSlot::GetCurrentCopyCount() const
{
    if (!m_pProto || !m_pProto->m_pBehavior)
        return 0;

    if (m_pProto->m_pBehavior->IsOpen())
        return static_cast<int>(m_pPetals.size());
    else
    {
        int baseCopy = static_cast<int>(m_pPetals.size());
        return (baseCopy > 0) ? 1 : 0;
    }
}

void CPetalSlot::ApplyStatsTo(SFlowerStats& target) const
{
    if (!m_pProto || !m_pProto->m_pBehavior) return;
    SFlowerStats stats = m_pProto->m_pBehavior->GetStats(m_StoredRarity);
    int count = 0;
    for (auto* cp : m_pPetals)
        if (cp) ++count;
    for (int i = 0; i < count; ++i)
        target.ActedOn(stats);
}