#pragma once
#include "../../../Shared/game_config.h"
#include "../../../Shared/math_types.h"
#include "../../../Shared/shared.h"
#include "../gamecontroller.h"
#include "mob.h"
#include "petals/petal_slot.h"
#include <cmath>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

class CPetal;
class CPetalSlot;

class CFlower : public CAttackableMob<SFlowerStats>
{
  public:
    CFlower(CGameWorld* pworld, florrbt::Vector2f pos, float r, EMobType mob_type, ERarity rarity,
            const SFlowerStats& base = SFlowerStats{})
        : CAttackableMob(pworld, pos, r, mob_type, rarity, base)
    {
        m_final_stats = base;
        Health() = base.max_health;
        m_target_radius = std::max(0.f, r);
        InitSlots();
    }
    ~CFlower() override;

    void OnTickBegin(float dt) override;
    void OnTickMotion(float dt) override;
    void OnTickEnvironment(float dt) override;
    void OnTickActions(float dt) override;
    void OnTickEffects(float dt) override;
    void CollectTickDependencies(std::vector<SEntityHandle>& output) const override;
    void CommitActionView() override;

    void Heal(float amount);
    void RestoreMana(float amount);
    bool TryConsumeMana(float amount);
    float GetMaxMana() const { return std::max(0.f, m_final_stats.max_mana); }
    void OnTakeDamage(float dmg, CEntity* attacker, EDamageType damage_type) override;
    void ClearPetals();
    void DestroyPetalEntities();
    void ReloadPetals();
    bool ArePetalsSuppressed() const;
    void RefreshPetalSuppression();
    bool HasPendingPetalReload() const { return m_petals_suspended; }
    void RestorePetalSuspension(bool suspended) { m_petals_suspended = suspended; }
    bool TryStartBurrowFromShovel();

    const SFlowerStats* GetBaseStats() const override { return &m_base_stats; }
    const SFlowerStats* GetFinalStats() const override { return &m_final_stats; }
    float WallCollisionRadius() const override
    {
        return std::clamp(Radius(), 0.f, game_config::mob_player_flower_radius);
    }

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
    // 操作无效时保持槽位不变并返回 false；涉及背包的调用方必须先检查结果再提交库存变更。
    bool EquipPetal(int slot_index, const CPetalPrototype* proto, ERarity rarity);
    void LoadPetalSlot(int slot_index, const CPetalPrototype* proto, ERarity rarity);
    bool UnequipPetal(int slot_index);
    void ForceUnequipPetal(int slot_index);
    bool CanUnequipPetal(int slot_index) const;
    void ApplyExclusivity(EPetalType type);
    void InstallPreparedLoadout(std::vector<std::optional<CPetalSlot>> prepared);
    void InstallPreparedProgress(std::vector<CPetalSlot> prepared, int capacity);
    void RefreshNullificationState();
    void RefreshCorruptionState();
    std::vector<CPetalSlot>& GetSlots() { return m_slots; }
    const std::vector<CPetalSlot>& GetSlots() const { return m_slots; }
    void SetBanned(bool banned, int slot_index);
    void SetSlotBanSource(std::uint64_t source, int slot_index, bool banned);
    void RevokeStateEffects(std::uint64_t source) override;
    float GetShield() const { return m_shield; }
    float GetStoredPetalRotationAngle() const { return m_petal_rotation_angle; }
    void RestoreFlowerRuntime(float shield, float mana, float petal_rotation_angle)
    {
        m_shield = std::isfinite(shield) ? std::max(0.f, shield) : 0.f;
        Mana() = std::isfinite(mana) ? std::max(0.f, mana) : 0.f;
        m_petal_rotation_angle = std::isfinite(petal_rotation_angle) ? petal_rotation_angle : 0.f;
        m_final_stats_dirty = true;
    }
    void RestoreSlotCount(int count) { SetPetalSlotCount(count); }

    void InitSlots();
    void SetPetalSlotCapacity(int capacity);
    int GetPetalSlotCapacity() const { return m_petal_slot_capacity; }
    void SetPetalSlotCount(int count);

    int m_total_copies = 0;

  protected:
    void OnEnvironmentChanged() override;
    void RefreshTargetRadiusFromFinalStats();
    void AdvanceRadiusTowardsTarget(float dt);
    void ClampShieldToMaxHealth();
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
    float m_target_radius = 0.f;
    bool m_final_stats_dirty = true;
    bool m_petal_state_dirty = true;
    bool m_petals_suspended = false;
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

    CMechaFlower(CGameWorld* pworld, florrbt::Vector2f pos, float r, EMobType mob_type, ERarity rarity,
                 const SFlowerStats& base = SFlowerStats{});
};

class CTitanFlower final : public CFlower
{
  public:
    using stats_type = SFlowerStats;

    CTitanFlower(CGameWorld* pworld, florrbt::Vector2f pos, float r, EMobType mob_type, ERarity rarity,
                 const SFlowerStats& base = SFlowerStats{})
        : CFlower(pworld, pos, r, mob_type, rarity, base)
    {
        // Titan 不参与视野外的闲置回收。
        TickMode() = EEntityTickMode::Always;
        SetPetalSlotCount(game_config::titan_petal_slot_count);
    }

    void OnTickBegin(float dt) override;
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
    CPlayerFlower(CGameWorld* pworld, florrbt::Vector2f pos, float r, EMobType mob_type, ERarity rarity,
                  const SFlowerStats& base = SFlowerStats{});

    void OnTickActions(float dt) override;
    void OnTakeDamage(float dmg, CEntity* attacker, EDamageType damage_type) override;
    void RebuildFinalStats() override;
    void RefreshTalentSlotCount();
    bool IsDead() const override { return m_is_dead || CFlower::IsDead(); }
    bool IsVisible() const override { return !IsMarkedForDes(); }
    bool CanCollide() const override { return !m_is_dead && CFlower::CanCollide(); }
    float WallCollisionRadius() const override { return std::max(0.f, Radius()); }
    bool CollidesWithWalls() const override;
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
