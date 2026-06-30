#pragma once
#include "../../Shared/shared.h"
#include <SFML/System/Vector2.hpp>

class CGameWorld;
class CEntity
{
  private:
    CGameWorld* m_pGameWorld;

  public:
    CEntity(CGameWorld* pworld, float x, float y, float r) : m_pGameWorld(pworld), m_Pos(x, y), m_Radius(r) {}
    virtual ~CEntity() = default;

    CGameWorld* GameWorld()
    {
        return m_pGameWorld;
    }

    virtual void Tick(float dt) = 0;

    bool isCollision(const CEntity& other) const;
    virtual void TakeDamage(float dmg, CEntity* attacker, EDamageType dmg_type);
    virtual void OnCollision(CEntity* other);

    sf::Vector2f m_Pos;
    float m_Radius;

    bool m_SkipWorldTick = false;

    int m_ID = -1;
    bool m_IsMarkedForDes = false;

    int m_Team = 0;
    float m_Mass = 0.f;
    float m_Health = 1.f;
};