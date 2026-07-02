#pragma once
#include "../../../Shared/game_config.h"
#include "../../../Shared/shared.h"
#include "mob.h"
#include "petals/petal_slot.h"
#include <vector>

class CPetal;
class CPetalSlot;

class CFlower : public CMob<SFlowerStats>
{
  public:
    CFlower(CGameWorld* pworld, float x, float y, float r, ERarity rarity, const SFlowerStats& base = SFlowerStats{})
        : CMob(pworld, x, y, r, rarity, base)
    {
        m_final_stats = base;
        m_health = base.max_health;
        InitSlots();
    }

    void Tick(float dt) override;

    void TakeDamage(float dmg, CEntity* attacker, EDamageType damage_type) override;

    const SFlowerStats* GetBaseStats() const override { return &m_base_stats; }
    const SFlowerStats* GetFinalStats() const override { return &m_final_stats; }

    int GetStartCopyIndex(int slot_index) const;

    void RebuildFinalStats();
    void EquipPetal(int slot_index, const CPetalPrototype* proto, ERarity rarity);
    void UnequipPetal(int slot_index);
    void ApplyExclusivity(EPetalType type);
    std::vector<CPetalSlot>& GetSlots() { return m_slots; }
    void SetBanned(bool banned, int slot_index);

    void InitSlots();

    float m_facing_angle = 0.f;
    bool m_attacking = false;
    bool m_defending = false;
    int m_total_copies = 0;

  private:
    int m_petal_num_max = static_cast<int>(game_config::default_flower_petal_num_max);
    int m_shield = 0;

    std::vector<CPetalSlot> m_slots;
};