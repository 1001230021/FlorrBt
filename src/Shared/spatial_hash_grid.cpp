#include "spatial_hash_grid.h"
#include "../Server/Game/entity.h"
#include <limits>
#include <cmath>
#include "../Server/Game/gameworld.h"
#include <cmath>
#include <limits>

namespace
{
constexpr float GLOBAL_RANGE = -1.0f;
}

CSpatialHashGrid::CSpatialHashGrid(CGameWorld* world, float cellSize) : m_CellSize(cellSize), m_pWorld(world) {}

void CSpatialHashGrid::Clear()
{
    m_Grid.clear();
}

void CSpatialHashGrid::Insert(CEntity* entity)
{
    if (!entity || entity->m_IsMarkedForDes)
        return;
    int cx = CellX(entity->m_Pos.x);
    int cy = CellY(entity->m_Pos.y);
    int id = entity->m_ID;
    if (id < 0)
        return;
    m_Grid[HashCell(cx, cy)].push_back(id);
}

void CSpatialHashGrid::Rebuild(const std::vector<CEntity*>& entities)
{
    Clear();
    for (CEntity* e : entities)
    {
        if (!e || e->m_IsMarkedForDes)
            continue;
        Insert(e);
    }
}

CEntity* CSpatialHashGrid::FindClosest(const sf::Vector2f& center, float maxRange,
                                       std::function<bool(const CEntity*)> filter) const
{
    CEntity* target = nullptr;
    float minDistSq = (maxRange == GLOBAL_RANGE) ? std::numeric_limits<float>::max() : maxRange * maxRange;

    if (maxRange == GLOBAL_RANGE)
    {
        for (const auto& pair : m_Grid)
        {
            for (int id : pair.second)
            {
                CEntity* e = m_pWorld ? m_pWorld->GetEntity(id) : nullptr;
                if (!e)
                    continue;
                if (e->m_IsMarkedForDes)
                    continue;
                if (filter && !filter(e))
                    continue;
                sf::Vector2f diff = e->m_Pos - center;
                float distSq = diff.x * diff.x + diff.y * diff.y;
                if (distSq < minDistSq)
                {
                    minDistSq = distSq;
                    target = e;
                }
            }
        }
    }
    else
    {
        int minCX = CellX(center.x - maxRange);
        int maxCX = CellX(center.x + maxRange);
        int minCY = CellY(center.y - maxRange);
        int maxCY = CellY(center.y + maxRange);

        for (int cx = minCX; cx <= maxCX; ++cx)
        {
            for (int cy = minCY; cy <= maxCY; ++cy)
            {
                int hash = HashCell(cx, cy);
                auto it = m_Grid.find(hash);
                if (it == m_Grid.end())
                    continue;

                for (int id : it->second)
                {
                    CEntity* e = m_pWorld ? m_pWorld->GetEntity(id) : nullptr;
                    if (!e)
                        continue;
                    if (e->m_IsMarkedForDes)
                        continue;
                    if (filter && !filter(e))
                        continue;
                    sf::Vector2f diff = e->m_Pos - center;
                    float distSq = diff.x * diff.x + diff.y * diff.y;
                    if (distSq < minDistSq)
                    {
                        minDistSq = distSq;
                        target = e;
                    }
                }
            }
        }

        if (!target)
        {
            for (const auto& pair : m_Grid)
            {
                for (int id : pair.second)
                {
                    CEntity* e = m_pWorld ? m_pWorld->GetEntity(id) : nullptr;
                    if (!e)
                        continue;
                    if (e->m_IsMarkedForDes)
                        continue;
                    if (filter && !filter(e))
                        continue;
                    sf::Vector2f diff = e->m_Pos - center;
                    float distSq = diff.x * diff.x + diff.y * diff.y;
                    if (distSq < minDistSq)
                    {
                        minDistSq = distSq;
                        target = e;
                    }
                }
            }
        }
    }
    return target;
}

std::vector<CEntity*> CSpatialHashGrid::QueryRange(const sf::Vector2f& center, float radius,
                                                   std::function<bool(const CEntity*)> filter) const
{
    std::vector<CEntity*> result;
    if (radius <= 0.f)
        return result;

    float rsq = radius * radius;
    int cx = CellX(center.x);
    int cy = CellY(center.y);

    for (int dx = -1; dx <= 1; ++dx)
    {
        for (int dy = -1; dy <= 1; ++dy)
        {
            int hash = HashCell(cx + dx, cy + dy);
            auto it = m_Grid.find(hash);
            if (it == m_Grid.end())
                continue;

            for (int id : it->second)
            {
                CEntity* e = m_pWorld ? m_pWorld->GetEntity(id) : nullptr;
                if (!e)
                    continue;
                if (e->m_IsMarkedForDes)
                    continue;
                if (filter && !filter(e))
                    continue;

                sf::Vector2f diff = e->m_Pos - center;
                float distSq = diff.x * diff.x + diff.y * diff.y;
                if (distSq <= rsq)
                    result.push_back(e);
            }
        }
    }
    return result;
}

int CSpatialHashGrid::HashCell(int cx, int cy) const
{
    return cx * 10000 + cy;
}

int CSpatialHashGrid::CellX(float worldX) const
{
    return static_cast<int>(std::floor(worldX / m_CellSize));
}

int CSpatialHashGrid::CellY(float worldY) const
{
    return static_cast<int>(std::floor(worldY / m_CellSize));
}
