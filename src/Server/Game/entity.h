#pragma once
#include "../../Shared/shared.h"
#include <SFML/System/Vector2.hpp>
#include <cstddef>
#include <cstdint>
#include <limits>

class CGameWorld;
class CGameContext;

enum class EEntityTag : std::uint32_t
{
    ClearOwnedEntitiesOnDestroy = 1u << 0,
    ClearOwnedSummonsOnDestroy = 1u << 1,
    NoDefeatRewards = 1u << 2,
};

enum class EEntityRemovalReason : std::uint8_t
{
    Despawned,
    Defeated,
    Expired,
    Consumed,
    Transformed,
    Replaced,
    OwnerRemoved,
};

class CEntity
{
  public:
    CEntity(CGameWorld* pworld, float x, float y, float r, SEntityTypeInfo entity_type)
        : m_p_game_world(pworld), m_entity_type(entity_type), m_pos(x, y), m_prev_pos(x, y), m_radius(r)
    {
    }
    virtual ~CEntity() = default;

    CGameWorld* GameWorld() const { return m_p_game_world; }
    void SetGameWorld(CGameWorld* world) { m_p_game_world = world; }
    CGameContext* GameContext();

    const SEntityTypeInfo& GetEntityTypeInfo() const { return m_entity_type; }
    EEntityType GetEntityType() const { return m_entity_type.family; }
    EProjectileType GetProjectileType() const { return m_entity_type.projectile; }
    std::uint8_t GetNetworkType() const { return m_entity_type.network_type; }
    bool IsEntityType(EEntityType type) const { return m_entity_type.Is(type); }
    bool IsProjectileType(EProjectileType type) const { return m_entity_type.Is(type); }

    virtual void Tick(float dt) = 0;

    bool IsCollision(const CEntity& other) const;
    virtual void TakeDamage(float dmg, CEntity* attacker, EDamageType dmg_type);
    void OnCollision(CEntity* other);
    virtual const SEntityStats& GetEntityStats() const { return m_entity_stats; }
    virtual bool IsDead() const { return m_health <= 0.f; }
    virtual bool CanCollide() const { return !IsDead(); }
    virtual bool CanPhysicallyCollideWith(const CEntity* other) const { return other != nullptr; }
    virtual bool IsCollisionPositionLocked() const { return false; }
    virtual bool CollidesWithWalls() const { return true; }
    virtual float WallCollisionRadius() const { return m_radius; }
    virtual bool IsVisible() const { return !IsDead(); }
    void MarkForDestroy(EEntityRemovalReason reason = EEntityRemovalReason::Despawned);
    void CancelDestroy();
    EEntityRemovalReason RemovalReason() const { return m_removal_reason; }

    bool HasTag(EEntityTag tag) const { return (m_tags & static_cast<std::uint32_t>(tag)) != 0; }
    void AddTag(EEntityTag tag) { m_tags |= static_cast<std::uint32_t>(tag); }
    void RemoveTag(EEntityTag tag) { m_tags &= ~static_cast<std::uint32_t>(tag); }

    sf::Vector2f m_pos;
    sf::Vector2f m_prev_pos;
    float m_radius = 0.f;

    bool m_skip_world_tick = false;
    bool m_allow_skip_tick = false;

    int m_id = -1;
    std::uint64_t m_generation = 0;
    size_t m_live_index = std::numeric_limits<size_t>::max();
    size_t m_cleanup_index = std::numeric_limits<size_t>::max();
    size_t m_always_tick_index = std::numeric_limits<size_t>::max();
    size_t m_large_spatial_index = std::numeric_limits<size_t>::max();
    std::uint64_t m_active_tick_marker = 0;
    bool m_is_marked_for_des = false;
    std::uint32_t m_tags = 0;

    int m_team = 0;
    float m_mass = 0.f;
    float m_health = 1.f;
    float m_facing_angle = 0.f;
    bool m_has_facing = false;
    SEntityStats m_entity_stats;

  protected:
    void SetNetworkType(std::uint8_t network_type) { m_entity_type.network_type = network_type; }

  private:
    CGameWorld* m_p_game_world = nullptr;
    SEntityTypeInfo m_entity_type;
    EEntityRemovalReason m_removal_reason = EEntityRemovalReason::Despawned;
};
