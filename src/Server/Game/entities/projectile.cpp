#include "projectile.h"
#include "../gameworld.h"

CEntity* CProjectile::GetOwner() const
{
    CGameWorld* world = const_cast<CProjectile*>(this)->GameWorld();
    if (!world || m_owner_id < 0) return nullptr;
    if (m_owner_generation != 0) return world->GetEntity(m_owner_id, m_owner_generation);
    return world->GetEntity(m_owner_id);
}

CMissile::CMissile(CGameWorld* world, sf::Vector2f pos, float radius, sf::Vector2f direction, float speed,
                   float damage, float health, float lifetime, CEntity* owner)
    : CProjectile(world ? world : (owner ? owner->GameWorld() : nullptr), pos, radius, owner),
      m_damage(std::max(0.f, damage)), m_lifetime(std::max(0.f, lifetime))
{
    if (owner) m_team = owner->m_team;
    m_health = std::max(0.f, health);

    float dir_len = Length(direction);
    if (dir_len > game_config::entity_collision_epsilon)
        m_vel = direction / dir_len * speed;
    else
        m_vel = {0.f, 0.f};

    if (LengthSq(m_vel) > game_config::entity_collision_epsilon * game_config::entity_collision_epsilon)
    {
        m_facing_angle = std::atan2(m_vel.y, m_vel.x);
        m_has_facing = true;
    }

    if (m_health <= 0.f || m_lifetime <= 0.f)
        m_is_marked_for_des = true;
}

void CMissile::Tick(float dt)
{
    if (m_health <= 0.f)
    {
        m_health = 0.f;
        m_is_marked_for_des = true;
        return;
    }

    if (m_attached_to_owner)
    {
        if (!RefreshAttachedTransform())
        {
            m_is_marked_for_des = true;
            return;
        }

        m_age = 0.f;
        return;
    }

    CProjectile::Tick(dt);
    m_age += dt;
    if (m_age >= m_lifetime || m_health <= 0.f)
        m_is_marked_for_des = true;
}

bool CMissile::ApplyHit(CEntity* target)
{
    if (m_attached_to_owner) return false;
    if (!target || target == this || target == GetOwner() || m_is_marked_for_des) return false;
    if (m_damage > 0.f)
        target->TakeDamage(m_damage, GetOwner(), EDamageType::Normal);
    m_health = 0.f;
    m_is_marked_for_des = true;
    return true;
}

void CMissile::AttachToOwner()
{
    m_attached_to_owner = true;
    m_age = 0.f;
    m_vel = {0.f, 0.f};
    m_has_facing = true;
    RefreshAttachedTransform();
}

bool CMissile::RefreshAttachedTransform()
{
    if (!m_attached_to_owner) return false;
    CEntity* owner = GetOwner();
    if (!owner || owner->m_is_marked_for_des || owner->IsDead()) return false;

    sf::Vector2f rear = AttachedDirection(*owner);
    if (LengthSq(rear) <= game_config::entity_collision_epsilon * game_config::entity_collision_epsilon)
        rear = {1.f, 0.f};
    m_pos = owner->m_pos + rear * (owner->m_radius * AttachedOffsetMultiplier());
    m_prev_pos = m_pos;
    m_vel = {0.f, 0.f};
    m_facing_angle = std::atan2(rear.y, rear.x);
    m_has_facing = true;
    return true;
}

bool CMissile::Fire(sf::Vector2f direction, float speed, float lifetime)
{
    float dir_len = Length(direction);
    if (dir_len <= game_config::entity_collision_epsilon || m_health <= 0.f || m_is_marked_for_des) return false;

    m_attached_to_owner = false;
    m_age = 0.f;
    m_lifetime = std::max(0.f, lifetime);
    if (m_lifetime <= 0.f)
    {
        m_is_marked_for_des = true;
        return false;
    }

    m_vel = direction / dir_len * speed;
    m_facing_angle = std::atan2(m_vel.y, m_vel.x);
    m_has_facing = true;
    return true;
}

sf::Vector2f CMissile::AttachedDirection(const CEntity& owner) const
{
    sf::Vector2f facing = {std::cos(owner.m_facing_angle), std::sin(owner.m_facing_angle)};
    if (LengthSq(facing) <= game_config::entity_collision_epsilon * game_config::entity_collision_epsilon)
        facing = {1.f, 0.f};
    return -facing;
}

float CMissile::AttachedOffsetMultiplier() const
{
    return game_config::mob_hornet_missile_attach_offset;
}

CDandelionMissile::CDandelionMissile(CGameWorld* world, sf::Vector2f pos, float radius, float attach_angle,
                                     float damage, float health, float lifetime, ERarity rarity, CEntity* owner)
    : CMissile(world, pos, radius, {std::cos(attach_angle), std::sin(attach_angle)}, 0.f,
               damage, health, lifetime, owner),
      m_attach_angle(attach_angle),
      m_rarity(rarity)
{
}

sf::Vector2f CDandelionMissile::AttachedDirection(const CEntity& owner) const
{
    float angle = owner.m_facing_angle + m_attach_angle;
    sf::Vector2f direction = {std::cos(angle), std::sin(angle)};
    if (LengthSq(direction) <= game_config::entity_collision_epsilon * game_config::entity_collision_epsilon)
        direction = {1.f, 0.f};
    return direction;
}

float CDandelionMissile::AttachedOffsetMultiplier() const
{
    return game_config::mob_dandelion_missile_attach_offset;
}

CPollenProjectile::CPollenProjectile(CGameWorld* world, sf::Vector2f pos, float radius, float damage, float health,
                                     float lifetime, float mass, CEntity* owner)
    : CProjectile(world ? world : (owner ? owner->GameWorld() : nullptr), pos, radius, owner),
      m_damage(std::max(0.f, damage)), m_lifetime(std::max(0.f, lifetime))
{
    if (owner) m_team = owner->m_team;
    m_health = std::max(0.f, health);
    m_mass = std::max(0.f, mass);
    m_vel = {0.f, 0.f};
    m_has_facing = true;
    m_facing_angle = GetLimitedRng(-game_config::pi, game_config::pi);

    if (m_health <= 0.f || m_lifetime <= 0.f)
        m_is_marked_for_des = true;
}

void CPollenProjectile::Tick(float dt)
{
    CProjectile::Tick(dt);
    m_age += dt;
    if (m_age >= m_lifetime || m_health <= 0.f)
        m_is_marked_for_des = true;
}

bool CPollenProjectile::ApplyHit(CEntity* target)
{
    if (!target || target == this || target == GetOwner() || m_is_marked_for_des) return false;
    if (m_damage > 0.f)
        target->TakeDamage(m_damage, GetOwner(), EDamageType::Normal);
    return true;
}
