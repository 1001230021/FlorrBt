#pragma once
#include "../../Engine/map_tools.h"
#include "../../Engine/spatial_hash_grid.h"
#include "../../Engine/logger.h"
#include "../../Shared/shared.h"
#include "gamecontroller.h"
#include "entity.h"
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

class CPlayer;

struct CActiveTickView
{
    sf::Vector2f center;
    float radius = 0.f;
};

class CGameWorld
{
  public:
    using entity_spatial_grid = CSpatialHashGrid<CEntity, int>;
    using wall_spatial_grid = CSpatialHashGrid<FlorrBtMap::Wall, int>;

    CGameWorld();
    explicit CGameWorld(const std::string& path);
    ~CGameWorld();

    int GetNewID();
    void FreeID(int id);

    class CGameContext* GameContext() const { return m_p_game_context; }
    void SetGameContext(class CGameContext* context) { m_p_game_context = context; }

    IGameController* GetController() const { return m_p_controller.get(); }
    void SetController(std::unique_ptr<IGameController> controller) { m_p_controller = std::move(controller); }

    CEntity* InsertEntity(std::unique_ptr<CEntity> entity);
    CEntity* InsertEntity(CEntity* entity);
    CEntity* InsertNonOwningEntity(CEntity* entity);

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

    const entity_spatial_grid& GetSpatialGrid() const { return m_spatial_grid; }
    float GetMaxEntityRadius() const { return m_normal_entity_radius_limit; }
    template <typename TVisitor> void ForEachEntityInEdgeRange(const sf::Vector2f& center, float edge_range,
                                                               TVisitor visitor) const
    {
        if (edge_range <= 0.f) return;

        const float query_radius = edge_range + m_normal_entity_radius_limit;
        m_spatial_grid.ForEachInRange(center, query_radius, [&](CEntity* entity)
        {
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
    bool SegmentBlockedByWall(sf::Vector2f start, sf::Vector2f end) const;
    bool CircleBlockedByWall(sf::Vector2f center, float radius) const;

  private:
    void RegisterLiveEntity(CEntity* entity);
    void UnregisterLiveEntity(CEntity* entity);
    void SyncLargeEntityMembership(CEntity* entity);
    void RemoveLargeEntity(CEntity* entity);
    void SpawnMapPortals();
    std::optional<sf::Vector2f> FindWarpPoint(const std::string& from) const;
    FlorrBtMap::Wall* GetWall(int id) const;
    void BuildWallGrid();
    void ResolveWallCollisions(const std::vector<CEntity*>& entities);
    void ResolveCollisions(const std::vector<CEntity*>& entities, float dt);
    void SyncSpatialGrid(const std::vector<CEntity*>& entities);
    void Cleanup();

    std::vector<int> m_free_ids;
    std::vector<std::uint8_t> m_id_in_use;
    int m_next_id = 0;
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
    std::vector<CEntity*> m_large_entities;
    std::vector<CEntity*> m_tick_entities;
    std::vector<CEntity*> m_active_entities;
    std::vector<CActiveTickView> m_active_tick_views;
    std::vector<CEntity*> m_collision_normal_entities;
    std::vector<std::pair<float, CEntity*>> m_collision_large_entities;
    std::vector<std::pair<int, std::uint64_t>> m_clear_owned_owner_keys;
    std::vector<std::pair<int, std::uint64_t>> m_clear_summon_owner_keys;
    std::unordered_set<int> m_wall_query_visited;
    std::uint64_t m_active_tick_marker = 1;
    float m_normal_entity_radius_limit = 1.f;
};
