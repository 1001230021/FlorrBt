#pragma once
#include "../../../Shared/shared.h"
#include "../entity.h"

class CGameWorld;

class CProjectile : public CEntity
{
  public:
    CProjectile(float x, float y, float r, CEntity* owner)
        : CEntity(owner->GameWorld(), x, y, r), m_p_owner(owner), m_owner_id(owner ? owner->m_id : -1)
    {
    }

    void Tick(float dt) override { m_pos += m_vel * dt; }
    CEntity* GetOwner() const;

    CEntity* m_p_owner = nullptr;
    int m_owner_id = -1;
    sf::Vector2f m_vel = {0.0f, 0.0f};
};
