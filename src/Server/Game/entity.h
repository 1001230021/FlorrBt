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

enum class EEntityTickMode : std::uint8_t
{
    Default,
    Always,
    PlayerVisible,
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

    // Reference accessors keep both `entity->Pos().x = v` and `entity->Health()`
    // styles uniform across the codebase; all state lives in the private section.
    sf::Vector2f& Pos() { return m_pos; }
    const sf::Vector2f& Pos() const { return m_pos; }
    sf::Vector2f& PrevPos() { return m_prev_pos; }
    const sf::Vector2f& PrevPos() const { return m_prev_pos; }
    float& Radius() { return m_radius; }
    const float& Radius() const { return m_radius; }
    bool& SkipWorldTick() { return m_skip_world_tick; }
    const bool& SkipWorldTick() const { return m_skip_world_tick; }
    bool& AllowSkipTick() { return m_allow_skip_tick; }
    const bool& AllowSkipTick() const { return m_allow_skip_tick; }
    EEntityTickMode& TickMode() { return m_tick_mode; }
    const EEntityTickMode& TickMode() const { return m_tick_mode; }
    int& Id() { return m_id; }
    const int& Id() const { return m_id; }
    std::uint64_t& Generation() { return m_generation; }
    const std::uint64_t& Generation() const { return m_generation; }
    size_t& LiveIndex() { return m_live_index; }
    const size_t& LiveIndex() const { return m_live_index; }
    size_t& CleanupIndex() { return m_cleanup_index; }
    const size_t& CleanupIndex() const { return m_cleanup_index; }
    size_t& AlwaysTickIndex() { return m_always_tick_index; }
    const size_t& AlwaysTickIndex() const { return m_always_tick_index; }
    size_t& ConditionalTickIndex() { return m_conditional_tick_index; }
    const size_t& ConditionalTickIndex() const { return m_conditional_tick_index; }
    size_t& LargeSpatialIndex() { return m_large_spatial_index; }
    const size_t& LargeSpatialIndex() const { return m_large_spatial_index; }
    std::uint64_t& ActiveTickMarker() { return m_active_tick_marker; }
    const std::uint64_t& ActiveTickMarker() const { return m_active_tick_marker; }
    std::uint64_t& CollisionSyncMarker() { return m_collision_sync_marker; }
    const std::uint64_t& CollisionSyncMarker() const { return m_collision_sync_marker; }
    bool& IsMarkedForDes() { return m_is_marked_for_des; }
    const bool& IsMarkedForDes() const { return m_is_marked_for_des; }
    std::uint32_t& Tags() { return m_tags; }
    const std::uint32_t& Tags() const { return m_tags; }
    int& Team() { return m_team; }
    const int& Team() const { return m_team; }
    void SetTickHorizon(float value) { m_tick_horizon = value; }
    // Non-const reference overload lets existing `TickHorizon() = v` calls work.
    float& TickHorizon() { return m_tick_horizon; }
    float& Mass() { return m_mass; }
    const float& Mass() const { return m_mass; }
    float& Health() { return m_health; }
    const float& Health() const { return m_health; }
    float& FacingAngle() { return m_facing_angle; }
    const float& FacingAngle() const { return m_facing_angle; }
    bool& HasFacing() { return m_has_facing; }
    const bool& HasFacing() const { return m_has_facing; }
    SEntityStats& EntityStats() { return m_entity_stats; }
    const SEntityStats& EntityStats() const { return m_entity_stats; }

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
    virtual float TickHorizon() const { return m_tick_horizon; }
    virtual bool IsVisible() const { return !IsDead(); }
    virtual void OnBeforeRemoved(EEntityRemovalReason) {}
    void MarkForDestroy(EEntityRemovalReason reason = EEntityRemovalReason::Despawned);
    void CancelDestroy();
    EEntityRemovalReason RemovalReason() const { return m_removal_reason; }

    bool HasTag(EEntityTag tag) const { return (Tags() & static_cast<std::uint32_t>(tag)) != 0; }
    void AddTag(EEntityTag tag) { Tags() |= static_cast<std::uint32_t>(tag); }
    void RemoveTag(EEntityTag tag) { Tags() &= ~static_cast<std::uint32_t>(tag); }

  protected:
    void SetNetworkType(std::uint8_t network_type) { m_entity_type.network_type = network_type; }

  private:
    CGameWorld* m_p_game_world = nullptr;
    SEntityTypeInfo m_entity_type;
    EEntityRemovalReason m_removal_reason = EEntityRemovalReason::Despawned;

    sf::Vector2f m_pos;
    sf::Vector2f m_prev_pos;
    float m_radius = 0.f;

    bool m_skip_world_tick = false;
    bool m_allow_skip_tick = false;
    EEntityTickMode m_tick_mode = EEntityTickMode::Default;
    float m_tick_horizon = 0.f;

    int m_id = -1;
    std::uint64_t m_generation = 0;
    size_t m_live_index = std::numeric_limits<size_t>::max();
    size_t m_cleanup_index = std::numeric_limits<size_t>::max();
    size_t m_always_tick_index = std::numeric_limits<size_t>::max();
    size_t m_conditional_tick_index = std::numeric_limits<size_t>::max();
    size_t m_large_spatial_index = std::numeric_limits<size_t>::max();
    std::uint64_t m_active_tick_marker = 0;
    std::uint64_t m_collision_sync_marker = 0;
    bool m_is_marked_for_des = false;
    std::uint32_t m_tags = 0;

    int m_team = 0;
    float m_mass = 0.f;
    float m_health = 1.f;
    float m_facing_angle = 0.f;
    bool m_has_facing = false;
    SEntityStats m_entity_stats;
};
