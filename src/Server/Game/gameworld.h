#pragma once
#include "../../Engine/logger.h"
#include "../../Shared/shared.h"
#include "../../Shared/spatial_hash_grid.h"
#include "entity.h"
#include <set>
#include <vector>
#include <memory>

class CGameWorld
{
  public:
    CGameWorld();
    ~CGameWorld();

    int GetNewID();
    void FreeID(int id);

    void InsertEntity(std::unique_ptr<CEntity> entity);
    void InsertEntity(CEntity* entity);
    void InsertNonOwningEntity(CEntity* entity);

    void RemoveEntity(int id);

    void Tick(float dt);

    CEntity* GetEntity(int id) const;
    CEntity* FindClosestEntity(const sf::Vector2f& center, float maxRange,
                               std::function<bool(const CEntity*)> filter = nullptr) const;

    std::vector<CEntity*> GetAllEntities() const
    {
        std::vector<CEntity*> res;
        res.reserve(m_pEntities.size());
        for (const auto& p : m_pEntities)
            res.push_back(p.get());
        return res;
    }

    const CSpatialHashGrid& GetSpatialGrid() const
    {
        return m_SpatialGrid;
    }

  private:
    void Cleanup();

    std::set<int> m_FreeIDs;
    int m_NextID = 0;

    CSpatialHashGrid m_SpatialGrid;

    std::vector<std::unique_ptr<CEntity>> m_pEntities;

    std::vector<CEntity*> m_pEntityRefs;
};
