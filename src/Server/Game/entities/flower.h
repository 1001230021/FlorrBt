#pragma once
#include "../../../Shared/game_config.h"
#include "../../../Shared/shared.h"
#include "mob.h"
#include "petals/petal_slot.h"
#include <string>
#include <vector>

class CPetal;
class CPetalSlot;

class CFlower : public CAttackableMob<SFlowerStats>
{
  public:
    CFlower(CGameWorld* pworld, sf::Vector2f pos, float r, ERarity rarity, const SFlowerStats& base = SFlowerStats{})
        : CAttackableMob(pworld, pos, r, rarity, base)
    {
        m_final_stats = base;
        m_health = base.max_health;
        InitSlots();
    }
    ~CFlower() override;

    void Tick(float dt) override;

    void TakeDamage(float dmg, CEntity* attacker, EDamageType damage_type) override;
    void ClearPetals();
    void DestroyPetalEntities();
    void ReloadAllPetals();
    bool TryStartBurrowFromShovel();

    const SFlowerStats* GetBaseStats() const override { return &m_base_stats; }
    const SFlowerStats* GetFinalStats() const override { return &m_final_stats; }

    int GetStartCopyIndex(int slot_index) const;
    CPetal* GetMoonPetal() const;
    int GetYinYangCount() const { return m_yinyang_layout_count; }
    float GetPetalRotationAngle() const;
    float GetPetalLayerDistance() const;
    int GetYinYangColumnCount() const;
    int GetPetalColumnIndex(const CPetal* petal) const;
    int GetPetalLayerIndex(const CPetal* petal) const;
    bool HasNonYinYangPetals() const;

    virtual void RebuildFinalStats();
    void EquipPetal(int slot_index, const CPetalPrototype* proto, ERarity rarity);
    void LoadPetalSlot(int slot_index, const CPetalPrototype* proto, ERarity rarity);
    void UnequipPetal(int slot_index);
    bool CanUnequipPetal(int slot_index) const;
    void ApplyExclusivity(EPetalType type);
    void RefreshNullificationState();
    void RefreshCorruptionState();
    std::vector<CPetalSlot>& GetSlots() { return m_slots; }
    const std::vector<CPetalSlot>& GetSlots() const { return m_slots; }
    void SetBanned(bool banned, int slot_index);

    void InitSlots();

    int m_total_copies = 0;

  protected:
    void SetPetalSlotCount(int count);

  private:
    int m_petal_num_max = static_cast<int>(game_config::default_flower_petal_num_max);
    int m_shield = 0;
    int m_yinyang_layout_count = 0;
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
    CPlayerFlower(CGameWorld* pworld, sf::Vector2f pos, float r, ERarity rarity,
                  const SFlowerStats& base = SFlowerStats{});

    void Tick(float dt) override;
    void TakeDamage(float dmg, CEntity* attacker, EDamageType damage_type) override;
    void RebuildFinalStats() override;
    void RefreshTalentSlotCount();
    bool IsDead() const override { return m_is_dead || CFlower::IsDead(); }
    bool IsVisible() const override { return !m_is_marked_for_des; }
    bool CanCollide() const override { return !m_is_dead && CFlower::CanCollide(); }
    void EnterDeathState();
    void PrepareRespawnDestroy();
    void ReviveFromYggdrasil(float health_fraction);
    void TakeExp(int exp);
    int ExpRequired() const;

    int m_level = 1;
    int m_exp = 0;
    std::string m_name = "Player";
    bool m_is_dead = false;

  private:
    void BeginBloodSacrifice();
    void ClearBloodSacrificeSlot(int slot_index);
    void ClearCorruptionOnDeath();
    bool TryEnterUndeadFromBandage();
};
