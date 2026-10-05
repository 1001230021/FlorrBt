#pragma once
#include "math_types.h"
#include "rarity.h"
#include <array>
#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <vector>
#include <cstdint>
#include <unordered_map>

namespace florrbt
{
// Polygon distance, not center distance: this also handles long, concave and
// adjacent spawn areas. A boundary point belongs to the polygon.
template <typename TPoints> float DistanceSquaredToSpawnPolygon(Vector2f point, const TPoints& points)
{
    if (points.size() < 3) return std::numeric_limits<float>::infinity();
    bool inside = false;
    float nearest = std::numeric_limits<float>::infinity();
    for (std::size_t i = 0, j = points.size() - 1; i < points.size(); j = i++)
    {
        const auto& a = points[j];
        const auto& b = points[i];
        const float dx = b.x - a.x, dy = b.y - a.y;
        const float length_sq = dx * dx + dy * dy;
        const float t = length_sq > 0.f ? std::clamp(((point.x - a.x) * dx + (point.y - a.y) * dy) / length_sq, 0.f, 1.f) : 0.f;
        const float x = point.x - a.x - dx * t, y = point.y - a.y - dy * t;
        nearest = std::min(nearest, x * x + y * y);
        if ((a.y > point.y) != (b.y > point.y) && point.x < (b.x - a.x) * (point.y - a.y) / (b.y - a.y) + a.x)
            inside = !inside;
    }
    return inside || nearest <= 0.000001f ? 0.f : nearest;
}

struct SpawnZoneBounds { Vector2f min; Vector2f max; };
template<class TZones> std::vector<SpawnZoneBounds> BuildSpawnZoneBounds(const TZones& zones)
{
    std::vector<SpawnZoneBounds> result;
    result.reserve(zones.size());
    for (const auto& zone : zones)
    {
        SpawnZoneBounds b{ { INFINITY, INFINITY }, { -INFINITY, -INFINITY } };
        for (const auto& p : zone.vertices)
        {
            b.min.x = std::min(b.min.x, p.x); b.min.y = std::min(b.min.y, p.y);
            b.max.x = std::max(b.max.x, p.x); b.max.y = std::max(b.max.y, p.y);
        }
        result.push_back(b);
    }
    return result;
}
template <typename TZones>
std::optional<float> FindSpawnHabitatDifficulty(const TZones& zones, Vector2f point,
                                               const std::vector<SpawnZoneBounds>* bounds = nullptr)
{
    std::optional<float> difficulty;
    float nearest = std::numeric_limits<float>::infinity();
    for (std::size_t index = 0; index < zones.size(); ++index)
    {
        const auto& zone = zones[index];
        if (zone.vertices.size() < 3 || (zone.mobs.empty() && zone.spawn_profile.empty()) || !std::isfinite(zone.difficulty))
            continue;
        if (bounds && index < bounds->size())
        {
            const auto& b = (*bounds)[index];
            const float dx = std::max({ b.min.x - point.x, 0.f, point.x - b.max.x });
            const float dy = std::max({ b.min.y - point.y, 0.f, point.y - b.max.y });
            if (dx * dx + dy * dy > std::max(nearest, 0.000001f)) continue;
        }
        const float distance = DistanceSquaredToSpawnPolygon(point, zone.vertices);
        if (!std::isfinite(distance)) continue;
        // For overlaps/equidistant areas use the harder one, never depending
        // on Tiled's serialization order to decide whether a mob retires.
        if (!difficulty || distance < nearest || (distance == nearest && zone.difficulty > *difficulty))
        {
            difficulty = zone.difficulty;
            nearest = distance;
        }
    }
    return difficulty;
}

inline int ComputeSpawnRarityExcess(int mob_level, float difficulty, float difficulty_scale = 10.f)
{
    if (!std::isfinite(difficulty) || !std::isfinite(difficulty_scale) || difficulty_scale <= 0.f) return 0;
    const int habitat_level = static_cast<int>(std::clamp(std::floor(difficulty / difficulty_scale), 1.f, 10.f));
    return std::max(0, mob_level - habitat_level);
}

struct SpawnInfluence
{
    Vector2f position;
    float radius = 0.f;
    float strength = 0.f;
};

inline SpawnInfluence MakeSpawnInfluence(Vector2f position, float body_radius, int excess, float base_range)
{
    if (excess < 2 || !(base_range > 0.f) || !std::isfinite(base_range)) return { position, 0.f, 0.f };
    return { position, std::max(0.f, body_radius) + base_range * (1.f + 0.5f * (excess - 2)),
             static_cast<float>(excess - 1) };
}

inline float MeasureSpawnPressure(Vector2f position, const std::vector<SpawnInfluence>& influences)
{
    float strongest = 0.f;
    for (const auto& source : influences)
    {
        if (!(source.radius > 0.f)) continue;
        const float dx = position.x - source.position.x, dy = position.y - source.position.y;
        const float distance_sq = dx * dx + dy * dy;
        if (distance_sq >= source.radius * source.radius) continue;
        const float t = std::sqrt(distance_sq) / source.radius;
        const float falloff = 1.f - t * t * (3.f - 2.f * t);
        // Overlapping auras don't multiply into a permanently empty region.
        strongest = std::max(strongest, source.strength * falloff);
    }
    return strongest;
}

// Grid narrows pressure queries; extremely large auras use a small fallback
// list instead of allocating an unbounded number of grid cells.
class SpawnInfluenceIndex
{
  public:
    void Clear() { m_sources.clear(); m_cells.clear(); m_large.clear(); }
    void Add(SpawnInfluence source)
    {
        if (!(source.radius > 0.f) || !std::isfinite(source.radius) ||
            !std::isfinite(source.position.x) || !std::isfinite(source.position.y)) return;
        const auto index = m_sources.size();
        m_sources.push_back(source);
        const int x0 = Cell(source.position.x - source.radius), x1 = Cell(source.position.x + source.radius);
        const int y0 = Cell(source.position.y - source.radius), y1 = Cell(source.position.y + source.radius);
        if ((static_cast<std::int64_t>(x1) - x0 + 1) * (static_cast<std::int64_t>(y1) - y0 + 1) > 256)
        { m_large.push_back(index); return; }
        for (int x = x0; x <= x1; ++x) for (int y = y0; y <= y1; ++y) m_cells[Key(x,y)].push_back(index);
    }
    float Measure(Vector2f pos) const
    {
        if (!std::isfinite(pos.x) || !std::isfinite(pos.y)) return 0.f;
        float pressure = 0.f;
        const auto visit = [&](std::size_t index) {
            const auto& source = m_sources[index];
            const float dx = pos.x - source.position.x, dy = pos.y - source.position.y;
            if (dx * dx + dy * dy >= source.radius * source.radius) return;
            const float t = std::sqrt(dx * dx + dy * dy) / source.radius;
            pressure = std::max(pressure, source.strength * (1.f - t * t * (3.f - 2.f * t)));
        };
        if (const auto it = m_cells.find(Key(Cell(pos.x), Cell(pos.y))); it != m_cells.end())
            for (auto index : it->second) visit(index);
        for (auto index : m_large) visit(index);
        return pressure;
    }
  private:
    static int Cell(float v) { return static_cast<int>(std::clamp(std::floor(v / 1024.f), -1000000.f, 1000000.f)); }
    static std::uint64_t Key(int x, int y) { return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(x)) << 32) | static_cast<std::uint32_t>(y); }
    std::vector<SpawnInfluence> m_sources;
    std::unordered_map<std::uint64_t, std::vector<std::size_t>> m_cells;
    std::vector<std::size_t> m_large;
};

inline float SpawnRateUnderPressure(float pressure, float rate_strength, float minimum_rate)
{
    return std::max(std::clamp(minimum_rate, 0.f, 1.f), 1.f / (1.f + std::max(0.f, pressure) * std::max(0.f, rate_strength)));
}

inline float SpawnLevelPenalty(float pressure, float penalty_per_level)
{
    return std::clamp(std::max(0.f, pressure) * std::max(0.f, penalty_per_level), 0.f, 3.f);
}

template <typename TValueLevel>
ERarity SampleSpawnRarity(float difficulty, float scale, float sigma, float quantile, TValueLevel value_level)
{
    constexpr std::array rarities{ ERarity::Common, ERarity::Unusual, ERarity::Rare, ERarity::Epic,
        ERarity::Legendary, ERarity::Mythic, ERarity::Ultra, ERarity::Super, ERarity::Eternal,
        ERarity::Unique, ERarity::Primordial };
    if (!std::isfinite(difficulty) || difficulty <= 0.f) return ERarity::Common;
    scale = std::isfinite(scale) ? std::max(0.000001f, scale) : 10.f;
    sigma = std::isfinite(sigma) ? std::max(0.000001f, sigma) : 0.36f;
    const float center = std::clamp(difficulty / scale, 1.f, 10.f);
    std::array<float, rarities.size()> weights{};
    float total = 0.f;
    for (std::size_t i = 0; i < rarities.size(); ++i)
    {
        const float delta = value_level(rarities[i]) - center;
        total += weights[i] = std::exp(-(delta * delta) / (2.f * sigma * sigma));
    }
    if (!(total > 0.f)) return ERarity::Common;
    float roll = std::clamp(quantile, 0.f, 1.f);
    ERarity last = ERarity::Common;
    for (std::size_t i = 0; i < rarities.size(); ++i)
    {
        if (weights[i] <= 0.f) continue;
        last = rarities[i];
        const float probability = weights[i] / total;
        if (roll <= probability) return last;
        roll -= probability;
    }
    return last;
}
}
