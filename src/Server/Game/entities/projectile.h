#pragma once
#include "../../../Shared/game_config.h"
#include "../../../Shared/shared.h"
#include "../entity.h"
#include <algorithm>
#include <cmath>

class CGameWorld;

class CProjectile : public CEntity
{
  public:
    CProjectile(float x, float y, float r, CEntity* owner)
        : CProjectile(owner ? owner->GameWorld() : nullptr, {x, y}, r, owner)
    {
    }

    CProjectile(CGameWorld* world, sf::Vector2f pos, float r, CEntity* owner = nullptr)
        : CEntity(world, pos.x, pos.y, r), m_p_owner(owner), m_owner_id(owner ? owner->m_id : -1)
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

class CMissile : public CProjectile
{
  public:
    CMissile(CGameWorld* world, sf::Vector2f pos, float radius, sf::Vector2f direction, float speed,
             float damage, float health, float lifetime, CEntity* owner = nullptr);

    void Tick(float dt) override;
    bool ApplyHit(CEntity* target);

    float m_damage = 0.f;
    float m_lifetime = 0.f;
    float m_age = 0.f;
};
