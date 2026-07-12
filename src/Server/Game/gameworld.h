#pragma once
#include "../../Engine/map_tools.h"
#include "../../Engine/spatial_hash_grid.h"
#include "../../Engine/logger.h"
#include "../../Shared/shared.h"
#include "gamecontroller.h"
#include "entity.h"
#include <memory>
#include <set>
#include <string>
#include <vector>

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

    void Tick(float dt);

    CEntity* GetEntity(int id) const;
    CEntity* FindClosestEntity(const sf::Vector2f& center, float max_range,
                               std::function<bool(const CEntity*)> filter = nullptr) const;
    CEntity* FindClosestEntityByEdge(const sf::Vector2f& center, float max_edge_range,
                                     std::function<bool(const CEntity*)> filter = nullptr) const;

    std::vector<CEntity*> GetAllEntities() const;

    const entity_spatial_grid& GetSpatialGrid() const { return m_spatial_grid; }
    const FlorrBtMap* GetMap() const { return m_map.get(); }
    const std::string& GetMapPath() const { return m_map_path; }
    bool SegmentBlockedByWall(sf::Vector2f start, sf::Vector2f end) const;

  private:
    FlorrBtMap::Wall* GetWall(int id) const;
    void BuildWallGrid();
    void ResolveWallCollisions(const std::vector<CEntity*>& entities);
    void ResolveCollisions(const std::vector<CEntity*>& entities, float dt);
    void Cleanup();

    std::set<int> m_free_ids;
    int m_next_id = 0;
    CGameContext* m_p_game_context = nullptr;

    std::unique_ptr<FlorrBtMap> m_map;
    std::string m_map_path;

    entity_spatial_grid m_spatial_grid;
    wall_spatial_grid m_wall_grid;
    float m_max_wall_query_radius = 0.f;
    std::unique_ptr<IGameController> m_p_controller = nullptr;

    std::vector<std::unique_ptr<CEntity>> m_p_entities;
    std::vector<CEntity*> m_p_entity_refs;
};
