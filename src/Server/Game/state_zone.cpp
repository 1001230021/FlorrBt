#include "state_zone.h"
#include "entities/flower.h"
#include "entities/mob.h"
#include "entities/projectile.h"
#include "gameworld.h"
#include "states/states.h"
#include "../../Shared/game_config.h"
#include "../../Shared/tools.h"
#include <algorithm>
#include <utility>

namespace
{
float WebReferenceMass(const CEntity* owner)
{
    if (const auto* flower = dynamic_cast<const CFlower*>(owner))
    {
        if (const SFlowerStats* stats = flower->GetFinalStats();
            stats && stats->mass > game_config::entity_collision_epsilon)
            return stats->mass;
    }
    if (owner && owner->m_mass > game_config::entity_collision_epsilon) return owner->m_mass;
    return std::max(game_config::entity_collision_epsilon, game_config::mob_player_flower_mass);
}
}

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

    m_apply_timer -= dt;
    if (m_apply_timer <= 0.f)
    {
        m_apply_timer = std::max(game_config::server_fixed_dt, m_apply_interval);
        Apply();
    }
}

void CStateZone::Apply()
{
    CGameWorld* world = GameWorld();
    if (!world || !m_state || m_radius <= 0.f) return;

    const float query_radius = m_radius + std::max(0.f, world->GetMaxEntityRadius());
    world->GetSpatialGrid().ForEachInRangeBroadphase(m_pos, query_radius, [this](CEntity* entity)
    {
        if (!entity || entity == this || entity->m_is_marked_for_des) return;
        if (entity->IsDead() || !entity->CanCollide()) return;
        const float radius = m_radius + std::max(0.f, entity->m_radius);
        if (DistanceSq(m_pos, entity->m_pos) > radius * radius) return;
        if (m_filter && !m_filter(entity)) return;
        auto* mob = dynamic_cast<CMobBase*>(entity);
        if (!mob) return;

        std::unique_ptr<CState> state = m_state(mob);
        if (state) mob->AddState(std::move(state));
    });
}

CSpiderWebZone::CSpiderWebZone(CGameWorld* world, sf::Vector2f pos, float radius, CEntity* owner,
                               float lifetime, float desired_speed_multiplier)
    : CStateZone(
          world, pos, radius,
          [desired_speed_multiplier, reference_mass = WebReferenceMass(owner)](CMobBase* mob) -> std::unique_ptr<CState>
          {
              auto state = std::make_unique<CWebSpeedReduceState>(
                  mob, game_config::mob_spider_web_slow_duration, desired_speed_multiplier, reference_mass);
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
    m_apply_interval = game_config::mob_spider_web_apply_interval;
}
