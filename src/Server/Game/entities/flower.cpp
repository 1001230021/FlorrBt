#include "flower.h"
#include "blood_sacrifice_ritual.h"
#include "petals/petal.h"
#include "../gamecontext.h"
#include "../gameworld.h"
#include "../player.h"
#include "../states/states.h"
#include "../talent.h"
#include "../zone_mob_tools.h"
#include "../../../Engine/account_data.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace
{
constexpr float screen_clockwise = 1.f;
constexpr float screen_counter_clockwise = -1.f;

struct yin_yang_layout
{
    int columns = 1;
    float direction = screen_clockwise;
};

yin_yang_layout GetYinYangLayout(int count)
{
    if (count <= 0) return {};

    static constexpr yin_yang_layout layouts[] = {
        {1, screen_counter_clockwise},
        {6, screen_clockwise},
        {6, screen_counter_clockwise},
        {4, screen_clockwise},
        {4, screen_counter_clockwise},
        {3, screen_clockwise},
        {2, screen_counter_clockwise},
        {2, screen_clockwise},
        {1, screen_counter_clockwise},
        {1, screen_clockwise},
    };
    return layouts[std::clamp(count, 1, 10) - 1];
}

int GetPlayerFlowerStatLevel(int level)
{
    return std::clamp(level, 1, std::max(1, game_config::mob_player_flower_max_stat_level));
}

float GetPlayerFlowerLevelMultiplier(int level, float growth)
{
    return std::pow(std::max(0.f, growth), static_cast<float>(GetPlayerFlowerStatLevel(level) - 1));
}

SFlowerStats BuildPlayerFlowerLevelStats(SFlowerStats stats, int level)
{
    stats.max_health *= GetPlayerFlowerLevelMultiplier(level, game_config::mob_player_flower_level_health_growth);
    stats.damage *= GetPlayerFlowerLevelMultiplier(level, game_config::mob_player_flower_level_damage_growth);
    return stats;
}

int PlayerFlowerExpRequired(int level)
{
    if (level <= 1) return 10;
    if (level >= 29) return std::numeric_limits<int>::max();
    return 10 * (1 << (level - 1));
}

int TalentPointGainForLevel(int level)
{
    if (level <= 1) return 0;

    int gain = 1;
    if (level % 5 == 0) gain += 5;
    if (level % 10 == 0) gain += 10;
    return gain;
}

void SyncFlowerRadiusWithStats(CFlower& flower, SFlowerStats& stats)
{
    flower.m_radius = std::max(0.f, stats.radius);
    stats.radius = flower.m_radius;
}

}

void CFlower::Tick(float dt)
{
    CAttackableMob<SFlowerStats>::Tick(dt);
    RebuildFinalStats();
    RefreshNullificationState();
    RefreshCorruptionState();

    for (auto& slot : m_slots)
    {
        if (slot.m_available && !slot.m_banned) slot.RefreshPetalState(this);
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
                if (petal) petal->m_is_marked_for_des = true;
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
        } else {
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
        if (slot.m_available && !slot.m_banned) slot.Tick(dt, this);
    }
}
void CFlower::TakeDamage(float dmg, CEntity* attacker, EDamageType damage_type)
{
    for (auto& slot : GetSlots())
    {
        if (!slot.m_p_proto || !slot.m_p_proto->m_p_behavior) continue;

        for (auto* petal : slot.m_p_petals)
        {
            if (petal && !petal->m_is_marked_for_des)
            {
                slot.m_p_proto->m_p_behavior->OnFlowerTakeDamage(petal, slot.m_stored_rarity, this, dmg, damage_type, attacker);
            }
        }
    }
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
            if (petal) petal->m_is_marked_for_des = true;
            petal = nullptr;
        }
        std::fill(slot.m_bonus_active.begin(), slot.m_bonus_active.end(), 0);
        slot.m_start_copy_index = -1;
    }
    m_total_copies = 0;
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
    float moving_fraction = std::clamp(1.f - game_config::default_cogwheel_stop_fraction, 0.001f, 1.f);
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
}

void CFlower::EquipPetal(int slot_index, const CPetalPrototype* proto, ERarity rarity)
{
    if (slot_index < 0 || slot_index >= static_cast<int>(m_slots.size())) return;
    if (!m_slots[slot_index].m_available) return;
    if (!proto || !proto->m_p_behavior) return;

    m_slots[slot_index].SetPetal(proto, slot_index, rarity);

    if (!proto->m_p_behavior->GetPetalStats(rarity).stack) ApplyExclusivity(proto->m_type);
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
}

void CFlower::UnequipPetal(int slot_index)
{
    if (slot_index < 0 || slot_index >= static_cast<int>(m_slots.size())) return;
    if (!CanUnequipPetal(slot_index)) return;

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

    if (!was_exclusive) return;

    for (auto& slot : m_slots)
    {
        if (slot.m_p_proto && slot.m_p_proto->m_type == old_type) slot.m_available = true;
    }
    ApplyExclusivity(old_type);
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
    CPetalSlot* p_best_slot = nullptr;
    int best_rarity = -1;

    for (auto& slot : m_slots)
    {
        if (!slot.m_p_proto || slot.m_p_proto->m_type != type) continue;
        if (!slot.m_p_proto->m_p_behavior) continue;
        if (slot.m_p_proto->m_p_behavior->GetPetalStats(slot.m_stored_rarity).stack) continue;

        int rarity_level = static_cast<int>(slot.m_stored_rarity);
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
        slot.m_available = (&slot == p_best_slot);
    }

    if (p_best_slot) return;

    for (auto& slot : m_slots)
    {
        if (slot.m_p_proto && slot.m_p_proto->m_type == type) slot.m_available = true;
    }
}

void CFlower::RefreshNullificationState()
{
    ERarity best_rarity = ERarity::Null;
    for (const auto& slot : m_slots)
    {
        if (!slot.m_available || slot.m_banned || !slot.m_p_proto) continue;
        if (slot.m_p_proto->m_type != EPetalType::Nullification) continue;
        if (GetLevel(slot.m_stored_rarity) > GetLevel(best_rarity)) best_rarity = slot.m_stored_rarity;
    }

    auto states = FindStates<CNullificationState>();
    if (best_rarity == ERarity::Null)
    {
        for (auto* state : states) RemoveState(state);
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
        if (GetLevel(slot.m_stored_rarity) > GetLevel(best_rarity)) best_rarity = slot.m_stored_rarity;
    }

    auto states = FindStates<CCorruptionState>();
    if (best_rarity == ERarity::Null)
    {
        for (auto* state : states) RemoveState(state);
        return;
    }

    if (states.empty())
    {
        AddState(std::make_unique<CCorruptionState>(this, endless, best_rarity));
    } else {
        states.front()->m_rarity = best_rarity;
        states.front()->m_timer = endless;
        for (size_t i = 1; i < states.size(); ++i)
        {
            RemoveState(states[i]);
        }
    }

    float radius = 0.f;
    if (best_rarity == ERarity::Primordial)
        radius = game_config::default_corruption_radius_primordial;
    else if (GetLevel(best_rarity) >= GetLevel(ERarity::Eternal))
        radius = game_config::default_corruption_radius_above_ultra;
    if (radius <= 0.f || !GameWorld()) return;

    auto candidates = GameWorld()->GetSpatialGrid().QueryRange(m_pos, radius, [this](const CEntity* entity)
    {
        if (!entity || entity == this || entity->m_is_marked_for_des || entity->IsDead()) return false;
        return dynamic_cast<const CFlower*>(entity) != nullptr;
    });

    for (CEntity* entity : candidates)
    {
        auto* flower = dynamic_cast<CFlower*>(entity);
        if (!flower) continue;
        if (flower->FindStates<CCorruptionState>().empty())
            flower->AddState(std::make_unique<CCorruptionState>(flower, endless, best_rarity));
    }
}

void CFlower::SetBanned(bool banned, int slot_index)
{
    if (slot_index < 0 || slot_index >= static_cast<int>(m_slots.size())) return;
    m_slots[slot_index].m_banned = banned;
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

void CFlower::SetPetalSlotCount(int count)
{
    int max_slots = std::max(0, static_cast<int>(game_config::default_flower_petal_num_max));
    m_petal_num_max = std::clamp(count, 0, max_slots);
    InitSlots();
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
    if (m_final_stats.petal_rotation_mode != EPetalRotationMode::YinYang || m_yinyang_layout_count <= 0) return std::max(1, m_total_copies);
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

CPlayerFlower::CPlayerFlower(CGameWorld* pworld, sf::Vector2f pos, float r, ERarity rarity, const SFlowerStats& base)
    : CFlower(pworld, pos, r, rarity, base)
{
    int slot_count = std::clamp(game_config::mob_player_flower_initial_petal_slots, 0,
                                std::max(0, game_config::mob_player_flower_max_petal_slots));
    SetPetalSlotCount(slot_count);
    RebuildFinalStats();
    m_health = m_final_stats.max_health;
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

            if (player && player->IsAuthenticated())
            {
                CAccountDataStore::AddItem(player->GetAccountName(), old_type, old_rarity, 1);
                CAccountDataStore::ClearSlot(player->GetAccountName(), static_cast<uint8_t>(i));
            }
        }
    }

    SetPetalSlotCount(slot_count);
}

void CPlayerFlower::TakeExp(int exp)
{
    if (exp <= 0 || m_is_dead) return;

    m_exp = std::max(0, m_exp + exp);
    bool leveled = false;
    int gained_talent_points = 0;
    for (;;)
    {
        int required = PlayerFlowerExpRequired(m_level);
        if (required <= 0 || m_exp < required) break;
        m_exp -= required;
        ++m_level;
        gained_talent_points += TalentPointGainForLevel(m_level);
        leveled = true;
    }

    if (leveled) RebuildFinalStats();

    CGameContext* context = GameContext();
    CPlayer* player = context ? context->FindPlayerFromEntity(this) : nullptr;
    if (player && gained_talent_points > 0) player->AddTalentPoints(gained_talent_points);
    if (player && player->IsAuthenticated())
        CAccountDataStore::SetProgress(player->GetAccountName(), m_level, m_exp);
}

int CPlayerFlower::ExpRequired() const
{
    return PlayerFlowerExpRequired(m_level);
}

void CPlayerFlower::Tick(float dt)
{
    if (m_is_dead)
    {
        m_vel = {0.f, 0.f};
        m_attacking = false;
        m_defending = false;
        TickStates(dt);
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
                m_is_marked_for_des = false;
                m_health = std::max(1.f, m_health);
                AddState(std::make_unique<CInvincibleState>(this, invincible_time, GetRarity()));
                return;
            }
        }
    }

    if (m_is_marked_for_des && TryEnterUndeadFromBandage())
    {
        m_is_marked_for_des = false;
        m_health = std::max(1.f, m_health);
        return;
    }
    if (m_is_marked_for_des) EnterDeathState();
}

void CPlayerFlower::EnterDeathState()
{
    if (m_is_dead) return;

    ClearCorruptionOnDeath();

    m_is_dead = true;
    m_is_marked_for_des = false;
    m_health = 0.f;
    m_vel = {0.f, 0.f};
    m_attacking = false;
    m_defending = false;
    BeginBloodSacrifice();
    DestroyPetalEntities();
}

void CPlayerFlower::PrepareRespawnDestroy()
{
    ClearPetals();
    m_is_dead = false;
    m_is_marked_for_des = true;
}

void CPlayerFlower::ReviveFromYggdrasil(float health_fraction)
{
    if (!m_is_dead) return;

    m_is_dead = false;
    m_is_marked_for_des = false;
    RebuildFinalStats();
    float max_health = GetFinalStats() ? GetFinalStats()->max_health : m_base_stats.max_health;
    m_health = std::max(1.f, max_health * std::clamp(health_fraction, 0.0f, 1.0f));
    m_vel = {0.f, 0.f};
    m_attacking = false;
    m_defending = false;
    AddState(std::make_unique<CInvincibleState>(this, 1.f, GetRarity()));
}

void CPlayerFlower::ClearCorruptionOnDeath()
{
    CGameContext* context = GameContext();
    CPlayer* player = context ? context->FindPlayerFromEntity(this) : nullptr;
    auto& slots = GetSlots();
    bool cleared = false;
    for (size_t i = 0; i < slots.size(); ++i)
    {
        CPetalSlot& slot = slots[i];
        if (!slot.m_p_proto || slot.m_p_proto->m_type != EPetalType::Corruption) continue;
        if (GetLevel(slot.m_stored_rarity) > GetLevel(ERarity::Ultra)) continue;
        slot.ClearPetal();
        slot.m_p_proto = nullptr;
        slot.m_stored_rarity = ERarity::Null;
        slot.m_available = true;
        cleared = true;
        if (player && player->IsAuthenticated())
            CAccountDataStore::ClearSlot(player->GetAccountName(), static_cast<uint8_t>(i));
    }

    if (!cleared) return;
    for (auto& slot : slots)
    {
        if (slot.m_p_proto && slot.m_p_proto->m_type == EPetalType::Corruption) slot.m_available = true;
    }
    ApplyExclusivity(EPetalType::Corruption);
    for (auto* state : FindStates<CCorruptionState>())
        RemoveState(state);
}

bool CPlayerFlower::TryEnterUndeadFromBandage()
{
    if (!FindStates<CUndeadState>().empty()) return false;
    if (!FindStates<CNoReviveState>().empty()) return false;

    auto& slots = GetSlots();
    int best_slot = -1;
    ERarity best_rarity = ERarity::Null;
    for (size_t i = 0; i < slots.size(); ++i)
    {
        const CPetalSlot& slot = slots[i];
        if (!slot.m_available || slot.m_banned || !slot.m_p_proto) continue;
        if (slot.m_p_proto->m_type != EPetalType::Bandage) continue;
        if (GetLevel(slot.m_stored_rarity) > GetLevel(best_rarity))
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

            auto ritual = std::make_unique<CBloodSacrificeRitual>(
                world, m_pos, mob_type, slot.m_stored_rarity, game_config::default_blood_sacrifice_delay);
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

    CGameContext* context = GameContext();
    CPlayer* player = context ? context->FindPlayerFromEntity(this) : nullptr;
    if (player && player->IsAuthenticated())
        CAccountDataStore::ClearSlot(player->GetAccountName(), static_cast<uint8_t>(slot_index));
}
