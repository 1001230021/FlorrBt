#include "projectile.h"
#include "../gameworld.h"

namespace
{
float LifetimeHealthFraction(float age, float lifetime)
{
    if (lifetime <= game_config::entity_collision_epsilon) return 0.f;
    const float progress = std::clamp(age / lifetime, 0.f, 1.f);
    return std::sqrt(std::max(0.f, 1.f - progress * progress));
}

bool ApplyLifetimeHealthDecay(float dt, float lifetime, float base_health, float& age, float& health)
{
    const float previous_fraction = LifetimeHealthFraction(age, lifetime);
    age = std::min(std::max(0.f, lifetime), age + std::max(0.f, dt));
    const float current_fraction = LifetimeHealthFraction(age, lifetime);
    health = std::max(0.f, health - std::max(0.f, base_health) *
                                         std::max(0.f, previous_fraction - current_fraction));
    return age >= lifetime;
}
} // namespace

CEntity* CProjectile::GetOwner() const
{
    CGameWorld* world = const_cast<CProjectile*>(this)->GameWorld();
    if (!world || m_owner_id < 0) return nullptr;
    if (m_owner_generation != 0) return world->GetEntity(m_owner_id, m_owner_generation);
    return world->GetEntity(m_owner_id);
}

CMissile::CMissile(CGameWorld* world, sf::Vector2f pos, float radius, sf::Vector2f direction, float speed, float damage,
                   float health, float lifetime, CEntity* owner, SEntityTypeInfo entity_type)
    : CProjectile(world ? world : (owner ? owner->GameWorld() : nullptr), pos, radius, owner, entity_type),
      m_damage(std::max(0.f, damage)), m_lifetime(std::max(0.f, lifetime))
{
    if (owner) m_team = owner->m_team;
    m_allow_skip_tick = false;
    m_health = std::max(0.f, health);
    m_decay_base_health = m_health;
    m_mass = std::max(0.f, game_config::default_missile_mass);

    float dir_len = Length(direction);
    if (dir_len > game_config::entity_collision_epsilon) m_vel = direction / dir_len * speed;
    else m_vel = { 0.f, 0.f };

    if (LengthSq(m_vel) > game_config::entity_collision_epsilon * game_config::entity_collision_epsilon)
    {
        m_facing_angle = std::atan2(m_vel.y, m_vel.x);
        m_has_facing = true;
    }

    if (m_health <= 0.f)
        MarkForDestroy(EEntityRemovalReason::Defeated);
    else if (m_lifetime <= 0.f)
        MarkForDestroy(EEntityRemovalReason::Expired);
}

void CMissile::Tick(float dt)
{
    if (m_health <= 0.f)
    {
        m_health = 0.f;
        MarkForDestroy(EEntityRemovalReason::Defeated);
        return;
    }

    if (m_attached_to_owner)
    {
        if (!RefreshAttachedTransform())
        {
            MarkForDestroy(EEntityRemovalReason::OwnerRemoved);
            return;
        }

        m_age = 0.f;
        return;
    }

    CProjectile::Tick(dt);
    const bool lifetime_depleted = ApplyLifetimeHealthDecay(dt, m_lifetime, m_decay_base_health, m_age, m_health);
    if (m_health <= 0.f)
        MarkForDestroy(lifetime_depleted ? EEntityRemovalReason::Expired : EEntityRemovalReason::Defeated);
}

bool CMissile::ApplyHit(CEntity* target)
{
    if (m_attached_to_owner) return false;
    if (!target || target == this || m_health <= 0.f) return false;
    if (target->m_id == m_owner_id && (m_owner_generation == 0 || target->m_generation == m_owner_generation))
        return false;
    if (target == GetOwner()) return false;
    if (m_damage > 0.f) target->TakeDamage(m_damage, GetOwner(), EDamageType::Normal);
    return true;
}

void CMissile::AttachToOwner()
{
    m_attached_to_owner = true;
    m_age = 0.f;
    m_vel = { 0.f, 0.f };
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
        rear = { 1.f, 0.f };
    m_pos = owner->m_pos + rear * (owner->m_radius * AttachedOffsetMultiplier());
    m_prev_pos = m_pos;
    m_vel = { 0.f, 0.f };
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
    m_decay_base_health = std::max(0.f, m_health);
    m_lifetime = std::max(0.f, lifetime);
    if (m_lifetime <= 0.f)
    {
        MarkForDestroy(EEntityRemovalReason::Expired);
        return false;
    }

    m_vel = direction / dir_len * speed;
    m_facing_angle = std::atan2(m_vel.y, m_vel.x);
    m_has_facing = true;
    return true;
}

sf::Vector2f CMissile::AttachedDirection(const CEntity& owner) const
{
    sf::Vector2f facing = { std::cos(owner.m_facing_angle), std::sin(owner.m_facing_angle) };
    if (LengthSq(facing) <= game_config::entity_collision_epsilon * game_config::entity_collision_epsilon)
        facing = { 1.f, 0.f };
    return -facing;
}

float CMissile::AttachedOffsetMultiplier() const { return game_config::mob_hornet_missile_attach_offset; }

CDandelionMissile::CDandelionMissile(CGameWorld* world, sf::Vector2f pos, float radius, float attach_angle,
                                     float damage, float health, float lifetime, ERarity rarity, CEntity* owner)
    : CMissile(world, pos, radius, { std::cos(attach_angle), std::sin(attach_angle) }, 0.f, damage, health, lifetime,
               owner,
               MakeProjectileEntityType(EProjectileType::Missile, server_dandelion_missile_entity_type)),
      m_attach_angle(attach_angle), m_rarity(rarity)
{
}

sf::Vector2f CDandelionMissile::AttachedDirection(const CEntity& owner) const
{
    float angle = owner.m_facing_angle + m_attach_angle;
    sf::Vector2f direction = { std::cos(angle), std::sin(angle) };
    if (LengthSq(direction) <= game_config::entity_collision_epsilon * game_config::entity_collision_epsilon)
        direction = { 1.f, 0.f };
    return direction;
}

float CDandelionMissile::AttachedOffsetMultiplier() const { return game_config::mob_dandelion_missile_attach_offset; }

CPollenProjectile::CPollenProjectile(CGameWorld* world, sf::Vector2f pos, float radius, float damage, float health,
                                     float lifetime, float mass, CEntity* owner, SEntityTypeInfo entity_type)
    : CProjectile(world ? world : (owner ? owner->GameWorld() : nullptr), pos, radius, owner, entity_type),
      m_damage(std::max(0.f, damage)), m_lifetime(std::max(0.f, lifetime))
{
    if (owner) m_team = owner->m_team;
    m_allow_skip_tick = false;
    m_health = std::max(0.f, health);
    m_decay_base_health = m_health;
    m_mass = std::max(0.f, mass);
    m_vel = { 0.f, 0.f };
    m_has_facing = true;
    m_facing_angle = GetLimitedRng(-game_config::pi, game_config::pi);

    if (m_health <= 0.f)
        MarkForDestroy(EEntityRemovalReason::Defeated);
    else if (m_lifetime <= 0.f)
        MarkForDestroy(EEntityRemovalReason::Expired);
}

void CPollenProjectile::Tick(float dt)
{
    CProjectile::Tick(dt);
    const bool lifetime_depleted = m_decay_health_over_lifetime
                                       ? ApplyLifetimeHealthDecay(dt, m_lifetime, m_decay_base_health, m_age, m_health)
                                       : ((m_age += std::max(0.f, dt)) >= m_lifetime);
    if (m_health <= 0.f)
        MarkForDestroy(lifetime_depleted ? EEntityRemovalReason::Expired : EEntityRemovalReason::Defeated);
    else if (lifetime_depleted)
        MarkForDestroy(EEntityRemovalReason::Expired);
}

bool CPollenProjectile::ApplyHit(CEntity* target)
{
    if (!target || target == this || m_health <= 0.f) return false;
    if (target->m_id == m_owner_id && (m_owner_generation == 0 || target->m_generation == m_owner_generation))
        return false;
    if (target == GetOwner()) return false;
    if (m_damage > 0.f) target->TakeDamage(m_damage, GetOwner(), EDamageType::Normal);
    return true;
}

CTrapProjectile::CTrapProjectile(CGameWorld* world, sf::Vector2f pos, float radius, sf::Vector2f direction,
                                 float speed, float damage, float health, float lifetime, float mass,
                                 float deceleration_time, ERarity rarity, CEntity* owner)
    : CPollenProjectile(world, pos, radius, damage, health, lifetime, mass, owner,
                        MakeProjectileEntityType(EProjectileType::Trap, server_trap_projectile_entity_type)),
      m_deceleration_time(std::max(0.f, deceleration_time)), m_rarity(rarity)
{
    m_decay_health_over_lifetime = false;
    const float length = Length(direction);
    if (length > game_config::entity_collision_epsilon)
        m_initial_velocity = direction / length * std::max(0.f, speed);
    m_vel = m_initial_velocity;
    if (LengthSq(direction) > game_config::entity_collision_epsilon * game_config::entity_collision_epsilon)
    {
        m_facing_angle = std::atan2(direction.y, direction.x);
        m_has_facing = true;
    }
}

void CTrapProjectile::Tick(float dt)
{
    if (m_deceleration_time <= game_config::entity_collision_epsilon)
    {
        m_vel = { 0.f, 0.f };
    } else
    {
        const float progress = std::clamp(m_move_age / m_deceleration_time, 0.f, 1.f);
        m_vel = m_initial_velocity * (1.f - progress);
    }

    CPollenProjectile::Tick(dt);
    m_move_age += std::max(0.f, dt);
    if (m_move_age >= m_deceleration_time) m_vel = { 0.f, 0.f };
}
