#include "state_zone.h"
#include "entities/mob.h"
#include "gameworld.h"
#include "../../Shared/tools.h"
#include <utility>
#include <vector>

CStateZone::CStateZone(CGameWorld* world, sf::Vector2f pos, float radius, state_factory state, zone_filter filter)
    : CEntity(world, pos.x, pos.y, radius), m_state(std::move(state)), m_filter(std::move(filter))
{
}

void CStateZone::Tick(float dt)
{
    if (m_timer != endless)
    {
        m_timer -= dt;
        if (m_timer <= 0.f)
        {
            m_is_marked_for_des = true;
            return;
        }
    }

    Apply();
}

void CStateZone::Apply()
{
    CGameWorld* world = GameWorld();
    if (!world || !m_state || m_radius <= 0.f) return;

    const float radius_sq = m_radius * m_radius;
    std::vector<CEntity*> entities = world->GetSpatialGrid().QueryRange(m_pos, m_radius, [this, radius_sq](const CEntity* entity)
    {
        if (!entity || entity == this || entity->m_is_marked_for_des) return false;
        if (DistanceSq(entity->m_pos, m_pos) > radius_sq) return false;
        return !m_filter || m_filter(const_cast<CEntity*>(entity));
    });

    for (CEntity* entity : entities)
    {
        auto* mob = dynamic_cast<CMobBase*>(entity);
        if (!mob) continue;

        std::unique_ptr<CState> state = m_state(mob);
        if (state) mob->AddState(std::move(state));
    }
}
