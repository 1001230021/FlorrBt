#include "spatial_hash_grid.h"
#include "../Server/Game/gameworld.h"
#include <cmath>
#include <limits>

namespace
{
constexpr float global_range = -1.0f;
}

CSpatialHashGrid::CSpatialHashGrid(CGameWorld* world, float cell_size) : m_cell_size(cell_size), m_p_world(world) {}

void CSpatialHashGrid::Clear()
{
    m_grid.clear();
}

void CSpatialHashGrid::Insert(CEntity* entity)
{
    if (!entity || entity->m_is_marked_for_des) return;

    int id = entity->m_id;
    if (id < 0) return;

    int cell_x = CellX(entity->m_pos.x);
    int cell_y = CellY(entity->m_pos.y);
    m_grid[HashCell(cell_x, cell_y)].push_back(id);
}

void CSpatialHashGrid::Rebuild(const std::vector<CEntity*>& entities)
{
    Clear();
    for (CEntity* entity : entities)
    {
        Insert(entity);
    }
}

CEntity* CSpatialHashGrid::FindClosest(const sf::Vector2f& center, float max_range,
                                       std::function<bool(const CEntity*)> filter) const
{
    CEntity* target = nullptr;
    float min_dist_sq = (max_range == global_range) ? std::numeric_limits<float>::max() : max_range * max_range;

    auto visit_entity = [&](int id)
    {
        CEntity* entity = m_p_world ? m_p_world->GetEntity(id) : nullptr;
        if (!entity || entity->m_is_marked_for_des) return;
        if (filter && !filter(entity)) return;

        float dist_sq = DistanceSq(entity->m_pos, center);
        if (dist_sq < min_dist_sq)
        {
            min_dist_sq = dist_sq;
            target = entity;
        }
    };

    if (max_range == global_range)
    {
        for (const auto& [cell, ids] : m_grid)
        {
            (void)cell;
            for (int id : ids)
            {
                visit_entity(id);
            }
        }
        return target;
    }

    int min_cell_x = CellX(center.x - max_range);
    int max_cell_x = CellX(center.x + max_range);
    int min_cell_y = CellY(center.y - max_range);
    int max_cell_y = CellY(center.y + max_range);

    for (int cell_x = min_cell_x; cell_x <= max_cell_x; ++cell_x)
    {
        for (int cell_y = min_cell_y; cell_y <= max_cell_y; ++cell_y)
        {
            auto it = m_grid.find(HashCell(cell_x, cell_y));
            if (it == m_grid.end()) continue;

            for (int id : it->second)
            {
                visit_entity(id);
            }
        }
    }
    return target;
}

std::vector<CEntity*> CSpatialHashGrid::QueryRange(const sf::Vector2f& center, float radius,
                                                   std::function<bool(const CEntity*)> filter) const
{
    std::vector<CEntity*> result;
    if (radius <= 0.f) return result;

    float radius_sq = radius * radius;
    int min_cell_x = CellX(center.x - radius);
    int max_cell_x = CellX(center.x + radius);
    int min_cell_y = CellY(center.y - radius);
    int max_cell_y = CellY(center.y + radius);

    for (int cell_x = min_cell_x; cell_x <= max_cell_x; ++cell_x)
    {
        for (int cell_y = min_cell_y; cell_y <= max_cell_y; ++cell_y)
        {
            auto it = m_grid.find(HashCell(cell_x, cell_y));
            if (it == m_grid.end()) continue;

            for (int id : it->second)
            {
                CEntity* entity = m_p_world ? m_p_world->GetEntity(id) : nullptr;
                if (!entity || entity->m_is_marked_for_des) continue;
                if (filter && !filter(entity)) continue;
                if (DistanceSq(entity->m_pos, center) <= radius_sq) result.push_back(entity);
            }
        }
    }
    return result;
}

std::uint64_t CSpatialHashGrid::HashCell(int cell_x, int cell_y) const
{
    return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(cell_x)) << 32) |
           static_cast<std::uint32_t>(cell_y);
}

int CSpatialHashGrid::CellX(float world_x) const
{
    return static_cast<int>(std::floor(world_x / m_cell_size));
}

int CSpatialHashGrid::CellY(float world_y) const
{
    return static_cast<int>(std::floor(world_y / m_cell_size));
}