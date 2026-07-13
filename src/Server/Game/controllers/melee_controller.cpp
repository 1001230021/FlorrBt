#include "melee_controller.h"
#include "../entities/projectile.h"
#include "../gameworld.h"
#include "../states/states.h"
#include "../../../Shared/game_config.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>

namespace
{
bool IsInvalidMeleeTarget(const CMobBase* mob, const CEntity* target, int ignored_id = -1, int ignored_owner_id = -1)
{
    if (!mob || !target) return true;
    if (target->m_is_marked_for_des || target->IsDead() || !target->CanCollide()) return true;
    if (target == mob) return true;
    if (target->m_id == ignored_id || target->m_id == ignored_owner_id) return true;
    if (CheckTeam(target->m_team, mob->m_team)) return true;
    if (BlocksNullifiedInteraction(mob, target)) return true;
    if (dynamic_cast<const CProjectile*>(target)) return true;
    return false;
}

bool IsMeleeTargetBlockedByWall(CMobBase* mob, const CEntity* target)
{
    if (!mob || !target || !mob->GameWorld()) return false;
    return mob->GameWorld()->SegmentBlockedByWall(mob->m_pos, target->m_pos);
}

bool IsUnavailableMeleeTarget(CMobBase* mob, const CEntity* target, int ignored_id = -1, int ignored_owner_id = -1)
{
    return IsInvalidMeleeTarget(mob, target, ignored_id, ignored_owner_id) ||
           IsMeleeTargetBlockedByWall(mob, target);
}

CEntity* FindClosestMeleeTarget(CMobBase* mob, float search_range)
{
    if (!mob || !mob->GameWorld() || search_range <= 0.f) return nullptr;

    auto candidates = mob->GameWorld()->GetSpatialGrid().QueryRange(
        mob->m_pos, search_range,
        [mob, search_range](const CEntity* entity) -> bool
        {
            if (IsUnavailableMeleeTarget(mob, entity)) return false;

            float detection_multiplier = 1.f;
            if (auto* flower = dynamic_cast<const CFlower*>(entity)) detection_multiplier = flower->m_final_stats.detection_multiplier;

            float range = search_range * detection_multiplier;
            return DistanceSq(mob->m_pos, entity->m_pos) <= range * range;
        });

    float best_dist_sq = std::numeric_limits<float>::max();
    CEntity* p_best_target = nullptr;
    for (auto* candidate : candidates)
    {
        float dist_sq = DistanceSq(mob->m_pos, candidate->m_pos);
        if (dist_sq < best_dist_sq)
        {
            best_dist_sq = dist_sq;
            p_best_target = candidate;
        }
    }
    return p_best_target;
}

void FaceTarget(CMobBase* mob, const CEntity* target)
{
    if (!mob || !target) return;
    if (mob->IsFacingLocked()) return;
    sf::Vector2f delta = target->m_pos - mob->m_pos;
    if (LengthSq(delta) <= game_config::entity_collision_epsilon * game_config::entity_collision_epsilon) return;
    mob->m_facing_angle = std::atan2(delta.y, delta.x);
    mob->m_has_facing = true;
}
}

void CMeleeController::PickRandomTargetPos(CMobBase* mob, const SMobStats& stats)
{
    if (!mob) return;

    float half_range = stats.horizon / game_config::melee_random_wander_divisor;
    PickRandomTargetPosNear(mob, mob->m_pos, half_range);
}

void CMeleeController::PickRandomTargetPosNear(CMobBase* mob, const sf::Vector2f& center, float half_range)
{
    if (!mob) return;
    m_random_idle = false;
    m_random_idle_timer = 0.f;

    if (CheckChance(game_config::melee_random_idle_chance))
    {
        m_target_pos = mob->m_pos;
        m_has_random_target_pos = true;
        m_random_idle = true;
        return;
    }

    half_range = std::max(0.f, half_range);
    sf::Vector2f min_pos = center - sf::Vector2f(half_range, half_range);
    m_target_pos = min_pos + sf::Vector2f(GetLimitedRng(0.f, half_range * 2.f), GetLimitedRng(0.f, half_range * 2.f));
    m_has_random_target_pos = true;
}

bool CMeleeController::IsRandomIdleDone(float dt)
{
    if (!m_random_idle) return false;
    m_random_idle_timer += dt;
    return m_random_idle_timer >= game_config::melee_random_idle_time;
}

void CMeleeController::OnTick(CMobBase* mob, float dt)
{
    if (!mob || !mob->GameWorld()) return;

    const SMobStats* stats = mob->GetFinalStats();
    if (!stats) return;

    m_change_target_count += dt;

    bool target_invalid = m_p_target && IsUnavailableMeleeTarget(mob, m_p_target);
    bool reached_random_target = !m_p_target && m_has_random_target_pos &&
                                 (m_random_idle ? IsRandomIdleDone(dt) :
                                  DistanceSq(mob->m_pos, m_target_pos) <= mob->m_radius * mob->m_radius);
    bool can_random_retarget = !m_p_target && m_change_target_count >= game_config::melee_target_time;
    float retarget_chance = game_config::melee_retarget_chance_multiplier * m_change_target_count * dt /
                            (game_config::melee_target_time * game_config::melee_target_time);
    retarget_chance = std::clamp(retarget_chance, 0.f, 1.f);
    bool should_retarget =
        target_invalid || !m_has_random_target_pos || reached_random_target || (can_random_retarget && CheckChance(retarget_chance));
    if (should_retarget)
    {
        m_change_target_count = 0.f;
        m_has_random_target_pos = false;
        m_random_idle = false;
        m_random_idle_timer = 0.f;

        auto candidates = mob->GameWorld()->GetSpatialGrid().QueryRange(
            mob->m_pos, stats->horizon,
            [mob, stats](const CEntity* entity) -> bool
            {
                if (IsUnavailableMeleeTarget(mob, entity)) return false;

                float detection_multiplier = 1.f;
                if (auto* flower = dynamic_cast<const CFlower*>(entity)) detection_multiplier = flower->m_final_stats.detection_multiplier;

                float range = stats->horizon * detection_multiplier;
                return DistanceSq(mob->m_pos, entity->m_pos) <= range * range;
            });

        float best_dist_sq = std::numeric_limits<float>::max();
        CEntity* p_best_target = nullptr;
        for (auto* candidate : candidates)
        {
            float dist_sq = DistanceSq(mob->m_pos, candidate->m_pos);
            if (dist_sq < best_dist_sq)
            {
                best_dist_sq = dist_sq;
                p_best_target = candidate;
            }
        }

        m_p_target = p_best_target;
        if (m_p_target)
        {
            m_target_pos = m_p_target->m_pos;
            m_has_random_target_pos = true;
            m_random_idle = false;
            m_random_idle_timer = 0.f;
        } else {
            PickRandomTargetPos(mob, *stats);
        }
    }

    if (m_p_target) m_target_pos = m_p_target->m_pos;
    mob->MoveTowards(m_target_pos, dt);
}

// ============ Summoned ============

void CSummonedMeleeController::OnTick(CMobBase* mob, float dt)
{
    if (!mob || !mob->GameWorld())
    {
        return;
    }

    CEntity* owner = mob->GameWorld()->GetEntity(m_owner_id);
    if (!owner || owner->m_is_marked_for_des)
    {
        mob->m_is_marked_for_des = true;
        return;
    }

    float owner_dist_sq = DistanceSq(mob->m_pos, owner->m_pos);
    float range_scale = std::max(1.f, mob->m_radius / std::max(1.f, game_config::mob_summoned_beetle_radius));
    float hard_range = game_config::mob_summoned_beetle_hard_range * range_scale;
    if (owner_dist_sq > hard_range * hard_range)
    {
        mob->m_is_marked_for_des = true;
        return;
    }

    float follow_range = game_config::mob_summoned_beetle_follow_range * range_scale;
    if (owner_dist_sq > follow_range * follow_range)
    {
        m_p_target = owner;
        m_target_pos = owner->m_pos;
        m_has_random_target_pos = true;
        m_random_idle = false;
        m_random_idle_timer = 0.f;
        m_change_target_count = 0.f;
        mob->MoveTowards(owner->m_pos, dt);
        return;
    }

    const SMobStats* stats = mob->GetFinalStats();
    if (!stats) return;

    m_change_target_count += dt;

    int owner_owner_id = -1;
    if (auto* owner_projectile = dynamic_cast<CProjectile*>(owner)) owner_owner_id = owner_projectile->m_owner_id;

    bool target_invalid = m_p_target && IsUnavailableMeleeTarget(mob, m_p_target, m_owner_id, owner_owner_id);
    bool reached_random_target = !m_p_target && m_has_random_target_pos &&
                                 (m_random_idle ? IsRandomIdleDone(dt) :
                                  DistanceSq(mob->m_pos, m_target_pos) <= mob->m_radius * mob->m_radius);
    bool can_random_retarget = !m_p_target && m_change_target_count >= game_config::melee_target_time;
    float retarget_chance = game_config::melee_retarget_chance_multiplier * m_change_target_count * dt /
                            (game_config::melee_target_time * game_config::melee_target_time);
    retarget_chance = std::clamp(retarget_chance, 0.f, 1.f);

    if (target_invalid || !m_has_random_target_pos || reached_random_target || (can_random_retarget && CheckChance(retarget_chance)))
    {
        m_change_target_count = 0.f;
        m_has_random_target_pos = false;
        m_random_idle = false;
        m_random_idle_timer = 0.f;

        float search_range = stats->horizon * game_config::mob_summoned_search_range_multiplier;
        auto candidates = mob->GameWorld()->GetSpatialGrid().QueryRange(
            mob->m_pos, search_range,
            [mob, stats, owner, owner_owner_id](const CEntity* entity) -> bool
            {
                if (entity == owner) return false;
                if (IsUnavailableMeleeTarget(mob, entity, owner->m_id, owner_owner_id)) return false;

                float detection_multiplier = 1.f;
                if (auto* flower = dynamic_cast<const CFlower*>(entity)) detection_multiplier = flower->m_final_stats.detection_multiplier;

                float range = stats->horizon * game_config::mob_summoned_search_range_multiplier * detection_multiplier;
                return DistanceSq(mob->m_pos, entity->m_pos) <= range * range;
            });

        float best_dist_sq = std::numeric_limits<float>::max();
        CEntity* p_best_target = nullptr;
        for (auto* candidate : candidates)
        {
            float dist_sq = DistanceSq(mob->m_pos, candidate->m_pos);
            if (dist_sq < best_dist_sq)
            {
                best_dist_sq = dist_sq;
                p_best_target = candidate;
            }
        }

        m_p_target = p_best_target;
        if (m_p_target)
        {
            m_target_pos = m_p_target->m_pos;
            m_has_random_target_pos = true;
            m_random_idle = false;
            m_random_idle_timer = 0.f;
        } else {
            float wander_half_range = std::max(mob->m_radius * game_config::mob_summoned_wander_radius_multiplier,
                                               follow_range * game_config::mob_summoned_wander_follow_range_multiplier);
            PickRandomTargetPosNear(mob, owner->m_pos, wander_half_range);
        }
    }

    if (m_p_target) m_target_pos = m_p_target->m_pos;
    mob->MoveTowards(m_target_pos, dt);
}

// ============ Neutral ============

void CNeutralMeleeController::OnTick(CMobBase* mob, float dt)
{
    if (!mob || !mob->GameWorld()) return;

    const SMobStats* stats = mob->GetFinalStats();
    if (!stats) return;

    if (m_p_target && IsUnavailableMeleeTarget(mob, m_p_target))
    {
        m_p_target = nullptr;
        m_has_random_target_pos = false;
        m_random_idle = false;
        m_random_idle_timer = 0.f;
    }

    if (m_p_target)
    {
        m_target_pos = m_p_target->m_pos;
        mob->MoveTowards(m_target_pos, dt);
        return;
    }

    bool reached_random_target = m_has_random_target_pos &&
                                 (m_random_idle ? IsRandomIdleDone(dt) :
                                  DistanceSq(mob->m_pos, m_target_pos) <= mob->m_radius * mob->m_radius);
    if (!m_has_random_target_pos || reached_random_target)
    {
        PickRandomTargetPos(mob, *stats);
    }
    mob->MoveTowards(m_target_pos, dt);
}

void CNeutralMeleeController::OnDamaged(CMobBase* mob, CEntity* attacker)
{
    if (!mob || !attacker) return;
    if (IsUnavailableMeleeTarget(mob, attacker)) return;

    m_p_target = attacker;
    m_target_pos = attacker->m_pos;
    m_has_random_target_pos = true;
    m_random_idle = false;
    m_random_idle_timer = 0.f;
    m_change_target_count = 0.f;
}

// ============ Hornet Ranged ============

void CHornetRangedController::OnTick(CMobBase* mob, float dt)
{
    if (!mob || !mob->GameWorld()) return;

    const SMobStats* stats = mob->GetFinalStats();
    if (!stats) return;

    m_change_target_count += dt;

    bool target_invalid = m_p_target && IsUnavailableMeleeTarget(mob, m_p_target);
    bool reached_random_target = !m_p_target && m_has_random_target_pos &&
                                 (m_random_idle ? IsRandomIdleDone(dt) :
                                  DistanceSq(mob->m_pos, m_target_pos) <= mob->m_radius * mob->m_radius);
    bool can_random_retarget = !m_p_target && m_change_target_count >= game_config::melee_target_time;
    float retarget_chance = game_config::melee_retarget_chance_multiplier * m_change_target_count * dt /
                            (game_config::melee_target_time * game_config::melee_target_time);
    retarget_chance = std::clamp(retarget_chance, 0.f, 1.f);

    bool should_retarget =
        target_invalid || !m_has_random_target_pos || reached_random_target || (can_random_retarget && CheckChance(retarget_chance));
    if (should_retarget)
    {
        m_change_target_count = 0.f;
        m_has_random_target_pos = false;
        m_random_idle = false;
        m_random_idle_timer = 0.f;

        m_p_target = FindClosestMeleeTarget(mob, stats->horizon);
        if (m_p_target)
        {
            m_target_pos = m_p_target->m_pos;
            m_has_random_target_pos = true;
        } else {
            PickRandomTargetPos(mob, *stats);
        }
    }

    if (!m_p_target)
    {
        mob->MoveTowards(m_target_pos, dt);
        return;
    }

    m_target_pos = m_p_target->m_pos;
    FaceTarget(mob, m_p_target);

    sf::Vector2f to_target = m_p_target->m_pos - mob->m_pos;
    float target_dist = Length(to_target);
    float stop_distance = 256.f + mob->m_radius;
    if (target_dist <= stop_distance)
        mob->MoveTowards(mob->m_pos, dt);
    else
        mob->MoveTowards(m_target_pos, dt);

    float missile_range = game_config::mob_hornet_missile_speed * game_config::default_missile_lifetime;
    if (target_dist <= missile_range * 0.5f)
    {
        if (auto* attackable = dynamic_cast<IAttackableMob*>(mob))
            attackable->TryAttack(m_p_target);
    }
}

// ============ Bumble Bee ============

void CBumbleBeeController::PickTurnTimer()
{
    float min_time = std::max(0.f, game_config::mob_bumblebee_turn_interval_min);
    float max_time = std::max(min_time, game_config::mob_bumblebee_turn_interval_max);
    m_turn_timer = GetLimitedRng(min_time, max_time);
}

void CBumbleBeeController::SpawnPollen(CMobBase* mob) const
{
    if (!mob || !mob->GameWorld()) return;

    int level = std::max(1, GetLevel(mob->GetRarity()));
    float damage = game_config::mob_bumblebee_pollen_base_damage * std::pow(3.f, static_cast<float>(level - 1));
    float health = game_config::mob_bumblebee_pollen_base_health * std::pow(5.f, static_cast<float>(level - 1));
    float mass = std::pow(2.f, static_cast<float>(level - 1));
    float radius = std::max(1.f, mob->m_radius * game_config::mob_bumblebee_pollen_radius_multiplier);

    auto pollen = std::make_unique<CPollenProjectile>(mob->GameWorld(), mob->m_pos, radius, damage, health,
                                                      game_config::mob_bumblebee_pollen_lifetime, mass, mob);
    pollen->m_team = mob->m_team;
    mob->GameWorld()->InsertEntity(std::move(pollen));
}

void CBumbleBeeController::OnTick(CMobBase* mob, float dt)
{
    if (!mob || !mob->GameWorld()) return;

    const SMobStats* stats = mob->GetFinalStats();
    if (!stats) return;

    if (!m_initialized)
    {
        m_heading = mob->m_has_facing ? mob->m_facing_angle : GetLimitedRng(-game_config::pi, game_config::pi);
        PickTurnTimer();
        m_initialized = true;
    }

    if (LengthSq(mob->m_vel) > game_config::entity_collision_epsilon * game_config::entity_collision_epsilon)
        m_heading = std::atan2(mob->m_vel.y, mob->m_vel.x);

    m_turn_timer -= dt;
    if (m_turn_timer <= 0.f)
    {
        float max_angle = std::max(0.f, game_config::mob_bumblebee_turn_max_angle);
        m_heading += GetLimitedRng(-max_angle, max_angle);
        PickTurnTimer();
    }

    m_wave_timer += dt;
    float wave = std::sin(m_wave_timer * game_config::mob_bee_wave_frequency) *
                 game_config::mob_bee_wave_strength * 0.55f;
    sf::Vector2f target = mob->m_pos + sf::Vector2f(std::cos(m_heading + wave), std::sin(m_heading + wave)) *
                          std::max(512.f, stats->max_velocity * 4.f);
    mob->MoveTowards(target, dt);

    m_pollen_timer += dt;
    float interval = std::max(game_config::server_fixed_dt, game_config::mob_bumblebee_pollen_interval);
    while (m_pollen_timer >= interval)
    {
        m_pollen_timer -= interval;
        SpawnPollen(mob);
    }
}
