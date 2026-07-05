#include "melee_controller.h"
#include "../entities/projectile.h"
#include "../gameworld.h"
#include "../../../Shared/game_config.h"
#include <limits>

void CMeleeController::PickRandomTargetPos(CMobBase* mob, const SMobStats& stats)
{
    if (!mob) return;

    float half_range = stats.horizon / game_config::melee_random_wander_divisor;
    sf::Vector2f min_pos = mob->m_pos - sf::Vector2f(half_range, half_range);
    m_target_pos = min_pos + sf::Vector2f(GetLimitedRng(0.f, half_range * 2.f), GetLimitedRng(0.f, half_range * 2.f));
    m_has_random_target_pos = true;
}
void CMeleeController::OnTick(CMobBase* mob, float dt)
{
    if (!mob || !mob->GameWorld()) return;

    const SMobStats* stats = mob->GetFinalStats();
    if (!stats) return;

    m_change_target_count += dt;

    bool target_invalid = m_p_target && (m_p_target->m_is_marked_for_des || CheckTeam(m_p_target->m_team, mob->m_team));
    bool reached_random_target =
        !m_p_target && m_has_random_target_pos && DistanceSq(mob->m_pos, m_target_pos) <= mob->m_radius * mob->m_radius;
    float retarget_chance = game_config::melee_retarget_chance_multiplier * m_change_target_count * dt /
                            (game_config::melee_target_time * game_config::melee_target_time);
    bool should_retarget =
        target_invalid || !m_has_random_target_pos || reached_random_target || CheckChance(retarget_chance);
    if (should_retarget)
    {
        m_change_target_count = 0.f;
        m_has_random_target_pos = false;

        auto candidates = mob->GameWorld()->GetSpatialGrid().QueryRange(
            mob->m_pos, stats->horizon,
            [mob, stats](const CEntity* entity) -> bool
            {
                if (!entity || entity->m_is_marked_for_des) return false;
                if (entity == mob) return false;
                if (CheckTeam(entity->m_team, mob->m_team)) return false;
                if (dynamic_cast<const CProjectile*>(entity)) return false;

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
    float hard_range = game_config::mob_summoned_beetle_hard_range;
    if (owner_dist_sq > hard_range * hard_range)
    {
        mob->m_is_marked_for_des = true;
        return;
    }

    float follow_range = game_config::mob_summoned_beetle_follow_range;
    if (owner_dist_sq > follow_range * follow_range)
    {
        m_p_target = owner;
        m_target_pos = owner->m_pos;
        m_has_random_target_pos = true;
        m_change_target_count = 0.f;
        mob->MoveTowards(owner->m_pos, dt);
        return;
    }

    CMeleeController::OnTick(mob, dt);
}
