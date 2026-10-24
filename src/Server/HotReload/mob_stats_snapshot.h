#pragma once

#include "snapshot_archive.h"
#include "../../Shared/stats.h"
#include <array>
#include <utility>

// Intrinsic per-instance rolls and spawn-time modifiers are not derivable
// from mob type/rarity. Use the same field table in both directions. Flower
// loadout/talent bonuses remain derived and must not be multiplied twice.
inline constexpr std::array<std::pair<const char*, float SMobStats::*>, 11> mob_snapshot_stat_fields{{
    { "max_health", &SMobStats::max_health }, { "armor", &SMobStats::armor },
    { "damage", &SMobStats::damage }, { "radius", &SMobStats::radius },
    { "mass", &SMobStats::mass }, { "horizon", &SMobStats::horizon },
    { "max_absorb_range", &SMobStats::max_absorb_range },
    { "detection_multiplier", &SMobStats::detection_multiplier },
    { "max_velocity", &SMobStats::max_velocity }, { "acceleration", &SMobStats::acceleration },
    { "turn_speed", &SMobStats::turn_speed },
}};

inline CJsonOwner CaptureMobBaseStats(const SMobStats& stats)
{
    CJsonOwner value = MakeJsonObject();
    CSnapshotWriter writer(value.get());
    writer.Field("extra_hit_num", stats.extra_hit_num);
    for (const auto& [key, member] : mob_snapshot_stat_fields) writer.Field(key, stats.*member);
    return value;
}

inline SMobStats ReadMobBaseStats(const CSnapshotReader& reader, SMobStats stats)
{
    stats.extra_hit_num = std::max(0, reader.Int("extra_hit_num", stats.extra_hit_num));
    for (const auto& [key, member] : mob_snapshot_stat_fields)
    {
        const float value = reader.Float(key, stats.*member);
        // Negative armor is a supported vulnerability modifier, not an
        // invalid radius/health/speed. Preserve its signed combat semantics.
        stats.*member = member == &SMobStats::armor ? value : std::max(0.f, value);
    }
    return stats;
}
