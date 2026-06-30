#include "melee_controller.h"
#include "../entities/projectile.h"
#include "../gameworld.h"

void CMeleeController::OnTick(CMobBase* mob, float dt)
{
    sf::Vector2f delta_pos = m_TargetPos;
    float angle = atan2f(delta_pos.x, delta_pos.y);
    sf::Vector2f target_vel = { mob->GetFinalStats()->max_velocity * cosf(angle),
                                mob->GetFinalStats()->max_velocity * sinf(angle) };
    sf::Vector2f delta_vel = target_vel - mob->m_Vel;
    float spu_angle = atan2f(delta_vel.x, delta_vel.y);
    mob->m_Vel += { mob->GetFinalStats()->acceleration * cosf(spu_angle) * dt,
                    mob->GetFinalStats()->acceleration * sinf(spu_angle) * dt };

    float p = 2.f * m_ChangeTargetCount / target_time;
    if (CheckChance(p))
    {
        m_ChangeTargetCount = 0.f;

        auto candidates = mob->GameWorld()->GetSpatialGrid().QueryRange(
            mob->m_Pos, mob->GetFinalStats()->search_range * 2.f,
            [mob](const CEntity* e) -> bool
            {
                if (e)
                    if (!CheckTeam(e->m_Team, mob->m_Team))
                    {
                        if (dynamic_cast<const CProjectile*>(e))
                            return false;

                        float mult = dynamic_cast<const CFlower*>(e)
                                         ? static_cast<const CFlower*>(e)->m_FinalStats.detection_multiplier
                                         : 1.f;
                        if ((mob->m_Pos - e->m_Pos).length() <= mob->GetFinalStats()->search_range * mult)
                            return true;
                        else
                            return false;
                    }
                    else
                    {
                        return false;
                    }
                else
                    return false;
            });

        float best_dist = std::numeric_limits<float>::max();
        CEntity* best_target = nullptr;
        for (auto* it : candidates)
        {
            if ((mob->m_Pos - it->m_Pos).length() <= best_dist)
            {
                best_dist = (mob->m_Pos - it->m_Pos).length();
                best_target = it;
            }
        }
        m_pTarget = best_target;
        if (m_pTarget)
            m_TargetPos = m_pTarget->m_Pos;
        else
        {
            float a = mob->GetFinalStats()->search_range / 8.f;
            sf::Vector2f s = mob->m_Pos - sf::Vector2f(a, a);
            s += sf::Vector2f(GetLimitedRng(0.f, a * 2), GetLimitedRng(0.f, a * 2));
            m_TargetPos = s;
        }
    }
    else
    {
        m_ChangeTargetCount += dt;
    }
}
