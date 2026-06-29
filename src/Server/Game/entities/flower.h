#pragma once
#include <vector>
#include "mob.h"
#include "petals/petal_slot.h"
#include "../../../Shared/shared.h"

class CPetal;
class CPetalSlot;

class CFlower : public CMob<SFlowerStats> {
public:
    CFlower(CGameWorld* pworld, float x, float y, float r,
        ERarity rarity,
        const SFlowerStats& base = SFlowerStats{})
        : CMob(pworld, x, y, r, rarity, base)
    {
        m_FinalStats = base;
        m_Health = base.max_health;
        InitSlots();
    }

    void Tick(float dt) override;

    void TakeDamage(float dmg, CEntity* attacker, EDamageType damage_type) override;

    int GetStartCopyIndex(int slotIndex) const;

    void RebuildFinalStats();
    void EquipPetal(int slotIdx, const CPetalPrototype* proto, ERarity rarity);
    void UnequipPetal(int slotIdx);
    void ApplyExclusivity(EPetalType type);
    std::vector<CPetalSlot>& GetSlots() { return m_Slots; }
    void SetBanned(bool banned, int slot);

    void InitSlots();

    float m_FacingAngle = 0.f;
    bool m_Attacking = false;
    bool m_Defending = false;
    int m_TotalCopies = 0;

private:
    int m_PetalNumMax = 5;

    int m_Shield = 0;

    std::vector<CPetalSlot> m_Slots;
};