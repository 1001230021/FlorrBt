#pragma once
#include "../../../Shared/game_config.h"
#include "../../../Shared/shared.h"
#include "../entity.h"
#include <cmath>

class CGameWorld;

class CProjectile : public CEntity
{
  public:
    CProjectile(float x, float y, float r, CEntity* owner)
        : CEntity(owner->GameWorld(), x, y, r), m_p_owner(owner), m_owner_id(owner ? owner->m_id : -1)
    {
    }

    void Tick(float dt) override
    {
        if (LengthSq(m_vel) > game_config::entity_collision_epsilon * game_config::entity_collision_epsilon)
        {
            m_facing_angle = std::atan2(m_vel.y, m_vel.x);
            m_has_facing = true;
        }
        m_pos += m_vel * dt;
    }
    CEntity* GetOwner() const;

    CEntity* m_p_owner = nullptr;
    int m_owner_id = -1;
    sf::Vector2f m_vel = {0.0f, 0.0f};
};
