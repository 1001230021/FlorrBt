#pragma once
#include "../../../Shared/shared.h"
#include "../entity.h"

class CProjectile : public CEntity
{
  public:
    CProjectile(float x, float y, float r, CEntity* owner) : CEntity(owner->GameWorld(), x, y, r), m_pOwner(owner)
    {
    }

    void Tick(float dt) override
    {
        m_Pos += m_Vel * dt;
    }

    CEntity* m_pOwner{ nullptr };
    sf::Vector2f m_Vel{ 0.0f, 0.0f };
};
