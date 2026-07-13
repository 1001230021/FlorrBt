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
inline std::string account_data_path = "data/accounts.json";
inline std::string server_log_path = "data/server.log";
inline std::string startup_commands_path = "data/server.cfg";
inline float default_acceleration = 150.0f;
inline float default_air_base_mass = 16.0f;
inline float default_air_base_radius = 8.0f;
inline float default_antegg_base_health = 1.0f;
inline float default_antegg_base_radius = 10.0f;
inline float default_antegg_copy = 4.0f;
inline float default_antegg_mass = 1.0f;
inline float default_antegg_reload_common = 24.0f;
inline float default_antegg_reload_epic = 10.0f;
inline float default_antegg_reload_eternal = 180.0f;
inline float default_antegg_reload_legendary = 12.0f;
inline float default_antegg_reload_mythic = 16.0f;
inline float default_antegg_reload_primordial = 480.0f;
inline float default_antegg_reload_rare = 9.0f;
inline float default_antegg_reload_super = 60.0f;
inline float default_antegg_reload_ultra = 24.0f;
inline float default_antegg_reload_unique = 180.0f;
inline float default_antegg_reload_unusual = 8.0f;
inline float default_antennae_horizon_common = 0.111f;
inline float default_antennae_horizon_epic = 0.333f;
inline float default_antennae_horizon_eternal = 9.0f;
inline float default_antennae_horizon_legendary = 0.429f;
inline float default_antennae_horizon_mythic = 1.0f;
inline float default_antennae_horizon_primordial = 19.0f;
inline float default_antennae_horizon_rare = 0.25f;
inline float default_antennae_horizon_super = 4.0f;
inline float default_antennae_horizon_ultra = 1.857f;
inline float default_antennae_horizon_unique = 9.0f;
inline float default_antennae_horizon_unusual = 0.176f;
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
inline float default_beetleegg_reload_common = 12.0f;
inline float default_beetleegg_reload_epic = 5.0f;
inline float default_beetleegg_reload_eternal = 90.0f;
inline float default_beetleegg_reload_legendary = 6.0f;
inline float default_beetleegg_reload_mythic = 8.0f;
inline float default_beetleegg_reload_primordial = 240.0f;
inline float default_beetleegg_reload_rare = 4.5f;
inline float default_beetleegg_reload_super = 30.0f;
inline float default_beetleegg_reload_ultra = 12.0f;
inline float default_beetleegg_reload_unique = 90.0f;
inline float default_beetleegg_reload_unusual = 4.0f;
inline float default_bandage_base_radius = 10.0f;
inline float default_bandage_copy = 0.0f;
inline float default_bandage_no_revive_duration = 30.0f;
inline float default_bandage_undead_common = 1.5f;
inline float default_bandage_undead_epic = 4.1f;
inline float default_bandage_undead_eternal = 22.1f;
inline float default_bandage_undead_legendary = 5.8f;
inline float default_bandage_undead_mythic = 8.1f;
inline float default_bandage_undead_primordial = 31.3f;
inline float default_bandage_undead_rare = 2.9f;
inline float default_bandage_undead_super = 15.8f;
inline float default_bandage_undead_ultra = 11.3f;
inline float default_bandage_undead_unique = 22.1f;
inline float default_bandage_undead_unusual = 2.1f;
inline float default_blood_sacrifice_base_radius = 10.0f;
inline float default_blood_sacrifice_copy = 0.0f;
inline float default_blood_sacrifice_delay = 10.0f;
inline float default_bubble_base_health = 1.0f;
inline float default_bubble_base_radius = 10.0f;
inline float default_bubble_boost_speed = 240.0f;
inline float default_bubble_copy = 1.0f;
inline float default_bubble_mass = 1.0f;
inline float default_bubble_reload_common = 1.0f;
inline float default_bubble_reload_epic = 0.5f;
inline float default_bubble_reload_eternal = 0.05f;
inline float default_bubble_reload_legendary = 0.4f;
inline float default_bubble_reload_mythic = 0.3f;
inline float default_bubble_reload_primordial = 0.025f;
inline float default_bubble_reload_rare = 0.6f;
inline float default_bubble_reload_super = 0.1f;
inline float default_bubble_reload_ultra = 0.2f;
inline float default_bubble_reload_unique = 0.05f;
inline float default_bubble_reload_unusual = 0.8f;
inline float default_carrot_base_damage = 18.0f;
inline float default_carrot_base_health = 18.0f;
inline float default_carrot_base_radius = 10.0f;
inline float default_carrot_copy = 1.0f;
inline float default_carrot_lifetime_multiplier = 0.6666667f;
inline float default_carrot_mass = 2.0f;
inline float default_carrot_reload = 1.0f;
inline float default_compass_base_damage = 1.0f;
inline float default_compass_base_health = 40.0f;
inline float default_compass_base_radius = 10.0f;
inline float default_compass_copy = 1.0f;
inline float default_compass_magnet_range = 1024.0f;
inline float default_compass_mass = 1.0f;
inline float default_compass_reload = 2.5f;
inline float default_compass_wait_ultra = 4.0f;
inline float default_compass_wait_super = 2.0f;
inline float default_compass_wait_unique = 1.0f;
inline float default_cogwheel_base_damage = 19.0f;
inline float default_cogwheel_base_health = 13.0f;
inline float default_cogwheel_base_radius = 10.0f;
inline float default_cogwheel_copy = 1.0f;
inline float default_cogwheel_mass = 2.0f;
inline float default_cogwheel_reload = 2.5f;
inline float default_cogwheel_stop_fraction = 0.55f;
inline float default_corruption_base_radius = 10.0f;
inline float default_corruption_copy = 0.0f;
inline float default_corruption_radius_above_ultra = 1024.0f;
inline float default_corruption_radius_primordial = 4096.0f;
inline float default_detection_radius = 512.0f;
inline float default_absorb_range = 64.0f;
inline float default_drop_lifetime = 30.0f;
inline float default_drop_mass = 0.01f;
inline float default_drop_pickup_delay = 0.5f;
inline float default_drop_pickup_range = 64.0f;
inline float default_drop_radius = 8.0f;
inline float default_drop_stack_range = 128.0f;
inline float default_dust_base_damage = 4.3f;
inline float default_dust_base_health = 1.7f;
inline float default_dust_base_radius = 7.5f;
inline float default_dust_copy = 3.0f;
inline float default_dust_mass = 1.0f;
inline float default_dust_reload_reduction = 0.03f;
inline float default_flower_petal_num_max = 10.0f;
inline float default_flower_radius = 20.0f;
inline float default_faster_base_damage = 12.0f;
inline float default_faster_base_health = 5.0f;
inline float default_faster_base_radius = 10.0f;
inline float default_faster_mass = 1.0f;
inline float default_faster_reload = 2.5f;
inline float default_faster_rotation_speed_base = 0.3f;
inline float default_faster_rotation_speed_level = 0.2f;
inline float default_faster_target_follow_multiplier = 2.0f;
inline float default_goldenleaf_base_damage = 16.0f;
inline float default_goldenleaf_base_health = 12.0f;
inline float default_goldenleaf_base_radius = 10.0f;
inline float default_goldenleaf_base_reload_reduction = 0.033f;
inline float default_goldenleaf_copy = 1.0f;
inline float default_goldenleaf_mass = 3.0f;
inline float default_horizon = 256.0f;
inline float default_heavy_base_damage = 10.0f;
inline float default_heavy_base_health = 150.0f;
inline float default_heavy_base_radius = 22.5f;
inline float default_heavy_mass_multiplier = 120.0f;
inline float default_heavy_reload = 10.0f;
inline float default_heavy_rotation_speed = -0.33f;
inline float default_iris_base_damage = 5.0f;
inline float default_iris_base_health = 5.0f;
inline float default_iris_base_radius = 10.0f;
inline float default_iris_copy = 1.0f;
inline float default_iris_flower_damage_multiplier = 0.5f;
inline float default_iris_mass = 2.0f;
inline float default_iris_poison_duration = 3.0f;
inline float default_iris_poison_total_damage = 70.0f;
inline float default_iris_reload = 4.0f;
inline float default_lentil_base_damage = 13.0f;
inline float default_lentil_base_health = 12.0f;
inline float default_lentil_base_radius = 10.0f;
inline float default_lentil_copy = 1.0f;
inline float default_lentil_mass = 2.0f;
inline float default_lentil_petal_attraction_range = 8.0f;
inline float default_lentil_reload = 2.0f;
inline std::string lobby_map_path = "data/maps/training_grounds.tmj";
inline float default_max_velocity = 75.0f;
inline float default_moon_base_damage = 3.0f;
inline float default_moon_base_health = 5000.0f;
inline float default_moon_base_mass = 5.0f;
inline float default_moon_orbit_k = 1.5f;
inline float default_moon_radius_step = 10.0f;
inline float default_moon_reload = 100.0f;
inline float default_moon_slowdown_time = 1.0f;
inline float default_moon_stop_velocity_epsilon = 1.0f;
inline float default_missile_arm_time = 0.5f;
inline float default_missile_base_damage = 35.0f;
inline float default_missile_base_health = 2.0f;
inline float default_missile_base_radius = 10.0f;
inline float default_missile_copy = 1.0f;
inline float default_missile_lifetime = 5.0f;
inline float default_missile_mass = 2.0f;
inline float default_missile_lock_angle_degrees = 30.0f;
inline float default_missile_lock_range = 1024.0f;
inline float default_missile_reload = 1.5f;
inline float default_missile_speed_multiplier = 8.0f;
inline float default_missile_unfired_damage_multiplier = 0.2f;
inline float default_petal_attack_offset = 18.0f;
inline float default_petal_defend_offset = -20.0f;
inline float default_petal_neutral_reach = 8.0f;
inline float default_petal_orbit_k = 5.0f;
inline float default_petal_orbit_radius = 16.0f;
inline float default_petal_pow = 3.0f;
inline float default_petal_preload = 2.5f;
inline float default_petal_radius = 15.0f;
inline float default_petal_reload = 2.5f;
inline float default_petal_reload_multiplier_min = 0.001f;
inline float default_petal_stat_reload_multiplier_min = 0.05f;
inline float default_pincer_base_damage = 5.0f;
inline float default_pincer_base_health = 5.0f;
inline float default_pincer_base_radius = 10.0f;
inline float default_pincer_copy = 1.0f;
inline float default_pincer_mass = 2.0f;
inline float default_pincer_poison_duration = 0.75f;
inline float default_pincer_poison_total_damage = 15.0f;
inline float default_pincer_reload = 2.5f;
inline float default_pincer_slow_duration = 0.8f;
inline float default_relic_base_damage = 0.0f;
inline float default_relic_base_health = 10.0f;
inline float default_relic_base_radius = 10.0f;
inline float default_relic_copy = 0.0f;
inline float default_relic_health_bonus_common = 0.10f;
inline float default_relic_health_bonus_epic = 0.40f;
inline float default_relic_health_bonus_eternal = 1.50f;
inline float default_relic_health_bonus_legendary = 0.50f;
inline float default_relic_health_bonus_mythic = 0.60f;
inline float default_relic_health_bonus_primordial = 2.50f;
inline float default_relic_health_bonus_rare = 0.30f;
inline float default_relic_health_bonus_super = 1.00f;
inline float default_relic_health_bonus_ultra = 0.75f;
inline float default_relic_health_bonus_unique = 1.50f;
inline float default_relic_health_bonus_unusual = 0.20f;
inline float default_relic_mass = 2.0f;
inline float default_relic_psionic_zone_radius = 1024.0f;
inline float default_relic_psionic_zone_radius_primordial = 4096.0f;
inline float default_relic_psionic_zone_refresh = 0.25f;
inline float default_relic_reload = 2.5f;
inline float default_rose_base_damage = 5.0f;
inline float default_rose_base_health = 5.0f;
inline float default_rose_base_medicine = 7.5f;
inline float default_rose_base_radius = 10.0f;
inline float default_rose_copy = 1.0f;
inline float default_rose_mass = 2.0f;
inline float default_rose_preload = 1.5f;
inline float default_rose_reload = 3.5f;
inline float default_stinger_base_damage = 100.0f;
inline float default_stinger_base_health = 1.0f;
inline float default_stinger_base_radius = 10.0f;
inline float default_stinger_mass = 2.0f;
inline float default_stinger_reload = 10.0f;
inline float default_yinyang_base_damage = 10.0f;
inline float default_yinyang_base_health = 10.0f;
inline float default_yinyang_base_radius = 12.0f;
inline float default_yinyang_copy = 1.0f;
inline float default_yinyang_mass = 2.0f;
inline float default_yinyang_reload = 2.0f;
inline float default_yggdrasil_base_health = 10.0f;
inline float default_yggdrasil_base_radius = 10.0f;
inline float default_yggdrasil_channel_common = 600.0f;
inline float default_yggdrasil_channel_super = 1.0f;
inline float default_yggdrasil_channel_eternal = 0.33f;
inline float default_yggdrasil_channel_primordial = 0.016f;
inline float default_yggdrasil_heal_fraction = 1.0f;
inline float default_yggdrasil_preload = 5.0f;
inline float entity_collision_epsilon = 0.001f;
inline float melee_random_idle_chance = 0.5f;
inline float melee_random_idle_time = 2.0f;
inline float melee_random_wander_divisor = 4.0f;
inline float melee_retarget_chance_multiplier = 2.0f;
inline float melee_target_time = 80.0f;
inline float mob_beetle_armor = 1.0f;
inline float mob_beetle_damage = 30.0f;
inline float mob_beetle_mass = 10.0f;
inline float mob_beetle_max_health = 100.0f;
inline float mob_beetle_radius = 18.0f;
inline float mob_beetle_turn_speed = 1.5f;
inline int mob_beetle_team = 2;
inline float mob_bee_acceleration = 450.0f;
inline float mob_bee_armor = 1.0f;
inline float mob_bee_damage = 50.0f;
inline float mob_bee_mass = 2.5f;
inline float mob_bee_max_health = 35.0f;
inline float mob_bee_max_velocity = 225.0f;
inline float mob_bee_radius = 12.0f;
inline int mob_bee_team = 2;
inline float mob_bee_wave_frequency = 5.2f;
inline float mob_bee_wave_strength = 0.85f;
inline float mob_bumblebee_acceleration = 450.0f;
inline float mob_bumblebee_armor = 1.0f;
inline float mob_bumblebee_damage = 20.0f;
inline float mob_bumblebee_mass = 2.5f;
inline float mob_bumblebee_max_health = 25.0f;
inline float mob_bumblebee_max_velocity = 225.0f;
inline float mob_bumblebee_pollen_base_damage = 10.0f;
inline float mob_bumblebee_pollen_base_health = 5.0f;
inline float mob_bumblebee_pollen_interval = 0.25f;
inline float mob_bumblebee_pollen_lifetime = 2.0f;
inline float mob_bumblebee_pollen_radius_multiplier = 0.125f;
inline float mob_bumblebee_radius = 12.0f;
inline int mob_bumblebee_team = 2;
inline float mob_bumblebee_turn_interval_max = 2.25f;
inline float mob_bumblebee_turn_interval_min = 0.65f;
inline float mob_bumblebee_turn_max_angle = 0.95f;
inline float mob_hornet_acceleration = 300.0f;
inline float mob_hornet_armor = 1.0f;
inline float mob_hornet_attack_interval = 1.75f;
inline float mob_hornet_attack_recovery = 0.3f;
inline float mob_hornet_attack_windup = 0.2f;
inline float mob_hornet_damage = 50.0f;
inline float mob_hornet_mass = 2.5f;
inline float mob_hornet_max_health = 40.0f;
inline float mob_hornet_max_velocity = default_max_velocity;
inline float mob_hornet_missile_base_damage = 10.0f;
inline float mob_hornet_missile_base_health = 5.0f;
inline float mob_hornet_missile_radius = 10.0f;
inline float mob_hornet_missile_speed = default_max_velocity * 4.0f;
inline float mob_hornet_radius = 12.0f;
inline float mob_hornet_recoil_speed = default_max_velocity * 2.0f;
inline int mob_hornet_team = 2;
inline float mob_bandage_beetle_armor = 1.0f;
inline float mob_bandage_beetle_damage = 30.0f;
inline float mob_bandage_beetle_mass = 10.0f;
inline float mob_bandage_beetle_max_health = 70.0f;
inline float mob_bandage_beetle_radius = 18.0f;
inline int mob_bandage_beetle_team = 2;
inline float mob_damage_scale_base = 3.0f;
inline float mob_horizon_scale_exp = 1.25f;
inline size_t max_lootable_players = 5;
inline size_t max_lootable_player_above_ultra = 25;
inline float mob_mass_scale_base = 2.5f;
inline float mob_mass_scale_exp_multiplier = 0.6f;
inline float mob_velocity_multiplier = 1.25f;
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
inline int nullification_level_gap = 2;
inline float psionic_connection_range = 2048.0f;
inline float mob_player_flower_armor = 0.0f;
inline float mob_player_flower_damage = 25.0f;
inline float mob_player_flower_level_damage_growth = 1.0565f;
inline float mob_player_flower_level_health_growth = 1.0565f;
inline float mob_player_flower_mass = 5.0f;
inline float mob_player_flower_max_health = 200.0f;
inline int mob_player_flower_max_stat_level = 75;
inline int mob_player_flower_initial_petal_slots = 5;
inline int mob_player_flower_max_petal_slots = 10;
inline float mob_player_flower_petal_rotation_speed = 1.5f;
inline float mob_player_flower_radius = 18.0f;
inline int mob_player_flower_team = 1;
inline float mob_radius_scale_exp = 1.5f;
inline float mob_stop_damping = 0.9f;
inline float mob_stop_velocity_epsilon = 1e-5f;
inline float mob_slow_to_max_velocity_time = 0.35f;
inline float mob_soldier_ant_armor = 1.0f;
inline float mob_soldier_ant_damage = 10.0f;
inline float mob_soldier_ant_mass = 3.3333333f;
inline float mob_soldier_ant_max_health = 100.0f;
inline float mob_soldier_ant_radius = 12.0f;
inline int mob_soldier_ant_team = 2;
inline float mob_soldier_fire_ant_armor = 1.0f;
inline float mob_soldier_fire_ant_damage = 20.0f;
inline float mob_soldier_fire_ant_mass = 3.3333333f;
inline float mob_soldier_fire_ant_max_health = 100.0f;
inline float mob_soldier_fire_ant_radius = 12.0f;
inline int mob_soldier_fire_ant_team = 2;
inline float mob_soldier_termite_armor = 1.0f;
inline float mob_soldier_termite_damage = 10.0f;
inline float mob_soldier_termite_mass = 3.3333333f;
inline float mob_soldier_termite_max_health = 200.0f;
inline float mob_soldier_termite_radius = 12.0f;
inline int mob_soldier_termite_team = 2;
inline float mob_summoned_beetle_armor = 1.0f;
inline float mob_summoned_beetle_damage = 30.0f;
inline float mob_summoned_beetle_follow_range = 120.0f;
inline float mob_summoned_beetle_hard_range = 360.0f;
inline float mob_summoned_beetle_horizon = 512.0f;
inline float mob_summoned_beetle_mass = 9.0f;
inline float mob_summoned_beetle_max_health = 100.0f;
inline float mob_summoned_beetle_radius = 16.0f;
inline float mob_summoned_beetle_scale_exp = 0.5f;
inline int mob_summoned_beetle_team = 1;
inline float mob_summoned_search_range_multiplier = 2.0f;
inline float mob_summoned_target_time = 0.35f;
inline float mob_summoned_wander_follow_range_multiplier = 0.08f;
inline float mob_summoned_wander_radius_multiplier = 1.2f;
inline float mob_summoned_soldier_ant_armor = 1.0f;
inline float mob_summoned_soldier_ant_damage = 10.0f;
inline float mob_summoned_soldier_ant_horizon = 512.0f;
inline float mob_summoned_soldier_ant_mass = 3.0f;
inline float mob_summoned_soldier_ant_max_health = 100.0f;
inline float mob_summoned_soldier_ant_radius = 10.0f;
inline int mob_summoned_soldier_ant_team = 1;
inline size_t network_max_receive_buffer_size = 1024;
inline size_t network_max_send_buffer_size = 1024 * 1024;
inline size_t network_receive_chunk_size = 256;
inline float network_snapshot_interval = 1.0f / 30.0f;
inline uint8_t network_petal_type_offset = 100;
inline float open_initial_spawn_delay = 5.f;
inline float open_spawn_interval = 1.f;
inline float open_super_plus_block_radius = 1024.0f * 8.0f;
inline float open_super_plus_suppress_multiplier = 0.35f;
inline float open_super_plus_suppress_radius = 1024.0f * 16.0f;
inline float open_spawn_x = 500.0f;
inline float open_spawn_y = 0.0f;
inline float pi = 3.14159265359f;
inline float player_input_axis_max = 127.0f;
inline float player_move_target_distance = 100123.0f;
inline float player_respawn_x = 5376.0f;
inline float player_respawn_y = 5376.0f;
inline int port = 10012;
inline std::string report_deepseek_api_key = "";
inline std::string report_deepseek_api_url = "https://api.deepseek.com/chat/completions";
inline std::string report_deepseek_model = "deepseek-chat";
inline int report_invalid_disable_count = 3;
inline float report_keyword_base_mute_seconds = 60.0f;
inline float report_scan_window_seconds = 60.0f;
inline float server_fixed_dt = 0.016f;
inline float spatial_grid_cell_size = 200.0f;
inline float timeout_protection_seconds = 30.0f;
inline bool gui_console_enabled = false;

template <typename T> inline void SetConfigValue(T& variable, const std::string& value)
{
    std::stringstream stream(value);
    if constexpr (std::is_same_v<T, uint8_t>)
    {
        int parsed = 0;
        stream >> parsed;
        if (!stream.fail()) variable = static_cast<uint8_t>(parsed);
    } else if constexpr (std::is_same_v<T, std::string>) {
        variable = value;
    } else {
        T parsed{};
        stream >> parsed;
        if (!stream.fail()) variable = parsed;
    }
}

template <typename T> inline std::string GetConfigValue(const T& variable)
{
    if constexpr (std::is_same_v<T, uint8_t>) return std::to_string(static_cast<int>(variable));
    else if constexpr (std::is_same_v<T, std::string>) return variable;
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
        REGISTER_CONFIG("account_data_path", account_data_path),
        REGISTER_CONFIG("startup_commands_path", startup_commands_path),
        REGISTER_CONFIG("gui_console", gui_console_enabled),
        REGISTER_CONFIG("acceleration", default_acceleration),
        REGISTER_CONFIG("air_base_mass", default_air_base_mass),
        REGISTER_CONFIG("air_base_radius", default_air_base_radius),
        REGISTER_CONFIG("antegg_base_health", default_antegg_base_health),
        REGISTER_CONFIG("antegg_base_radius", default_antegg_base_radius),
        REGISTER_CONFIG("antegg_copy", default_antegg_copy),
        REGISTER_CONFIG("antegg_mass", default_antegg_mass),
        REGISTER_CONFIG("antegg_reload_common", default_antegg_reload_common),
        REGISTER_CONFIG("antegg_reload_epic", default_antegg_reload_epic),
        REGISTER_CONFIG("antegg_reload_eternal", default_antegg_reload_eternal),
        REGISTER_CONFIG("antegg_reload_legendary", default_antegg_reload_legendary),
        REGISTER_CONFIG("antegg_reload_mythic", default_antegg_reload_mythic),
        REGISTER_CONFIG("antegg_reload_primordial", default_antegg_reload_primordial),
        REGISTER_CONFIG("antegg_reload_rare", default_antegg_reload_rare),
        REGISTER_CONFIG("antegg_reload_super", default_antegg_reload_super),
        REGISTER_CONFIG("antegg_reload_ultra", default_antegg_reload_ultra),
        REGISTER_CONFIG("antegg_reload_unique", default_antegg_reload_unique),
        REGISTER_CONFIG("antegg_reload_unusual", default_antegg_reload_unusual),
        REGISTER_CONFIG("antennae_horizon_common", default_antennae_horizon_common),
        REGISTER_CONFIG("antennae_horizon_epic", default_antennae_horizon_epic),
        REGISTER_CONFIG("antennae_horizon_eternal", default_antennae_horizon_eternal),
        REGISTER_CONFIG("antennae_horizon_legendary", default_antennae_horizon_legendary),
        REGISTER_CONFIG("antennae_horizon_mythic", default_antennae_horizon_mythic),
        REGISTER_CONFIG("antennae_horizon_primordial", default_antennae_horizon_primordial),
        REGISTER_CONFIG("antennae_horizon_rare", default_antennae_horizon_rare),
        REGISTER_CONFIG("antennae_horizon_super", default_antennae_horizon_super),
        REGISTER_CONFIG("antennae_horizon_ultra", default_antennae_horizon_ultra),
        REGISTER_CONFIG("antennae_horizon_unique", default_antennae_horizon_unique),
        REGISTER_CONFIG("antennae_horizon_unusual", default_antennae_horizon_unusual),
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
        REGISTER_CONFIG("bandage_base_radius", default_bandage_base_radius),
        REGISTER_CONFIG("bandage_copy", default_bandage_copy),
        REGISTER_CONFIG("bandage_no_revive_duration", default_bandage_no_revive_duration),
        REGISTER_CONFIG("bandage_undead_common", default_bandage_undead_common),
        REGISTER_CONFIG("bandage_undead_epic", default_bandage_undead_epic),
        REGISTER_CONFIG("bandage_undead_eternal", default_bandage_undead_eternal),
        REGISTER_CONFIG("bandage_undead_legendary", default_bandage_undead_legendary),
        REGISTER_CONFIG("bandage_undead_mythic", default_bandage_undead_mythic),
        REGISTER_CONFIG("bandage_undead_primordial", default_bandage_undead_primordial),
        REGISTER_CONFIG("bandage_undead_rare", default_bandage_undead_rare),
        REGISTER_CONFIG("bandage_undead_super", default_bandage_undead_super),
        REGISTER_CONFIG("bandage_undead_ultra", default_bandage_undead_ultra),
        REGISTER_CONFIG("bandage_undead_unique", default_bandage_undead_unique),
        REGISTER_CONFIG("bandage_undead_unusual", default_bandage_undead_unusual),
        REGISTER_CONFIG("blood_sacrifice_base_radius", default_blood_sacrifice_base_radius),
        REGISTER_CONFIG("blood_sacrifice_copy", default_blood_sacrifice_copy),
        REGISTER_CONFIG("blood_sacrifice_delay", default_blood_sacrifice_delay),
        REGISTER_CONFIG("bubble_base_health", default_bubble_base_health),
        REGISTER_CONFIG("bubble_base_radius", default_bubble_base_radius),
        REGISTER_CONFIG("bubble_boost_speed", default_bubble_boost_speed),
        REGISTER_CONFIG("bubble_copy", default_bubble_copy),
        REGISTER_CONFIG("bubble_mass", default_bubble_mass),
        REGISTER_CONFIG("bubble_reload_common", default_bubble_reload_common),
        REGISTER_CONFIG("bubble_reload_epic", default_bubble_reload_epic),
        REGISTER_CONFIG("bubble_reload_eternal", default_bubble_reload_eternal),
        REGISTER_CONFIG("bubble_reload_legendary", default_bubble_reload_legendary),
        REGISTER_CONFIG("bubble_reload_mythic", default_bubble_reload_mythic),
        REGISTER_CONFIG("bubble_reload_primordial", default_bubble_reload_primordial),
        REGISTER_CONFIG("bubble_reload_rare", default_bubble_reload_rare),
        REGISTER_CONFIG("bubble_reload_super", default_bubble_reload_super),
        REGISTER_CONFIG("bubble_reload_ultra", default_bubble_reload_ultra),
        REGISTER_CONFIG("bubble_reload_unique", default_bubble_reload_unique),
        REGISTER_CONFIG("bubble_reload_unusual", default_bubble_reload_unusual),
        REGISTER_CONFIG("carrot_base_damage", default_carrot_base_damage),
        REGISTER_CONFIG("carrot_base_health", default_carrot_base_health),
        REGISTER_CONFIG("carrot_base_radius", default_carrot_base_radius),
        REGISTER_CONFIG("carrot_copy", default_carrot_copy),
        REGISTER_CONFIG("carrot_lifetime_multiplier", default_carrot_lifetime_multiplier),
        REGISTER_CONFIG("carrot_mass", default_carrot_mass),
        REGISTER_CONFIG("carrot_reload", default_carrot_reload),
        REGISTER_CONFIG("compass_base_damage", default_compass_base_damage),
        REGISTER_CONFIG("compass_base_health", default_compass_base_health),
        REGISTER_CONFIG("compass_base_radius", default_compass_base_radius),
        REGISTER_CONFIG("compass_copy", default_compass_copy),
        REGISTER_CONFIG("compass_magnet_range", default_compass_magnet_range),
        REGISTER_CONFIG("compass_mass", default_compass_mass),
        REGISTER_CONFIG("compass_reload", default_compass_reload),
        REGISTER_CONFIG("compass_wait_super", default_compass_wait_super),
        REGISTER_CONFIG("compass_wait_ultra", default_compass_wait_ultra),
        REGISTER_CONFIG("compass_wait_unique", default_compass_wait_unique),
        REGISTER_CONFIG("cogwheel_base_damage", default_cogwheel_base_damage),
        REGISTER_CONFIG("cogwheel_base_health", default_cogwheel_base_health),
        REGISTER_CONFIG("cogwheel_base_radius", default_cogwheel_base_radius),
        REGISTER_CONFIG("cogwheel_copy", default_cogwheel_copy),
        REGISTER_CONFIG("cogwheel_mass", default_cogwheel_mass),
        REGISTER_CONFIG("cogwheel_reload", default_cogwheel_reload),
        REGISTER_CONFIG("cogwheel_stop_fraction", default_cogwheel_stop_fraction),
        REGISTER_CONFIG("corruption_base_radius", default_corruption_base_radius),
        REGISTER_CONFIG("corruption_copy", default_corruption_copy),
        REGISTER_CONFIG("corruption_radius_above_ultra", default_corruption_radius_above_ultra),
        REGISTER_CONFIG("corruption_radius_primordial", default_corruption_radius_primordial),
        REGISTER_CONFIG("detection_radius", default_detection_radius),
        REGISTER_CONFIG("absorb_range", default_absorb_range),
        REGISTER_CONFIG("drop_lifetime", default_drop_lifetime),
        REGISTER_CONFIG("drop_mass", default_drop_mass),
        REGISTER_CONFIG("drop_pickup_delay", default_drop_pickup_delay),
        REGISTER_CONFIG("drop_pickup_range", default_drop_pickup_range),
        REGISTER_CONFIG("drop_radius", default_drop_radius),
        REGISTER_CONFIG("drop_stack_range", default_drop_stack_range),
        REGISTER_CONFIG("dust_base_damage", default_dust_base_damage),
        REGISTER_CONFIG("dust_base_health", default_dust_base_health),
        REGISTER_CONFIG("dust_base_radius", default_dust_base_radius),
        REGISTER_CONFIG("dust_copy", default_dust_copy),
        REGISTER_CONFIG("dust_mass", default_dust_mass),
        REGISTER_CONFIG("dust_reload_reduction", default_dust_reload_reduction),
        REGISTER_CONFIG("entity_collision_epsilon", entity_collision_epsilon),
        REGISTER_CONFIG("faster_base_damage", default_faster_base_damage),
        REGISTER_CONFIG("faster_base_health", default_faster_base_health),
        REGISTER_CONFIG("faster_base_radius", default_faster_base_radius),
        REGISTER_CONFIG("faster_mass", default_faster_mass),
        REGISTER_CONFIG("faster_reload", default_faster_reload),
        REGISTER_CONFIG("faster_rotation_speed_base", default_faster_rotation_speed_base),
        REGISTER_CONFIG("faster_rotation_speed_level", default_faster_rotation_speed_level),
        REGISTER_CONFIG("faster_target_follow_multiplier", default_faster_target_follow_multiplier),
        REGISTER_CONFIG("flower_petal_num_max", default_flower_petal_num_max),
        REGISTER_CONFIG("flower_radius", default_flower_radius),
        REGISTER_CONFIG("goldenleaf_base_damage", default_goldenleaf_base_damage),
        REGISTER_CONFIG("goldenleaf_base_health", default_goldenleaf_base_health),
        REGISTER_CONFIG("goldenleaf_base_radius", default_goldenleaf_base_radius),
        REGISTER_CONFIG("goldenleaf_base_reload_reduction", default_goldenleaf_base_reload_reduction),
        REGISTER_CONFIG("goldenleaf_copy", default_goldenleaf_copy),
        REGISTER_CONFIG("goldenleaf_mass", default_goldenleaf_mass),
        REGISTER_CONFIG("heavy_base_damage", default_heavy_base_damage),
        REGISTER_CONFIG("heavy_base_health", default_heavy_base_health),
        REGISTER_CONFIG("heavy_base_radius", default_heavy_base_radius),
        REGISTER_CONFIG("heavy_mass_multiplier", default_heavy_mass_multiplier),
        REGISTER_CONFIG("heavy_reload", default_heavy_reload),
        REGISTER_CONFIG("heavy_rotation_speed", default_heavy_rotation_speed),
        REGISTER_CONFIG("horizon", default_horizon),
        REGISTER_CONFIG("iris_base_damage", default_iris_base_damage),
        REGISTER_CONFIG("iris_base_health", default_iris_base_health),
        REGISTER_CONFIG("iris_base_radius", default_iris_base_radius),
        REGISTER_CONFIG("iris_copy", default_iris_copy),
        REGISTER_CONFIG("iris_flower_damage_multiplier", default_iris_flower_damage_multiplier),
        REGISTER_CONFIG("iris_mass", default_iris_mass),
        REGISTER_CONFIG("iris_poison_duration", default_iris_poison_duration),
        REGISTER_CONFIG("iris_poison_total_damage", default_iris_poison_total_damage),
        REGISTER_CONFIG("iris_reload", default_iris_reload),
        REGISTER_CONFIG("lentil_base_damage", default_lentil_base_damage),
        REGISTER_CONFIG("lentil_base_health", default_lentil_base_health),
        REGISTER_CONFIG("lentil_base_radius", default_lentil_base_radius),
        REGISTER_CONFIG("lentil_copy", default_lentil_copy),
        REGISTER_CONFIG("lentil_mass", default_lentil_mass),
        REGISTER_CONFIG("lentil_petal_attraction_range", default_lentil_petal_attraction_range),
        REGISTER_CONFIG("lentil_reload", default_lentil_reload),
        REGISTER_CONFIG("lobby_map_path", lobby_map_path),
        REGISTER_CONFIG("max_velocity", default_max_velocity),
        REGISTER_CONFIG("melee_random_idle_chance", melee_random_idle_chance),
        REGISTER_CONFIG("melee_random_idle_time", melee_random_idle_time),
        REGISTER_CONFIG("melee_random_wander_divisor", melee_random_wander_divisor),
        REGISTER_CONFIG("melee_retarget_chance_multiplier", melee_retarget_chance_multiplier),
        REGISTER_CONFIG("melee_target_time", melee_target_time),
        REGISTER_CONFIG("moon_base_damage", default_moon_base_damage),
        REGISTER_CONFIG("moon_base_health", default_moon_base_health),
        REGISTER_CONFIG("moon_base_mass", default_moon_base_mass),
        REGISTER_CONFIG("moon_orbit_k", default_moon_orbit_k),
        REGISTER_CONFIG("moon_radius_step", default_moon_radius_step),
        REGISTER_CONFIG("moon_reload", default_moon_reload),
        REGISTER_CONFIG("moon_slowdown_time", default_moon_slowdown_time),
        REGISTER_CONFIG("moon_stop_velocity_epsilon", default_moon_stop_velocity_epsilon),
        REGISTER_CONFIG("missile_arm_time", default_missile_arm_time),
        REGISTER_CONFIG("missile_base_damage", default_missile_base_damage),
        REGISTER_CONFIG("missile_base_health", default_missile_base_health),
        REGISTER_CONFIG("missile_base_radius", default_missile_base_radius),
        REGISTER_CONFIG("missile_copy", default_missile_copy),
        REGISTER_CONFIG("missile_lifetime", default_missile_lifetime),
        REGISTER_CONFIG("missile_lock_angle_degrees", default_missile_lock_angle_degrees),
        REGISTER_CONFIG("missile_lock_range", default_missile_lock_range),
        REGISTER_CONFIG("missile_mass", default_missile_mass),
        REGISTER_CONFIG("missile_reload", default_missile_reload),
        REGISTER_CONFIG("missile_speed_multiplier", default_missile_speed_multiplier),
        REGISTER_CONFIG("missile_unfired_damage_multiplier", default_missile_unfired_damage_multiplier),
        REGISTER_CONFIG("mob_beetle_armor", mob_beetle_armor),
        REGISTER_CONFIG("mob_beetle_damage", mob_beetle_damage),
        REGISTER_CONFIG("mob_beetle_mass", mob_beetle_mass),
        REGISTER_CONFIG("mob_beetle_max_health", mob_beetle_max_health),
        REGISTER_CONFIG("mob_beetle_radius", mob_beetle_radius),
        REGISTER_CONFIG("mob_beetle_turn_speed", mob_beetle_turn_speed),
        REGISTER_CONFIG("mob_beetle_team", mob_beetle_team),
        REGISTER_CONFIG("mob_bee_acceleration", mob_bee_acceleration),
        REGISTER_CONFIG("mob_bee_armor", mob_bee_armor),
        REGISTER_CONFIG("mob_bee_damage", mob_bee_damage),
        REGISTER_CONFIG("mob_bee_mass", mob_bee_mass),
        REGISTER_CONFIG("mob_bee_max_health", mob_bee_max_health),
        REGISTER_CONFIG("mob_bee_max_velocity", mob_bee_max_velocity),
        REGISTER_CONFIG("mob_bee_radius", mob_bee_radius),
        REGISTER_CONFIG("mob_bee_team", mob_bee_team),
        REGISTER_CONFIG("mob_bee_wave_frequency", mob_bee_wave_frequency),
        REGISTER_CONFIG("mob_bee_wave_strength", mob_bee_wave_strength),
        REGISTER_CONFIG("mob_bumblebee_acceleration", mob_bumblebee_acceleration),
        REGISTER_CONFIG("mob_bumblebee_armor", mob_bumblebee_armor),
        REGISTER_CONFIG("mob_bumblebee_damage", mob_bumblebee_damage),
        REGISTER_CONFIG("mob_bumblebee_mass", mob_bumblebee_mass),
        REGISTER_CONFIG("mob_bumblebee_max_health", mob_bumblebee_max_health),
        REGISTER_CONFIG("mob_bumblebee_max_velocity", mob_bumblebee_max_velocity),
        REGISTER_CONFIG("mob_bumblebee_pollen_base_damage", mob_bumblebee_pollen_base_damage),
        REGISTER_CONFIG("mob_bumblebee_pollen_base_health", mob_bumblebee_pollen_base_health),
        REGISTER_CONFIG("mob_bumblebee_pollen_interval", mob_bumblebee_pollen_interval),
        REGISTER_CONFIG("mob_bumblebee_pollen_lifetime", mob_bumblebee_pollen_lifetime),
        REGISTER_CONFIG("mob_bumblebee_pollen_radius_multiplier", mob_bumblebee_pollen_radius_multiplier),
        REGISTER_CONFIG("mob_bumblebee_radius", mob_bumblebee_radius),
        REGISTER_CONFIG("mob_bumblebee_team", mob_bumblebee_team),
        REGISTER_CONFIG("mob_bumblebee_turn_interval_max", mob_bumblebee_turn_interval_max),
        REGISTER_CONFIG("mob_bumblebee_turn_interval_min", mob_bumblebee_turn_interval_min),
        REGISTER_CONFIG("mob_bumblebee_turn_max_angle", mob_bumblebee_turn_max_angle),
        REGISTER_CONFIG("mob_hornet_acceleration", mob_hornet_acceleration),
        REGISTER_CONFIG("mob_hornet_armor", mob_hornet_armor),
        REGISTER_CONFIG("mob_hornet_attack_interval", mob_hornet_attack_interval),
        REGISTER_CONFIG("mob_hornet_attack_recovery", mob_hornet_attack_recovery),
        REGISTER_CONFIG("mob_hornet_attack_windup", mob_hornet_attack_windup),
        REGISTER_CONFIG("mob_hornet_damage", mob_hornet_damage),
        REGISTER_CONFIG("mob_hornet_mass", mob_hornet_mass),
        REGISTER_CONFIG("mob_hornet_max_health", mob_hornet_max_health),
        REGISTER_CONFIG("mob_hornet_max_velocity", mob_hornet_max_velocity),
        REGISTER_CONFIG("mob_hornet_missile_base_damage", mob_hornet_missile_base_damage),
        REGISTER_CONFIG("mob_hornet_missile_base_health", mob_hornet_missile_base_health),
        REGISTER_CONFIG("mob_hornet_missile_radius", mob_hornet_missile_radius),
        REGISTER_CONFIG("mob_hornet_missile_speed", mob_hornet_missile_speed),
        REGISTER_CONFIG("mob_hornet_radius", mob_hornet_radius),
        REGISTER_CONFIG("mob_hornet_recoil_speed", mob_hornet_recoil_speed),
        REGISTER_CONFIG("mob_hornet_team", mob_hornet_team),
        REGISTER_CONFIG("mob_bandage_beetle_armor", mob_bandage_beetle_armor),
        REGISTER_CONFIG("mob_bandage_beetle_damage", mob_bandage_beetle_damage),
        REGISTER_CONFIG("mob_bandage_beetle_mass", mob_bandage_beetle_mass),
        REGISTER_CONFIG("mob_bandage_beetle_max_health", mob_bandage_beetle_max_health),
        REGISTER_CONFIG("mob_bandage_beetle_radius", mob_bandage_beetle_radius),
        REGISTER_CONFIG("mob_bandage_beetle_team", mob_bandage_beetle_team),
        REGISTER_CONFIG("mob_damage_scale_base", mob_damage_scale_base),
        REGISTER_CONFIG("mob_horizon_scale_exp", mob_horizon_scale_exp),
        REGISTER_CONFIG("max_lootable_players", max_lootable_players),
        REGISTER_CONFIG("max_lootable_player_above_ultra", max_lootable_player_above_ultra),
        REGISTER_CONFIG("server_log_path", server_log_path),
        REGISTER_CONFIG("mob_mass_scale_base", mob_mass_scale_base),
        REGISTER_CONFIG("mob_mass_scale_exp_multiplier", mob_mass_scale_exp_multiplier),
        REGISTER_CONFIG("mob_velocity_multiplier", mob_velocity_multiplier),
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
        REGISTER_CONFIG("nullification_level_gap", nullification_level_gap),
        REGISTER_CONFIG("psionic_connection_range", psionic_connection_range),
        REGISTER_CONFIG("mob_player_flower_armor", mob_player_flower_armor),
        REGISTER_CONFIG("mob_player_flower_damage", mob_player_flower_damage),
        REGISTER_CONFIG("mob_player_flower_level_damage_growth", mob_player_flower_level_damage_growth),
        REGISTER_CONFIG("mob_player_flower_level_health_growth", mob_player_flower_level_health_growth),
        REGISTER_CONFIG("mob_player_flower_initial_petal_slots", mob_player_flower_initial_petal_slots),
        REGISTER_CONFIG("mob_player_flower_mass", mob_player_flower_mass),
        REGISTER_CONFIG("mob_player_flower_max_health", mob_player_flower_max_health),
        REGISTER_CONFIG("mob_player_flower_max_petal_slots", mob_player_flower_max_petal_slots),
        REGISTER_CONFIG("mob_player_flower_max_stat_level", mob_player_flower_max_stat_level),
        REGISTER_CONFIG("mob_player_flower_petal_rotation_speed", mob_player_flower_petal_rotation_speed),
        REGISTER_CONFIG("mob_player_flower_radius", mob_player_flower_radius),
        REGISTER_CONFIG("mob_player_flower_team", mob_player_flower_team),
        REGISTER_CONFIG("mob_radius_scale_exp", mob_radius_scale_exp),
        REGISTER_CONFIG("mob_slow_to_max_velocity_time", mob_slow_to_max_velocity_time),
        REGISTER_CONFIG("mob_stop_damping", mob_stop_damping),
        REGISTER_CONFIG("mob_stop_velocity_epsilon", mob_stop_velocity_epsilon),
        REGISTER_CONFIG("mob_soldier_ant_armor", mob_soldier_ant_armor),
        REGISTER_CONFIG("mob_soldier_ant_damage", mob_soldier_ant_damage),
        REGISTER_CONFIG("mob_soldier_ant_mass", mob_soldier_ant_mass),
        REGISTER_CONFIG("mob_soldier_ant_max_health", mob_soldier_ant_max_health),
        REGISTER_CONFIG("mob_soldier_ant_radius", mob_soldier_ant_radius),
        REGISTER_CONFIG("mob_soldier_ant_team", mob_soldier_ant_team),
        REGISTER_CONFIG("mob_soldier_fire_ant_armor", mob_soldier_fire_ant_armor),
        REGISTER_CONFIG("mob_soldier_fire_ant_damage", mob_soldier_fire_ant_damage),
        REGISTER_CONFIG("mob_soldier_fire_ant_mass", mob_soldier_fire_ant_mass),
        REGISTER_CONFIG("mob_soldier_fire_ant_max_health", mob_soldier_fire_ant_max_health),
        REGISTER_CONFIG("mob_soldier_fire_ant_radius", mob_soldier_fire_ant_radius),
        REGISTER_CONFIG("mob_soldier_fire_ant_team", mob_soldier_fire_ant_team),
        REGISTER_CONFIG("mob_soldier_termite_armor", mob_soldier_termite_armor),
        REGISTER_CONFIG("mob_soldier_termite_damage", mob_soldier_termite_damage),
        REGISTER_CONFIG("mob_soldier_termite_mass", mob_soldier_termite_mass),
        REGISTER_CONFIG("mob_soldier_termite_max_health", mob_soldier_termite_max_health),
        REGISTER_CONFIG("mob_soldier_termite_radius", mob_soldier_termite_radius),
        REGISTER_CONFIG("mob_soldier_termite_team", mob_soldier_termite_team),
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
        REGISTER_CONFIG("mob_summoned_search_range_multiplier", mob_summoned_search_range_multiplier),
        REGISTER_CONFIG("mob_summoned_target_time", mob_summoned_target_time),
        REGISTER_CONFIG("mob_summoned_wander_follow_range_multiplier", mob_summoned_wander_follow_range_multiplier),
        REGISTER_CONFIG("mob_summoned_wander_radius_multiplier", mob_summoned_wander_radius_multiplier),
        REGISTER_CONFIG("mob_summoned_soldier_ant_armor", mob_summoned_soldier_ant_armor),
        REGISTER_CONFIG("mob_summoned_soldier_ant_damage", mob_summoned_soldier_ant_damage),
        REGISTER_CONFIG("mob_summoned_soldier_ant_horizon", mob_summoned_soldier_ant_horizon),
        REGISTER_CONFIG("mob_summoned_soldier_ant_mass", mob_summoned_soldier_ant_mass),
        REGISTER_CONFIG("mob_summoned_soldier_ant_max_health", mob_summoned_soldier_ant_max_health),
        REGISTER_CONFIG("mob_summoned_soldier_ant_radius", mob_summoned_soldier_ant_radius),
        REGISTER_CONFIG("mob_summoned_soldier_ant_team", mob_summoned_soldier_ant_team),
        REGISTER_CONFIG("network_max_receive_buffer_size", network_max_receive_buffer_size),
        REGISTER_CONFIG("network_max_send_buffer_size", network_max_send_buffer_size),
        REGISTER_CONFIG("network_petal_type_offset", network_petal_type_offset),
        REGISTER_CONFIG("network_receive_chunk_size", network_receive_chunk_size),
        REGISTER_CONFIG("network_snapshot_interval", network_snapshot_interval),
        REGISTER_CONFIG("open_initial_spawn_delay", open_initial_spawn_delay),
        REGISTER_CONFIG("open_spawn_interval", open_spawn_interval),
        REGISTER_CONFIG("open_super_plus_block_radius", open_super_plus_block_radius),
        REGISTER_CONFIG("open_super_plus_suppress_multiplier", open_super_plus_suppress_multiplier),
        REGISTER_CONFIG("open_super_plus_suppress_radius", open_super_plus_suppress_radius),
        REGISTER_CONFIG("open_spawn_x", open_spawn_x),
        REGISTER_CONFIG("open_spawn_y", open_spawn_y),
        REGISTER_CONFIG("petal_attack_offset", default_petal_attack_offset),
        REGISTER_CONFIG("petal_defend_offset", default_petal_defend_offset),
        REGISTER_CONFIG("petal_neutral_reach", default_petal_neutral_reach),
        REGISTER_CONFIG("petal_orbit_k", default_petal_orbit_k),
        REGISTER_CONFIG("petal_orbit_radius", default_petal_orbit_radius),
        REGISTER_CONFIG("petal_pow", default_petal_pow),
        REGISTER_CONFIG("petal_preload", default_petal_preload),
        REGISTER_CONFIG("petal_radius", default_petal_radius),
        REGISTER_CONFIG("petal_reload", default_petal_reload),
        REGISTER_CONFIG("petal_reload_multiplier_min", default_petal_reload_multiplier_min),
        REGISTER_CONFIG("petal_stat_reload_multiplier_min", default_petal_stat_reload_multiplier_min),
        REGISTER_CONFIG("pincer_base_damage", default_pincer_base_damage),
        REGISTER_CONFIG("pincer_base_health", default_pincer_base_health),
        REGISTER_CONFIG("pincer_base_radius", default_pincer_base_radius),
        REGISTER_CONFIG("pincer_copy", default_pincer_copy),
        REGISTER_CONFIG("pincer_mass", default_pincer_mass),
        REGISTER_CONFIG("pincer_poison_duration", default_pincer_poison_duration),
        REGISTER_CONFIG("pincer_poison_total_damage", default_pincer_poison_total_damage),
        REGISTER_CONFIG("pincer_reload", default_pincer_reload),
        REGISTER_CONFIG("pincer_slow_duration", default_pincer_slow_duration),
        REGISTER_CONFIG("pi", pi),
        REGISTER_CONFIG("player_input_axis_max", player_input_axis_max),
        REGISTER_CONFIG("player_move_target_distance", player_move_target_distance),
        REGISTER_CONFIG("player_respawn_x", player_respawn_x),
        REGISTER_CONFIG("player_respawn_y", player_respawn_y),
        REGISTER_CONFIG("port", port),
        REGISTER_CONFIG("report_deepseek_api_key", report_deepseek_api_key),
        REGISTER_CONFIG("report_deepseek_api_url", report_deepseek_api_url),
        REGISTER_CONFIG("report_deepseek_model", report_deepseek_model),
        REGISTER_CONFIG("report_invalid_disable_count", report_invalid_disable_count),
        REGISTER_CONFIG("report_keyword_base_mute_seconds", report_keyword_base_mute_seconds),
        REGISTER_CONFIG("report_scan_window_seconds", report_scan_window_seconds),
        REGISTER_CONFIG("relic_base_damage", default_relic_base_damage),
        REGISTER_CONFIG("relic_base_health", default_relic_base_health),
        REGISTER_CONFIG("relic_base_radius", default_relic_base_radius),
        REGISTER_CONFIG("relic_copy", default_relic_copy),
        REGISTER_CONFIG("relic_health_bonus_common", default_relic_health_bonus_common),
        REGISTER_CONFIG("relic_health_bonus_epic", default_relic_health_bonus_epic),
        REGISTER_CONFIG("relic_health_bonus_eternal", default_relic_health_bonus_eternal),
        REGISTER_CONFIG("relic_health_bonus_legendary", default_relic_health_bonus_legendary),
        REGISTER_CONFIG("relic_health_bonus_mythic", default_relic_health_bonus_mythic),
        REGISTER_CONFIG("relic_health_bonus_primordial", default_relic_health_bonus_primordial),
        REGISTER_CONFIG("relic_health_bonus_rare", default_relic_health_bonus_rare),
        REGISTER_CONFIG("relic_health_bonus_super", default_relic_health_bonus_super),
        REGISTER_CONFIG("relic_health_bonus_ultra", default_relic_health_bonus_ultra),
        REGISTER_CONFIG("relic_health_bonus_unique", default_relic_health_bonus_unique),
        REGISTER_CONFIG("relic_health_bonus_unusual", default_relic_health_bonus_unusual),
        REGISTER_CONFIG("relic_mass", default_relic_mass),
        REGISTER_CONFIG("relic_psionic_zone_radius", default_relic_psionic_zone_radius),
        REGISTER_CONFIG("relic_psionic_zone_radius_primordial", default_relic_psionic_zone_radius_primordial),
        REGISTER_CONFIG("relic_psionic_zone_refresh", default_relic_psionic_zone_refresh),
        REGISTER_CONFIG("relic_reload", default_relic_reload),
        REGISTER_CONFIG("rose_base_damage", default_rose_base_damage),
        REGISTER_CONFIG("rose_base_health", default_rose_base_health),
        REGISTER_CONFIG("rose_base_medicine", default_rose_base_medicine),
        REGISTER_CONFIG("rose_base_radius", default_rose_base_radius),
        REGISTER_CONFIG("rose_copy", default_rose_copy),
        REGISTER_CONFIG("rose_mass", default_rose_mass),
        REGISTER_CONFIG("rose_preload", default_rose_preload),
        REGISTER_CONFIG("rose_reload", default_rose_reload),
        REGISTER_CONFIG("server_fixed_dt", server_fixed_dt),
        REGISTER_CONFIG("spatial_grid_cell_size", spatial_grid_cell_size),
        REGISTER_CONFIG("stinger_base_damage", default_stinger_base_damage),
        REGISTER_CONFIG("stinger_base_health", default_stinger_base_health),
        REGISTER_CONFIG("stinger_base_radius", default_stinger_base_radius),
        REGISTER_CONFIG("stinger_mass", default_stinger_mass),
        REGISTER_CONFIG("stinger_reload", default_stinger_reload),
        REGISTER_CONFIG("timeout_protection_seconds", timeout_protection_seconds),
        REGISTER_CONFIG("yinyang_base_damage", default_yinyang_base_damage),
        REGISTER_CONFIG("yinyang_base_health", default_yinyang_base_health),
        REGISTER_CONFIG("yinyang_base_radius", default_yinyang_base_radius),
        REGISTER_CONFIG("yinyang_copy", default_yinyang_copy),
        REGISTER_CONFIG("yinyang_mass", default_yinyang_mass),
        REGISTER_CONFIG("yinyang_reload", default_yinyang_reload),
        REGISTER_CONFIG("yggdrasil_base_health", default_yggdrasil_base_health),
        REGISTER_CONFIG("yggdrasil_base_radius", default_yggdrasil_base_radius),
        REGISTER_CONFIG("yggdrasil_channel_common", default_yggdrasil_channel_common),
        REGISTER_CONFIG("yggdrasil_channel_super", default_yggdrasil_channel_super),
        REGISTER_CONFIG("yggdrasil_channel_eternal", default_yggdrasil_channel_eternal),
        REGISTER_CONFIG("yggdrasil_channel_primordial", default_yggdrasil_channel_primordial),
        REGISTER_CONFIG("yggdrasil_heal_fraction", default_yggdrasil_heal_fraction),
        REGISTER_CONFIG("yggdrasil_preload", default_yggdrasil_preload),
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
