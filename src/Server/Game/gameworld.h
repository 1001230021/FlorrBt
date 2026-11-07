#pragma once
#include "../../Engine/logger.h"
#include "../../Engine/map_tools.h"
#include "../../Shared/math_types.h"
#include "../../Shared/swept_collision.h"
#include "../../Shared/spawn_dominance.h"
#include "../../Engine/spatial_hash_grid.h"
#include "../../Shared/shared.h"
#include "entity.h"
#include "gamecontroller.h"
#include "extensions/extension_runtime.h"
#include "systems/environment_system.h"
#include "systems/world_terrain.h"
#include "systems/combat_events.h"
#include <array>
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

class CPlayer;
class CMobBase;
class CChainEntity;

struct CActiveTickView
{
    florrbt::Vector2f center;
    float radius = 0.f;
};

struct CPlayerTickView
{
    florrbt::Vector2f center;
    float horizon = 0.f;
    float radius = 0.f;
};

class CGameWorld
{
  public:
    using entity_spatial_grid = CSpatialHashGrid<CEntity, int>;
    using wall_spatial_grid = CSpatialHashGrid<FlorrBtMap::Wall, int>;

    enum class ETickPhase : std::uint8_t
    {
        ActiveCollection,
        Rules,
        Begin,
        Intent,
        Forces,
        Motion,
        Constraints,
        Environment,
        Actions,
        Requests,
        EntityCollision,
        Effects,
        Cleanup,
        Count,
    };

    static constexpr std::size_t tick_phase_count = static_cast<std::size_t>(ETickPhase::Count);

    struct STickPhaseTelemetry
    {
        std::uint64_t samples = 0;
        std::array<double, tick_phase_count> last_ms{};
        std::array<double, tick_phase_count> total_ms{};
        std::array<double, tick_phase_count> max_ms{};

        double AverageMs(ETickPhase phase) const
        {
            const std::size_t index = static_cast<std::size_t>(phase);
            return samples > 0 && index < tick_phase_count ? total_ms[index] / static_cast<double>(samples) : 0.0;
        }
    };

    CGameWorld();
    explicit CGameWorld(const std::string& path, std::uint32_t world_id = 0);
    ~CGameWorld();
    std::uint32_t GetId() const { return m_world_id; }
    // 房间 ID 可复用，调试与编辑引用须同时携带实例代数。
    std::uint64_t GetInstanceGeneration() const { return m_instance_generation; }
    bool RestoreInstanceGeneration(std::uint64_t generation);
    std::uint64_t NextEntityGeneration() const { return m_next_generation; }
    bool RestoreNextEntityGeneration(std::uint64_t generation);

    int GetNewID();
    void FreeID(int id);

    class CGameContext* GameContext() const { return m_p_game_context; }
    void SetGameContext(class CGameContext* context);
    const CExtensionView& Extensions() const { return m_extensions; }
    const SWorldExtensionConfig& ExtensionConfig() const { return m_extension_config; }
    void ConfigureExtensions(SWorldExtensionConfig config);

    IGameController* GetController() const { return m_p_controller.get(); }
    CWorldTerrain& Terrain() { return m_terrain; }
    const CWorldTerrain& Terrain() const { return m_terrain; }
    CCombatEventSystem& CombatEvents() { return m_combat_events; }
    bool IsCombatEventActive() const { return m_combat_events.IsActive(); }
    SControllerChangeResult SetController(std::unique_ptr<IGameController> controller, bool notify_players = true);
    bool IsRecovering() const { return !m_recovery_error.empty(); }
    const std::string& RecoveryError() const { return m_recovery_error; }
    void EnterRecovery(std::string error);
    // 借用会话视图，包含身体已移除、等待重生的玩家。
    std::vector<CPlayer*> Players() const;

    CEntity* InsertEntity(std::unique_ptr<CEntity> entity);
    CEntity* InsertEntity(CEntity* entity);
    CEntity* InsertEntityWithIdentity(std::unique_ptr<CEntity> entity, int id, std::uint64_t generation);
    void ClearEntitiesForRestore();
    bool ReloadMapForRestore(std::string& error);
    void FinalizeEntityRestore();
    // 持久化快照发布前完成级联移除。
    void FlushEntityRemovals();
    bool CanSimulateThisTick(const CEntity& entity) const
    { return entity.GameWorld() == this && (!m_tick_active || entity.BirthTickMarker() != m_active_tick_marker); }

    void RemoveEntity(int id);
    void DestroyProjectilesOwnedBy(const SEntityHandle& owner_ref);
    void DestroySummonedMobsOwnedBy(const SEntityHandle& owner_ref);
    SPlayerTransitionResult TransferPlayerEntityToWorld(CPlayer& player, CGameWorld& target_world,
                                         std::optional<florrbt::Vector2f> target_pos = std::nullopt,
                                         std::optional<std::string> from = std::nullopt);
    SPlayerTransitionResult TransferPlayerEntityToWorld(CPlayer& player, CGameWorld& target_world, const std::string& from);
    bool IsTransitionActive() const { return m_transition_active; }

    void Tick(float dt);
    const CEnvironmentSystem& Environment() const { return m_environment; }
    bool IsTicking() const { return m_tick_active; }
    void ConstrainControlledMotion(CMobBase& mob, florrbt::Vector2f start);
    void ApplyChainConstraints(float dt);
    void BreakChainsForEndpoint(const CEntity& endpoint);

    CEntity* GetEntity(int id) const;
    CEntity* GetEntity(int id, std::uint64_t generation) const;
    CEntity* ResolveEntity(const SEntityHandle& handle) const;
    CEntity* GetEntity(const SEntityHandle& handle) const;
    bool TryRegisterCarriedEntity(const CEntity& carried, const CEntity& carrier);
    void UnregisterCarriedEntity(const SEntityHandle& carried, const SEntityHandle& carrier);
    bool IsCarriedEntity(const CEntity& carried);
    void ReleaseCarriedRelations(CEntity& entity);
    void QueueEntityForCleanup(CEntity* entity);
    void QueuePsionicDamage(CMobBase* receiver, float damage, CEntity* attacker, EDamageType damage_type);
    void RecordCombatDamage(const CEntity& target, float amount, EDamageType kind, CEntity* source);
    const std::vector<ServerMessage::SCombatHit>& GetCombatFeedback() const { return m_combat_feedback; }
    void ClearCombatFeedback() { m_combat_feedback.clear(); m_combat_feedback_indices.clear(); }
    CEntity* FindClosestEntity(const florrbt::Vector2f& center, float max_range,
                               std::function<bool(const CEntity*)> filter = nullptr) const;
    CEntity* FindClosestEntityByEdge(const florrbt::Vector2f& center, float max_edge_range,
                                     std::function<bool(const CEntity*)> filter = nullptr) const;

    template <typename TVisitor> void ForEachEntity(TVisitor visitor) const
    {
        const size_t count = m_live_entities.size();
        for (size_t i = 0; i < count; ++i)
            if (CEntity* entity = m_live_entities[i]) visitor(entity);
    }

    void CollectEntities(std::vector<CEntity*>& result) const;
    std::vector<CEntity*> GetAllEntities() const;
    std::size_t GetEntityCount() const { return m_live_entities.size(); }
    std::size_t GetLastActiveEntityCount() const { return m_last_active_entity_count; }
    const STickPhaseTelemetry& GetTickPhaseTelemetry() const { return m_tick_phase_telemetry; }

    const entity_spatial_grid& GetSpatialGrid() const { return m_spatial_grid; }
    float GetMaxEntityRadius() const { return m_normal_entity_radius_limit; }
    std::size_t GetPlayerCount() const;
    template <typename TVisitor>
    void ForEachEntityInEdgeRange(const florrbt::Vector2f& center, float edge_range, TVisitor visitor) const
    {
        if (edge_range <= 0.f) return;

        const float query_radius = edge_range + m_normal_entity_radius_limit;
        m_spatial_grid.ForEachInRange(center, query_radius, [&](CEntity* entity) {
            if (!entity || entity->Radius() > m_normal_entity_radius_limit) return;
            const float reach = edge_range + std::max(0.f, entity->Radius());
            if (DistanceSq(center, entity->Pos()) <= reach * reach) visitor(entity);
        });

        const size_t large_count = m_large_entities.size();
        for (size_t i = 0; i < large_count; ++i)
        {
            CEntity* entity = m_large_entities[i];
            if (!entity || entity->GameWorld() != this) continue;
            const float reach = edge_range + std::max(0.f, entity->Radius());
            if (DistanceSq(center, entity->Pos()) <= reach * reach) visitor(entity);
        }
    }
    const FlorrBtMap* GetMap() const { return m_map.get(); }
    const std::string& GetMapPath() const { return m_map_path; }
    std::string GetMapName() const;
    ERarity GetSpawnZoneRarity(const florrbt::Vector2f& pos) const;
    int GetMobSpawnRarityExcess(const CMobBase& mob) const;
    bool SegmentBlockedByWall(florrbt::Vector2f start, florrbt::Vector2f end) const;
    bool CircleBlockedByWall(florrbt::Vector2f center, float radius, EWallQuery mask = EWallQuery::All) const;
    bool SweptCircleBlockedByWall(florrbt::Vector2f start, florrbt::Vector2f end, float radius,
                                  EWallQuery mask = EWallQuery::All) const;
    std::optional<florrbt::Vector2f> NearestUnblockedPosition(florrbt::Vector2f from, float radius) const;
    // 删除与矩形相交的墙体，记录世界级历史并重建碰撞索引。
    int RemoveWallsInRect(florrbt::Vector2f min_pos, florrbt::Vector2f max_pos);

    // 从 origin 逐次命中最近的合格目标，每个目标只命中一次；伤害归属 owner，视效只广播本世界。
    void ChainLightning(CEntity& origin, float damage, int bounces, CEntity* owner,
                        const std::function<bool(const CMobBase&)>& target_filter = {});
    // 魔法闪电分别限制增幅跳数和伤害跳数，优先寻找增幅源，找不到时尝试敌方伤害目标。
    // 敌方增幅源先承受当前伤害，再把自身闪电强度加到链上。
    void MagicChainLightning(CEntity& origin, float damage, int boost_bounces, int damage_bounces, CEntity* owner,
                             const std::function<bool(const CEntity&)>& boost_filter = {},
                             const std::function<float(const CEntity&)>& boost_reader = {},
                             const std::function<bool(const CMobBase&)>& damage_filter = {});

  private:
    friend class CWorldTransfer;
    friend class CServerTestAccess;
    bool m_transition_active = false;
    bool m_tick_active = false;
    std::string m_recovery_error;
    // 故障注入仅供离线回归夹具使用。
    int m_transfer_fault_after_insert = -1;
    CExtensionView m_extensions;
    SWorldExtensionConfig m_extension_config;
    inline static std::atomic<std::uint64_t> s_next_instance_generation{ 0 };
    std::uint64_t m_instance_generation = ++s_next_instance_generation;
    struct SPsionicReceiverKey
    {
        SEntityHandle entity_ref;

        bool operator==(const SPsionicReceiverKey& other) const
        {
            return entity_ref == other.entity_ref;
        }
    };

    struct SPsionicReceiverKeyHash
    {
        std::size_t operator()(const SPsionicReceiverKey& key) const
        {
            return SEntityHandleHash{}(key.entity_ref);
        }
    };

    struct SPsionicSourceKey
    {
        SEntityHandle attacker_ref;
        EDamageType damage_type = EDamageType::Normal;

        bool operator==(const SPsionicSourceKey& other) const
        {
            return attacker_ref == other.attacker_ref && damage_type == other.damage_type;
        }
    };

    struct SPsionicSourceKeyHash
    {
        std::size_t operator()(const SPsionicSourceKey& key) const
        {
            std::size_t seed = SEntityHandleHash{}(key.attacker_ref);
            seed ^= std::hash<int>{}(static_cast<int>(key.damage_type)) + 0x9e3779b9U + (seed << 6U) + (seed >> 2U);
            return seed;
        }
    };

    struct SPsionicDamageKey
    {
        SEntityHandle entity_ref;
        SEntityHandle attacker_ref;
        EDamageType damage_type = EDamageType::SharedNormal;

        bool operator==(const SPsionicDamageKey& other) const
        {
            return entity_ref == other.entity_ref && attacker_ref == other.attacker_ref &&
                   damage_type == other.damage_type;
        }
    };

    struct SPsionicDamageKeyHash
    {
        std::size_t operator()(const SPsionicDamageKey& key) const
        {
            std::size_t seed = SEntityHandleHash{}(key.entity_ref);
            seed ^= SEntityHandleHash{}(key.attacker_ref) + 0x9e3779b9U + (seed << 6U) + (seed >> 2U);
            seed ^= std::hash<int>{}(static_cast<int>(key.damage_type)) + 0x9e3779b9U + (seed << 6U) + (seed >> 2U);
            return seed;
        }
    };

    struct SPendingPsionicDamage
    {
        florrbt::Vector2f origin = { 0.f, 0.f };
        int team = 0;
        std::unordered_map<SPsionicSourceKey, float, SPsionicSourceKeyHash> damage_by_source;
    };

    void RegisterLiveEntity(CEntity* entity);
    void UnregisterLiveEntity(CEntity* entity);
    void RemoveQueuedCleanupEntity(CEntity* entity);
    void SyncAlwaysTickMembership(CEntity* entity);
    void RemoveAlwaysTickEntity(CEntity* entity);
    void SyncConditionalTickMembership(CEntity* entity);
    void RemoveConditionalTickEntity(CEntity* entity);
    void SyncLargeEntityMembership(CEntity* entity);
    void RemoveLargeEntity(CEntity* entity);
    void SpawnMapPortals();
    std::optional<florrbt::Vector2f> FindWarpPoint(const std::string& from) const;
    FlorrBtMap::Wall* GetWall(int id) const;
    void BuildWallGrid();
    void CollectActiveEntitiesForTick();
    void RetireInactiveMobs(float elapsed);
    void TickEntityPhase(CEntity::ETickPhase phase, float dt);
    void FlushPendingPsionicDamage();
    void RemoveTransferredActiveEntities();
    void ResolveWallCollisions(std::span<CEntity* const> entities, bool sweep_motion = true);
    void ResolveBodyGeometry(const std::vector<CEntity*>& entities, float dt);
    void ResolveCollisions(const std::vector<CEntity*>& entities, float dt);
    void SyncSpatialPositions(const std::vector<CEntity*>& entities);
    void SyncSpatialPositionsAndLargeMemberships(const std::vector<CEntity*>& entities);
    void SyncSpatialPositionsAndMemberships(const std::vector<CEntity*>& entities);
    void Cleanup();
    void ClearFrameViews();

    std::vector<int> m_free_ids;
    std::vector<std::uint8_t> m_id_in_use;
    int m_next_id = 0;
    std::uint32_t m_world_id = 0;
    std::uint64_t m_next_generation = 1;
    CGameContext* m_p_game_context = nullptr;

    std::unique_ptr<FlorrBtMap> m_map;
    CWorldTerrain m_terrain{*this};
    CCombatEventSystem m_combat_events{*this};
    std::string m_map_path;
    std::vector<florrbt::SpawnZoneBounds> m_spawn_zone_bounds;
    struct SHabitatCache
    {
        bool valid = false;
        std::uint64_t generation = 0;
        florrbt::Vector2f position{};
        std::optional<float> difficulty;
    };
    mutable std::unordered_map<int, SHabitatCache> m_spawn_habitat_cache;

    entity_spatial_grid m_spatial_grid;
    wall_spatial_grid m_wall_grid;
    CEnvironmentSystem m_environment;
    std::unique_ptr<IGameController> m_p_controller = nullptr;

    // m_p_entities 是唯一所有权容器；下列同世界索引只借用指针，统一由注册/注销入口维护。
    std::vector<std::unique_ptr<CEntity>> m_p_entities;
    std::vector<CEntity*> m_live_entities;
    std::vector<CChainEntity*> m_chains;
    std::vector<CEntity*> m_cleanup_entities;
    std::vector<CEntity*> m_always_tick_entities;
    std::vector<CEntity*> m_conditional_tick_entities;
    std::vector<CEntity*> m_large_entities;
    std::vector<CEntity*> m_active_entities;
    std::size_t m_last_active_entity_count = 0;
    std::vector<CEntity*> m_owner_driven_entities;
    std::vector<CActiveTickView> m_active_tick_views;
    std::vector<CPlayerTickView> m_player_tick_views;
    std::vector<CEntity*> m_collision_inactive_entities;
    std::vector<std::pair<CMobBase*, CMobBase*>> m_collision_mob_contacts;
    struct SContactSweepEntry
    {
        florrbt::SweptCircleBounds bounds;
        CEntity* entity = nullptr;
        int id = -1;
        bool active = false;
        float prefix_max_x = 0.f;
    };
    std::vector<SContactSweepEntry> m_contact_sweep;
    struct SBodyContactSweepEntry : SContactSweepEntry
    {
        SEntityHandle handle;
        florrbt::Vector2f position;
        float radius = 0.f;
    };
    // 只保存每个参与者的几何快照，密集接触不会占用 O(接触对数) 的缓存。
    std::vector<SBodyContactSweepEntry> m_body_contact_sweep;
    std::vector<SEntityHandle> m_clear_owned_owner_keys;
    std::vector<SEntityHandle> m_clear_summon_owner_keys;
    std::unordered_map<SPsionicReceiverKey, SPendingPsionicDamage, SPsionicReceiverKeyHash>
        m_pending_psionic_damage;
    struct SCarriedEntityRecord
    {
        SEntityHandle carrier;
    };
    std::unordered_map<SEntityHandle, SCarriedEntityRecord, SEntityHandleHash> m_carried_entities;
    std::unordered_set<int> m_wall_query_visited;
    std::uint64_t m_active_tick_marker = 1;
    std::uint64_t m_collision_sync_marker = 1;
    float m_retire_sweep_timer = 0.f;
    float m_normal_entity_radius_limit = 1.f;
    STickPhaseTelemetry m_tick_phase_telemetry;
    std::vector<ServerMessage::SCombatHit> m_combat_feedback;
    std::unordered_map<std::uint64_t, size_t> m_combat_feedback_indices;
};
