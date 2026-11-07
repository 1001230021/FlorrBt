#pragma once

#include "snapshot_archive.h"
#include "../../Shared/stats.h"
#include <array>
#include <utility>

// 实例随机属性和出生修正不能从类型/稀有度推导，读写共用字段表。
// 装备和天赋仍按规则派生，避免重复叠加。
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
        // 负护甲是合法的易伤修正，需保留其有符号语义。
        stats.*member = member == &SMobStats::armor ? value : std::max(0.f, value);
    }
    return stats;
}
