#include "petal.h"
#include "petal_slot.h"
#include "../../gamecontext.h"
#include "../../gameworld.h"
#include "../../player.h"
#include "../../talent.h"
#include <algorithm>
#include <cmath>
#include <optional>

namespace
{
std::optional<sf::Vector2f> CalculateSpawnGlobal(CPetal* petal, CFlower* flower)
{
    if (!petal || !flower || !flower->GetFinalStats()) return std::nullopt;

    int total_copies = flower->m_total_copies;
    int start_index = flower->GetStartCopyIndex(petal->m_slot_index);
    if (start_index < 0 || total_copies <= 0) return std::nullopt;

    sf::Vector2f orbit_center = flower->m_pos;
    if (petal->m_type != EPetalType::Moon)
    {
        if (CPetal* moon = flower->GetMoonPetal())
            orbit_center = moon->m_pos;
    }

    float base_radius = petal->m_type == EPetalType::Moon ? flower->GetFinalStats()->radius :
                        (flower->GetMoonPetal() && flower->GetMoonPetal() != petal ? flower->GetMoonPetal()->m_radius :
                         flower->GetFinalStats()->radius);
    float reach = game_config::default_petal_neutral_reach;
    if (!PetalIgnoresReachBonus(petal->m_type))
    {
        if (flower->m_attacking)
        {
            reach += flower->GetFinalStats()->reach + game_config::default_petal_attack_offset;
        } else if (flower->m_defending) {
            reach += game_config::default_petal_defend_offset;
        }
    }

    float orbit_distance = base_radius + game_config::default_petal_orbit_radius + reach;
    float angle = flower->GetPetalRotationAngle() +
                  (2.0f * game_config::pi * (start_index + petal->m_copy_index)) /
                      static_cast<float>(total_copies);
    return orbit_center + sf::Vector2f(std::cos(angle), std::sin(angle)) * orbit_distance;
}

bool IsLiveSlotPetal(const CPetal* petal, const CFlower* flower)
{
    if (!petal || !flower) return false;

    CGameWorld* world = const_cast<CFlower*>(flower)->GameWorld();
    if (!world || petal->m_id < 0) return false;
    return world->GetEntity(petal->m_id) == petal && !petal->m_is_marked_for_des;
}

const CPetalPrototype* RuntimePrototypeForSlot(const CPetalSlot* slot, const CFlower* flower)
{
    if (!slot || !slot->m_p_proto || !slot->m_p_proto->m_p_behavior) return nullptr;

    const CPetalPrototype* runtime =
        slot->m_p_proto->m_p_behavior->GetRuntimePrototypeForSlot(slot->m_stored_rarity, slot, flower);
    if (runtime && runtime->m_p_behavior && runtime->m_factory) return runtime;
    return slot->m_p_proto;
}

const CPetalPrototype* ActivePrototypeForPetal(const CPetalSlot* slot, const CPetal* petal, const CFlower* flower)
{
    if (petal)
    {
        const CPetalPrototype* active = FindPetalPrototype(petal->m_type);
        if (active && active->m_p_behavior) return active;
    }
    return RuntimePrototypeForSlot(slot, flower);
}

SPetalStats PetalStatsForSlot(const CPetalSlot* slot, const CFlower* flower)
{
    if (!slot || !slot->m_p_proto || !slot->m_p_proto->m_p_behavior) return {};

    SPetalStats stats = slot->m_p_proto->m_p_behavior->GetPetalStatsForSlot(slot->m_stored_rarity, slot, flower);
    CFlower* mutable_flower = const_cast<CFlower*>(flower);
    CGameContext* context = mutable_flower ? mutable_flower->GameContext() : nullptr;
    CPlayer* player = context && mutable_flower ? context->FindPlayerFromEntity(mutable_flower) : nullptr;
    if (!player) return stats;

    const CPetalPrototype* runtime_proto = RuntimePrototypeForSlot(slot, flower);
    STalentContext talent_ctx;
    talent_ctx.world = mutable_flower ? mutable_flower->GameWorld() : nullptr;
    talent_ctx.player = player;
    talent_ctx.entity = mutable_flower;
    talent_ctx.flower = mutable_flower;
    talent_ctx.slot = const_cast<CPetalSlot*>(slot);
    talent_ctx.petal_stats = &stats;
    talent_ctx.is_leftmost_slot = slot->m_slot_index == 0;
    talent_ctx.petal_type = runtime_proto ? runtime_proto->m_type : slot->m_p_proto->m_type;
    talent_ctx.rarity = slot->m_stored_rarity;
    player->ApplyTalents(ETalentEvent::RebuildPetalStats, talent_ctx);
    return stats;
}

EPetalBonusMode BonusModeForSlot(const CPetalSlot* slot, const CFlower* flower)
{
    if (!slot || !slot->m_p_proto || !slot->m_p_proto->m_p_behavior) return EPetalBonusMode::AliveOnly;
    return slot->m_p_proto->m_p_behavior->GetBonusModeForSlot(slot->m_stored_rarity, slot, flower);
}

void ClearLivePetalFromSlot(const CPetalSlot* slot, CPetal* petal, CFlower* flower)
{
    if (!petal) return;

    const CPetalPrototype* active_proto = ActivePrototypeForPetal(slot, petal, flower);
    if (active_proto && active_proto->m_p_behavior)
        active_proto->m_p_behavior->OnPetalCleared(petal, slot ? slot->m_stored_rarity : petal->m_rarity, flower);
    else if (slot && slot->m_p_proto && slot->m_p_proto->m_p_behavior)
        slot->m_p_proto->m_p_behavior->OnPetalCleared(petal, slot->m_stored_rarity, flower);

    petal->m_is_marked_for_des = true;
}
}

CPetal::CPetal(float r, CFlower* owner, int slot, SPetalStats petal_stats)
    : CProjectile(owner->m_pos.x, owner->m_pos.y, r, owner), m_base_petal_stats(petal_stats),
      m_final_petal_stats(petal_stats), m_max_slot_num(petal_stats.copy), m_slot_index(slot)
{
    m_skip_world_tick = true;
    m_mass = petal_stats.mass;
    m_health = petal_stats.health;
}

void CPetal::Tick(float dt)
{
    auto* flower = dynamic_cast<CFlower*>(GetOwner());
    if (flower)
    {
        m_team = flower->m_team;
        float old_max_health = m_final_petal_stats.health;
        m_final_petal_stats = m_base_petal_stats;
        m_final_petal_stats.ActedOn(*flower->GetFinalStats());
        float new_max_health = m_final_petal_stats.health;
        if (old_max_health > 0.f && new_max_health > 0.f && new_max_health != old_max_health)
            m_health = m_health * new_max_health / old_max_health;
        if (new_max_health > 0.f) m_health = std::clamp(m_health, 0.f, new_max_health);
    } else {
        m_is_marked_for_des = true;
        return;
    }
    CProjectile::Tick(dt);
    m_lifetime += dt;
    if (m_timer > 0.f)
    {
        m_timer -= dt;
        if (m_timer <= 0.f)
        {
            m_timer = 0.f;
            m_health = 0.f;
            m_is_marked_for_des = true;
        }
    }

    for (auto it = m_hit_credits.begin(); it != m_hit_credits.end();)
    {
        CEntity* target = GameWorld() ? GameWorld()->GetEntity(it->first) : nullptr;
        if (!target || target->m_is_marked_for_des)
            it = m_hit_credits.erase(it);
        else
            ++it;
    }
}

void CPetal::TakeDamage(float dmg, CEntity*, EDamageType damage_type)
{
    if (damage_type == EDamageType::Normal) dmg = std::max(0.f, dmg - m_final_petal_stats.armor);
    if (dmg <= 0.f) return;

    m_health -= dmg;
    if (m_health <= 0.f)
    {
        m_health = 0.f;
    }
}

bool CPetal::CollidesWithWalls() const
{
    switch (m_type)
    {
    case EPetalType::AntEgg:
    case EPetalType::BeetleEgg:
    case EPetalType::BrokenEgg:
    case EPetalType::Carrot:
    case EPetalType::Web:
    case EPetalType::Pollen:
    case EPetalType::Honey:
    case EPetalType::Wax:
        return true;
    default:
        return false;
    }
}

void CBeetleEggPetal::TakeDamage(float dmg, CEntity* attacker, EDamageType damage_type)
{
    CEntity* summon = GameWorld() && m_summon_id >= 0 ?
        GameWorld()->GetEntity(m_summon_id, m_summon_generation) : nullptr;
    if (m_has_spawned_summon && summon && !summon->m_is_marked_for_des)
    {
        summon->TakeDamage(dmg, attacker, damage_type);
        m_health = std::max(1.f, summon->m_health);
        return;
    }

    CPetal::TakeDamage(dmg, attacker, damage_type);
}

void CGlassPetal::TakeDamage(float, CEntity*, EDamageType)
{
    m_health = std::max(m_health, std::max(1.f, m_final_petal_stats.health));
}

void CMissilePetal::Tick(float dt)
{
    bool restore_fired_angle = m_fired && m_type == EPetalType::Missile && m_has_fired_angle;
    float fired_angle = m_fired_angle;

    CThrownPetal::Tick(dt);
    if (restore_fired_angle)
    {
        m_facing_angle = fired_angle;
        m_has_facing = true;
    }
    if (!m_fired) return;

    m_fired_lifetime += dt;
    float lifetime = game_config::default_missile_lifetime;
    if (m_type == EPetalType::Carrot)
        lifetime *= game_config::default_carrot_lifetime_multiplier;
    if (m_fired_lifetime >= lifetime || m_health <= 0.f)
        m_is_marked_for_des = true;
}

void CThrownPetal::BeginThrow(sf::Vector2f direction, float speed, float deceleration_time,
                              bool destroy_when_stopped, bool tick_from_world, bool decelerates)
{
    float len = Length(direction);
    if (len <= game_config::entity_collision_epsilon)
        direction = {1.f, 0.f};
    else
        direction /= len;

    m_thrown = true;
    m_throw_decelerates = decelerates;
    m_destroy_when_stopped = destroy_when_stopped;
    m_throw_age = 0.f;
    m_throw_deceleration_time = std::max(0.001f, deceleration_time);
    m_throw_initial_speed = std::max(0.f, speed);
    m_throw_direction = direction;
    m_vel = m_throw_direction * m_throw_initial_speed;
    m_facing_angle = std::atan2(m_throw_direction.y, m_throw_direction.x);
    m_has_facing = true;
    m_skip_world_tick = !tick_from_world;
}

void CThrownPetal::StopThrow(bool destroy)
{
    if (!m_thrown) return;
    m_throw_age = m_throw_deceleration_time;
    m_vel = {0.f, 0.f};
    if (destroy) m_health = 0.f;
}

void CThrownPetal::Tick(float dt)
{
    if (m_thrown)
    {
        if (m_throw_decelerates)
        {
            float t = std::clamp(m_throw_age / std::max(0.001f, m_throw_deceleration_time), 0.f, 1.f);
            m_vel = m_throw_direction * (m_throw_initial_speed * (1.f - t));
        }
    }

    CPetal::Tick(dt);

    if (!m_thrown) return;
    m_throw_age += dt;
    if (m_throw_decelerates && m_throw_age >= m_throw_deceleration_time)
    {
        m_vel = {0.f, 0.f};
        if (m_destroy_when_stopped) m_health = 0.f;
    }
}

void CPetalSlot::SpawnCopy(int copy_index, CFlower* flower)
{
    const CPetalPrototype* runtime_proto = RuntimePrototypeForSlot(this, flower);
    if (!flower || !m_p_proto || !m_p_proto->m_p_behavior || !runtime_proto || !runtime_proto->m_factory) return;
    if (copy_index < 0 || copy_index >= static_cast<int>(m_p_petals.size())) return;
    if (m_p_petals[copy_index] != nullptr || m_banned) return;

    std::unique_ptr<CPetal> petal = runtime_proto->m_factory(flower, m_slot_index, m_stored_rarity);
    if (!petal) return;

    if (runtime_proto->m_type != EPetalType::Moon)
    {
        if (CPetal* moon = flower->GetMoonPetal()) petal->m_pos = moon->m_pos;
    }

    petal->m_copy_index = copy_index;
    petal->m_max_slot_num = static_cast<int>(m_p_petals.size());
    if (std::optional<sf::Vector2f> global = CalculateSpawnGlobal(petal.get(), flower))
    {
        sf::Vector2f center = (runtime_proto->m_type == EPetalType::Moon) ? flower->m_pos :
                              (flower->GetMoonPetal() ? flower->GetMoonPetal()->m_pos : flower->m_pos);
        petal->m_pos = (center + *global) * 0.5f;
        petal->m_spawn_flight_boost = true;
    }

    CPetal* p_petal = static_cast<CPetal*>(flower->GameWorld()->InsertEntity(std::move(petal)));
    if (!p_petal) return;

    m_p_petals[copy_index] = p_petal;
    m_bonus_active[copy_index] = true;
    flower->MarkFinalStatsDirty();
    runtime_proto->m_p_behavior->OnPetalSpawned(p_petal, m_stored_rarity, flower);
}

void CPetalSlot::SetPetal(const CPetalPrototype* proto, int slot_index, ERarity rarity)
{
    ClearPetal();

    m_p_proto = proto;
    m_stored_rarity = rarity;
    m_slot_index = slot_index;
    m_runtime_type = m_p_proto ? m_p_proto->m_type : EPetalType::None;

    if (!m_p_proto || !m_p_proto->m_p_behavior) return;

    SPetalStats petal_stats = m_p_proto->m_p_behavior->GetPetalStats(rarity);
    int copies = std::max(0, petal_stats.copy);

    m_p_petals.assign(copies, nullptr);
    m_bonus_active.assign(copies, false);
    m_reload_timers.assign(copies, std::max(0.f, petal_stats.preload));
    m_reload_ignore_multiplier.assign(copies, false);
}

void CPetalSlot::ClearPetal()
{
    for (CPetal* petal : m_p_petals)
    {
        if (!petal) continue;
        auto* flower = dynamic_cast<CFlower*>(petal->GetOwner());
        ClearLivePetalFromSlot(this, petal, flower);
    }
    m_p_petals.clear();
    m_bonus_active.clear();
    m_reload_timers.clear();
    m_reload_ignore_multiplier.clear();
    m_runtime_type = EPetalType::None;
}

void CPetalSlot::KillCopy(int copy_index)
{
    if (copy_index < 0 || copy_index >= static_cast<int>(m_reload_timers.size())) return;

    CPetal* petal = m_p_petals[copy_index];
    if (petal) petal->m_health = 0.f;
}

void CPetalSlot::RefreshPetalState(CFlower* flower)
{
    if (m_banned || !m_p_proto || !m_p_proto->m_p_behavior) return;

    const CPetalPrototype* runtime_proto = RuntimePrototypeForSlot(this, flower);
    EPetalType runtime_type = runtime_proto ? runtime_proto->m_type : EPetalType::None;
    SPetalStats petal_stats = PetalStatsForSlot(this, flower);
    int copies = std::max(0, petal_stats.copy);

    if (runtime_type != m_runtime_type)
    {
        for (CPetal* petal : m_p_petals)
            ClearLivePetalFromSlot(this, petal, flower);

        m_p_petals.assign(copies, nullptr);
        m_bonus_active.assign(copies, false);
        m_reload_timers.assign(copies, std::max(0.f, petal_stats.preload));
        m_reload_ignore_multiplier.assign(copies, false);
        m_runtime_type = runtime_type;
        if (flower) flower->MarkFinalStatsDirty();
        return;
    }

    int old_copies = static_cast<int>(m_p_petals.size());
    if (copies == old_copies) return;

    if (copies < old_copies)
    {
        for (int i = copies; i < old_copies; ++i)
            ClearLivePetalFromSlot(this, m_p_petals[i], flower);
    }

    m_p_petals.resize(copies, nullptr);
    m_bonus_active.resize(copies, false);
    m_reload_timers.resize(copies, std::max(0.f, petal_stats.preload));
    m_reload_ignore_multiplier.resize(copies, false);

    for (CPetal* petal : m_p_petals)
    {
        if (petal) petal->m_max_slot_num = copies;
    }
    if (flower) flower->MarkFinalStatsDirty();
}

void CPetalSlot::Tick(float dt, CFlower* flower, bool state_refreshed)
{
    if (m_banned || !m_p_proto || !m_p_proto->m_p_behavior || !flower) return;
    if (!state_refreshed) RefreshPetalState(flower);

    for (size_t i = 0; i < m_reload_timers.size(); ++i)
    {
        if (m_reload_timers[i] > 0.0f)
        {
            float multiplier = std::max(game_config::default_petal_reload_multiplier_min,
                                        flower->GetFinalStats()->petal_reload_multiplier);
            m_reload_timers[i] -= (i < m_reload_ignore_multiplier.size() && m_reload_ignore_multiplier[i]) ? dt : dt / multiplier;
            if (m_reload_timers[i] <= 0.0f)
            {
                m_reload_timers[i] = 0.0f;
                if (i < m_reload_ignore_multiplier.size()) m_reload_ignore_multiplier[i] = false;
                SpawnCopy(static_cast<int>(i), flower);
            }
        } else if (m_p_petals[i] == nullptr) {
            SpawnCopy(static_cast<int>(i), flower);
        }
    }

    for (size_t i = 0; i < m_p_petals.size(); ++i)
    {
        CPetal* petal = m_p_petals[i];
        if (!petal) continue;
        petal->m_max_slot_num = static_cast<int>(m_p_petals.size());
        const CPetalPrototype* active_proto = ActivePrototypeForPetal(this, petal, flower);
        CPetalBehavior* active_behavior =
            active_proto && active_proto->m_p_behavior ? active_proto->m_p_behavior.get() : m_p_proto->m_p_behavior.get();
        if (petal->m_hidden)
        {
            active_behavior->OnTick(petal, m_stored_rarity, flower, dt);
            if (petal->m_health <= 0.f || petal->m_is_marked_for_des)
            {
                float reload = petal->m_reload_override >= 0.f ? petal->m_reload_override : petal->m_final_petal_stats.reload;
                petal->m_is_marked_for_des = true;
                m_p_petals[i] = nullptr;
                m_reload_timers[i] = reload;
                if (i < m_reload_ignore_multiplier.size()) m_reload_ignore_multiplier[i] = petal->m_reload_ignore_multiplier;
                flower->MarkFinalStatsDirty();
            }
            continue;
        }

        if (!IsLiveSlotPetal(petal, flower))
        {
            auto* egg = dynamic_cast<CBeetleEggPetal*>(petal);
            if (egg && egg->m_has_spawned_summon)
            {
                active_behavior->OnTick(petal, m_stored_rarity, flower, dt);
                if (petal->m_health <= 0.f)
                {
                    m_p_petals[i] = nullptr;
                    m_reload_timers[i] = petal->m_final_petal_stats.reload;
                    if (i < m_reload_ignore_multiplier.size()) m_reload_ignore_multiplier[i] = false;
                    flower->MarkFinalStatsDirty();
                }
            } else {
                m_p_petals[i] = nullptr;
                flower->MarkFinalStatsDirty();
            }
            continue;
        }

        petal->Tick(dt);
        if (IsLiveSlotPetal(petal, flower)) active_behavior->OnTick(petal, m_stored_rarity, flower, dt);

        if (petal->m_detach_from_slot)
        {
            float reload = petal->m_reload_override >= 0.f ? petal->m_reload_override : petal->m_final_petal_stats.reload;
            petal->m_detach_from_slot = false;
            m_p_petals[i] = nullptr;
            m_reload_timers[i] = reload;
            if (i < m_reload_ignore_multiplier.size()) m_reload_ignore_multiplier[i] = petal->m_reload_ignore_multiplier;
            flower->MarkFinalStatsDirty();
            continue;
        }

        if (petal->m_health <= 0.f || petal->m_is_marked_for_des)
        {
            float reload = petal->m_reload_override >= 0.f ? petal->m_reload_override : petal->m_final_petal_stats.reload;
            active_behavior->OnPetalDestroyed(petal, m_stored_rarity, flower);
            if (petal->m_reload_override >= 0.f) reload = petal->m_reload_override;
            bool should_reload = active_behavior->ShouldReloadAfterPetalDestroyed(petal);
            if (should_reload)
            {
                petal->m_is_marked_for_des = true;
                m_p_petals[i] = nullptr;
                m_reload_timers[i] = reload;
                if (i < m_reload_ignore_multiplier.size()) m_reload_ignore_multiplier[i] = petal->m_reload_ignore_multiplier;
                flower->MarkFinalStatsDirty();
            } else {
                petal->m_hidden = true;
                petal->m_vel = {0.f, 0.f};
                petal->m_is_marked_for_des = false;
                petal->m_health = std::max(1.f, petal->m_health);
                flower->MarkFinalStatsDirty();
            }
        }
    }
}

int CPetalSlot::GetBonusCopyCount(const CFlower* flower) const
{
    if (!m_p_proto || !m_p_proto->m_p_behavior) return 0;

    SPetalStats petal_stats = PetalStatsForSlot(this, flower);
    if (petal_stats.copy <= 0) return 1;

    EPetalBonusMode bonus_mode = BonusModeForSlot(this, flower);
    if (KeepsBonusDuringPreload(bonus_mode))
        return static_cast<int>(m_bonus_active.size());

    if (LosesBonusDuringReload(bonus_mode))
    {
        int count = 0;
        for (const CPetal* petal : m_p_petals)
        {
            if (petal && !petal->m_is_marked_for_des && petal->m_health > 0.f) ++count;
        }
        return count;
    }

    int count = 0;
    for (uint8_t active : m_bonus_active)
    {
        if (active != 0) ++count;
    }
    return count;
}

int CPetalSlot::GetCurrentCopyCount(const CFlower* flower) const
{
    if (!m_p_proto || !m_p_proto->m_p_behavior) return 0;
    const CPetalPrototype* runtime_proto = RuntimePrototypeForSlot(this, flower);
    const CPetalBehavior* behavior = runtime_proto && runtime_proto->m_p_behavior ? runtime_proto->m_p_behavior.get() :
                                     m_p_proto->m_p_behavior.get();
    if (behavior->IsOpen()) return static_cast<int>(m_p_petals.size());

    int base_copy = static_cast<int>(m_p_petals.size());
    return (base_copy > 0) ? 1 : 0;
}

void CPetalSlot::ApplyStatsTo(SFlowerStats& target, const CFlower* flower) const
{
    if (!m_p_proto || !m_p_proto->m_p_behavior) return;
    if (!m_available || m_banned) return;

    SFlowerStats stats = m_p_proto->m_p_behavior->GetStatsForSlot(m_stored_rarity, this, flower);
    int count = GetBonusCopyCount(flower);
    for (int i = 0; i < count; ++i)
    {
        target.ActedOn(stats);
    }
}

const CPetalPrototype* FindPetalPrototype(EPetalType type)
{
    auto it = g_petal_registry.find(type);
    if (it == g_petal_registry.end()) return nullptr;
    return it->second.get();
}
