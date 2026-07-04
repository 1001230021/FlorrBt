#pragma once
#include "../../Shared/shared.h"
#include <SFML/System/Vector2.hpp>

class CGameWorld;

class CEntity
{
  public:
    CEntity(CGameWorld* pworld, float x, float y, float r) : m_p_game_world(pworld), m_pos(x, y), m_radius(r) {}
    virtual ~CEntity() = default;

    CGameWorld* GameWorld() { return m_p_game_world; }

    virtual void Tick(float dt) = 0;

    bool IsCollision(const CEntity& other) const;
    virtual void TakeDamage(float dmg, CEntity* attacker, EDamageType dmg_type);
    virtual void OnCollision(CEntity* other);

    sf::Vector2f m_pos;
    float m_radius = 0.f;

    bool m_skip_world_tick = false;

    int m_id = -1;
    bool m_is_marked_for_des = false;

    int m_team = 0;
    float m_mass = 0.f;
    float m_health = 1.f;

  private:
    CGameWorld* m_p_game_world = nullptr;
};
