#pragma once

#include "../extensions/weather_view.h"
#include "../zone_mob_tools.h"
#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

struct SResolvedSpawnProfile
{
    std::vector<SZoneMobEntry> mob_entries;
    float density_multiplier = 1.f;
    float difficulty_offset = 0.f;
};

class CSpawnRuleSet
{
  public:
    bool Load(const std::filesystem::path& path, std::string& error);
    void Clear();
    bool Empty() const { return m_profiles.empty(); }
    bool HasProfile(const std::string& name) const;
    const SResolvedSpawnProfile* Resolve(const std::string& name,
                                         const SWeatherDiurnalSnapshot* environment);

  private:
    struct STimeCondition
    {
        double from = 0.0;
        double until = 0.0;
        double fade = 0.0;
        bool enabled = false;
    };

    struct SSpawnModifier
    {
        std::string id;
        STimeCondition time;
        std::vector<EWeatherType> weather;
        std::unordered_map<EMobType, float> weight_multipliers;
        std::vector<SZoneMobEntry> additions;
        std::vector<EMobType> disabled;
        float density_multiplier = 1.f;
        float difficulty_offset = 0.f;
    };

    struct SSpawnProfile
    {
        std::vector<SZoneMobEntry> base_pool;
        std::vector<SSpawnModifier> modifiers;
    };

    struct SCachedProfile
    {
        std::uint64_t environment_revision = 0;
        std::uint16_t time_slot = 0;
        std::uint32_t weather_intensity_signature = 0;
        bool has_environment = false;
        SResolvedSpawnProfile resolved;
    };

    std::vector<SSpawnModifier> m_shared_modifiers;
    std::unordered_map<std::string, SSpawnProfile> m_profiles;
    std::unordered_map<std::string, SCachedProfile> m_cache;
};
