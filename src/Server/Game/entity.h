#pragma once
#include "../../Shared/shared.h"
#include "../../Shared/math_types.h"
#include "../../Shared/terrain_rules.h"
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <span>
#include <utility>
#include <vector>

class CGameWorld;
class CGameContext;

// A stable reference to an entity.  An id alone is not sufficient because
// CGameWorld intentionally reuses ids after cleanup; generation closes that
// hole.  The world instance fields additionally prevent a handle captured in
// one room from resolving an entity in a different room after a transfer or
// room recreation.  Zero world_instance_generation is accepted only for
// legacy snapshots and is always resolved against the caller's world.
struct SEntityHandle
{
    int id = -1;
    std::uint64_t generation = 0;
    std::uint32_t world_id = 0;
    std::uint64_t world_instance_generation = 0;

    bool IsValid() const { return id >= 0 && generation != 0; }
    bool HasWorldIdentity() const { return world_instance_generation != 0; }

    // Legacy snapshots have id + generation but no process-local world
    // identity. They can still be compared safely after being restored into
    // the current world; fully-qualified handles must match their world too.
    bool RefersTo(const SEntityHandle& other) const
    {
        if (!IsValid() || !other.IsValid() || id != other.id || generation != other.generation) return false;
        if (!HasWorldIdentity() || !other.HasWorldIdentity()) return true;
        return world_id == other.world_id && world_instance_generation == other.world_instance_generation;
    }

    bool operator==(const SEntityHandle& other) const
    {
        return id == other.id && generation == other.generation && world_id == other.world_id &&
               world_instance_generation == other.world_instance_generation;
    }
};

struct SEntityHandleHash
{
    std::size_t operator()(const SEntityHandle& value) const noexcept
    {
        std::size_t seed = std::hash<int>{}(value.id);
        seed ^= std::hash<std::uint64_t>{}(value.generation) + 0x9e3779b9U + (seed << 6U) + (seed >> 2U);
        seed ^= std::hash<std::uint32_t>{}(value.world_id) + 0x9e3779b9U + (seed << 6U) + (seed >> 2U);
        seed ^= std::hash<std::uint64_t>{}(value.world_instance_generation) + 0x9e3779b9U + (seed << 6U) +
                (seed >> 2U);
        return seed;
    }
};

using TEntityHandleRemap = std::span<const std::pair<SEntityHandle, SEntityHandle>>;
inline SEntityHandle RemapTransferredHandle(const SEntityHandle& handle, TEntityHandleRemap mapping) noexcept
{
    for (const auto& [before, after] : mapping) if (handle.RefersTo(before)) return after;
    return {};
}

enum class EEntityTag : std::uint32_t
{
    ClearOwnedEntitiesOnDestroy = 1u << 0,
    ClearOwnedSummonsOnDestroy = 1u << 1,
    NoDefeatRewards = 1u << 2,
    FireflySkillSummon = 1u << 3,
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
    SEntityHandle GetHandle() const;
    CGameContext* GameContext();

    const SEntityTypeInfo& GetEntityTypeInfo() const { return m_entity_type; }
    EEntityType GetEntityType() const { return m_entity_type.family; }
    EProjectileType GetProjectileType() const { return m_entity_type.projectile; }
    std::uint8_t GetNetworkType() const { return m_entity_type.network_type; }
    bool IsEntityType(EEntityType type) const { return m_entity_type.Is(type); }
    bool IsProjectileType(EProjectileType type) const { return m_entity_type.Is(type); }

    // Reference accessors keep both `entity->Pos().x = v` and `entity->Health()`
    // styles uniform across the codebase; all state lives in the private section.
    florrbt::Vector2f& Pos() { return m_pos; }
    const florrbt::Vector2f& Pos() const { return m_pos; }
    florrbt::Vector2f& PrevPos() { return m_prev_pos; }
    const florrbt::Vector2f& PrevPos() const { return m_prev_pos; }
    float& Radius() { return m_radius; }
    const float& Radius() const { return m_radius; }
    bool& SkipWorldTick() { return m_skip_world_tick; }
    const bool& SkipWorldTick() const { return m_skip_world_tick; }
    bool& AllowSkipTick() { return m_allow_skip_tick; }
    const bool& AllowSkipTick() const { return m_allow_skip_tick; }
    EEntityTickMode& TickMode() { return m_tick_mode; }
    const EEntityTickMode& TickMode() const { return m_tick_mode; }
    int Id() const { return m_id; }
    std::uint64_t Generation() const { return m_generation; }
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
    float& Mana() { return m_mana; }
    const float& Mana() const { return m_mana; }
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
    virtual EWallQuery WallCollisionMask() const { return EWallQuery::All; }
    virtual float WallCollisionRadius() const { return m_radius; }
    virtual float TickHorizon() const { return m_tick_horizon; }
    virtual bool IsVisible() const { return !IsDead(); }
    virtual void OnBeforeRemoved(EEntityRemovalReason) {}
    virtual void CollectTransferDependencies(std::vector<SEntityHandle>&) const {}
    virtual void RemapTransferredReferences(TEntityHandleRemap) noexcept {}
    virtual void OnWorldTransferCommitted() {}
    void MarkForDestroy(EEntityRemovalReason reason = EEntityRemovalReason::Despawned);
    void CancelDestroy();
    EEntityRemovalReason RemovalReason() const { return m_removal_reason; }

    bool HasTag(EEntityTag tag) const { return (Tags() & static_cast<std::uint32_t>(tag)) != 0; }
    void AddTag(EEntityTag tag) { Tags() |= static_cast<std::uint32_t>(tag); }
    void RemoveTag(EEntityTag tag) { Tags() &= ~static_cast<std::uint32_t>(tag); }

  protected:
    void SetNetworkType(std::uint8_t network_type) { m_entity_type.network_type = network_type; }

  private:
    friend class CGameWorld;
    friend class CWorldTransfer;

    void SetGameWorld(CGameWorld* world) { m_p_game_world = world; }
    void SetIdentity(int id, std::uint64_t generation)
    {
        m_id = id;
        m_generation = generation;
    }

    CGameWorld* m_p_game_world = nullptr;
    SEntityTypeInfo m_entity_type;
    EEntityRemovalReason m_removal_reason = EEntityRemovalReason::Despawned;

    florrbt::Vector2f m_pos;
    florrbt::Vector2f m_prev_pos;
    float m_radius = 0.f;

    bool m_skip_world_tick = false;
    bool m_allow_skip_tick = false;
    EEntityTickMode m_tick_mode = EEntityTickMode::Default;
    float m_tick_horizon = 0.f;

    int m_id = -1;
    std::uint64_t m_generation = 0;
    size_t m_live_index = (std::numeric_limits<size_t>::max)();
    size_t m_cleanup_index = (std::numeric_limits<size_t>::max)();
    size_t m_always_tick_index = (std::numeric_limits<size_t>::max)();
    size_t m_conditional_tick_index = (std::numeric_limits<size_t>::max)();
    size_t m_large_spatial_index = (std::numeric_limits<size_t>::max)();
    std::uint64_t m_active_tick_marker = 0;
    std::uint64_t m_collision_sync_marker = 0;
    bool m_is_marked_for_des = false;
    std::uint32_t m_tags = 0;

    int m_team = 0;
    float m_mass = 0.f;
    float m_health = 1.f;
    float m_mana = 0.f;
    float m_facing_angle = 0.f;
    bool m_has_facing = false;
    SEntityStats m_entity_stats;
};
