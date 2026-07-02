#include "melee_controller.h"
#include "../entities/projectile.h"
#include "../gameworld.h"
#include <limits>

void CMeleeController::OnTick(CMobBase* mob, float dt)
{
    if (!mob || !mob->GameWorld()) return;

    const SMobStats* stats = mob->GetFinalStats();
    if (!stats) return;

    bool target_invalid = !m_p_target || m_p_target->m_is_marked_for_des || CheckTeam(m_p_target->m_team, mob->m_team);
    float retarget_chance = m_change_target_count / target_time;
    if (target_invalid || CheckChance(retarget_chance))
    {
        m_change_target_count = 0.f;

        auto candidates = mob->GameWorld()->GetSpatialGrid().QueryRange(
            mob->m_pos, stats->search_range,
            [mob, stats](const CEntity* entity) -> bool
            {
                if (!entity || entity->m_is_marked_for_des) return false;
                if (entity == mob) return false;
                if (CheckTeam(entity->m_team, mob->m_team)) return false;
                if (dynamic_cast<const CProjectile*>(entity)) return false;

                float detection_multiplier = 1.f;
                if (auto* flower = dynamic_cast<const CFlower*>(entity)) detection_multiplier = flower->m_final_stats.detection_multiplier;

                float range = stats->search_range * detection_multiplier;
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
        } else {
            float half_range = stats->search_range / 8.f;
            sf::Vector2f min_pos = mob->m_pos - sf::Vector2f(half_range, half_range);
            m_target_pos = min_pos + sf::Vector2f(GetLimitedRng(0.f, half_range * 2.f), GetLimitedRng(0.f, half_range * 2.f));
        }
    } else {
        m_change_target_count += dt;
        if (m_p_target) m_target_pos = m_p_target->m_pos;
    }

    mob->MoveTowards(m_target_pos, dt);
}