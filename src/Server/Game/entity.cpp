#include "entity.h"
#include "../../Shared/game_config.h"
#include "gameworld.h"
#include <algorithm>
#include <cmath>
#include <cstdint>

CGameContext* CEntity::GameContext() { return m_p_game_world ? m_p_game_world->GameContext() : nullptr; }

void CEntity::MarkForDestroy(EEntityRemovalReason reason)
{
    if (!m_is_marked_for_des || reason == EEntityRemovalReason::Defeated)
        m_removal_reason = reason;
    m_is_marked_for_des = true;
    if (m_p_game_world) m_p_game_world->QueueEntityForCleanup(this);
}

void CEntity::CancelDestroy()
{
    m_is_marked_for_des = false;
    m_removal_reason = EEntityRemovalReason::Despawned;
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
        MarkForDestroy(EEntityRemovalReason::Defeated);
    }
}

void CEntity::OnCollision(CEntity* other)
{
    if (!other) return;

    sf::Vector2f diff = m_pos - other->m_pos;
    float dist = std::sqrt(diff.x * diff.x + diff.y * diff.y);
    float overlap = (m_radius + other->m_radius) - dist;
    if (overlap <= 0.0f) return;

    sf::Vector2f normal;
    if (dist > game_config::entity_collision_epsilon)
    {
        normal = diff / dist;
    } else
    {
        const std::uint32_t low_id = static_cast<std::uint32_t>(std::min(m_id, other->m_id));
        const std::uint32_t high_id = static_cast<std::uint32_t>(std::max(m_id, other->m_id));
        const std::uint32_t seed = low_id * 1103515245u + high_id * 2654435761u;
        const float angle = static_cast<float>(seed % 6283u) / 1000.f;
        normal = { std::cos(angle), std::sin(angle) };
        if (m_id > other->m_id) normal = -normal;
    }

    const bool self_locked = IsCollisionPositionLocked();
    const bool other_locked = other->IsCollisionPositionLocked();
    if (self_locked && other_locked) return;

    float self_ratio = 0.f;
    float other_ratio = 0.f;
    if (self_locked)
    {
        other_ratio = 1.f;
    } else if (other_locked)
    {
        self_ratio = 1.f;
    } else
    {
        const float self_mass = m_mass > 0.f ? m_mass : 1.f;
        const float other_mass = other->m_mass > 0.f ? other->m_mass : 1.f;
        const float total_mass = self_mass + other_mass;
        self_ratio = other_mass / total_mass;
        other_ratio = self_mass / total_mass;
    }

    float separation = overlap + game_config::entity_collision_epsilon;
    m_pos += normal * separation * self_ratio;
    other->m_pos -= normal * separation * other_ratio;
}
