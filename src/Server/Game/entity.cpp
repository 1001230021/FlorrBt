#include "entity.h"
#include "../../Shared/game_config.h"
#include "../../Shared/circle_contact.h"
#include "gameworld.h"
#include <algorithm>
#include <cmath>
#include <cstdint>

CGameContext* CEntity::GameContext() { return m_p_game_world ? m_p_game_world->GameContext() : nullptr; }

SEntityHandle CEntity::GetHandle() const
{
    if (!m_p_game_world || m_id < 0 || m_generation == 0) return {};
    return { m_id, m_generation, m_p_game_world->GetId(), m_p_game_world->GetInstanceGeneration() };
}

void CEntity::MarkForDestroy(EEntityRemovalReason reason)
{
    if (!IsMarkedForDes() || reason == EEntityRemovalReason::Defeated)
        m_removal_reason = reason;
    IsMarkedForDes() = true;
    if (m_p_game_world) m_p_game_world->QueueEntityForCleanup(this);
}

void CEntity::CancelDestroy()
{
    IsMarkedForDes() = false;
    m_removal_reason = EEntityRemovalReason::Despawned;
}

bool CEntity::IsCollision(const CEntity& other) const
{
    if (!std::isfinite(Pos().x) || !std::isfinite(Pos().y) || !std::isfinite(other.Pos().x) ||
        !std::isfinite(other.Pos().y) || !std::isfinite(Radius()) || !std::isfinite(other.Radius()))
        return false;
    florrbt::Vector2f diff = Pos() - other.Pos();
    float dist_sq = diff.x * diff.x + diff.y * diff.y;
    float radius_sum = Radius() + other.Radius();
    return dist_sq <= radius_sum * radius_sum;
}

void CEntity::TakeDamage(float dmg, CEntity*, EDamageType)
{
    if (!std::isfinite(dmg)) return;
    dmg = std::max(0.f, dmg);
    if (dmg <= 0.f) return;

    Health() -= dmg;
    if (Health() <= 0.f)
    {
        Health() = 0.f;
        MarkForDestroy(EEntityRemovalReason::Defeated);
    }
}

void CEntity::OnCollision(CEntity* other)
{
    if (!other) return;
    if (!std::isfinite(Pos().x) || !std::isfinite(Pos().y) || !std::isfinite(other->Pos().x) ||
        !std::isfinite(other->Pos().y) || !std::isfinite(Radius()) || !std::isfinite(other->Radius()))
        return;

    florrbt::Vector2f diff = Pos() - other->Pos();
    float dist = std::sqrt(diff.x * diff.x + diff.y * diff.y);
    float overlap = (Radius() + other->Radius()) - dist;
    if (overlap <= 0.0f) return;

    florrbt::Vector2f normal;
    if (dist > game_config::entity_collision_epsilon)
    {
        normal = diff / dist;
    } else
    {
        const std::uint32_t low_id = static_cast<std::uint32_t>(std::min(Id(), other->Id()));
        const std::uint32_t high_id = static_cast<std::uint32_t>(std::max(Id(), other->Id()));
        const std::uint32_t seed = low_id * 1103515245u + high_id * 2654435761u;
        const float angle = static_cast<float>(seed % 6283u) / 1000.f;
        normal = { std::cos(angle), std::sin(angle) };
        if (Id() > other->Id()) normal = -normal;
    }

    const bool self_locked = IsCollisionPositionLocked();
    const bool other_locked = other->IsCollisionPositionLocked();
    if (self_locked && other_locked) return;

    const auto weights = florrbt::ComputeCircleContactWeights(Mass(), other->Mass(), self_locked, other_locked,
                                                             game_config::mob_contact_mass_exponent);

    float separation = overlap + game_config::entity_collision_epsilon;
    Pos() += normal * separation * weights.lhs;
    other->Pos() -= normal * separation * weights.rhs;
}
