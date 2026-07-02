#pragma once
#include "../../../Shared/shared.h"
#include "../entity.h"

class CProjectile : public CEntity
{
  public:
    CProjectile(float x, float y, float r, CEntity* owner) : CEntity(owner->GameWorld(), x, y, r), m_p_owner(owner) {}

    void Tick(float dt) override { m_pos += m_vel * dt; }

    CEntity* m_p_owner = nullptr;
    sf::Vector2f m_vel = {0.0f, 0.0f};
};