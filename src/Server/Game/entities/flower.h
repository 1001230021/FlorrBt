#pragma once
#include "../../../Shared/game_config.h"
#include "../../../Shared/shared.h"
#include "mob.h"
#include "petals/petal_slot.h"
#include <string>
#include <vector>

class CPetal;
class CPetalSlot;

class CFlower : public CMob<SFlowerStats>
{
  public:
    CFlower(CGameWorld* pworld, sf::Vector2f pos, float r, ERarity rarity, const SFlowerStats& base = SFlowerStats{})
        : CMob(pworld, pos, r, rarity, base)
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
    int GetYinYangCount() const { return m_yinyang_count; }
    float GetPetalRotationAngle() const { return m_petal_rotation_angle; }
    float GetPetalLayerDistance() const;
    int GetYinYangColumnCount() const;
    int GetPetalColumnIndex(const CPetal* petal) const;
    int GetPetalLayerIndex(const CPetal* petal) const;
    bool HasNonYinYangPetals() const;

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
    int m_yinyang_count = 0;
    float m_petal_rotation_angle = 0.f;

    std::vector<CPetalSlot> m_slots;
};

class CNormalFlower : public CFlower
{
  public:
    using stats_type = SFlowerStats;
    using CFlower::CFlower;
};

class CPlayerFlower : public CFlower
{
  public:
    using stats_type = SFlowerStats;
    using CFlower::CFlower;

    int m_level = 1;
    std::string m_name = "Player";
};
