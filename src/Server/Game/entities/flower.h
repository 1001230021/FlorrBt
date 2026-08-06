#pragma once
#include "../../../Shared/game_config.h"
#include "../../../Shared/shared.h"
#include "mob.h"
#include "petals/petal_slot.h"
#include <cstdint>
#include <string>
#include <vector>

class CPetal;
class CPetalSlot;

class CFlower : public CAttackableMob<SFlowerStats>
{
  public:
    CFlower(CGameWorld* pworld, sf::Vector2f pos, float r, EMobType mob_type, ERarity rarity,
            const SFlowerStats& base = SFlowerStats{})
        : CAttackableMob(pworld, pos, r, mob_type, rarity, base)
    {
        m_final_stats = base;
        m_health = base.max_health;
        InitSlots();
    }
    ~CFlower() override;

    void Tick(float dt) override;

    void Heal(float amount);
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
    bool HasActivePetal(EPetalType type, ERarity rarity = ERarity::Null) const;

    virtual void RebuildFinalStats();
    void MarkFinalStatsDirty()
    {
        m_final_stats_dirty = true;
        m_petal_state_dirty = true;
    }
    std::uint64_t GetFinalStatsRevision() const { return m_final_stats_revision; }
    void EquipPetal(int slot_index, const CPetalPrototype* proto, ERarity rarity);
    void LoadPetalSlot(int slot_index, const CPetalPrototype* proto, ERarity rarity);
    void UnequipPetal(int slot_index);
    void ForceUnequipPetal(int slot_index);
    bool CanUnequipPetal(int slot_index) const;
    void ApplyExclusivity(EPetalType type);
    void RefreshNullificationState();
    void RefreshCorruptionState();
    std::vector<CPetalSlot>& GetSlots() { return m_slots; }
    const std::vector<CPetalSlot>& GetSlots() const { return m_slots; }
    void SetBanned(bool banned, int slot_index);
    float GetShield() const { return m_shield; }
    float GetStoredPetalRotationAngle() const { return m_petal_rotation_angle; }
    void RestoreFlowerRuntime(float shield, float petal_rotation_angle)
    {
        m_shield = shield;
        m_petal_rotation_angle = petal_rotation_angle;
        m_final_stats_dirty = true;
    }
    void RestoreSlotCount(int count) { SetPetalSlotCount(count); }

    void InitSlots();

    int m_total_copies = 0;

  protected:
    void ClampShieldToMaxHealth();
    void SetPetalSlotCapacity(int capacity);
    void SetPetalSlotCount(int count);
    void ClearFinalStatsDirty() { m_final_stats_dirty = false; }
    void FinishFinalStatsRebuild()
    {
        ++m_final_stats_revision;
        if (m_final_stats_revision == 0) ++m_final_stats_revision;
        m_final_stats_dirty = false;
        m_petal_state_dirty = true;
    }

  private:
    int m_petal_slot_capacity = static_cast<int>(game_config::default_flower_petal_num_max);
    int m_petal_num_max = static_cast<int>(game_config::default_flower_petal_num_max);
    float m_shield = 0.f;
    int m_yinyang_layout_count = 0;
    float m_petal_rotation_angle = 0.f;
    bool m_final_stats_dirty = true;
    bool m_petal_state_dirty = true;
    std::uint64_t m_final_stats_revision = 0;
    std::uint64_t m_config_revision = game_config::GetConfigRevision();

    std::vector<CPetalSlot> m_slots;
};

class CNormalFlower : public CFlower
{
  public:
    using stats_type = SFlowerStats;
    using CFlower::CFlower;
};

class CMechaFlower final : public CFlower
{
  public:
    using stats_type = SFlowerStats;

    CMechaFlower(CGameWorld* pworld, sf::Vector2f pos, float r, EMobType mob_type, ERarity rarity,
                 const SFlowerStats& base = SFlowerStats{});
};

class CTitanFlower final : public CFlower
{
  public:
    using stats_type = SFlowerStats;

    CTitanFlower(CGameWorld* pworld, sf::Vector2f pos, float r, EMobType mob_type, ERarity rarity,
                 const SFlowerStats& base = SFlowerStats{})
        : CFlower(pworld, pos, r, mob_type, rarity, base)
    {
        SetPetalSlotCount(game_config::titan_petal_slot_count);
    }

    void Tick(float dt) override;
    void CaptureRuntimeSnapshot(CSnapshotWriter& writer) const override;
    bool RestoreRuntimeSnapshot(const CSnapshotReader& reader, std::uint32_t version, std::string& error) override;
    void ReplaceForgedUniquePetal(EPetalType type);

  private:
    bool HasUniquePetal(EPetalType type) const;
    void RefreshUniqueLoadout();

    float m_unique_refresh_timer = 0.f;
};

class CPlayerFlower : public CFlower
{
  public:
    using stats_type = SFlowerStats;
    CPlayerFlower(CGameWorld* pworld, sf::Vector2f pos, float r, EMobType mob_type, ERarity rarity,
                  const SFlowerStats& base = SFlowerStats{});

    void Tick(float dt) override;
    void TakeDamage(float dmg, CEntity* attacker, EDamageType damage_type) override;
    void RebuildFinalStats() override;
    void RefreshTalentSlotCount();
    bool IsDead() const override { return m_is_dead || CFlower::IsDead(); }
    bool IsVisible() const override { return !m_is_marked_for_des; }
    bool CanCollide() const override { return !m_is_dead && CFlower::CanCollide(); }
    float WallCollisionRadius() const override { return std::max(0.f, m_radius); }
    bool CollidesWithWalls() const override
    {
        return !HasActivePetal(EPetalType::Nullification, ERarity::Primordial);
    }
    void EnterDeathState();
    void PrepareRespawnDestroy(EEntityRemovalReason reason = EEntityRemovalReason::Replaced);
    bool ReviveFromYggdrasil(float health_fraction);
    void TakeExp(std::int64_t exp);
    std::int64_t ExpRequired() const;

    int m_level = 1;
    std::int64_t m_exp = 0;
    std::string m_name = "Player";
    bool m_is_dead = false;

  private:
    void BeginBloodSacrifice();
    void ClearBloodSacrificeSlot(int slot_index);
    void ConsumeCorruptionPetalsOnDeath();
    bool TryEnterUndeadFromBandage();
};
