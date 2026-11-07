#include "flower.h"
#include "../../HotReload/snapshot_archive.h"
#include "../../Persistence/account_store.h"
#include "../gamecontext.h"
#include "../gameworld.h"
#include "../player.h"
#include "../states/states.h"
#include "../talent.h"
#include "../zone_mob_tools.h"
#include "blood_sacrifice_ritual.h"
#include "petals/petal.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <limits>

namespace
{
constexpr float screen_clockwise = 1.f;
constexpr float screen_counter_clockwise = -1.f;
constexpr int mecha_flower_base_cogwheel_count = 4;
constexpr int mecha_flower_max_cogwheel_count = 15;

struct yin_yang_layout
{
    int columns = 1;
    float direction = screen_clockwise;
};

yin_yang_layout GetYinYangLayout(int count)
{
    if (count <= 0) return {};

    static constexpr yin_yang_layout layouts[] = {
        { 1, screen_counter_clockwise }, { 6, screen_clockwise },         { 6, screen_counter_clockwise },
        { 4, screen_clockwise },         { 4, screen_counter_clockwise }, { 3, screen_clockwise },
        { 2, screen_counter_clockwise }, { 2, screen_clockwise },         { 1, screen_counter_clockwise },
        { 1, screen_clockwise },
    };
    return layouts[std::clamp(count, 1, 10) - 1];
}

int GetPlayerFlowerStatLevel(int level)
{
    return std::clamp(level, 1, std::max(1, game_config::mob_player_flower_max_stat_level));
}

float GetPlayerFlowerBodyDamageMultiplier(int level)
{
    const float exponent = std::max(0.f, game_config::mob_player_flower_level_damage_exponent);
    return std::pow(static_cast<float>(std::max(1, level)), exponent);
}

SFlowerStats BuildPlayerFlowerLevelStats(SFlowerStats stats, int level)
{
    stats.max_health *= std::pow(std::max(0.f, game_config::mob_player_flower_level_health_growth),
                                 static_cast<float>(GetPlayerFlowerStatLevel(level)));
    stats.damage *= GetPlayerFlowerBodyDamageMultiplier(level);
    return stats;
}

std::int64_t GetRequiredPlayerExperience(int level)
{
    const int safe_level = std::max(1, level);
    const long double base_exp = std::max(1.0L, static_cast<long double>(game_config::player_level_exp_base));
    const long double level_growth = std::max(1.0L, static_cast<long double>(game_config::player_level_exp_growth));

    // 经验曲线以旧公式的 50 级成本为锚点，调整后续增长率时保持该点不变。
    const long double legacy_growth = std::max(1.0L, static_cast<long double>(game_config::player_level_exp_legacy_growth));
    const long double legacy_50 = base_exp * std::pow(legacy_growth, 49.0L);
    const long double cost_base = legacy_50 / std::pow(level_growth, 49.0L);

    const long double required =
        (cost_base * std::pow(level_growth, static_cast<long double>(safe_level - 1))) * (7.0L / 8.0L) - 24.0L;
    const long double max_exp = static_cast<long double>(std::numeric_limits<std::int64_t>::max());
    if (!std::isfinite(static_cast<double>(required)) || required >= max_exp)
        return std::numeric_limits<std::int64_t>::max();
    return std::max<std::int64_t>(1, static_cast<std::int64_t>(std::llround(required)));
}

int CalculateTalentPointsForLevel(int level)
{
    if (level <= 1) return 0;

    int gain = game_config::player_talent_point_base_gain;
    const int minor_interval = game_config::player_talent_point_minor_level_interval;
    const int major_interval = game_config::player_talent_point_major_level_interval;
    if (minor_interval > 0 && level % minor_interval == 0) gain += game_config::player_talent_point_minor_bonus;
    if (major_interval > 0 && level % major_interval == 0) gain += game_config::player_talent_point_major_bonus;
    return gain;
}

}

CFlower::~CFlower()
{
    for (CBanSlotState* state : FindStates<CBanSlotState>())
        RemoveState(state);
}

CMechaFlower::CMechaFlower(CGameWorld* pworld, florrbt::Vector2f pos, float r, EMobType mob_type, ERarity rarity,
                           const SFlowerStats& base)
    : CFlower(pworld, pos, r, mob_type, rarity, base)
{
    SetPetalSlotCapacity(mecha_flower_max_cogwheel_count);
    const int cogwheel_count =
        std::min(mecha_flower_max_cogwheel_count, mecha_flower_base_cogwheel_count + GetLevel(rarity));
    SetPetalSlotCount(cogwheel_count);

    const CPetalPrototype* cogwheel = FindPetalPrototype(EPetalType::Cogwheel);
    if (!cogwheel) return;

    for (int slot = 0; slot < cogwheel_count; ++slot)
        LoadPetalSlot(slot, cogwheel, rarity);
}

void CTitanFlower::OnTickBegin(float dt)
{
    CFlower::OnTickBegin(dt);
    if (!HasPlayerControlLease()) m_unique_refresh_timer -= std::max(0.f, dt);
    if (!HasPlayerControlLease() && m_unique_refresh_timer <= 0.f)
    {
        RefreshUniqueLoadout();
        RebuildFinalStats();
        m_unique_refresh_timer = std::max(1.f, game_config::titan_unique_refresh_seconds);
    }
}

void CTitanFlower::CaptureRuntimeSnapshot(CSnapshotWriter& writer) const
{
    writer.Field("unique_refresh_timer", m_unique_refresh_timer);
}

bool CTitanFlower::RestoreRuntimeSnapshot(const CSnapshotReader& reader, std::uint32_t version, std::string& error)
{
    if (!CFlower::RestoreRuntimeSnapshot(reader, version, error)) return false;
    m_unique_refresh_timer = std::max(0.f, reader.Float("unique_refresh_timer", m_unique_refresh_timer));
    return true;
}

void CTitanFlower::ReplaceForgedUniquePetal(EPetalType type)
{
    // 附身 Titan 的临时装备属于玩家进度作用域，释放控制权后才恢复自主库存。
    if (HasPlayerControlLease()) { m_unique_refresh_timer = 0.f; return; }
    auto& slots = GetSlots();
    const auto forged_slot = std::find_if(slots.begin(), slots.end(), [type](const CPetalSlot& slot) {
        return slot.m_p_proto && slot.m_p_proto->m_type == type && slot.m_stored_rarity == ERarity::Unique;
    });
    if (forged_slot == slots.end()) return;

    CGameContext* context = GameContext();
    if (!context) return;

    std::vector<const CPetalPrototype*> candidates;
    candidates.reserve(GetPetalRegistry().size());
    for (const auto& [candidate_type, prototype] : GetPetalRegistry())
    {
        if (candidate_type == EPetalType::None || !prototype || !prototype->m_p_behavior ||
            context->UniquePetals().IsUniquePetalOwned(candidate_type) || HasUniquePetal(candidate_type))
            continue;
        candidates.push_back(prototype);
    }

    const int slot_index = static_cast<int>(std::distance(slots.begin(), forged_slot));
    ForceUnequipPetal(slot_index);
    if (candidates.empty()) return;

    std::shuffle(candidates.begin(), candidates.end(), GetRng());
    LoadPetalSlot(slot_index, candidates.front(), ERarity::Unique);
}

bool CTitanFlower::HasUniquePetal(EPetalType type) const
{
    return std::any_of(GetSlots().begin(), GetSlots().end(), [type](const CPetalSlot& slot) {
        return slot.m_p_proto && slot.m_p_proto->m_type == type && slot.m_stored_rarity == ERarity::Unique;
    });
}

void CTitanFlower::RefreshUniqueLoadout()
{
    CGameContext* context = GameContext();
    if (!context) return;

    std::vector<const CPetalPrototype*> candidates;
    candidates.reserve(GetPetalRegistry().size());
    for (const auto& [type, prototype] : GetPetalRegistry())
    {
        if (type == EPetalType::None || !prototype || !prototype->m_p_behavior || context->UniquePetals().IsUniquePetalOwned(type)) continue;
        candidates.push_back(prototype);
    }

    auto& slots = GetSlots();
    std::shuffle(candidates.begin(), candidates.end(), GetRng());
    const size_t equipped_count = std::min(slots.size(), candidates.size());
    for (size_t i = 0; i < slots.size(); ++i)
    {
        ForceUnequipPetal(static_cast<int>(i));
        if (i < equipped_count) LoadPetalSlot(static_cast<int>(i), candidates[i], ERarity::Unique);
    }
}

void CFlower::CollectTickDependencies(std::vector<SEntityHandle>& output) const
{
    CAttackableMob<SFlowerStats>::CollectTickDependencies(output);
    for (const auto& slot : m_slots)
        for (const auto* petal : slot.m_p_petals)
            if (petal && petal->GetOwner() == this && !petal->IsMarkedForDes()) output.push_back(petal->GetHandle());
}

void CFlower::OnTickBegin(float dt)
{
    const std::uint64_t config_revision = game_config::GetConfigRevision();
    if (m_config_revision != config_revision)
    {
        m_config_revision = config_revision;
        MarkFinalStatsDirty();
    }

    CAttackableMob<SFlowerStats>::OnTickBegin(dt);
    if (IsDead() || IsMarkedForDes()) { DestroyPetalEntities(); return; }
    if (m_final_stats_dirty) RebuildFinalStats();
}

void CFlower::OnTickMotion(float dt)
{
    AdvanceRadiusTowardsTarget(dt);
    CAttackableMob<SFlowerStats>::OnTickMotion(dt);
}

void CFlower::OnTickEnvironment(float dt)
{
    CAttackableMob<SFlowerStats>::OnTickEnvironment(dt);
    if (IsDead() || IsMarkedForDes()) return;
    if (m_final_stats_dirty) RebuildFinalStats();
    RefreshNullificationState();
    RefreshCorruptionState();
}

void CFlower::CommitActionView()
{
    CAttackableMob<SFlowerStats>::CommitActionView();
    if (IsDead() || IsMarkedForDes()) { DestroyPetalEntities(); return; }
    if (m_final_stats_dirty)
    {
        RebuildFinalStats();
        RefreshNullificationState();
        RefreshCorruptionState();
    }
    RefreshPetalSuppression();
    for (auto& slot : m_slots)
    {
        if (slot.m_available && !slot.IsBanned()) continue;
        for (auto*& petal : slot.m_p_petals)
        {
            if (petal) petal->MarkForDestroy(EEntityRemovalReason::OwnerRemoved);
            petal = nullptr;
        }
        slot.m_start_copy_index = -1;
    }
}

void CFlower::OnTickEffects(float dt)
{
    CAttackableMob<SFlowerStats>::OnTickEffects(dt);
    if (IsDead() || IsMarkedForDes()) return;
    float health_regen = m_final_stats.health_regen;
    if (m_defending && !m_attacking) health_regen += m_final_stats.defense_health_regen;
    if (health_regen > 0.f && m_final_stats.max_health > 0.f && Health() > 0.f)
    {
        float regen = health_regen * std::max(0.f, m_final_stats.recovery_multiplier) *
                      std::max(0.f, m_final_stats.petal_medicine_multiplier) *
                      std::max(0.f, m_final_stats.healing_received_multiplier) *
                      std::max(0.f, GetMedicMultiplier(this));
        Heal(regen * dt);
    }
    if (m_final_stats.mana_regen > 0.f && m_final_stats.max_mana > 0.f && Health() > 0.f)
        RestoreMana(m_final_stats.mana_regen * std::max(0.f, m_final_stats.recovery_multiplier) * dt);
}

void CFlower::OnTickActions(float dt)
{
    RefreshPetalSuppression();
    if (ArePetalsSuppressed()) return;

    if (m_petal_state_dirty)
    {
        m_petal_state_dirty = false;
        for (auto& slot : m_slots)
        {
            if (slot.m_available && !slot.IsBanned()) slot.RefreshPetalState(this);
        }
    }

    m_total_copies = 0;
    m_yinyang_layout_count = 0;
    int yinyang_bonus_count = 0;
    for (auto& slot : m_slots)
    {
        if (!slot.m_available || slot.IsBanned())
        {
            for (CPetal*& petal : slot.m_p_petals)
            {
                if (petal) petal->MarkForDestroy(EEntityRemovalReason::OwnerRemoved);
                petal = nullptr;
            }
            slot.m_start_copy_index = -1;
            continue;
        }

        int copies = slot.GetCurrentCopyCount(this);
        if (slot.m_p_proto && slot.m_p_proto->m_type == EPetalType::YinYang)
            yinyang_bonus_count += slot.GetBonusCopyCount(this);
        if (slot.m_p_proto && slot.m_p_proto->m_type == EPetalType::Moon && copies > 0)
        {
            slot.m_start_copy_index = 0;
            continue;
        }

        if (copies > 0)
        {
            slot.m_start_copy_index = m_total_copies;
            m_total_copies += copies;
        } else
        {
            slot.m_start_copy_index = -1;
        }
    }

    if (m_final_stats.petal_rotation_mode == EPetalRotationMode::YinYang && yinyang_bonus_count > 0)
        m_yinyang_layout_count = std::clamp(yinyang_bonus_count, 1, 10);

    float direction = screen_clockwise;
    if (m_final_stats.petal_rotation_mode == EPetalRotationMode::YinYang)
        direction = GetYinYangLayout(m_yinyang_layout_count).direction;
    m_petal_rotation_angle += m_final_stats.petal_rotation_speed * GetPulseMovementMultiplier(this) * direction * dt;
    m_petal_rotation_angle = std::fmod(m_petal_rotation_angle, 2.f * game_config::pi);

    for (auto& slot : m_slots)
    {
        if (slot.m_available && !slot.IsBanned()) slot.Tick(dt, this, true);
    }
}
void CFlower::Heal(float amount)
{
    if (amount <= 0.f || Health() <= 0.f || m_final_stats.max_health <= 0.f) return;

    const float missing_health = std::max(0.f, m_final_stats.max_health - Health());
    const float health_gain = std::min(amount, missing_health);
    Health() += health_gain;

    const float excess_healing = std::max(0.f, amount - health_gain);
    const float conversion = std::max(0.f, m_final_stats.overheal_to_shield);
    const float shield_cap = std::max(0.f, m_final_stats.max_health);
    if (excess_healing > 0.f && conversion > 0.f && shield_cap > 0.f)
        m_shield = std::min(shield_cap, m_shield + excess_healing * conversion);
}

void CFlower::RestoreMana(float amount)
{
    if (!(amount > 0.f) || !std::isfinite(amount)) return;
    const float maximum = GetMaxMana();
    if (!(maximum > 0.f))
    {
        Mana() = 0.f;
        return;
    }
    Mana() = std::clamp(Mana() + amount, 0.f, maximum);
}

bool CFlower::TryConsumeMana(float amount)
{
    if (!(amount > 0.f) || !std::isfinite(amount)) return true;
    const float available = std::max(0.f, Mana());
    if (available + game_config::entity_collision_epsilon < amount) return false;
    Mana() = std::clamp(available - amount, 0.f, GetMaxMana());
    return true;
}

void CFlower::ClampShieldToMaxHealth()
{
    const float shield_cap = std::max(0.f, m_final_stats.max_health);
    m_shield = std::clamp(m_shield, 0.f, std::max(0.f, shield_cap));
}

void CFlower::OnTakeDamage(float dmg, CEntity* attacker, EDamageType damage_type)
{
    if (ShouldBlockDiggingDamage(this, attacker, damage_type)) return;

    for (auto& slot : GetSlots())
    {
        if (!slot.m_p_proto || !slot.m_p_proto->m_p_behavior) continue;

        for (auto* petal : slot.m_p_petals)
        {
            if (petal && !petal->IsMarkedForDes())
            {
                slot.m_p_proto->m_p_behavior->OnFlowerTakeDamage(petal, slot.m_stored_rarity, this, dmg, damage_type,
                                                                 attacker);
            }
        }
    }

    if (dmg <= 0.f) return;
    const float absorbed = std::min(std::max(0.f, m_shield), dmg);
    m_shield -= absorbed;
    dmg -= absorbed;
    if (dmg <= 0.f) return;

    CAttackableMob<SFlowerStats>::OnTakeDamage(dmg, attacker, damage_type);
}

void CFlower::ClearPetals()
{
    for (auto& slot : m_slots)
    {
        slot.ClearPetal();
        slot.m_start_copy_index = -1;
    }
    m_total_copies = 0;
}

void CFlower::DestroyPetalEntities()
{
    for (auto& slot : m_slots)
    {
        for (CPetal*& petal : slot.m_p_petals)
        {
            if (petal)
            {
                petal->NotifyCleared();
                petal->MarkForDestroy(EEntityRemovalReason::OwnerRemoved);
            }
            petal = nullptr;
        }
        std::fill(slot.m_bonus_active.begin(), slot.m_bonus_active.end(), 0);
        slot.m_start_copy_index = -1;
    }
    m_total_copies = 0;
}

void CFlower::ReloadPetals()
{
    m_capabilities.Set(ERestrictionSource::Digging, HasState<CDiggingState>()
        ? static_cast<std::uint8_t>(ECapabilityRestriction::Petals) : 0);
    if (ArePetalsSuppressed())
    {
        if (!m_petals_suspended) DestroyPetalEntities();
        m_petals_suspended = true;
        return;
    }
    m_petals_suspended = false;
    for (auto& slot : m_slots)
    {
        if (!slot.m_p_proto || !slot.m_p_proto->m_p_behavior) continue;

        SPetalStats stats = slot.m_p_proto->m_p_behavior->GetSlotPetalStats(slot.m_stored_rarity, &slot, this);
        const float reload = std::max(0.f, slot.m_p_proto->m_type == EPetalType::Shovel ? stats.preload : stats.reload);
        for (size_t i = 0; i < slot.m_p_petals.size(); ++i)
        {
            if (slot.m_p_petals[i])
            {
                CPetal* petal = slot.m_p_petals[i];
                // 替换花瓣前先清理行为附属物，尤其是 Egg 召唤物；此路径不会经过 ClearPetal。
                petal->NotifyCleared();
                petal->MarkForDestroy(EEntityRemovalReason::Replaced);
                slot.m_p_petals[i] = nullptr;
            }
            if (i < slot.m_bonus_active.size()) slot.m_bonus_active[i] = false;
            slot.StartReload(static_cast<int>(i), reload);
            if (i < slot.m_reload_ignore_multiplier.size()) slot.m_reload_ignore_multiplier[i] = false;
        }
        slot.m_start_copy_index = -1;
    }
    m_total_copies = 0;
    MarkFinalStatsDirty();
}

bool CFlower::ArePetalsSuppressed() const
{
    // 直接读取挖掘状态，使来源撤销时能立即更新重载限制。
    return m_capabilities.Restricted(ECapabilityRestriction::Petals) || HasState<CDiggingState>();
}

void CFlower::RefreshPetalSuppression()
{
    m_capabilities.Set(ERestrictionSource::Digging, HasState<CDiggingState>()
        ? static_cast<std::uint8_t>(ECapabilityRestriction::Petals) : 0);
    if (ArePetalsSuppressed())
    {
        if (!m_petals_suspended) DestroyPetalEntities();
        m_petals_suspended = true;
    }
    else if (m_petals_suspended) ReloadPetals();
}

void CFlower::OnEnvironmentChanged()
{
    MarkFinalStatsDirty();
    RebuildFinalStats();
    RefreshPetalSuppression();
}

bool CFlower::TryStartBurrowFromShovel()
{
    if (ArePetalsSuppressed()) return false;

    for (auto& slot : m_slots)
    {
        if (!slot.m_available || slot.IsBanned() || !slot.m_p_proto) continue;
        if (slot.m_p_proto->m_type != EPetalType::Shovel) continue;

        for (size_t i = 0; i < slot.m_p_petals.size(); ++i)
        {
            CPetal* petal = slot.m_p_petals[i];
            if (!petal || petal->IsMarkedForDes() || petal->Health() <= 0.f || petal->m_hidden) continue;

            float duration = std::max(game_config::server_fixed_dt, game_config::default_shovel_dig_duration);
            petal->m_reload_override = std::max(0.f, petal->m_final_petal_stats.preload);
            petal->Health() = 0.f;
            AddState(std::make_unique<CDiggingState>(this, duration, slot.m_stored_rarity));
            return true;
        }
    }
    return false;
}

int CFlower::GetStartCopyIndex(int slot_index) const
{
    if (slot_index >= 0 && slot_index < static_cast<int>(m_slots.size())) return m_slots[slot_index].m_start_copy_index;
    return -1;
}

CPetal* CFlower::GetMoonPetal() const
{
    for (const auto& slot : m_slots)
    {
        if (!slot.m_available || slot.IsBanned() || !slot.m_p_proto) continue;
        if (slot.m_p_proto->m_type != EPetalType::Moon) continue;

        for (CPetal* petal : slot.m_p_petals)
        {
            if (petal && !petal->IsMarkedForDes() && petal->Health() > 0.f && !petal->m_hidden) return petal;
        }
    }
    return nullptr;
}

float CFlower::GetPetalRotationAngle() const
{
    if (!m_final_stats.petal_rotation_quantized || m_total_copies <= 1) return m_petal_rotation_angle;

    float step = 2.f * game_config::pi / static_cast<float>(m_total_copies);
    if (step <= 0.f) return m_petal_rotation_angle;

    float normalized = std::fmod(m_petal_rotation_angle, 2.f * game_config::pi);
    if (normalized < 0.f) normalized += 2.f * game_config::pi;

    float index = normalized / step;
    float base_index = std::floor(index);
    float phase = index - base_index;
    float moving_fraction = std::clamp(1.f - game_config::default_cogwheel_stop_fraction,
                                       game_config::flower_cogwheel_moving_fraction_min, 1.f);
    if (phase > game_config::default_cogwheel_stop_fraction)
    {
        base_index += (phase - game_config::default_cogwheel_stop_fraction) / moving_fraction;
    }
    return std::fmod(base_index * step, 2.f * game_config::pi);
}

void CFlower::RebuildFinalStats()
{
    float old_max = m_final_stats.max_health;

    m_final_stats = m_base_stats;
    for (const auto& slot : m_slots)
    {
        slot.ApplyStatsTo(m_final_stats, this);
    }

    float new_max = m_final_stats.max_health;
    RefreshTargetRadiusFromFinalStats();
    if (old_max > 0.f && new_max > 0.f && new_max != old_max) Health() = Health() * new_max / old_max;
    if (new_max > 0.f) Health() = std::clamp(Health(), 0.f, new_max);
    Mana() = std::clamp(Mana(), 0.f, std::max(0.f, m_final_stats.max_mana));
    ClampShieldToMaxHealth();
    FinishFinalStatsRebuild();
}

bool CFlower::EquipPetal(int slot_index, const CPetalPrototype* proto, ERarity rarity)
{
    if (slot_index < 0 || slot_index >= static_cast<int>(m_slots.size())) return false;
    if (!m_slots[slot_index].m_available) return false;
    if (m_slots[slot_index].IsBanned()) return false;
    if (!proto || !proto->m_p_behavior) return false;

    m_slots[slot_index].SetPetal(proto, slot_index, rarity);

    if (!proto->m_p_behavior->GetPetalStats(rarity).stack) ApplyExclusivity(proto->m_type);
    MarkFinalStatsDirty();
    return true;
}

void CFlower::InstallPreparedLoadout(std::vector<std::optional<CPetalSlot>> prepared)
{
    assert(prepared.size() == m_slots.size());
    // 槽位已完成校验和分配；先完成所有清理回调，再转移限制并重算互斥，因为回调可能撤销其他槽位的效果。
    for (size_t i = 0; i < prepared.size(); ++i)
        if (prepared[i]) m_slots[i].ClearPetal();
    for (size_t i = 0; i < prepared.size(); ++i)
        if (prepared[i])
        {
            prepared[i]->TakeRestrictionsFrom(m_slots[i]);
            m_slots[i] = std::move(*prepared[i]);
        }
    for (const auto& slot : m_slots)
        if (slot.m_p_proto) ApplyExclusivity(slot.m_p_proto->m_type);
    MarkFinalStatsDirty();
}

void CFlower::InstallPreparedProgress(std::vector<CPetalSlot> prepared, int capacity)
{
    for (auto& slot : m_slots) slot.ClearPetal();
    DestroyPetalEntities();
    for (size_t i = 0; i < prepared.size() && i < m_slots.size(); ++i)
    {
        prepared[i].TakeRestrictionsFrom(m_slots[i]);
        prepared[i].SetPolicyBan(false);
    }
    m_slots.swap(prepared);
    m_petal_slot_capacity = std::max(0, capacity);
    m_petal_num_max = static_cast<int>(m_slots.size());
    for (const auto& slot : m_slots)
        if (slot.m_p_proto) ApplyExclusivity(slot.m_p_proto->m_type);
    MarkFinalStatsDirty();
}

void CFlower::RefreshTargetRadiusFromFinalStats()
{
    m_target_radius = std::max(0.f, m_final_stats.radius);
    // 运行时使用真实碰撞半径，属性目标保存在 m_target_radius，并在运动阶段插值。
    m_final_stats.radius = std::max(0.f, Radius());
}

void CFlower::AdvanceRadiusTowardsTarget(float dt)
{
    const float target = std::max(0.f, m_target_radius);
    const float current = std::max(0.f, Radius());
    const float elapsed = std::isfinite(dt) ? std::max(0.f, dt) : 0.f;
    const float speed = std::max(0.f, game_config::default_flower_radius_change_speed);
    const float max_step = speed * elapsed;
    const float delta = target - current;
    if (speed <= game_config::entity_collision_epsilon || std::abs(delta) <= max_step)
        Radius() = target;
    else
        Radius() = current + std::copysign(max_step, delta);
    m_final_stats.radius = Radius();
}

void CFlower::LoadPetalSlot(int slot_index, const CPetalPrototype* proto, ERarity rarity)
{
    if (slot_index < 0 || slot_index >= static_cast<int>(m_slots.size())) return;
    if (!proto || !proto->m_p_behavior) return;

    CPetalSlot& slot = m_slots[slot_index];
    slot.m_available = true;
    slot.SetPolicyBan(false);
    slot.SetPetal(proto, slot_index, rarity);
    if (!proto->m_p_behavior->GetPetalStats(rarity).stack) ApplyExclusivity(proto->m_type);
    MarkFinalStatsDirty();
}

bool CFlower::UnequipPetal(int slot_index)
{
    if (slot_index < 0 || slot_index >= static_cast<int>(m_slots.size())) return false;
    if (!CanUnequipPetal(slot_index)) return false;

    ForceUnequipPetal(slot_index);
    return true;
}

void CFlower::ForceUnequipPetal(int slot_index)
{
    if (slot_index < 0 || slot_index >= static_cast<int>(m_slots.size())) return;
    if (!m_slots[slot_index].m_p_proto) return;

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
    MarkFinalStatsDirty();

    if (!was_exclusive) return;

    for (auto& slot : m_slots)
    {
        if (slot.m_p_proto && slot.m_p_proto->m_type == old_type) slot.m_available = true;
    }
    ApplyExclusivity(old_type);
    MarkFinalStatsDirty();
}

bool CFlower::CanUnequipPetal(int slot_index) const
{
    if (slot_index < 0 || slot_index >= static_cast<int>(m_slots.size())) return false;

    const CPetalSlot& slot = m_slots[slot_index];
    if (!slot.m_p_proto) return false;

    if (!slot.m_available || slot.IsBanned()) return true;

    // 绷带续命不锁定原槽位，卸装不撤销已经应用的独立状态。
    return true;
}

void CFlower::ApplyExclusivity(EPetalType type)
{
    bool changed = false;
    CPetalSlot* p_best_slot = nullptr;
    float best_rarity = -1.f;

    for (auto& slot : m_slots)
    {
        if (!slot.m_p_proto || slot.m_p_proto->m_type != type) continue;
        if (!slot.m_p_proto->m_p_behavior) continue;
        if (slot.m_p_proto->m_p_behavior->GetPetalStats(slot.m_stored_rarity).stack) continue;

        float rarity_level = static_cast<float>(GetRarityValueRank(slot.m_stored_rarity));
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
        bool available = (&slot == p_best_slot);
        if (slot.m_available != available)
        {
            slot.m_available = available;
            changed = true;
        }
    }

    if (p_best_slot)
    {
        if (changed) MarkFinalStatsDirty();
        return;
    }

    for (auto& slot : m_slots)
    {
        if (slot.m_p_proto && slot.m_p_proto->m_type == type && !slot.m_available)
        {
            slot.m_available = true;
            changed = true;
        }
    }
    if (changed) MarkFinalStatsDirty();
}

bool CPlayerFlower::CollidesWithWalls() const
{
    if (HasActivePetal(EPetalType::Nullification, ERarity::Primordial)) return false;
    CGameWorld* world = GameWorld();
    const IGameController* controller = world ? world->GetController() : nullptr;
    return !(controller && controller->ForcesPrimordialNullification());
}

void CFlower::RefreshNullificationState()
{
    ERarity best_rarity = ERarity::Null;
    for (const auto& slot : m_slots)
    {
        if (!slot.m_available || slot.IsBanned() || !slot.m_p_proto) continue;
        if (slot.m_p_proto->m_type != EPetalType::Nullification) continue;
        if (GetRarityValueRank(slot.m_stored_rarity) > GetRarityValueRank(best_rarity))
            best_rarity = slot.m_stored_rarity;
    }

    // 世界规则可以强制维持玩家花朵的无效化状态。
    if (best_rarity == ERarity::Null)
    {
        CGameWorld* world = GameWorld();
        const IGameController* controller = world ? world->GetController() : nullptr;
        if (controller && controller->ForcesPrimordialNullification()) best_rarity = ERarity::Primordial;
    }

    auto states = FindStates<CNullificationState>();
    std::erase_if(states, [](const auto* state) { return !state->IsRuleManaged(); });
    if (best_rarity == ERarity::Null)
    {
        for (auto* state : states)
            RemoveState(state);
        return;
    }

    if (states.empty())
    {
        auto state = std::make_unique<CNullificationState>(this, endless, best_rarity);
        state->SetRuleManaged(true); AddState(std::move(state));
        return;
    }

    states.front()->m_rarity = best_rarity;
    states.front()->m_timer = endless;
    for (size_t i = 1; i < states.size(); ++i)
    {
        RemoveState(states[i]);
    }
}

static bool InsideCorruptionSpreadRange(const CFlower* self)
{
    if (!self || !self->GameWorld()) return false;

    const float query_radius = std::max(game_config::default_corruption_radius_primordial,
                                        game_config::default_corruption_radius_above_ultra);
    if (query_radius <= 0.f) return false;

    bool in_range = false;
    self->GameWorld()->GetSpatialGrid().ForEachInRange(self->Pos(), query_radius, [&](CEntity* entity) {
        if (in_range || !entity || entity == self || entity->IsMarkedForDes() || entity->IsDead()) return;
        const auto* flower = dynamic_cast<const CFlower*>(entity);
        if (!flower) return;
        flower->ForEachState<CCorruptionState>([&](const auto* state) {
            if (!state->Spreads()) return;
            const float radius = state->m_rarity == ERarity::Primordial
                ? game_config::default_corruption_radius_primordial : game_config::default_corruption_radius_above_ultra;
            if (radius > 0.f && DistanceSq(self->Pos(), flower->Pos()) <= radius * radius) in_range = true;
        });
    });
    return in_range;
}

void CFlower::RefreshCorruptionState()
{
    ERarity best_rarity = ERarity::Null;
    for (const auto& slot : m_slots)
    {
        if (!slot.m_available || slot.IsBanned() || !slot.m_p_proto) continue;
        if (slot.m_p_proto->m_type != EPetalType::Corruption) continue;
        if (GetRarityValueRank(slot.m_stored_rarity) > GetRarityValueRank(best_rarity))
            best_rarity = slot.m_stored_rarity;
    }

    auto states = FindStates<CCorruptionState>();
    std::erase_if(states, [](const auto* state) { return !state->IsRuleManaged(); });
    if (best_rarity == ERarity::Null)
    {
        // 无装备且离开传播范围时只撤销规则派生腐化，独立状态继续自身生命周期。
        if (!InsideCorruptionSpreadRange(this))
        {
            for (auto* state : states)
                RemoveState(state);
            return;
        }
        if (!states.empty()) states.front()->SetSpreads(false);
        return;
    }

    if (states.empty())
    {
        auto state = std::make_unique<CCorruptionState>(this, endless, best_rarity);
        state->SetRuleManaged(true); AddState(std::move(state));
        states = FindStates<CCorruptionState>();
        std::erase_if(states, [](const auto* state) { return !state->IsRuleManaged(); });
    } else
    {
        states.front()->m_rarity = best_rarity;
        states.front()->m_timer = endless;
        for (size_t i = 1; i < states.size(); ++i)
        {
            RemoveState(states[i]);
        }
        states = FindStates<CCorruptionState>();
        std::erase_if(states, [](const auto* state) { return !state->IsRuleManaged(); });
    }

    float radius = 0.f;
    if (best_rarity == ERarity::Primordial) radius = game_config::default_corruption_radius_primordial;
    else if (GetLevel(best_rarity) >= GetLevel(ERarity::Eternal))
        radius = game_config::default_corruption_radius_above_ultra;

    if (!states.empty()) states.front()->SetSpreads(radius > 0.f);

    if (radius <= 0.f || !GameWorld()) return;

    GameWorld()->GetSpatialGrid().ForEachInRange(Pos(), radius, [this, best_rarity](CEntity* entity) {
        if (!entity || entity == this || entity->IsMarkedForDes() || entity->IsDead()) return;
        auto* flower = dynamic_cast<CFlower*>(entity);
        if (!flower) return;
        const auto states = flower->FindStates<CCorruptionState>();
        if (std::none_of(states.begin(), states.end(), [](const auto* state) { return state->IsRuleManaged(); }))
        {
            auto state = std::make_unique<CCorruptionState>(flower, endless, best_rarity);
            state->SetRuleManaged(true); flower->AddState(std::move(state));
        }
    });
}

void CFlower::SetBanned(bool banned, int slot_index)
{
    if (slot_index < 0 || slot_index >= static_cast<int>(m_slots.size())) return;
    if (m_slots[slot_index].IsPolicyBanned() == banned) return;
    m_slots[slot_index].SetPolicyBan(banned);
    MarkFinalStatsDirty();
}

void CFlower::SetSlotBanSource(std::uint64_t source, int slot_index, bool banned)
{
    if (slot_index < 0 || slot_index >= static_cast<int>(m_slots.size())) return;
    const bool previous = m_slots[slot_index].IsBanned();
    m_slots[slot_index].SetBanSource(source, banned);
    if (previous != m_slots[slot_index].IsBanned()) MarkFinalStatsDirty();
}

void CFlower::RevokeStateEffects(std::uint64_t source)
{
    CMobBase::RevokeStateEffects(source);
    for (size_t i = 0; i < m_slots.size(); ++i) SetSlotBanSource(source, static_cast<int>(i), false);
}

void CFlower::InitSlots()
{
    size_t old_size = m_slots.size();
    m_slots.resize(m_petal_num_max);

    for (size_t i = old_size; i < m_slots.size(); ++i)
    {
        m_slots[i].m_slot_index = static_cast<int>(i);
    }
    for (const auto* state : FindStates<CBanSlotState>())
        SetSlotBanSource(state->EffectSourceId(), state->GetSlotIndex(), true);
}

void CFlower::SetPetalSlotCapacity(int capacity)
{
    m_petal_slot_capacity = std::max(0, capacity);
}

void CFlower::SetPetalSlotCount(int count)
{
    int max_slots = std::max(0, m_petal_slot_capacity);
    int petal_num_max = std::clamp(count, 0, max_slots);
    if (m_petal_num_max == petal_num_max) return;
    m_petal_num_max = petal_num_max;
    InitSlots();
    MarkFinalStatsDirty();
}

float CFlower::GetPetalLayerDistance() const
{
    if (m_final_stats.petal_rotation_mode != EPetalRotationMode::YinYang || m_yinyang_layout_count <= 0) return 0.f;

    int columns = GetYinYangColumnCount();
    if (columns <= 0) return 0.f;

    int max_column_size = (m_total_copies + columns - 1) / columns;
    int layers = std::max(1, max_column_size);
    return static_cast<float>(std::max(0, layers - 1)) * game_config::default_petal_orbit_radius;
}

int CFlower::GetYinYangColumnCount() const
{
    if (m_final_stats.petal_rotation_mode != EPetalRotationMode::YinYang || m_yinyang_layout_count <= 0)
        return std::max(1, m_total_copies);
    if (!HasNonYinYangPetals()) return 1;
    if (m_yinyang_layout_count == 1) return std::max(1, m_total_copies);
    return GetYinYangLayout(m_yinyang_layout_count).columns;
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
        if (!slot.m_available || slot.IsBanned() || !slot.m_p_proto) continue;
        if (slot.m_p_proto->m_type != EPetalType::YinYang && slot.GetCurrentCopyCount(this) > 0) return true;
    }
    return false;
}

bool CFlower::HasActivePetal(EPetalType type, ERarity rarity) const
{
    for (const auto& slot : m_slots)
    {
        if (!slot.m_available || slot.IsBanned() || !slot.m_p_proto) continue;
        if (slot.m_p_proto->m_type != type) continue;
        if (rarity == ERarity::Null || slot.m_stored_rarity == rarity) return true;
    }
    return false;
}

CPlayerFlower::CPlayerFlower(CGameWorld* pworld, florrbt::Vector2f pos, float r, EMobType mob_type, ERarity rarity,
                             const SFlowerStats& base)
    : CFlower(pworld, pos, r, mob_type, rarity, base)
{
    AddTag(EEntityTag::ClearOwnedSummonsOnDestroy);

    int slot_count = std::clamp(game_config::mob_player_flower_initial_petal_slots, 0,
                                std::max(0, game_config::mob_player_flower_max_petal_slots));
    SetPetalSlotCount(slot_count);
    RebuildFinalStats();
    Health() = m_final_stats.max_health;
    if (game_config::player_spawn_invincible_duration > 0.f)
        AddState(std::make_unique<CInvincibleState>(this, game_config::player_spawn_invincible_duration, GetRarity()));
}

void CPlayerFlower::RebuildFinalStats()
{
    // 死亡期间只保存下一次配装，尸体保留死亡瞬间的外观和属性。
    if (m_is_dead) return;
    float old_max = m_final_stats.max_health;

    m_final_stats = BuildPlayerFlowerLevelStats(m_base_stats, m_level);
    CGameContext* context = GameContext();
    CPlayer* player = context ? context->FindPlayerFromEntity(this) : nullptr;
    if (player)
    {
        STalentContext talent_ctx;
        talent_ctx.world = GameWorld();
        talent_ctx.player = player;
        talent_ctx.entity = this;
        talent_ctx.flower = this;
        talent_ctx.player_flower = this;
        talent_ctx.flower_stats = &m_final_stats;
        player->ApplyTalents(ETalentEvent::RebuildFlowerStats, talent_ctx);
    }

    for (const auto& slot : GetSlots())
    {
        slot.ApplyStatsTo(m_final_stats, this);
    }

    float new_max = m_final_stats.max_health;
    RefreshTargetRadiusFromFinalStats();
    if (old_max > 0.f && new_max > 0.f && new_max != old_max) Health() = Health() * new_max / old_max;
    if (new_max > 0.f) Health() = std::clamp(Health(), 0.f, new_max);
    // 容量变化不按比例补充法力，仅裁剪超出新上限的部分。
    Mana() = std::clamp(Mana(), 0.f, std::max(0.f, m_final_stats.max_mana));
    ClampShieldToMaxHealth();
    FinishFinalStatsRebuild();
}

void CPlayerFlower::RefreshTalentSlotCount()
{
    const IGameController* controller = GameWorld() ? GameWorld()->GetController() : nullptr;
    // 玩法提供的卡片和槽位不是账号库存，缩减时不能把信息素等代理卡片持久化为战利品。
    if (controller && controller->LocksPetalLoadout()) return;
    int slot_count = std::clamp(game_config::mob_player_flower_initial_petal_slots, 0,
                                std::max(0, game_config::mob_player_flower_max_petal_slots));
    CGameContext* context = GameContext();
    CPlayer* player = context ? context->FindPlayerFromEntity(this) : nullptr;
    if (player) slot_count = player->CountTalentSlots();

    auto& slots = GetSlots();
    int old_count = static_cast<int>(slots.size());
    bool changed = false;
    if (slot_count < old_count)
    {
        for (int i = slot_count; i < old_count; ++i)
        {
            CPetalSlot& slot = slots[i];
            if (!slot.m_p_proto) continue;

            uint8_t old_type = static_cast<uint8_t>(slot.m_p_proto->m_type);
            uint8_t old_rarity = static_cast<uint8_t>(slot.m_stored_rarity);
            slot.ClearPetal();
            slot.m_p_proto = nullptr;
            slot.m_stored_rarity = ERarity::Null;
            slot.m_available = true;
            slot.SetPolicyBan(false);
            changed = true;

            if (player && player->IsAuthenticated())
            {
                CAccountDataStore::AddItem(player->GetInventoryAccountName(), old_type, old_rarity, 1);
                CAccountDataStore::ClearSlot(player->GetInventoryAccountName(), static_cast<uint8_t>(i));
            }
        }
    }

    SetPetalSlotCount(slot_count);
    if (changed) MarkFinalStatsDirty();
}

void CPlayerFlower::TakeExp(std::int64_t exp)
{
    if (exp <= 0 || m_is_dead) return;

    const std::int64_t max_exp = std::numeric_limits<std::int64_t>::max();
    m_exp = std::max<std::int64_t>(0, m_exp);
    m_exp = (exp > max_exp - m_exp) ? max_exp : (m_exp + exp);
    bool leveled = false;
    int gained_talent_points = 0;
    const int max_level = std::max(1, game_config::player_level_max);
    while (m_level < max_level)
    {
        std::int64_t required = GetRequiredPlayerExperience(m_level);
        if (required <= 0 || m_exp < required) break;
        m_exp -= required;
        ++m_level;
        gained_talent_points += CalculateTalentPointsForLevel(m_level);
        leveled = true;
    }
    if (m_level >= max_level) m_exp = 0;

    if (leveled) RebuildFinalStats();

    CGameContext* context = GameContext();
    CPlayer* player = context ? context->FindPlayerFromEntity(this) : nullptr;
    if (player && gained_talent_points > 0) player->AddTalentPoints(gained_talent_points);
    if (player && player->IsAuthenticated()) CAccountDataStore::SetProgress(player->GetProgressAccountName(), m_level, m_exp);
}

std::int64_t CPlayerFlower::ExpRequired() const { return GetRequiredPlayerExperience(m_level); }

void CPlayerFlower::OnTickActions(float dt)
{
    if (m_is_dead)
    {
        m_vel = { 0.f, 0.f };
        m_attacking = false;
        m_defending = false;
        return;
    }

    CFlower::OnTickActions(dt);
}

void CPlayerFlower::OnTakeDamage(float dmg, CEntity* attacker, EDamageType damage_type)
{
    if (m_is_dead) return;

    CFlower::OnTakeDamage(dmg, attacker, damage_type);
    if (IsMarkedForDes())
    {
        CGameContext* context = GameContext();
        CPlayer* player = context ? context->FindPlayerFromEntity(this) : nullptr;
        if (player)
        {
            bool prevent_death = false;
            float invincible_time = 0.f;
            float cooldown = player->GetSecondChanceCooldown();

            STalentContext talent_ctx;
            talent_ctx.world = GameWorld();
            talent_ctx.player = player;
            talent_ctx.entity = this;
            talent_ctx.attacker = attacker;
            talent_ctx.flower = this;
            talent_ctx.player_flower = this;
            talent_ctx.invincible_time = &invincible_time;
            talent_ctx.cooldown = &cooldown;
            talent_ctx.prevent_death = &prevent_death;
            talent_ctx.damage_type = damage_type;
            player->ApplyTalents(ETalentEvent::OnFlowerFatalDamage, talent_ctx);
            player->SetSecondChanceCooldown(cooldown);

            if (prevent_death && invincible_time > 0.f)
            {
                CancelDestroy();
                Health() = std::max(1.f, Health());
                AddState(std::make_unique<CInvincibleState>(this, invincible_time, GetRarity()), EStateApplyTiming::DamageProtection);
                return;
            }
        }
    }

    if (IsMarkedForDes() && TryEnterUndeadFromBandage())
    {
        CancelDestroy();
        Health() = std::max(1.f, Health());
        return;
    }
    if (IsMarkedForDes()) EnterDeathState();
}

void CPlayerFlower::EnterDeathState()
{
    if (m_is_dead) return;

    const SFlowerStats death_stats = m_final_stats;
    const float death_radius = Radius();
    const float death_mass = Mass();

    ConsumeCorruptionPetalsOnDeath();

    m_is_dead = true;
    CancelDestroy();
    Health() = 0.f;
    m_vel = { 0.f, 0.f };
    m_attacking = false;
    m_defending = false;
    BeginBloodSacrifice();
    if (GameWorld()) GameWorld()->DestroySummonedMobsOwnedBy(GetHandle());
    DestroyPetalEntities();

    m_final_stats = death_stats;
    Radius() = death_radius;
    Mass() = death_mass;
    ClearFinalStatsDirty();
}

void CPlayerFlower::PrepareRespawnDestroy(EEntityRemovalReason reason)
{
    if (GameWorld()) GameWorld()->DestroySummonedMobsOwnedBy(GetHandle());
    ClearPetals();
    m_is_dead = false;
    MarkForDestroy(reason);
}

bool CPlayerFlower::ReviveFromYggdrasil(float health_fraction)
{
    if (!m_is_dead || IsMarkedForDes()) return false;

    for (auto* undead : FindStates<CUndeadState>())
    {
        undead->CancelDeathOnDestroy();
        RemoveState(undead, EStateApplyTiming::DamageProtection);
    }
    for (auto* corruption : FindStates<CCorruptionState>())
        RemoveState(corruption, EStateApplyTiming::DamageProtection);
    m_is_dead = false;
    CancelDestroy();
    RebuildFinalStats();
    float max_health = GetFinalStats() ? GetFinalStats()->max_health : m_base_stats.max_health;
    Health() = std::max(game_config::flower_revive_min_health, max_health * std::clamp(health_fraction, 0.0f, 1.0f));
    m_vel = { 0.f, 0.f };
    m_attacking = false;
    m_defending = false;
    AddState(std::make_unique<CInvincibleState>(this, 1.f, GetRarity()), EStateApplyTiming::DamageProtection);
    return true;
}

void CPlayerFlower::ConsumeCorruptionPetalsOnDeath()
{
    CGameContext* context = GameContext();
    CPlayer* player = context ? context->FindPlayerFromEntity(this) : nullptr;
    auto& slots = GetSlots();
    bool cleared = false;
    for (size_t i = 0; i < slots.size(); ++i)
    {
        CPetalSlot& slot = slots[i];
        if (!slot.m_p_proto || slot.m_p_proto->m_type != EPetalType::Corruption) continue;
        const uint8_t old_type = static_cast<uint8_t>(slot.m_p_proto->m_type);
        const uint8_t old_rarity = static_cast<uint8_t>(slot.m_stored_rarity);
        const bool should_unequip = IsAtLeastRarity(slot.m_stored_rarity, ERarity::Super);
        if (!should_unequip && GetLevel(slot.m_stored_rarity) > GetLevel(ERarity::Ultra)) continue;
        slot.ClearPetal();
        slot.m_p_proto = nullptr;
        slot.m_stored_rarity = ERarity::Null;
        slot.m_available = true;
        cleared = true;
        if (player && player->IsAuthenticated())
        {
            if (should_unequip) CAccountDataStore::AddItem(player->GetInventoryAccountName(), old_type, old_rarity, 1);
            CAccountDataStore::ClearSlot(player->GetInventoryAccountName(), static_cast<uint8_t>(i));
        }
    }

    if (!cleared) return;
    for (auto& slot : slots)
    {
        if (slot.m_p_proto && slot.m_p_proto->m_type == EPetalType::Corruption) slot.m_available = true;
    }
    ApplyExclusivity(EPetalType::Corruption);
    MarkFinalStatsDirty();
}

bool CPlayerFlower::TryEnterUndeadFromBandage()
{
    if (HasState<CUndeadState>()) return false;
    if (HasState<CNoReviveState>()) return false;

    auto& slots = GetSlots();
    int best_slot = -1;
    ERarity best_rarity = ERarity::Null;
    for (size_t i = 0; i < slots.size(); ++i)
    {
        const CPetalSlot& slot = slots[i];
        if (!slot.m_available || slot.IsBanned() || !slot.m_p_proto) continue;
        if (slot.m_p_proto->m_type != EPetalType::Bandage) continue;
        if (GetRarityValueRank(slot.m_stored_rarity) > GetRarityValueRank(best_rarity))
        {
            best_rarity = slot.m_stored_rarity;
            best_slot = static_cast<int>(i);
        }
    }

    if (best_slot < 0 || best_rarity == ERarity::Null) return false;

    float duration = GetBandageUndeadDuration(best_rarity);
    if (duration <= 0.f) return false;

    Health() = 1.f;
    AddState(std::make_unique<CUndeadState>(this, duration, best_rarity, best_slot), EStateApplyTiming::DamageProtection);
    AddState(std::make_unique<CNoReviveState>(this, game_config::default_bandage_no_revive_duration, best_rarity), EStateApplyTiming::DamageProtection);
    return true;
}

void CPlayerFlower::BeginBloodSacrifice()
{
    CGameWorld* world = GameWorld();
    const FlorrBtMap* map = world ? world->GetMap() : nullptr;
    if (!map)
    {
        LOG_INFO("blood_sacrifice", "Skipped: no map");
        return;
    }

    auto& slots = GetSlots();
    for (size_t i = 0; i < slots.size(); ++i)
    {
        CPetalSlot& slot = slots[i];
        if (!slot.m_available || slot.IsBanned() || !slot.m_p_proto) continue;
        if (slot.m_p_proto->m_type != EPetalType::BloodSacrifice) continue;

        for (const FlorrBtMap::Zone& zone : map->zones)
        {
            if (!IsPointInZone(zone, Pos())) continue;

            std::vector<SZoneMobEntry> mob_entries = ParseZoneMobEntries(zone.mobs);
            if (mob_entries.empty())
            {
                LOG_INFO("blood_sacrifice", "Skipped: zone mob pool is empty: " + zone.mobs);
                return;
            }

            EMobType mob_type = PickZoneMobType(mob_entries);
            if (mob_type == EMobType::None)
            {
                LOG_INFO("blood_sacrifice", "Skipped: picked no mob from zone pool: " + zone.mobs);
                return;
            }

            auto ritual = std::make_unique<CBloodSacrificeRitual>(world, Pos(), mob_type, slot.m_stored_rarity,
                                                                  game_config::default_blood_sacrifice_delay);
            CBloodSacrificeRitual* raw_ritual =
                dynamic_cast<CBloodSacrificeRitual*>(world->InsertEntity(std::move(ritual)));
            if (!raw_ritual)
            {
                LOG_INFO("blood_sacrifice", "Skipped: failed to create ritual entity");
                return;
            }

            LOG_INFO("blood_sacrifice", "Created ritual id " + std::to_string(raw_ritual->Id()) + " for " +
                                            std::string(GetRarityName(slot.m_stored_rarity)) + " " +
                                            std::string(GetMobTypeName(mob_type)) + " from slot " + std::to_string(i) +
                                            " at " + std::to_string(Pos().x) + "," + std::to_string(Pos().y));
            ClearBloodSacrificeSlot(static_cast<int>(i));
            return;
        }

        LOG_INFO("blood_sacrifice", "Skipped: death position is outside spawn zones");
        return;
    }
}

void CPlayerFlower::ClearBloodSacrificeSlot(int slot_index)
{
    auto& slots = GetSlots();
    if (slot_index < 0 || slot_index >= static_cast<int>(slots.size())) return;

    CPetalSlot& slot = slots[slot_index];
    if (!slot.m_p_proto || slot.m_p_proto->m_type != EPetalType::BloodSacrifice) return;

    slot.ClearPetal();
    slot.m_p_proto = nullptr;
    slot.m_stored_rarity = ERarity::Null;
    slot.m_available = true;

    for (auto& other_slot : slots)
    {
        if (other_slot.m_p_proto && other_slot.m_p_proto->m_type == EPetalType::BloodSacrifice)
            other_slot.m_available = true;
    }
    ApplyExclusivity(EPetalType::BloodSacrifice);
    MarkFinalStatsDirty();

    CGameContext* context = GameContext();
    CPlayer* player = context ? context->FindPlayerFromEntity(this) : nullptr;
    if (player && player->IsAuthenticated())
        CAccountDataStore::ClearSlot(player->GetInventoryAccountName(), static_cast<uint8_t>(slot_index));
}
