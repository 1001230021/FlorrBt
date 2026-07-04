#include "../controllers/melee_controller.h"
#include "../controllers/player_controller.h"
#include "mob.h"
#include <cmath>
#include <map>
#include <mutex>

namespace
{
using CBasicMob = CMob<SMobStats>;

std::once_flag g_beetle_registered;
std::once_flag g_normal_ladybug_registered;
std::once_flag g_playerflower_registered;

inline float GetHealthMult(int level)
{
    if (level <= 0 || level > 10) return 0.f;
    static const std::map<int, float> s_mult_list = {
        { 1, 1.f },    { 2, 3.75f },    { 3, 13.5f },     { 4, 54.f },       { 5, 324.f },
        { 6, 3159.f }, { 7, 145800.f }, { 8, 4374000.f }, { 9, 78732000.f }, { 10, 944784000.f },
    };
    return s_mult_list.at(level);
}

SMobStats ScaleMobStats(SMobStats stats, ERarity rarity)
{
    int level = GetLevel(rarity);
    float health_scale = GetHealthMult(level);
    float damage_scale = std::pow(3.0f, static_cast<float>(level - 1));
    float radius_scale = std::pow(static_cast<float>(level), 1.5f);
    float search_range_scale = std::pow(static_cast<float>(level), 1.25f);
    float mass_scale = std::pow(2.5f, static_cast<float>(level - 1) * 0.6f);

    stats.max_health *= health_scale;
    stats.damage *= damage_scale;
    stats.radius *= radius_scale;
    stats.search_range *= search_range_scale;
    stats.mass *= mass_scale;
    return stats;
}
} // namespace

void RegisterBeetle()
{
    std::call_once(g_beetle_registered, []() {
        CMobPrototype proto;
        proto.m_type = EMobType::Beetle;
        proto.m_name = "Beetle";
        proto.m_team = 2;
        proto.m_base_stats.max_health = 100.f;
        proto.m_base_stats.armor = 1.f;
        proto.m_base_stats.damage = 30.f;
        proto.m_base_stats.radius = 18.f;
        proto.m_base_stats.mass = 10.f;
        proto.m_base_stats.search_range = game_config::default_search_range;
        proto.m_base_stats.max_velocity = game_config::default_max_velocity;
        proto.m_base_stats.acceleration = game_config::default_acceleration;
        proto.m_stats_factory = [base_stats = proto.m_base_stats](ERarity rarity) {
            return ScaleMobStats(base_stats, rarity);
        };
        proto.m_controller_factory = []() { return std::make_unique<CMeleeController>(); };
        REGISTER_MOB(EMobType::Beetle, CBasicMob, proto);
    });
}

void RegisterNormalLadybug()
{
    std::call_once(g_normal_ladybug_registered, []() {
        CMobPrototype proto;
        proto.m_type = EMobType::NormalLadybug;
        proto.m_name = "NormalLadybug";
        proto.m_team = 2;
        proto.m_base_stats.max_health = 60.f;
        proto.m_base_stats.armor = 0.8f;
        proto.m_base_stats.damage = 10.f;
        proto.m_base_stats.radius = 18.f;
        proto.m_base_stats.mass = 5.f;
        proto.m_base_stats.search_range = game_config::default_search_range;
        proto.m_base_stats.max_velocity = game_config::default_max_velocity;
        proto.m_base_stats.acceleration = game_config::default_acceleration;
        proto.m_stats_factory = [base_stats = proto.m_base_stats](ERarity rarity) {
            return ScaleMobStats(base_stats, rarity);
        };
        proto.m_controller_factory = []() { return std::make_unique<CMeleeController>(); };
        REGISTER_MOB(EMobType::NormalLadybug, CBasicMob, proto);
    });
}

void RegisterPlayerFlower()
{
    std::call_once(g_playerflower_registered, []() {
        CMobPrototype proto;
        proto.m_type = EMobType::PlayerFlower;
        proto.m_name = "PlayerFlower";
        proto.m_team = 1;
        proto.m_base_stats.max_health = 100.f;
        proto.m_base_stats.armor = 0.f;
        proto.m_base_stats.damage = 10.f;
        proto.m_base_stats.radius = 18.f;
        proto.m_base_stats.mass = 5.f;
        proto.m_base_stats.search_range = game_config::default_search_range;
        proto.m_base_stats.max_velocity = game_config::default_max_velocity;
        proto.m_base_stats.acceleration = game_config::default_acceleration;
        proto.m_stats_factory = [base_stats = proto.m_base_stats](ERarity rarity) {
            return ScaleMobStats(base_stats, rarity);
        };
        proto.m_controller_factory = []() { return std::make_unique<CPlayerController>(); };
        REGISTER_MOB(EMobType::PlayerFlower, CFlower, proto);
    });
}
