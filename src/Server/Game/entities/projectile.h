#pragma once
#include "../../../Shared/game_config.h"
#include "../../../Shared/math_types.h"
#include "../../../Shared/shared.h"
#include "../entity.h"
#include <algorithm>
#include <cmath>

class CGameWorld;

class CProjectile : public CEntity
{
  public:
    CProjectile(float x, float y, float r, CEntity* owner, SEntityTypeInfo entity_type)
        : CProjectile(owner ? owner->GameWorld() : nullptr, { x, y }, r, owner, entity_type)
    {
    }

    CProjectile(CGameWorld* world, florrbt::Vector2f pos, float r, CEntity* owner, SEntityTypeInfo entity_type)
        : CEntity(world, pos.x, pos.y, r, entity_type), m_p_owner(owner), m_owner_id(owner ? owner->Id() : -1),
          m_owner_generation(owner ? owner->Generation() : 0)
    {
        AllowSkipTick() = owner && owner->AllowSkipTick();
        if (owner)
        {
            TickMode() = owner->TickMode();
            TickHorizon() = owner->TickHorizon();
        }
        if (owner) owner->AddTag(EEntityTag::ClearOwnedEntitiesOnDestroy);
    }

    void Tick(float dt) override
    {
        if (LengthSq(m_vel) > game_config::entity_collision_epsilon * game_config::entity_collision_epsilon)
        {
            FacingAngle() = std::atan2(m_vel.y, m_vel.x);
            HasFacing() = true;
        }
        Pos() += m_vel * dt;
    }
    CEntity* GetOwner() const;
    bool CanPhysicallyCollideWith(const CEntity* other) const override
    {
        return other && !CheckTeam(Team(), other->Team());
    }

    CEntity* m_p_owner = nullptr;
    int m_owner_id = -1;
    std::uint64_t m_owner_generation = 0;
    florrbt::Vector2f m_vel = { 0.0f, 0.0f };
};

class CMissile : public CProjectile
{
  public:
    CMissile(CGameWorld* world, florrbt::Vector2f pos, float radius, florrbt::Vector2f direction, float speed, float damage,
             float health, float lifetime, CEntity* owner = nullptr,
             SEntityTypeInfo entity_type =
                 MakeProjectileEntityType(EProjectileType::Missile, server_missile_entity_type));

    void Tick(float dt) override;
    bool ApplyHit(CEntity* target);
    void AttachToOwner();
    bool RefreshAttachedTransform();
    bool Fire(florrbt::Vector2f direction, float speed, float lifetime);
    bool IsAttachedToOwner() const { return m_attached_to_owner; }
    void RestoreAttachedToOwner(bool attached) { m_attached_to_owner = attached; }

    float m_damage = 0.f;
    float m_lifetime = 0.f;
    float m_age = 0.f;
    float m_decay_base_health = 0.f;

  protected:
    virtual florrbt::Vector2f AttachedDirection(const CEntity& owner) const;
    virtual float AttachedOffsetMultiplier() const;

    bool m_attached_to_owner = false;
};

class CDandelionMissile : public CMissile
{
  public:
    CDandelionMissile(CGameWorld* world, florrbt::Vector2f pos, float radius, float attach_angle, float damage, float health,
                      float lifetime, ERarity rarity, CEntity* owner = nullptr);

    ERarity GetRarity() const { return m_rarity; }
    float GetAttachAngle() const { return m_attach_angle; }

  protected:
    florrbt::Vector2f AttachedDirection(const CEntity& owner) const override;
    float AttachedOffsetMultiplier() const override;

  private:
    float m_attach_angle = 0.f;
    ERarity m_rarity = ERarity::Common;
};

class CPollenProjectile : public CProjectile
{
  public:
    CPollenProjectile(CGameWorld* world, florrbt::Vector2f pos, float radius, float damage, float health, float lifetime,
                      float mass, CEntity* owner = nullptr,
                      SEntityTypeInfo entity_type =
                          MakeProjectileEntityType(EProjectileType::Pollen, server_pollen_entity_type));

    void Tick(float dt) override;
    bool ApplyHit(CEntity* target);

    float m_damage = 0.f;
    float m_lifetime = 0.f;
    float m_age = 0.f;
    float m_decay_base_health = 0.f;

  protected:
    bool m_decay_health_over_lifetime = true;
};

class CTrapProjectile final : public CPollenProjectile
{
  public:
    CTrapProjectile(CGameWorld* world, florrbt::Vector2f pos, float radius, florrbt::Vector2f direction, float speed,
                    float damage, float health, float lifetime, float mass, float deceleration_time, ERarity rarity,
                    CEntity* owner = nullptr);

    void Tick(float dt) override;
    ERarity GetRarity() const { return m_rarity; }

    florrbt::Vector2f m_initial_velocity = { 0.f, 0.f };
    float m_move_age = 0.f;
    float m_deceleration_time = 0.f;

  private:
    ERarity m_rarity = ERarity::Common;
};
