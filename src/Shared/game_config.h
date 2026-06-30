#pragma once
#include <functional>
#include <string>
#include <unordered_map>

struct ConfigEntry
{
    std::function<void(const std::string&)> setter;
    std::function<std::string()> getter;
};

namespace GameConfig
{

// ================ Petal ================
inline float default_petal_orbit_radius = 20.0f;
inline float default_petal_attack_offset = 30.0f;
inline float default_petal_defend_offset = -10.0f;
inline float default_petal_orbit_k = 5.0f;
inline float default_petal_reload = 2.5f;
inline float default_petal_preload = 2.5f;
inline float default_petal_radius = 15.0f;

// ================  Mob  ================
inline float default_search_range = 1024.0f;
inline float default_detection_radius = 512.0f;
inline float default_max_velocity = 50.0f;
inline float default_acceleration = 100.0f;

// =============== Flower ================
inline float default_flower_radius = 20.0f;
inline float default_flower_petal_num_max = 5.0f;

// ============== Register ===============
inline std::unordered_map<std::string, ConfigEntry>& GetConfigEntries()
{
    static std::unordered_map<std::string, ConfigEntry> entries = {
        { "petal_orbit_radius",
          { [](const std::string& v) { default_petal_orbit_radius = std::stof(v); },
            []() -> std::string { return std::to_string(default_petal_orbit_radius); } } },
        { "petal_attack_offset",
          { [](const std::string& v) { default_petal_attack_offset = std::stof(v); },
            []() -> std::string { return std::to_string(default_petal_attack_offset); } } },
        { "petal_defend_offset",
          { [](const std::string& v) { default_petal_defend_offset = std::stof(v); },
            []() -> std::string { return std::to_string(default_petal_defend_offset); } } },
        { "petal_orbit_k",
          { [](const std::string& v) { default_petal_orbit_k = std::stof(v); },
            []() -> std::string { return std::to_string(default_petal_orbit_k); } } },
        { "petal_reload",
          { [](const std::string& v) { default_petal_reload = std::stof(v); },
            []() -> std::string { return std::to_string(default_petal_reload); } } },
        { "petal_preload",
          { [](const std::string& v) { default_petal_preload = std::stof(v); },
            []() -> std::string { return std::to_string(default_petal_preload); } } },
        { "petal_radius",
          { [](const std::string& v) { default_petal_radius = std::stof(v); },
            []() -> std::string { return std::to_string(default_petal_radius); } } },
        { "search_range",
          { [](const std::string& v) { default_search_range = std::stof(v); },
            []() -> std::string { return std::to_string(default_search_range); } } },
        { "detection_radius",
          { [](const std::string& v) { default_detection_radius = std::stof(v); },
            []() -> std::string { return std::to_string(default_detection_radius); } } },
        { "max_velocity",
          { [](const std::string& v) { default_max_velocity = std::stof(v); },
            []() -> std::string { return std::to_string(default_max_velocity); } } },
        { "acceleration",
          { [](const std::string& v) { default_acceleration = std::stof(v); },
            []() -> std::string { return std::to_string(default_acceleration); } } },
        { "flower_radius",
          { [](const std::string& v) { default_flower_radius = std::stof(v); },
            []() -> std::string { return std::to_string(default_flower_radius); } } },
        { "flower_petal_num_max",
          { [](const std::string& v) { default_flower_petal_num_max = std::stof(v); },
            []() -> std::string { return std::to_string(default_flower_petal_num_max); } } },
    };
    return entries;
}

inline bool SetConfig(const std::string& name, const std::string& value)
{
    auto& entries = GetConfigEntries();
    auto it = entries.find(name);
    if (it != entries.end())
    {
        it->second.setter(value);
        return true;
    }
    return false;
}

inline std::string GetConfig(const std::string& name)
{
    auto& entries = GetConfigEntries();
    auto it = entries.find(name);
    if (it != entries.end())
        return it->second.getter();
    return "Unknown config";
}
}