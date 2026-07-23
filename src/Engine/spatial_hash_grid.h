#pragma once
#include "../Shared/tools.h"
#include <SFML/System/Vector2.hpp>
#include <cmath>
#include <cstdint>
#include <functional>
#include <limits>
#include <optional>
#include <unordered_map>
#include <unordered_set>
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

    void Clear()
    {
        m_grid.clear();
        m_tracked.clear();
    }

    void InsertTracked(TObject* object)
    {
        if (!object || !IsValid(*object)) return;
        if (m_query_depth > 0)
        {
            m_pending_tracked_ops.push_back({tracked_op_type::Insert, object, m_get_id(*object)});
            return;
        }

        const TId id = m_get_id(*object);
        RemoveTracked(id);

        const sf::Vector2f pos = m_get_position(*object);
        const std::uint64_t cell = HashCell(CellX(pos.x), CellY(pos.y));
        std::vector<TId>& ids = m_grid[cell];
        m_tracked[id] = {cell, ids.size()};
        ids.push_back(id);
    }

    void UpdateTracked(TObject* object)
    {
        if (!object) return;
        const TId id = m_get_id(*object);
        if (m_query_depth > 0)
        {
            m_pending_tracked_ops.push_back({tracked_op_type::Update, object, id});
            return;
        }
        if (!IsValid(*object))
        {
            RemoveTracked(id);
            return;
        }

        const sf::Vector2f pos = m_get_position(*object);
        const std::uint64_t cell = HashCell(CellX(pos.x), CellY(pos.y));
        auto tracked = m_tracked.find(id);
        if (tracked != m_tracked.end() && tracked->second.cell == cell) return;

        RemoveTracked(id);
        std::vector<TId>& ids = m_grid[cell];
        m_tracked[id] = {cell, ids.size()};
        ids.push_back(id);
    }

    void RemoveTracked(TId id)
    {
        if (m_query_depth > 0)
        {
            m_pending_tracked_ops.push_back({tracked_op_type::Remove, nullptr, id});
            return;
        }

        auto tracked = m_tracked.find(id);
        if (tracked == m_tracked.end()) return;

        auto cell_it = m_grid.find(tracked->second.cell);
        if (cell_it != m_grid.end())
        {
            std::vector<TId>& ids = cell_it->second;
            const size_t index = tracked->second.index;
            if (index < ids.size())
            {
                const TId moved_id = ids.back();
                ids[index] = moved_id;
                ids.pop_back();
                if (index < ids.size())
                {
                    auto moved = m_tracked.find(moved_id);
                    if (moved != m_tracked.end()) moved->second.index = index;
                }
            }
            if (ids.empty()) m_grid.erase(cell_it);
        }
        m_tracked.erase(tracked);
    }

    void RebuildTracked(const std::vector<TObject*>& objects)
    {
        Clear();
        m_grid.reserve(objects.size());
        m_tracked.reserve(objects.size());
        for (TObject* object : objects) InsertTracked(object);
    }

    void Insert(TObject* object)
    {
        if (!object || !IsValid(*object)) return;

        TId id = m_get_id(*object);
        sf::Vector2f pos = m_get_position(*object);
        int cell_x = CellX(pos.x);
        int cell_y = CellY(pos.y);
        m_grid[HashCell(cell_x, cell_y)].push_back(id);
    }

    void Insert(TObject* object, float radius)
    {
        if (!object || !IsValid(*object)) return;
        if (radius <= 0.f)
        {
            Insert(object);
            return;
        }

        TId id = m_get_id(*object);
        sf::Vector2f pos = m_get_position(*object);
        int min_cell_x = CellX(pos.x - radius);
        int max_cell_x = CellX(pos.x + radius);
        int min_cell_y = CellY(pos.y - radius);
        int max_cell_y = CellY(pos.y + radius);
        for (int cell_x = min_cell_x; cell_x <= max_cell_x; ++cell_x)
        {
            for (int cell_y = min_cell_y; cell_y <= max_cell_y; ++cell_y)
            {
                m_grid[HashCell(cell_x, cell_y)].push_back(id);
            }
        }
    }

    void Rebuild(const std::vector<TObject*>& objects)
    {
        for (auto& [cell, ids] : m_grid)
        {
            (void)cell;
            ids.clear();
        }
        if (m_grid.bucket_count() < objects.size()) m_grid.reserve(objects.size());
        for (TObject* object : objects)
        {
            Insert(object);
        }
        if (m_grid.size() > objects.size() * 4 + 64)
        {
            for (auto it = m_grid.begin(); it != m_grid.end();)
            {
                if (it->second.empty()) it = m_grid.erase(it);
                else ++it;
            }
        }
    }

    TObject* FindClosest(const sf::Vector2f& center, float max_range, query_filter filter = nullptr) const
    {
        query_guard guard(*this);
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
        result.reserve(8);

        ForEachInRange(center, radius, [&](TObject* object)
        {
            if (!filter || filter(object)) result.push_back(object);
        });
        return result;
    }

    template <typename TVisitor> void ForEachInRange(const sf::Vector2f& center, float radius, TVisitor visitor) const
    {
        if (radius <= 0.f) return;
        query_guard guard(*this);

        float radius_sq = radius * radius;
        VisitCellsInRange(center, radius, [&](const std::vector<TId>& ids)
        {
            for (TId id : ids)
            {
                TObject* object = Resolve(id);
                if (!object || !IsValid(*object)) continue;
                if (DistanceSq(m_get_position(*object), center) <= radius_sq) visitor(object);
            }
        });
    }

    template <typename TVisitor> void ForEachInRangeBroadphase(const sf::Vector2f& center, float radius, TVisitor visitor) const
    {
        std::unordered_set<TId> visited;
        ForEachInRangeBroadphase(center, radius, visited, visitor);
    }

    template <typename TVisitor>
    void ForEachInRangeBroadphase(const sf::Vector2f& center, float radius, std::unordered_set<TId>& visited,
                                   TVisitor visitor) const
    {
        if (radius <= 0.f) return;
        query_guard guard(*this);

        visited.clear();
        VisitCellsInRange(center, radius, [&](const std::vector<TId>& ids)
        {
            for (TId id : ids)
            {
                if (!visited.insert(id).second) continue;
                TObject* object = Resolve(id);
                if (!object || !IsValid(*object)) continue;
                visitor(object);
            }
        });
    }

  private:
    enum class tracked_op_type : std::uint8_t
    {
        Insert,
        Update,
        Remove,
    };

    struct tracked_location
    {
        std::uint64_t cell = 0;
        size_t index = 0;
    };

    struct pending_tracked_op
    {
        tracked_op_type type = tracked_op_type::Update;
        TObject* object = nullptr;
        TId id{};
    };

    class query_guard
    {
      public:
        explicit query_guard(const CSpatialHashGrid& grid) : m_grid(grid) { ++m_grid.m_query_depth; }
        ~query_guard()
        {
            if (--m_grid.m_query_depth == 0)
                const_cast<CSpatialHashGrid&>(m_grid).FlushPendingTrackedOps();
        }

      private:
        const CSpatialHashGrid& m_grid;
    };

    void FlushPendingTrackedOps()
    {
        if (m_query_depth > 0 || m_pending_tracked_ops.empty()) return;

        std::vector<pending_tracked_op> pending;
        pending.swap(m_pending_tracked_ops);
        for (const pending_tracked_op& op : pending)
        {
            switch (op.type)
            {
            case tracked_op_type::Insert:
                if (!op.object || m_get_id(*op.object) != op.id || Resolve(op.id) != op.object) break;
                InsertTracked(op.object);
                break;
            case tracked_op_type::Update:
                if (!op.object || m_get_id(*op.object) != op.id || Resolve(op.id) != op.object) break;
                UpdateTracked(op.object);
                break;
            case tracked_op_type::Remove:
                RemoveTracked(op.id);
                break;
            }
        }
    }

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
    std::unordered_map<TId, tracked_location> m_tracked;
    mutable size_t m_query_depth = 0;
    mutable std::vector<pending_tracked_op> m_pending_tracked_ops;
};
