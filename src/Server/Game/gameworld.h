#pragma once
#include "../../Engine/logger.h"
#include "../../Shared/shared.h"
#include "../../Shared/spatial_hash_grid.h"
#include "entity.h"
#include <memory>
#include <set>
#include <vector>

class CGameWorld
{
  public:
    CGameWorld();
    ~CGameWorld();

    int GetNewID();
    void FreeID(int id);

    CEntity* InsertEntity(std::unique_ptr<CEntity> entity);
    CEntity* InsertEntity(CEntity* entity);
    CEntity* InsertNonOwningEntity(CEntity* entity);

    void RemoveEntity(int id);

    void Tick(float dt);

    CEntity* GetEntity(int id) const;
    CEntity* FindClosestEntity(const sf::Vector2f& center, float max_range,
                               std::function<bool(const CEntity*)> filter = nullptr) const;

    std::vector<CEntity*> GetAllEntities() const
    {
        std::vector<CEntity*> result;
        result.reserve(m_p_entities.size() + m_p_entity_refs.size());
        for (const auto& entity : m_p_entities)
        {
            if (entity) result.push_back(entity.get());
        }
        for (CEntity* entity : m_p_entity_refs)
        {
            if (entity) result.push_back(entity);
        }
        return result;
    }

    const CSpatialHashGrid& GetSpatialGrid() const { return m_spatial_grid; }

  private:
    void Cleanup();

    std::set<int> m_free_ids;
    int m_next_id = 0;

    CSpatialHashGrid m_spatial_grid;

    std::vector<std::unique_ptr<CEntity>> m_p_entities;
    std::vector<CEntity*> m_p_entity_refs;
};