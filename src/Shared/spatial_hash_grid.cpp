#include "spatial_hash_grid.h"
#include "../Server/Game/entity.h"
#include <cmath>
#include <limits>

namespace {
    constexpr float GLOBAL_RANGE = -1.0f;
}

CSpatialHashGrid::CSpatialHashGrid(float cellSize)
    : m_CellSize(cellSize)
{
}

void CSpatialHashGrid::Clear() {
    m_Grid.clear();
}

void CSpatialHashGrid::Insert(CEntity* entity) {
    if (!entity || entity->m_IsMarkedForDes) return;
    int cx = CellX(entity->m_Pos.x);
    int cy = CellY(entity->m_Pos.y);
    m_Grid[HashCell(cx, cy)].push_back(entity);
}

void CSpatialHashGrid::Rebuild(const std::vector<CEntity*>& entities) {
    Clear();
    for (CEntity* e : entities) {
        if (!e || e->m_IsMarkedForDes) continue;
        Insert(e);
    }
}

CEntity* CSpatialHashGrid::FindClosest(
    const sf::Vector2f& center,
    float maxRange,
    std::function<bool(const CEntity*)> filter) const
{
    CEntity* target = nullptr;
    float minDistSq = (maxRange == GLOBAL_RANGE)
        ? std::numeric_limits<float>::max()
        : maxRange * maxRange;

    if (maxRange == GLOBAL_RANGE) {
        for (const auto& pair : m_Grid) {
            for (CEntity* e : pair.second) {
                if (!e) continue;
                if (filter && !filter(e)) continue;
                sf::Vector2f diff = e->m_Pos - center;
                float distSq = diff.x * diff.x + diff.y * diff.y;
                if (distSq < minDistSq) {
                    minDistSq = distSq;
                    target = e;
                }
            }
        }
    }
    else {
        int minCX = CellX(center.x - maxRange);
        int maxCX = CellX(center.x + maxRange);
        int minCY = CellY(center.y - maxRange);
        int maxCY = CellY(center.y + maxRange);

        for (int cx = minCX; cx <= maxCX; ++cx) {
            for (int cy = minCY; cy <= maxCY; ++cy) {
                int hash = HashCell(cx, cy);
                auto it = m_Grid.find(hash);
                if (it == m_Grid.end()) continue;

                for (CEntity* e : it->second) {
                    if (!e) continue;
                    if (filter && !filter(e)) continue;
                    sf::Vector2f diff = e->m_Pos - center;
                    float distSq = diff.x * diff.x + diff.y * diff.y;
                    if (distSq < minDistSq) {
                        minDistSq = distSq;
                        target = e;
                    }
                }
            }
        }

        if (!target) {
            for (const auto& pair : m_Grid) {
                for (CEntity* e : pair.second) {
                    if (!e) continue;
                    if (filter && !filter(e)) continue;
                    sf::Vector2f diff = e->m_Pos - center;
                    float distSq = diff.x * diff.x + diff.y * diff.y;
                    if (distSq < minDistSq) {
                        minDistSq = distSq;
                        target = e;
                    }
                }
            }
        }
    }
    return target;
}

int CSpatialHashGrid::HashCell(int cx, int cy) const {
    return cx * 10000 + cy;
}

int CSpatialHashGrid::CellX(float worldX) const {
    return static_cast<int>(std::floor(worldX / m_CellSize));
}

int CSpatialHashGrid::CellY(float worldY) const {
    return static_cast<int>(std::floor(worldY / m_CellSize));
}