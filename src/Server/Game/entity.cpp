#include "entity.h"
#include "gameworld.h"
#include "../../Shared/game_config.h"
#include <algorithm>
#include <cmath>

CGameContext* CEntity::GameContext()
{
    return m_p_game_world ? m_p_game_world->GameContext() : nullptr;
}

bool CEntity::IsCollision(const CEntity& other) const
{
    sf::Vector2f diff = m_pos - other.m_pos;
    float dist_sq = diff.x * diff.x + diff.y * diff.y;
    float radius_sum = m_radius + other.m_radius;
    return dist_sq <= radius_sum * radius_sum;
}

void CEntity::TakeDamage(float dmg, CEntity*, EDamageType)
{
    dmg = std::max(0.f, dmg);
    if (dmg <= 0.f) return;

    m_health -= dmg;
    if (m_health <= 0.f)
    {
        m_health = 0.f;
        m_is_marked_for_des = true;
    }
}

void CEntity::OnCollision(CEntity* other)
{
    if (!other) return;

    sf::Vector2f diff = m_pos - other->m_pos;
    float dist = std::sqrt(diff.x * diff.x + diff.y * diff.y);
    float overlap = (m_radius + other->m_radius) - dist;
    if (overlap <= 0.0f || dist <= game_config::entity_collision_epsilon) return;

    sf::Vector2f normal = diff / dist;

    float self_mass = (m_mass > 0.f) ? m_mass : 1.f;
    float other_mass = (other->m_mass > 0.f) ? other->m_mass : 1.f;
    float total_mass = self_mass + other_mass;

    float self_ratio = other_mass / total_mass;
    m_pos += normal * overlap * self_ratio;
}
