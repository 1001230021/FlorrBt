#pragma once
#include <cstddef>
#include <cstdint>
#include <algorithm>
#include <functional>
#include <sstream>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <vector>

struct config_entry
{
    std::function<void(const std::string&)> setter;
    std::function<std::string()> getter;
};

namespace game_config
{
inline float default_acceleration = 100.0f;
inline float default_air_base_mass = 16.0f;
inline float default_air_base_radius = 8.0f;
inline float default_basic_base_damage = 10.0f;
inline float default_basic_base_health = 10.0f;
inline float default_basic_base_radius = 10.0f;
inline float default_basic_copy = 1.0f;
inline float default_basic_mass = 2.0f;
inline float default_basic_reload = 2.5f;
inline float default_beetleegg_base_health = 1.0f;
inline float default_beetleegg_base_radius = 10.0f;
inline float default_beetleegg_copy = 1.0f;
inline float default_beetleegg_mass = 1.0f;
inline float default_beetleegg_reload_common = 0.0f;
inline float default_beetleegg_reload_epic = 0.0f;
inline float default_beetleegg_reload_eternal = 0.0f;
inline float default_beetleegg_reload_legendary = 0.0f;
inline float default_beetleegg_reload_mythic = 0.0f;
inline float default_beetleegg_reload_primordial = 0.0f;
inline float default_beetleegg_reload_rare = 0.0f;
inline float default_beetleegg_reload_super = 0.0f;
inline float default_beetleegg_reload_ultra = 0.0f;
inline float default_beetleegg_reload_unique = 0.0f;
inline float default_beetleegg_reload_unusual = 0.0f;
inline float default_detection_radius = 512.0f;
inline float default_dust_base_damage = 4.3f;
inline float default_dust_base_health = 1.7f;
inline float default_dust_base_radius = 7.5f;
inline float default_dust_copy = 3.0f;
inline float default_dust_mass = 1.0f;
inline float default_dust_reload_reduction = 0.03f;
inline float default_flower_petal_num_max = 5.0f;
inline float default_flower_radius = 20.0f;
inline float default_goldenleaf_base_damage = 16.0f;
inline float default_goldenleaf_base_health = 12.0f;
inline float default_goldenleaf_base_radius = 15.0f;
inline float default_goldenleaf_base_reload_reduction = 0.05f;
inline float default_goldenleaf_copy = 1.0f;
inline float default_goldenleaf_mass = 3.0f;
inline float default_horizon = 1024.0f;
inline float default_max_velocity = 50.0f;
inline float default_petal_attack_offset = 30.0f;
inline float default_petal_defend_offset = -10.0f;
inline float default_petal_orbit_k = 5.0f;
inline float default_petal_orbit_radius = 20.0f;
inline float default_petal_pow = 3.0f;
inline float default_petal_preload = 2.5f;
inline float default_petal_radius = 15.0f;
inline float default_petal_reload = 2.5f;
inline float default_petal_reload_multiplier_min = 0.001f;
inline float default_petal_stat_reload_multiplier_min = 0.05f;
inline float default_yinyang_base_damage = 10.0f;
inline float default_yinyang_base_health = 10.0f;
inline float default_yinyang_base_radius = 12.0f;
inline float default_yinyang_copy = 1.0f;
inline float default_yinyang_mass = 2.0f;
inline float default_yinyang_reload = 2.0f;
inline float entity_collision_epsilon = 0.001f;
inline float melee_random_wander_divisor = 8.0f;
inline float melee_retarget_chance_multiplier = 2.0f;
inline float melee_target_time = 16.0f;
inline float mob_beetle_armor = 1.0f;
inline float mob_beetle_damage = 30.0f;
inline float mob_beetle_mass = 10.0f;
inline float mob_beetle_max_health = 100.0f;
inline float mob_beetle_radius = 18.0f;
inline int mob_beetle_team = 2;
inline float mob_damage_scale_base = 3.0f;
inline float mob_horizon_scale_exp = 1.25f;
inline float mob_mass_scale_base = 2.5f;
inline float mob_mass_scale_exp_multiplier = 0.6f;
inline float mob_normal_flower_armor = 0.0f;
inline float mob_normal_flower_damage = 8.0f;
inline float mob_normal_flower_mass = 5.0f;
inline float mob_normal_flower_max_health = 60.0f;
inline float mob_normal_flower_petal_rotation_speed = 1.5f;
inline int mob_normal_flower_team = 2;
inline float mob_normal_ladybug_armor = 0.8f;
inline float mob_normal_ladybug_damage = 10.0f;
inline float mob_normal_ladybug_mass = 5.0f;
inline float mob_normal_ladybug_max_health = 60.0f;
inline float mob_normal_ladybug_radius = 18.0f;
inline int mob_normal_ladybug_team = 2;
inline float mob_player_flower_armor = 0.0f;
inline float mob_player_flower_damage = 10.0f;
inline float mob_player_flower_mass = 5.0f;
inline float mob_player_flower_max_health = 100.0f;
inline float mob_player_flower_petal_rotation_speed = 1.5f;
inline float mob_player_flower_radius = 18.0f;
inline int mob_player_flower_team = 1;
inline float mob_radius_scale_exp = 1.5f;
inline float mob_stop_damping = 0.9f;
inline float mob_stop_velocity_epsilon = 1e-5f;
inline float mob_summoned_beetle_armor = 0.0f;
inline float mob_summoned_beetle_damage = 10.0f;
inline float mob_summoned_beetle_follow_range = 320.0f;
inline float mob_summoned_beetle_hard_range = 960.0f;
inline float mob_summoned_beetle_horizon = 512.0f;
inline float mob_summoned_beetle_mass = 2.0f;
inline float mob_summoned_beetle_max_health = 30.0f;
inline float mob_summoned_beetle_radius = 10.0f;
inline float mob_summoned_beetle_scale_exp = 0.5f;
inline int mob_summoned_beetle_team = 1;
inline size_t network_max_receive_buffer_size = 1024;
inline size_t network_max_send_buffer_size = 1024 * 1024;
inline size_t network_receive_chunk_size = 256;
inline uint8_t network_petal_type_offset = 100;
inline float open_initial_spawn_delay = 5.0f;
inline float open_spawn_interval = 9999.9f;
inline float open_spawn_x = 500.0f;
inline float open_spawn_y = 0.0f;
inline float pi = 3.14159265359f;
inline float player_input_axis_max = 127.0f;
inline float player_move_target_distance = 100123.0f;
inline float player_respawn_x = 0.0f;
inline float player_respawn_y = 0.0f;
inline int port = 10012;
inline float server_fixed_dt = 0.016f;
inline float spatial_grid_cell_size = 200.0f;
inline float timeout_protection_seconds = 30.0f;

template <typename T> inline void SetConfigValue(T& variable, const std::string& value)
{
    std::stringstream stream(value);
    if constexpr (std::is_same_v<T, uint8_t>)
    {
        int parsed = 0;
        stream >> parsed;
        if (!stream.fail()) variable = static_cast<uint8_t>(parsed);
    } else {
        T parsed{};
        stream >> parsed;
        if (!stream.fail()) variable = parsed;
    }
}

template <typename T> inline std::string GetConfigValue(const T& variable)
{
    if constexpr (std::is_same_v<T, uint8_t>) return std::to_string(static_cast<int>(variable));
    else return std::to_string(variable);
}

template <typename T> inline config_entry MakeConfigEntry(T& variable)
{
    return {
        [&variable](const std::string& value) { SetConfigValue(variable, value); },
        [&variable]() -> std::string { return GetConfigValue(variable); }
    };
}

#define REGISTER_CONFIG(name, variable) {name, game_config::MakeConfigEntry(variable)}

inline std::unordered_map<std::string, config_entry>& GetConfigEntries()
{
    static std::unordered_map<std::string, config_entry> entries = {
        REGISTER_CONFIG("acceleration", default_acceleration),
        REGISTER_CONFIG("air_base_mass", default_air_base_mass),
        REGISTER_CONFIG("air_base_radius", default_air_base_radius),
        REGISTER_CONFIG("basic_base_damage", default_basic_base_damage),
        REGISTER_CONFIG("basic_base_health", default_basic_base_health),
        REGISTER_CONFIG("basic_base_radius", default_basic_base_radius),
        REGISTER_CONFIG("basic_copy", default_basic_copy),
        REGISTER_CONFIG("basic_mass", default_basic_mass),
        REGISTER_CONFIG("basic_reload", default_basic_reload),
        REGISTER_CONFIG("beetleegg_base_health", default_beetleegg_base_health),
        REGISTER_CONFIG("beetleegg_base_radius", default_beetleegg_base_radius),
        REGISTER_CONFIG("beetleegg_copy", default_beetleegg_copy),
        REGISTER_CONFIG("beetleegg_mass", default_beetleegg_mass),
        REGISTER_CONFIG("beetleegg_reload_common", default_beetleegg_reload_common),
        REGISTER_CONFIG("beetleegg_reload_epic", default_beetleegg_reload_epic),
        REGISTER_CONFIG("beetleegg_reload_eternal", default_beetleegg_reload_eternal),
        REGISTER_CONFIG("beetleegg_reload_legendary", default_beetleegg_reload_legendary),
        REGISTER_CONFIG("beetleegg_reload_mythic", default_beetleegg_reload_mythic),
        REGISTER_CONFIG("beetleegg_reload_primordial", default_beetleegg_reload_primordial),
        REGISTER_CONFIG("beetleegg_reload_rare", default_beetleegg_reload_rare),
        REGISTER_CONFIG("beetleegg_reload_super", default_beetleegg_reload_super),
        REGISTER_CONFIG("beetleegg_reload_ultra", default_beetleegg_reload_ultra),
        REGISTER_CONFIG("beetleegg_reload_unique", default_beetleegg_reload_unique),
        REGISTER_CONFIG("beetleegg_reload_unusual", default_beetleegg_reload_unusual),
        REGISTER_CONFIG("detection_radius", default_detection_radius),
        REGISTER_CONFIG("dust_base_damage", default_dust_base_damage),
        REGISTER_CONFIG("dust_base_health", default_dust_base_health),
        REGISTER_CONFIG("dust_base_radius", default_dust_base_radius),
        REGISTER_CONFIG("dust_copy", default_dust_copy),
        REGISTER_CONFIG("dust_mass", default_dust_mass),
        REGISTER_CONFIG("dust_reload_reduction", default_dust_reload_reduction),
        REGISTER_CONFIG("entity_collision_epsilon", entity_collision_epsilon),
        REGISTER_CONFIG("flower_petal_num_max", default_flower_petal_num_max),
        REGISTER_CONFIG("flower_radius", default_flower_radius),
        REGISTER_CONFIG("goldenleaf_base_damage", default_goldenleaf_base_damage),
        REGISTER_CONFIG("goldenleaf_base_health", default_goldenleaf_base_health),
        REGISTER_CONFIG("goldenleaf_base_radius", default_goldenleaf_base_radius),
        REGISTER_CONFIG("goldenleaf_base_reload_reduction", default_goldenleaf_base_reload_reduction),
        REGISTER_CONFIG("goldenleaf_copy", default_goldenleaf_copy),
        REGISTER_CONFIG("goldenleaf_mass", default_goldenleaf_mass),
        REGISTER_CONFIG("horizon", default_horizon),
        REGISTER_CONFIG("max_velocity", default_max_velocity),
        REGISTER_CONFIG("melee_random_wander_divisor", melee_random_wander_divisor),
        REGISTER_CONFIG("melee_retarget_chance_multiplier", melee_retarget_chance_multiplier),
        REGISTER_CONFIG("melee_target_time", melee_target_time),
        REGISTER_CONFIG("mob_beetle_armor", mob_beetle_armor),
        REGISTER_CONFIG("mob_beetle_damage", mob_beetle_damage),
        REGISTER_CONFIG("mob_beetle_mass", mob_beetle_mass),
        REGISTER_CONFIG("mob_beetle_max_health", mob_beetle_max_health),
        REGISTER_CONFIG("mob_beetle_radius", mob_beetle_radius),
        REGISTER_CONFIG("mob_beetle_team", mob_beetle_team),
        REGISTER_CONFIG("mob_damage_scale_base", mob_damage_scale_base),
        REGISTER_CONFIG("mob_horizon_scale_exp", mob_horizon_scale_exp),
        REGISTER_CONFIG("mob_mass_scale_base", mob_mass_scale_base),
        REGISTER_CONFIG("mob_mass_scale_exp_multiplier", mob_mass_scale_exp_multiplier),
        REGISTER_CONFIG("mob_normal_flower_armor", mob_normal_flower_armor),
        REGISTER_CONFIG("mob_normal_flower_damage", mob_normal_flower_damage),
        REGISTER_CONFIG("mob_normal_flower_mass", mob_normal_flower_mass),
        REGISTER_CONFIG("mob_normal_flower_max_health", mob_normal_flower_max_health),
        REGISTER_CONFIG("mob_normal_flower_petal_rotation_speed", mob_normal_flower_petal_rotation_speed),
        REGISTER_CONFIG("mob_normal_flower_team", mob_normal_flower_team),
        REGISTER_CONFIG("mob_normal_ladybug_armor", mob_normal_ladybug_armor),
        REGISTER_CONFIG("mob_normal_ladybug_damage", mob_normal_ladybug_damage),
        REGISTER_CONFIG("mob_normal_ladybug_mass", mob_normal_ladybug_mass),
        REGISTER_CONFIG("mob_normal_ladybug_max_health", mob_normal_ladybug_max_health),
        REGISTER_CONFIG("mob_normal_ladybug_radius", mob_normal_ladybug_radius),
        REGISTER_CONFIG("mob_normal_ladybug_team", mob_normal_ladybug_team),
        REGISTER_CONFIG("mob_player_flower_armor", mob_player_flower_armor),
        REGISTER_CONFIG("mob_player_flower_damage", mob_player_flower_damage),
        REGISTER_CONFIG("mob_player_flower_mass", mob_player_flower_mass),
        REGISTER_CONFIG("mob_player_flower_max_health", mob_player_flower_max_health),
        REGISTER_CONFIG("mob_player_flower_petal_rotation_speed", mob_player_flower_petal_rotation_speed),
        REGISTER_CONFIG("mob_player_flower_radius", mob_player_flower_radius),
        REGISTER_CONFIG("mob_player_flower_team", mob_player_flower_team),
        REGISTER_CONFIG("mob_radius_scale_exp", mob_radius_scale_exp),
        REGISTER_CONFIG("mob_stop_damping", mob_stop_damping),
        REGISTER_CONFIG("mob_stop_velocity_epsilon", mob_stop_velocity_epsilon),
        REGISTER_CONFIG("mob_summoned_beetle_armor", mob_summoned_beetle_armor),
        REGISTER_CONFIG("mob_summoned_beetle_damage", mob_summoned_beetle_damage),
        REGISTER_CONFIG("mob_summoned_beetle_follow_range", mob_summoned_beetle_follow_range),
        REGISTER_CONFIG("mob_summoned_beetle_hard_range", mob_summoned_beetle_hard_range),
        REGISTER_CONFIG("mob_summoned_beetle_horizon", mob_summoned_beetle_horizon),
        REGISTER_CONFIG("mob_summoned_beetle_mass", mob_summoned_beetle_mass),
        REGISTER_CONFIG("mob_summoned_beetle_max_health", mob_summoned_beetle_max_health),
        REGISTER_CONFIG("mob_summoned_beetle_radius", mob_summoned_beetle_radius),
        REGISTER_CONFIG("mob_summoned_beetle_scale_exp", mob_summoned_beetle_scale_exp),
        REGISTER_CONFIG("mob_summoned_beetle_team", mob_summoned_beetle_team),
        REGISTER_CONFIG("network_max_receive_buffer_size", network_max_receive_buffer_size),
        REGISTER_CONFIG("network_max_send_buffer_size", network_max_send_buffer_size),
        REGISTER_CONFIG("network_petal_type_offset", network_petal_type_offset),
        REGISTER_CONFIG("network_receive_chunk_size", network_receive_chunk_size),
        REGISTER_CONFIG("open_initial_spawn_delay", open_initial_spawn_delay),
        REGISTER_CONFIG("open_spawn_interval", open_spawn_interval),
        REGISTER_CONFIG("open_spawn_x", open_spawn_x),
        REGISTER_CONFIG("open_spawn_y", open_spawn_y),
        REGISTER_CONFIG("petal_attack_offset", default_petal_attack_offset),
        REGISTER_CONFIG("petal_defend_offset", default_petal_defend_offset),
        REGISTER_CONFIG("petal_orbit_k", default_petal_orbit_k),
        REGISTER_CONFIG("petal_orbit_radius", default_petal_orbit_radius),
        REGISTER_CONFIG("petal_pow", default_petal_pow),
        REGISTER_CONFIG("petal_preload", default_petal_preload),
        REGISTER_CONFIG("petal_radius", default_petal_radius),
        REGISTER_CONFIG("petal_reload", default_petal_reload),
        REGISTER_CONFIG("petal_reload_multiplier_min", default_petal_reload_multiplier_min),
        REGISTER_CONFIG("petal_stat_reload_multiplier_min", default_petal_stat_reload_multiplier_min),
        REGISTER_CONFIG("pi", pi),
        REGISTER_CONFIG("player_input_axis_max", player_input_axis_max),
        REGISTER_CONFIG("player_move_target_distance", player_move_target_distance),
        REGISTER_CONFIG("player_respawn_x", player_respawn_x),
        REGISTER_CONFIG("player_respawn_y", player_respawn_y),
        REGISTER_CONFIG("port", port),
        REGISTER_CONFIG("server_fixed_dt", server_fixed_dt),
        REGISTER_CONFIG("spatial_grid_cell_size", spatial_grid_cell_size),
        REGISTER_CONFIG("timeout_protection_seconds", timeout_protection_seconds),
        REGISTER_CONFIG("yinyang_base_damage", default_yinyang_base_damage),
        REGISTER_CONFIG("yinyang_base_health", default_yinyang_base_health),
        REGISTER_CONFIG("yinyang_base_radius", default_yinyang_base_radius),
        REGISTER_CONFIG("yinyang_copy", default_yinyang_copy),
        REGISTER_CONFIG("yinyang_mass", default_yinyang_mass),
        REGISTER_CONFIG("yinyang_reload", default_yinyang_reload),
    };
    return entries;
}

inline bool SetConfig(const std::string& name, const std::string& value)
{
    auto& entries = GetConfigEntries();
    auto it = entries.find(name);
    if (it == entries.end()) return false;

    it->second.setter(value);
    return true;
}

inline std::string GetConfig(const std::string& name)
{
    auto& entries = GetConfigEntries();
    auto it = entries.find(name);
    if (it != entries.end()) return it->second.getter();
    return "Unknown config";
}

inline std::vector<std::string> ConfigNames()
{
    std::vector<std::string> names;
    auto& entries = GetConfigEntries();
    names.reserve(entries.size());
    for (const auto& [name, entry] : entries)
    {
        (void)entry;
        names.push_back(name);
    }
    std::sort(names.begin(), names.end());
    return names;
}
}
