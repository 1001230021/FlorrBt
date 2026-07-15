#include "state_zone.h"
#include "entities/mob.h"
#include "entities/projectile.h"
#include "gameworld.h"
#include "states/states.h"
#include "../../Shared/game_config.h"
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

CSpiderWebZone::CSpiderWebZone(CGameWorld* world, sf::Vector2f pos, float radius, CEntity* owner,
                               float lifetime, float desired_speed_multiplier)
    : CStateZone(
          world, pos, radius,
          [desired_speed_multiplier](CMobBase* mob) -> std::unique_ptr<CState>
          {
              auto state = std::make_unique<CWebSpeedReduceState>(
                  mob, game_config::mob_spider_web_slow_duration, desired_speed_multiplier);
              if (!state->IsValid()) return nullptr;
              return state;
          },
          [owner_team = owner ? owner->m_team : 0, owner_id = owner ? owner->m_id : -1](CEntity* entity) -> bool
          {
              if (!entity || entity->m_id == owner_id) return false;
              if (dynamic_cast<CProjectile*>(entity)) return false;
              if (owner_team != 0 && CheckTeam(owner_team, entity->m_team)) return false;
              return true;
          })
{
    m_team = owner ? owner->m_team : 0;
    m_mass = 0.f;
    m_health = 1.f;
    m_timer = lifetime;
}
