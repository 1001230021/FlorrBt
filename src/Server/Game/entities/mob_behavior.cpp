#include "mob.h"
#include "../controllers/melee_controller.h"
#include <cmath>
#include <mutex>

namespace
{
using CBasicMob = CMob<SMobStats>;

std::once_flag g_normal_ladybug_registered;

SMobStats ScaleMobStats(SMobStats stats, ERarity rarity)
{
    int level = GetLevel(rarity);
    float health_scale = std::pow(1.45f, static_cast<float>(level - 1));
    float damage_scale = std::pow(1.25f, static_cast<float>(level - 1));

    stats.max_health *= health_scale;
    stats.damage *= damage_scale;
    return stats;
}
}

void RegisterNormalLadybug()
{
    std::call_once(g_normal_ladybug_registered, []()
    {
        CMobPrototype proto;
        proto.m_type = EMobType::NormalLadybug;
        proto.m_name = "NormalLadybug";
        proto.m_team = 2;
        proto.m_base_stats.max_health = 25.f;
        proto.m_base_stats.armor = 0.f;
        proto.m_base_stats.damage = 8.f;
        proto.m_base_stats.radius = 18.f;
        proto.m_base_stats.mass = 8.f;
        proto.m_base_stats.search_range = game_config::default_search_range;
        proto.m_base_stats.detection_multiplier = 1.f;
        proto.m_base_stats.max_velocity = game_config::default_max_velocity;
        proto.m_base_stats.acceleration = game_config::default_acceleration;
        proto.m_stats_factory = [base_stats = proto.m_base_stats](ERarity rarity) { return ScaleMobStats(base_stats, rarity); };
        proto.m_controller_factory = []() { return std::make_unique<CMeleeController>(); };
        REGISTER_MOB(EMobType::NormalLadybug, CBasicMob, proto);
    });
}