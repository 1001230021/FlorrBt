#pragma once
#include "../../Engine/logger.h"
#include "../../Engine/map_tools.h"
#include "../../Engine/spatial_hash_grid.h"
#include "../../Shared/shared.h"
#include "entity.h"
#include "gamecontroller.h"
#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

class CPlayer;
class CMobBase;

struct CActiveTickView
{
    sf::Vector2f center;
    float radius = 0.f;
};

struct CPlayerTickView
{
    sf::Vector2f center;
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
        EntityUpdate,
        ControllerAndSync,
        WallResolution,
        EntityCollision,
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

    int GetNewID();
    void FreeID(int id);

    class CGameContext* GameContext() const { return m_p_game_context; }
    void SetGameContext(class CGameContext* context) { m_p_game_context = context; }

    IGameController* GetController() const { return m_p_controller.get(); }
    void SetController(std::unique_ptr<IGameController> controller);

    CEntity* InsertEntity(std::unique_ptr<CEntity> entity);
    CEntity* InsertEntity(CEntity* entity);
    CEntity* InsertNonOwningEntity(CEntity* entity);
    CEntity* InsertEntityWithIdentity(std::unique_ptr<CEntity> entity, int id, std::uint64_t generation);
    void ClearEntitiesForRestore();
    void FinalizeEntityRestore();

    void RemoveEntity(int id);
    void DestroyProjectilesOwnedBy(int owner_id);
    void DestroySummonedMobsOwnedBy(int owner_id, std::uint64_t owner_generation = 0);
    CEntity* TransferPlayerEntityToWorld(CPlayer& player, CGameWorld& target_world,
                                         std::optional<sf::Vector2f> target_pos = std::nullopt,
                                         std::optional<std::string> from = std::nullopt);
    CEntity* TransferPlayerEntityToWorld(CPlayer& player, CGameWorld& target_world, const std::string& from);

    void Tick(float dt);

    CEntity* GetEntity(int id) const;
    CEntity* GetEntity(int id, std::uint64_t generation) const;
    void QueueEntityForCleanup(CEntity* entity);
    void QueuePsionicDamage(CMobBase* receiver, float damage, CEntity* attacker, EDamageType damage_type);
    CEntity* FindClosestEntity(const sf::Vector2f& center, float max_range,
                               std::function<bool(const CEntity*)> filter = nullptr) const;
    CEntity* FindClosestEntityByEdge(const sf::Vector2f& center, float max_edge_range,
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
    std::size_t GetLastActiveEntityCount() const { return m_active_entities.size(); }
    const STickPhaseTelemetry& GetTickPhaseTelemetry() const { return m_tick_phase_telemetry; }

    const entity_spatial_grid& GetSpatialGrid() const { return m_spatial_grid; }
    float GetMaxEntityRadius() const { return m_normal_entity_radius_limit; }
    std::size_t GetPlayerCount() const;
    template <typename TVisitor>
    void ForEachEntityInEdgeRange(const sf::Vector2f& center, float edge_range, TVisitor visitor) const
    {
        if (edge_range <= 0.f) return;

        const float query_radius = edge_range + m_normal_entity_radius_limit;
        m_spatial_grid.ForEachInRange(center, query_radius, [&](CEntity* entity) {
            if (!entity || entity->m_radius > m_normal_entity_radius_limit) return;
            const float reach = edge_range + std::max(0.f, entity->m_radius);
            if (DistanceSq(center, entity->m_pos) <= reach * reach) visitor(entity);
        });

        const size_t large_count = m_large_entities.size();
        for (size_t i = 0; i < large_count; ++i)
        {
            CEntity* entity = m_large_entities[i];
            if (!entity || entity->GameWorld() != this) continue;
            const float reach = edge_range + std::max(0.f, entity->m_radius);
            if (DistanceSq(center, entity->m_pos) <= reach * reach) visitor(entity);
        }
    }
    const FlorrBtMap* GetMap() const { return m_map.get(); }
    const std::string& GetMapPath() const { return m_map_path; }
    std::string GetMapName() const;
    ERarity GetSpawnZoneRarity(const sf::Vector2f& pos) const;
    bool SegmentBlockedByWall(sf::Vector2f start, sf::Vector2f end) const;
    bool CircleBlockedByWall(sf::Vector2f center, float radius) const;
    bool SweptCircleBlockedByWall(sf::Vector2f start, sf::Vector2f end, float radius) const;

  private:
    struct SPsionicReceiverKey
    {
        int entity_id = -1;
        std::uint64_t entity_generation = 0;

        bool operator==(const SPsionicReceiverKey& other) const
        {
            return entity_id == other.entity_id && entity_generation == other.entity_generation;
        }
    };

    struct SPsionicReceiverKeyHash
    {
        std::size_t operator()(const SPsionicReceiverKey& key) const
        {
            std::size_t seed = std::hash<int>{}(key.entity_id);
            seed ^= std::hash<std::uint64_t>{}(key.entity_generation) + 0x9e3779b9U + (seed << 6U) + (seed >> 2U);
            return seed;
        }
    };

    struct SPsionicSourceKey
    {
        CEntity* attacker = nullptr;
        EDamageType damage_type = EDamageType::Normal;

        bool operator==(const SPsionicSourceKey& other) const
        {
            return attacker == other.attacker && damage_type == other.damage_type;
        }
    };

    struct SPsionicSourceKeyHash
    {
        std::size_t operator()(const SPsionicSourceKey& key) const
        {
            std::size_t seed = std::hash<CEntity*>{}(key.attacker);
            seed ^= std::hash<int>{}(static_cast<int>(key.damage_type)) + 0x9e3779b9U + (seed << 6U) + (seed >> 2U);
            return seed;
        }
    };

    struct SPsionicDamageKey
    {
        int entity_id = -1;
        std::uint64_t entity_generation = 0;
        CEntity* attacker = nullptr;
        EDamageType damage_type = EDamageType::SharedNormal;

        bool operator==(const SPsionicDamageKey& other) const
        {
            return entity_id == other.entity_id && entity_generation == other.entity_generation &&
                   attacker == other.attacker && damage_type == other.damage_type;
        }
    };

    struct SPsionicDamageKeyHash
    {
        std::size_t operator()(const SPsionicDamageKey& key) const
        {
            std::size_t seed = std::hash<int>{}(key.entity_id);
            seed ^= std::hash<std::uint64_t>{}(key.entity_generation) + 0x9e3779b9U + (seed << 6U) + (seed >> 2U);
            seed ^= std::hash<CEntity*>{}(key.attacker) + 0x9e3779b9U + (seed << 6U) + (seed >> 2U);
            seed ^= std::hash<int>{}(static_cast<int>(key.damage_type)) + 0x9e3779b9U + (seed << 6U) + (seed >> 2U);
            return seed;
        }
    };

    struct SPendingPsionicDamage
    {
        sf::Vector2f origin = { 0.f, 0.f };
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
    std::optional<sf::Vector2f> FindWarpPoint(const std::string& from) const;
    FlorrBtMap::Wall* GetWall(int id) const;
    void BuildWallGrid();
    void CollectActiveEntitiesForTick();
    void TickActiveEntities(float dt);
    void FlushPendingPsionicDamage();
    void RemoveTransferredActiveEntities();
    void ResolveWallCollisions(const std::vector<CEntity*>& entities);
    void ResolveCollisions(const std::vector<CEntity*>& entities, float dt);
    void SyncSpatialPositions(const std::vector<CEntity*>& entities);
    void SyncSpatialPositionsAndMemberships(const std::vector<CEntity*>& entities);
    void Cleanup();

    std::vector<int> m_free_ids;
    std::vector<std::uint8_t> m_id_in_use;
    int m_next_id = 0;
    std::uint32_t m_world_id = 0;
    std::uint64_t m_next_generation = 1;
    CGameContext* m_p_game_context = nullptr;

    std::unique_ptr<FlorrBtMap> m_map;
    std::string m_map_path;

    entity_spatial_grid m_spatial_grid;
    wall_spatial_grid m_wall_grid;
    std::unique_ptr<IGameController> m_p_controller = nullptr;

    std::vector<std::unique_ptr<CEntity>> m_p_entities;
    std::vector<CEntity*> m_p_entity_refs;
    std::vector<CEntity*> m_live_entities;
    std::vector<CEntity*> m_cleanup_entities;
    std::vector<CEntity*> m_always_tick_entities;
    std::vector<CEntity*> m_conditional_tick_entities;
    std::vector<CEntity*> m_large_entities;
    std::vector<CEntity*> m_active_entities;
    std::vector<CActiveTickView> m_active_tick_views;
    std::vector<CPlayerTickView> m_player_tick_views;
    std::vector<CEntity*> m_collision_normal_entities;
    std::vector<std::pair<float, CEntity*>> m_collision_large_entities;
    std::vector<CEntity*> m_collision_inactive_entities;
    std::vector<std::pair<int, std::uint64_t>> m_clear_owned_owner_keys;
    std::vector<std::pair<int, std::uint64_t>> m_clear_summon_owner_keys;
    std::unordered_map<SPsionicReceiverKey, SPendingPsionicDamage, SPsionicReceiverKeyHash>
        m_pending_psionic_damage;
    std::unordered_set<int> m_wall_query_visited;
    std::uint64_t m_active_tick_marker = 1;
    float m_normal_entity_radius_limit = 1.f;
    STickPhaseTelemetry m_tick_phase_telemetry;
};
