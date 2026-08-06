#include "flower.h"
#include "../../HotReload/snapshot_archive.h"
#include "../../Persistence/account_store.h"
#include "../../Persistence/unique_petal_registry.h"
#include "../../server.h"
#include "../gamecontext.h"
#include "../gameworld.h"
#include "../player.h"
#include "../states/states.h"
#include "../talent.h"
#include "../zone_mob_tools.h"
#include "blood_sacrifice_ritual.h"
#include "petals/petal.h"
#include <algorithm>
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

std::int64_t PlayerFlowerExpRequired(int level)
{
    const int safe_level = std::max(1, level);
    const long double base_exp = std::max(1.0L, static_cast<long double>(game_config::player_level_exp_base));
    const long double level_growth = std::max(1.0L, static_cast<long double>(game_config::player_level_exp_growth));
    const long double required = base_exp * std::pow(level_growth, static_cast<long double>(safe_level - 1));
    const long double max_exp = static_cast<long double>(std::numeric_limits<std::int64_t>::max());
    if (!std::isfinite(static_cast<double>(required)) || required >= max_exp)
        return std::numeric_limits<std::int64_t>::max();
    return std::max<std::int64_t>(1, static_cast<std::int64_t>(std::llround(required)));
}

int TalentPointGainForLevel(int level)
{
    if (level <= 1) return 0;

    int gain = game_config::player_talent_point_base_gain;
    const int minor_interval = game_config::player_talent_point_minor_level_interval;
    const int major_interval = game_config::player_talent_point_major_level_interval;
    if (minor_interval > 0 && level % minor_interval == 0) gain += game_config::player_talent_point_minor_bonus;
    if (major_interval > 0 && level % major_interval == 0) gain += game_config::player_talent_point_major_bonus;
    return gain;
}

void SyncFlowerRadiusWithStats(CFlower& flower, SFlowerStats& stats)
{
    flower.m_radius = std::max(0.f, stats.radius);
    stats.radius = flower.m_radius;
}

} // namespace

CFlower::~CFlower()
{
    for (CBanSlotState* state : FindStates<CBanSlotState>())
        RemoveState(state);
}

CMechaFlower::CMechaFlower(CGameWorld* pworld, sf::Vector2f pos, float r, EMobType mob_type, ERarity rarity,
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

void CTitanFlower::Tick(float dt)
{
    m_unique_refresh_timer -= std::max(0.f, dt);
    if (m_unique_refresh_timer <= 0.f)
    {
        RefreshUniqueLoadout();
        m_unique_refresh_timer = std::max(1.f, game_config::titan_unique_refresh_seconds);
    }
    CFlower::Tick(dt);
}

void CTitanFlower::CaptureRuntimeSnapshot(CSnapshotWriter& writer) const
{
    writer.Field("unique_refresh_timer", m_unique_refresh_timer);
}

bool CTitanFlower::RestoreRuntimeSnapshot(const CSnapshotReader& reader, std::uint32_t version, std::string& error)
{
    if (!CFlower::RestoreRuntimeSnapshot(reader, version, error)) return false;
    if (reader.Has("forge_cooldown_timer"))
    {
        if (CServer* server = CServer::GetInstance())
            server->MergeLegacyTitanForgeCooldownForRestore(reader.Float("forge_cooldown_timer"));
    }
    m_unique_refresh_timer = std::max(0.f, reader.Float("unique_refresh_timer", m_unique_refresh_timer));
    return true;
}

void CTitanFlower::ReplaceForgedUniquePetal(EPetalType type)
{
    auto& slots = GetSlots();
    const auto forged_slot = std::find_if(slots.begin(), slots.end(), [type](const CPetalSlot& slot) {
        return slot.m_p_proto && slot.m_p_proto->m_type == type && slot.m_stored_rarity == ERarity::Unique;
    });
    if (forged_slot == slots.end()) return;

    CServer* server = CServer::GetInstance();
    IUniquePetalRegistry* registry = server ? server->GetUniquePetalRegistry() : nullptr;
    if (!registry) return;

    std::vector<const CPetalPrototype*> candidates;
    candidates.reserve(PetalRegistry().size());
    for (const auto& [candidate_type, prototype] : PetalRegistry())
    {
        if (candidate_type == EPetalType::None || !prototype || !prototype->m_p_behavior ||
            registry->FindOwner(candidate_type) || HasUniquePetal(candidate_type))
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
    CServer* server = CServer::GetInstance();
    IUniquePetalRegistry* registry = server ? server->GetUniquePetalRegistry() : nullptr;
    if (!registry) return;

    std::vector<const CPetalPrototype*> candidates;
    candidates.reserve(PetalRegistry().size());
    for (const auto& [type, prototype] : PetalRegistry())
    {
        if (type == EPetalType::None || !prototype || !prototype->m_p_behavior || registry->FindOwner(type)) continue;
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

void CFlower::Tick(float dt)
{
    const std::uint64_t config_revision = game_config::GetConfigRevision();
    if (m_config_revision != config_revision)
    {
        m_config_revision = config_revision;
        MarkFinalStatsDirty();
    }

    CAttackableMob<SFlowerStats>::Tick(dt);
    if (m_final_stats_dirty) RebuildFinalStats();
    float health_regen = m_final_stats.health_regen;
    if (m_defending && !m_attacking) health_regen += m_final_stats.defense_health_regen;
    if (health_regen > 0.f && m_final_stats.max_health > 0.f && m_health > 0.f)
    {
        float regen = health_regen * std::max(0.f, m_final_stats.petal_medicine_multiplier) *
                      std::max(0.f, m_final_stats.healing_received_multiplier) *
                      std::max(0.f, GetMedicMultiplier(this));
        Heal(regen * dt);
    }
    RefreshNullificationState();
    RefreshCorruptionState();

    if (HasState<CDiggingState>())
    {
        DestroyPetalEntities();
        return;
    }

    if (m_petal_state_dirty)
    {
        m_petal_state_dirty = false;
        for (auto& slot : m_slots)
        {
            if (slot.m_available && !slot.m_banned) slot.RefreshPetalState(this);
        }
    }

    m_total_copies = 0;
    m_yinyang_layout_count = 0;
    int yinyang_bonus_count = 0;
    for (auto& slot : m_slots)
    {
        if (!slot.m_available || slot.m_banned)
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
    m_petal_rotation_angle += m_final_stats.petal_rotation_speed * direction * dt;
    m_petal_rotation_angle = std::fmod(m_petal_rotation_angle, 2.f * game_config::pi);

    for (auto& slot : m_slots)
    {
        if (slot.m_available && !slot.m_banned) slot.Tick(dt, this, true);
    }
}
void CFlower::Heal(float amount)
{
    if (amount <= 0.f || m_health <= 0.f || m_final_stats.max_health <= 0.f) return;

    const float missing_health = std::max(0.f, m_final_stats.max_health - m_health);
    const float health_gain = std::min(amount, missing_health);
    m_health += health_gain;

    const float excess_healing = std::max(0.f, amount - health_gain);
    const float conversion = std::max(0.f, m_final_stats.overheal_to_shield);
    const float shield_cap = std::max(0.f, m_final_stats.max_health);
    if (excess_healing > 0.f && conversion > 0.f && shield_cap > 0.f)
        m_shield = std::min(shield_cap, m_shield + excess_healing * conversion);
}

void CFlower::ClampShieldToMaxHealth()
{
    const float shield_cap = std::max(0.f, m_final_stats.max_health);
    m_shield = std::clamp(m_shield, 0.f, std::max(0.f, shield_cap));
}

void CFlower::TakeDamage(float dmg, CEntity* attacker, EDamageType damage_type)
{
    if (ShouldBlockDiggingDamage(this, attacker, damage_type)) return;

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

    if (dmg <= 0.f) return;
    const float absorbed = std::min(std::max(0.f, m_shield), dmg);
    m_shield -= absorbed;
    dmg -= absorbed;
    if (dmg <= 0.f) return;

    CAttackableMob<SFlowerStats>::TakeDamage(dmg, attacker, damage_type);
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
            if (petal) petal->MarkForDestroy(EEntityRemovalReason::OwnerRemoved);
            petal = nullptr;
        }
        std::fill(slot.m_bonus_active.begin(), slot.m_bonus_active.end(), 0);
        slot.m_start_copy_index = -1;
    }
    m_total_copies = 0;
}

void CFlower::ReloadAllPetals()
{
    for (auto& slot : m_slots)
    {
        if (!slot.m_p_proto || !slot.m_p_proto->m_p_behavior) continue;

        SPetalStats stats = slot.m_p_proto->m_p_behavior->GetPetalStatsForSlot(slot.m_stored_rarity, &slot, this);
        const float reload = std::max(0.f, slot.m_p_proto->m_type == EPetalType::Shovel ? stats.preload : stats.reload);
        for (size_t i = 0; i < slot.m_p_petals.size(); ++i)
        {
            if (slot.m_p_petals[i])
            {
                slot.m_p_petals[i]->MarkForDestroy(EEntityRemovalReason::Replaced);
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

bool CFlower::TryStartBurrowFromShovel()
{
    if (HasState<CDiggingState>()) return false;

    for (auto& slot : m_slots)
    {
        if (!slot.m_available || slot.m_banned || !slot.m_p_proto) continue;
        if (slot.m_p_proto->m_type != EPetalType::Shovel) continue;

        for (size_t i = 0; i < slot.m_p_petals.size(); ++i)
        {
            CPetal* petal = slot.m_p_petals[i];
            if (!petal || petal->m_is_marked_for_des || petal->m_health <= 0.f || petal->m_hidden) continue;

            float duration = std::max(game_config::server_fixed_dt, game_config::default_shovel_dig_duration);
            petal->m_reload_override = std::max(0.f, petal->m_final_petal_stats.preload);
            petal->m_health = 0.f;
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
        if (!slot.m_available || slot.m_banned || !slot.m_p_proto) continue;
        if (slot.m_p_proto->m_type != EPetalType::Moon) continue;

        for (CPetal* petal : slot.m_p_petals)
        {
            if (petal && !petal->m_is_marked_for_des && petal->m_health > 0.f && !petal->m_hidden) return petal;
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
    SyncFlowerRadiusWithStats(*this, m_final_stats);
    if (old_max > 0.f && new_max > 0.f && new_max != old_max) m_health = m_health * new_max / old_max;
    if (new_max > 0.f) m_health = std::clamp(m_health, 0.f, new_max);
    ClampShieldToMaxHealth();
    FinishFinalStatsRebuild();
}

void CFlower::EquipPetal(int slot_index, const CPetalPrototype* proto, ERarity rarity)
{
    if (slot_index < 0 || slot_index >= static_cast<int>(m_slots.size())) return;
    if (!m_slots[slot_index].m_available) return;
    if (!proto || !proto->m_p_behavior) return;

    m_slots[slot_index].SetPetal(proto, slot_index, rarity);

    if (!proto->m_p_behavior->GetPetalStats(rarity).stack) ApplyExclusivity(proto->m_type);
    MarkFinalStatsDirty();
}

void CFlower::LoadPetalSlot(int slot_index, const CPetalPrototype* proto, ERarity rarity)
{
    if (slot_index < 0 || slot_index >= static_cast<int>(m_slots.size())) return;
    if (!proto || !proto->m_p_behavior) return;

    CPetalSlot& slot = m_slots[slot_index];
    slot.m_available = true;
    slot.m_banned = false;
    slot.SetPetal(proto, slot_index, rarity);
    if (!proto->m_p_behavior->GetPetalStats(rarity).stack) ApplyExclusivity(proto->m_type);
    MarkFinalStatsDirty();
}

void CFlower::UnequipPetal(int slot_index)
{
    if (slot_index < 0 || slot_index >= static_cast<int>(m_slots.size())) return;
    if (!CanUnequipPetal(slot_index)) return;

    ForceUnequipPetal(slot_index);
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

    if (!slot.m_available || slot.m_banned) return true;

    if (slot.m_p_proto->m_type == EPetalType::Corruption)
    {
        if (GetLevel(slot.m_stored_rarity) <= GetLevel(ERarity::Ultra)) return false;
        return IsDead();
    }

    if (slot.m_p_proto->m_type == EPetalType::Bandage)
    {
        auto undead_states = const_cast<CFlower*>(this)->FindStates<CUndeadState>();
        for (const auto* undead : undead_states)
        {
            if (undead && undead->SourceSlot() == slot_index) return false;
        }
    }

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

void CFlower::RefreshNullificationState()
{
    ERarity best_rarity = ERarity::Null;
    for (const auto& slot : m_slots)
    {
        if (!slot.m_available || slot.m_banned || !slot.m_p_proto) continue;
        if (slot.m_p_proto->m_type != EPetalType::Nullification) continue;
        if (GetRarityValueRank(slot.m_stored_rarity) > GetRarityValueRank(best_rarity))
            best_rarity = slot.m_stored_rarity;
    }

    auto states = FindStates<CNullificationState>();
    if (best_rarity == ERarity::Null)
    {
        for (auto* state : states)
            RemoveState(state);
        return;
    }

    if (states.empty())
    {
        AddState(std::make_unique<CNullificationState>(this, endless, best_rarity));
        return;
    }

    states.front()->m_rarity = best_rarity;
    states.front()->m_timer = endless;
    for (size_t i = 1; i < states.size(); ++i)
    {
        RemoveState(states[i]);
    }
}

void CFlower::RefreshCorruptionState()
{
    ERarity best_rarity = ERarity::Null;
    for (const auto& slot : m_slots)
    {
        if (!slot.m_available || slot.m_banned || !slot.m_p_proto) continue;
        if (slot.m_p_proto->m_type != EPetalType::Corruption) continue;
        if (GetRarityValueRank(slot.m_stored_rarity) > GetRarityValueRank(best_rarity))
            best_rarity = slot.m_stored_rarity;
    }

    auto states = FindStates<CCorruptionState>();
    if (best_rarity == ERarity::Null)
    {
        for (auto* state : states)
            RemoveState(state);
        return;
    }

    if (states.empty())
    {
        AddState(std::make_unique<CCorruptionState>(this, endless, best_rarity));
    } else
    {
        states.front()->m_rarity = best_rarity;
        states.front()->m_timer = endless;
        for (size_t i = 1; i < states.size(); ++i)
        {
            RemoveState(states[i]);
        }
    }

    float radius = 0.f;
    if (best_rarity == ERarity::Primordial) radius = game_config::default_corruption_radius_primordial;
    else if (GetLevel(best_rarity) >= GetLevel(ERarity::Eternal))
        radius = game_config::default_corruption_radius_above_ultra;
    if (radius <= 0.f || !GameWorld()) return;

    GameWorld()->GetSpatialGrid().ForEachInRange(m_pos, radius, [this, best_rarity](CEntity* entity) {
        if (!entity || entity == this || entity->m_is_marked_for_des || entity->IsDead()) return;
        auto* flower = dynamic_cast<CFlower*>(entity);
        if (!flower) return;
        if (!flower->HasState<CCorruptionState>())
            flower->AddState(std::make_unique<CCorruptionState>(flower, endless, best_rarity));
    });
}

void CFlower::SetBanned(bool banned, int slot_index)
{
    if (slot_index < 0 || slot_index >= static_cast<int>(m_slots.size())) return;
    if (m_slots[slot_index].m_banned == banned) return;
    m_slots[slot_index].m_banned = banned;
    MarkFinalStatsDirty();
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
        if (!slot.m_available || slot.m_banned || !slot.m_p_proto) continue;
        if (slot.m_p_proto->m_type != EPetalType::YinYang && slot.GetCurrentCopyCount(this) > 0) return true;
    }
    return false;
}

bool CFlower::HasActivePetal(EPetalType type, ERarity rarity) const
{
    for (const auto& slot : m_slots)
    {
        if (!slot.m_available || slot.m_banned || !slot.m_p_proto) continue;
        if (slot.m_p_proto->m_type != type) continue;
        if (rarity == ERarity::Null || slot.m_stored_rarity == rarity) return true;
    }
    return false;
}

CPlayerFlower::CPlayerFlower(CGameWorld* pworld, sf::Vector2f pos, float r, EMobType mob_type, ERarity rarity,
                             const SFlowerStats& base)
    : CFlower(pworld, pos, r, mob_type, rarity, base)
{
    AddTag(EEntityTag::ClearOwnedSummonsOnDestroy);

    int slot_count = std::clamp(game_config::mob_player_flower_initial_petal_slots, 0,
                                std::max(0, game_config::mob_player_flower_max_petal_slots));
    SetPetalSlotCount(slot_count);
    RebuildFinalStats();
    m_health = m_final_stats.max_health;
    if (game_config::player_spawn_invincible_duration > 0.f)
        AddState(std::make_unique<CInvincibleState>(this, game_config::player_spawn_invincible_duration, GetRarity()));
}

void CPlayerFlower::RebuildFinalStats()
{
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
    SyncFlowerRadiusWithStats(*this, m_final_stats);
    if (old_max > 0.f && new_max > 0.f && new_max != old_max) m_health = m_health * new_max / old_max;
    if (new_max > 0.f) m_health = std::clamp(m_health, 0.f, new_max);
    ClampShieldToMaxHealth();
    FinishFinalStatsRebuild();
}

void CPlayerFlower::RefreshTalentSlotCount()
{
    int slot_count = std::clamp(game_config::mob_player_flower_initial_petal_slots, 0,
                                std::max(0, game_config::mob_player_flower_max_petal_slots));
    CGameContext* context = GameContext();
    CPlayer* player = context ? context->FindPlayerFromEntity(this) : nullptr;
    if (player) slot_count = player->CalculateTalentSlotCount();

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
            slot.m_banned = false;
            changed = true;

            if (player && player->IsAuthenticated())
            {
                CAccountDataStore::AddItem(player->GetAccountName(), old_type, old_rarity, 1);
                CAccountDataStore::ClearSlot(player->GetAccountName(), static_cast<uint8_t>(i));
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
        std::int64_t required = PlayerFlowerExpRequired(m_level);
        if (required <= 0 || m_exp < required) break;
        m_exp -= required;
        ++m_level;
        gained_talent_points += TalentPointGainForLevel(m_level);
        leveled = true;
    }
    if (m_level >= max_level) m_exp = 0;

    if (leveled) RebuildFinalStats();

    CGameContext* context = GameContext();
    CPlayer* player = context ? context->FindPlayerFromEntity(this) : nullptr;
    if (player && gained_talent_points > 0) player->AddTalentPoints(gained_talent_points);
    if (player && player->IsAuthenticated()) CAccountDataStore::SetProgress(player->GetAccountName(), m_level, m_exp);
}

std::int64_t CPlayerFlower::ExpRequired() const { return PlayerFlowerExpRequired(m_level); }

void CPlayerFlower::Tick(float dt)
{
    if (m_is_dead)
    {
        m_vel = { 0.f, 0.f };
        m_attacking = false;
        m_defending = false;
        return;
    }

    CFlower::Tick(dt);
}

void CPlayerFlower::TakeDamage(float dmg, CEntity* attacker, EDamageType damage_type)
{
    if (m_is_dead) return;

    CFlower::TakeDamage(dmg, attacker, damage_type);
    if (m_is_marked_for_des)
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
                m_health = std::max(1.f, m_health);
                AddState(std::make_unique<CInvincibleState>(this, invincible_time, GetRarity()));
                return;
            }
        }
    }

    if (m_is_marked_for_des && TryEnterUndeadFromBandage())
    {
        CancelDestroy();
        m_health = std::max(1.f, m_health);
        return;
    }
    if (m_is_marked_for_des) EnterDeathState();
}

void CPlayerFlower::EnterDeathState()
{
    if (m_is_dead) return;

    const SFlowerStats death_stats = m_final_stats;
    const float death_radius = m_radius;
    const float death_mass = m_mass;

    ConsumeCorruptionPetalsOnDeath();

    m_is_dead = true;
    CancelDestroy();
    m_health = 0.f;
    m_vel = { 0.f, 0.f };
    m_attacking = false;
    m_defending = false;
    BeginBloodSacrifice();
    if (GameWorld()) GameWorld()->DestroySummonedMobsOwnedBy(m_id, m_generation);
    DestroyPetalEntities();

    m_final_stats = death_stats;
    m_radius = death_radius;
    m_mass = death_mass;
    ClearFinalStatsDirty();
}

void CPlayerFlower::PrepareRespawnDestroy(EEntityRemovalReason reason)
{
    if (GameWorld()) GameWorld()->DestroySummonedMobsOwnedBy(m_id, m_generation);
    ClearPetals();
    m_is_dead = false;
    MarkForDestroy(reason);
}

bool CPlayerFlower::ReviveFromYggdrasil(float health_fraction)
{
    if (!m_is_dead || m_is_marked_for_des) return false;

    for (auto* undead : FindStates<CUndeadState>())
    {
        undead->CancelDeathOnDestroy();
        RemoveState(undead);
    }
    for (auto* corruption : FindStates<CCorruptionState>())
        RemoveState(corruption);
    m_is_dead = false;
    CancelDestroy();
    RebuildFinalStats();
    float max_health = GetFinalStats() ? GetFinalStats()->max_health : m_base_stats.max_health;
    m_health = std::max(game_config::flower_revive_min_health, max_health * std::clamp(health_fraction, 0.0f, 1.0f));
    m_vel = { 0.f, 0.f };
    m_attacking = false;
    m_defending = false;
    AddState(std::make_unique<CInvincibleState>(this, 1.f, GetRarity()));
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
            if (should_unequip) CAccountDataStore::AddItem(player->GetAccountName(), old_type, old_rarity, 1);
            CAccountDataStore::ClearSlot(player->GetAccountName(), static_cast<uint8_t>(i));
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
        if (!slot.m_available || slot.m_banned || !slot.m_p_proto) continue;
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

    m_health = 1.f;
    AddState(std::make_unique<CUndeadState>(this, duration, best_rarity, best_slot));
    AddState(std::make_unique<CNoReviveState>(this, game_config::default_bandage_no_revive_duration, best_rarity));
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
        if (!slot.m_available || slot.m_banned || !slot.m_p_proto) continue;
        if (slot.m_p_proto->m_type != EPetalType::BloodSacrifice) continue;

        for (const FlorrBtMap::Zone& zone : map->zones)
        {
            if (!IsPointInZone(zone, m_pos)) continue;

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

            auto ritual = std::make_unique<CBloodSacrificeRitual>(world, m_pos, mob_type, slot.m_stored_rarity,
                                                                  game_config::default_blood_sacrifice_delay);
            CBloodSacrificeRitual* raw_ritual =
                dynamic_cast<CBloodSacrificeRitual*>(world->InsertEntity(std::move(ritual)));
            if (!raw_ritual)
            {
                LOG_INFO("blood_sacrifice", "Skipped: failed to create ritual entity");
                return;
            }

            LOG_INFO("blood_sacrifice", "Created ritual id " + std::to_string(raw_ritual->m_id) + " for " +
                                            std::string(GetRarityName(slot.m_stored_rarity)) + " " +
                                            std::string(GetMobTypeName(mob_type)) + " from slot " + std::to_string(i) +
                                            " at " + std::to_string(m_pos.x) + "," + std::to_string(m_pos.y));
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
        CAccountDataStore::ClearSlot(player->GetAccountName(), static_cast<uint8_t>(slot_index));
}
