#include "entity.h"

#include <cmath>

bool CEntity::isCollision(const CEntity& other) const
{
    sf::Vector2f diff = m_Pos - other.m_Pos;
    float distSq = diff.x * diff.x + diff.y * diff.y;
    float radiusSum = m_Radius + other.m_Radius;
    return distSq <= radiusSum * radiusSum;
}

void CEntity::TakeDamage(float dmg, CEntity* attacker, EDamageType dmg_type)
{
    m_Health -= dmg;
    if (m_Health <= 0)
    {
        m_Health = 0;
        m_IsMarkedForDes = true;
    }
}

void CEntity::OnCollision(CEntity* other)
{
    if (!other)
        return;

    sf::Vector2f diff = m_Pos - other->m_Pos;
    float dist = std::sqrt(diff.x * diff.x + diff.y * diff.y);
    float overlap = (m_Radius + other->m_Radius) - dist;
    if (overlap <= 0.0f || dist <= 0.001f)
        return;

    sf::Vector2f normal = diff / dist;

    float massThis = (m_Mass > 0.f) ? m_Mass : 1.f;
    float massOther = (other->m_Mass > 0.f) ? other->m_Mass : 1.f;
    float totalMass = massThis + massOther;

    float ratioThis = massOther / totalMass;
    m_Pos += normal * overlap * ratioThis;
}
