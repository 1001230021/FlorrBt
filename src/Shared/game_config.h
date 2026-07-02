#pragma once
#include <functional>
#include <string>
#include <unordered_map>

struct config_entry
{
    std::function<void(const std::string&)> setter;
    std::function<std::string()> getter;
};

namespace game_config
{
inline float default_petal_orbit_radius = 20.0f;
inline float default_petal_attack_offset = 30.0f;
inline float default_petal_defend_offset = -10.0f;
inline float default_petal_orbit_k = 5.0f;
inline float default_petal_reload = 2.5f;
inline float default_petal_preload = 2.5f;
inline float default_petal_radius = 15.0f;
inline float default_petal_pow = 3.0f;
inline float pi = 3.14159265359f;

inline float default_air_base_radius = 8.0f;
inline float default_air_base_mass = 16.0f;

inline float default_dust_base_radius = 7.5f;
inline float default_dust_base_damage = 4.3f;
inline float default_dust_base_health = 1.7f;
inline float default_dust_copy = 3.0f;
inline float default_dust_mass = 1.0f;

inline float default_goldenleaf_base_radius = 15.0f;
inline float default_goldenleaf_base_damage = 16.0f;
inline float default_goldenleaf_base_health = 12.0f;
inline float default_goldenleaf_base_reload_reduction = 0.05f;
inline float default_goldenleaf_copy = 1.0f;
inline float default_goldenleaf_mass = 3.0f;

inline float default_search_range = 1024.0f;
inline float default_detection_radius = 512.0f;
inline float default_max_velocity = 50.0f;
inline float default_acceleration = 100.0f;

inline float default_flower_radius = 20.0f;
inline float default_flower_petal_num_max = 5.0f;

inline int default_port = 10012;

#define REGISTER_CONFIG(name, variable)                                                                                \
    {                                                                                                                  \
        name,                                                                                                          \
        {                                                                                                              \
            [](const std::string& value) { variable = std::stof(value); },                                             \
            []() -> std::string { return std::to_string(variable); }                                                   \
        }                                                                                                              \
    }

inline std::unordered_map<std::string, config_entry>& GetConfigEntries()
{
    static std::unordered_map<std::string, config_entry> entries = {
        REGISTER_CONFIG("petal_orbit_radius", default_petal_orbit_radius),
        REGISTER_CONFIG("petal_attack_offset", default_petal_attack_offset),
        REGISTER_CONFIG("petal_defend_offset", default_petal_defend_offset),
        REGISTER_CONFIG("petal_orbit_k", default_petal_orbit_k),
        REGISTER_CONFIG("petal_reload", default_petal_reload),
        REGISTER_CONFIG("petal_preload", default_petal_preload),
        REGISTER_CONFIG("petal_radius", default_petal_radius),
        REGISTER_CONFIG("petal_pow", default_petal_pow),
        REGISTER_CONFIG("air_base_radius", default_air_base_radius),
        REGISTER_CONFIG("air_base_mass", default_air_base_mass),
        REGISTER_CONFIG("dust_base_radius", default_dust_base_radius),
        REGISTER_CONFIG("dust_base_damage", default_dust_base_damage),
        REGISTER_CONFIG("dust_base_health", default_dust_base_health),
        REGISTER_CONFIG("dust_copy", default_dust_copy),
        REGISTER_CONFIG("dust_mass", default_dust_mass),
        REGISTER_CONFIG("goldenleaf_base_radius", default_goldenleaf_base_radius),
        REGISTER_CONFIG("goldenleaf_base_damage", default_goldenleaf_base_damage),
        REGISTER_CONFIG("goldenleaf_base_health", default_goldenleaf_base_health),
        REGISTER_CONFIG("goldenleaf_base_reload_reduction", default_goldenleaf_base_reload_reduction),
        REGISTER_CONFIG("goldenleaf_copy", default_goldenleaf_copy),
        REGISTER_CONFIG("goldenleaf_mass", default_goldenleaf_mass),
        REGISTER_CONFIG("search_range", default_search_range),
        REGISTER_CONFIG("detection_radius", default_detection_radius),
        REGISTER_CONFIG("max_velocity", default_max_velocity),
        REGISTER_CONFIG("acceleration", default_acceleration),
        REGISTER_CONFIG("flower_radius", default_flower_radius),
        REGISTER_CONFIG("flower_petal_num_max", default_flower_petal_num_max),
        {"port", {[](const std::string& value) { default_port = std::stoi(value); },
                  []() -> std::string { return std::to_string(default_port); }}},
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
}