#pragma once
#include "../Shared/tools.h"
#include <SFML/System/Vector2.hpp>
#include <cmath>
#include <cstdint>
#include <functional>
#include <limits>
#include <optional>
#include <unordered_map>
#include <vector>

template <typename TObject, typename TId = int> class CSpatialHashGrid
{
  public:
    using position_getter = std::function<sf::Vector2f(const TObject&)>;
    using id_getter = std::function<TId(const TObject&)>;
    using resolver = std::function<TObject*(TId)>;
    using valid_filter = std::function<bool(const TObject&)>;
    using query_filter = std::function<bool(const TObject*)>;

    static constexpr float global_range = -1.0f;

    CSpatialHashGrid(float cell_size, position_getter get_position, id_getter get_id, resolver resolve,
                     valid_filter is_valid)
        : m_cell_size(cell_size), m_get_position(std::move(get_position)), m_get_id(std::move(get_id)),
          m_resolve(std::move(resolve)), m_is_valid(std::move(is_valid))
    {
    }

    void Clear() { m_grid.clear(); }

    void Insert(TObject* object)
    {
        if (!object || !IsValid(*object)) return;

        TId id = m_get_id(*object);
        sf::Vector2f pos = m_get_position(*object);
        int cell_x = CellX(pos.x);
        int cell_y = CellY(pos.y);
        m_grid[HashCell(cell_x, cell_y)].push_back(id);
    }

    void Rebuild(const std::vector<TObject*>& objects)
    {
        Clear();
        for (TObject* object : objects)
        {
            Insert(object);
        }
    }

    TObject* FindClosest(const sf::Vector2f& center, float max_range, query_filter filter = nullptr) const
    {
        TObject* target = nullptr;
        float min_dist_sq = (max_range == global_range) ? std::numeric_limits<float>::max() : max_range * max_range;

        auto visit_object = [&](TId id)
        {
            TObject* object = Resolve(id);
            if (!object || !IsValid(*object)) return;
            if (filter && !filter(object)) return;

            float dist_sq = DistanceSq(m_get_position(*object), center);
            if (dist_sq < min_dist_sq)
            {
                min_dist_sq = dist_sq;
                target = object;
            }
        };

        if (max_range == global_range)
        {
            for (const auto& [cell, ids] : m_grid)
            {
                (void)cell;
                for (TId id : ids)
                {
                    visit_object(id);
                }
            }
            return target;
        }

        VisitCellsInRange(center, max_range, [&](const std::vector<TId>& ids)
        {
            for (TId id : ids)
            {
                visit_object(id);
            }
        });
        return target;
    }

    std::vector<TObject*> QueryRange(const sf::Vector2f& center, float radius, query_filter filter = nullptr) const
    {
        std::vector<TObject*> result;
        if (radius <= 0.f) return result;

        float radius_sq = radius * radius;
        VisitCellsInRange(center, radius, [&](const std::vector<TId>& ids)
        {
            for (TId id : ids)
            {
                TObject* object = Resolve(id);
                if (!object || !IsValid(*object)) continue;
                if (filter && !filter(object)) continue;
                if (DistanceSq(m_get_position(*object), center) <= radius_sq) result.push_back(object);
            }
        });
        return result;
    }

  private:
    template <typename TVisitor> void VisitCellsInRange(const sf::Vector2f& center, float radius, TVisitor visitor) const
    {
        int min_cell_x = CellX(center.x - radius);
        int max_cell_x = CellX(center.x + radius);
        int min_cell_y = CellY(center.y - radius);
        int max_cell_y = CellY(center.y + radius);

        for (int cell_x = min_cell_x; cell_x <= max_cell_x; ++cell_x)
        {
            for (int cell_y = min_cell_y; cell_y <= max_cell_y; ++cell_y)
            {
                auto it = m_grid.find(HashCell(cell_x, cell_y));
                if (it != m_grid.end()) visitor(it->second);
            }
        }
    }

    TObject* Resolve(TId id) const { return m_resolve ? m_resolve(id) : nullptr; }
    bool IsValid(const TObject& object) const { return m_is_valid ? m_is_valid(object) : true; }

    std::uint64_t HashCell(int cell_x, int cell_y) const
    {
        return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(cell_x)) << 32) |
               static_cast<std::uint32_t>(cell_y);
    }

    int CellX(float world_x) const { return static_cast<int>(std::floor(world_x / m_cell_size)); }
    int CellY(float world_y) const { return static_cast<int>(std::floor(world_y / m_cell_size)); }

    float m_cell_size = 1.f;
    position_getter m_get_position;
    id_getter m_get_id;
    resolver m_resolve;
    valid_filter m_is_valid;
    std::unordered_map<std::uint64_t, std::vector<TId>> m_grid;
};
