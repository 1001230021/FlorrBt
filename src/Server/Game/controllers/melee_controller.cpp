#include "melee_controller.h"
#include "../entities/projectile.h"
#include "../gameworld.h"
#include "../states/states.h"
#include "../../../Shared/game_config.h"
#include <algorithm>
#include <limits>

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

    bool target_invalid = m_p_target && IsInvalidMeleeTarget(mob, m_p_target);
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
                if (IsInvalidMeleeTarget(mob, entity)) return false;

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

    bool target_invalid = m_p_target && IsInvalidMeleeTarget(mob, m_p_target, m_owner_id, owner_owner_id);
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
                if (IsInvalidMeleeTarget(mob, entity, owner->m_id, owner_owner_id)) return false;

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

    if (m_p_target && IsInvalidMeleeTarget(mob, m_p_target))
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
    if (IsInvalidMeleeTarget(mob, attacker)) return;

    m_p_target = attacker;
    m_target_pos = attacker->m_pos;
    m_has_random_target_pos = true;
    m_random_idle = false;
    m_random_idle_timer = 0.f;
    m_change_target_count = 0.f;
}
