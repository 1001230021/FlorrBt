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
        for (const auto& entity : m_p_entities)
        {
            if (entity) visitor(entity.get());
        }
        for (CEntity* entity : m_p_entity_refs)
        {
            if (entity) visitor(entity);
        }
    }

    void CollectEntities(std::vector<CEntity*>& result) const;
    std::vector<CEntity*> GetAllEntities() const;

    const entity_spatial_grid& GetSpatialGrid() const { return m_spatial_grid; }
    float GetMaxEntityRadius() const { return m_cached_max_entity_radius; }
    const FlorrBtMap* GetMap() const { return m_map.get(); }
    const std::string& GetMapPath() const { return m_map_path; }
    std::string GetMapName() const;
    bool SegmentBlockedByWall(sf::Vector2f start, sf::Vector2f end) const;
    bool CircleBlockedByWall(sf::Vector2f center, float radius) const;

  private:
    void SpawnMapPortals();
    std::optional<sf::Vector2f> FindWarpPoint(const std::string& from) const;
    FlorrBtMap::Wall* GetWall(int id) const;
    void BuildWallGrid();
    void ResolveWallCollisions(const std::vector<CEntity*>& entities);
    void ResolveCollisions(const std::vector<CEntity*>& entities, float dt);
    void Cleanup();

    std::vector<int> m_free_ids;
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
    std::vector<CEntity*> m_tick_entities;
    std::vector<CEntity*> m_active_entities;
    std::unordered_set<int> m_wall_query_visited;
    std::uint64_t m_active_tick_marker = 1;
    float m_cached_max_entity_radius = 0.f;
};
