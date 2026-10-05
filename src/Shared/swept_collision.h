#pragma once
#include "math_types.h"
#include <algorithm>
#include <cmath>
#include <cstddef>

namespace florrbt
{
struct SweptCircleBounds { Vector2f min; Vector2f max; };
inline SweptCircleBounds MakeSweptCircleBounds(Vector2f from, Vector2f to, float radius)
{
    radius = std::max(0.f, radius);
    return { { std::min(from.x, to.x) - radius, std::min(from.y, to.y) - radius },
             { std::max(from.x, to.x) + radius, std::max(from.y, to.y) + radius } };
}
inline bool OverlapSweptBounds(const SweptCircleBounds& a, const SweptCircleBounds& b)
{
    return a.min.x <= b.max.x && b.min.x <= a.max.x && a.min.y <= b.max.y && b.min.y <= a.max.y;
}
template<class TEntries, class TVisitor> void VisitSweptCirclePairs(TEntries& entries, TVisitor visit)
{
    std::sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) {
        return a.bounds.min.x == b.bounds.min.x ? a.id < b.id : a.bounds.min.x < b.bounds.min.x;
    });
    float max_x = -INFINITY;
    for (auto& entry : entries) entry.prefix_max_x = max_x = std::max(max_x, entry.bounds.max.x);
    for (const auto& active : entries)
    {
        if (!active.active) continue;
        auto start = std::lower_bound(entries.begin(), entries.end(), active.bounds.min.x,
            [](const auto& entry, float x) { return entry.prefix_max_x < x; });
        for (auto it = start; it != entries.end() && it->bounds.min.x <= active.bounds.max.x; ++it)
            if (active.id != it->id && (!it->active || active.id < it->id) && OverlapSweptBounds(active.bounds, it->bounds))
                visit(active, *it);
    }
}
}
