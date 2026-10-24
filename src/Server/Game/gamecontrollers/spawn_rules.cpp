#include "spawn_rules.h"
#include "../../../Engine/json_value.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <unordered_set>

namespace
{
const CJsonValue* Field(const CJsonValue& object, const char* key) { return object.Find(key); }

std::optional<double> ParseVirtualTime(std::string text)
{
    const size_t separator = text.find(':');
    int hour = 0;
    int minute = 0;
    if (!ParseStrictIntText(text.substr(0, separator), hour) ||
        (separator != std::string::npos && !ParseStrictIntText(text.substr(separator + 1), minute)))
        return std::nullopt;
    if (hour < 0 || hour >= 24 || minute < 0 || minute >= 60) return std::nullopt;
    return (static_cast<double>(hour) + static_cast<double>(minute) / 60.0) / 24.0;
}

bool ParseMob(const CJsonValue& value, EMobType& type)
{
    return value.IsString() && TryParseZoneMobType(value.AsString(), type);
}

bool ParsePool(const CJsonValue* value, std::vector<SZoneMobEntry>& result, std::string& error,
               const std::string& context)
{
    result.clear();
    if (!value || !value->IsArray())
    {
        error = context + " must be an array";
        return false;
    }

    std::unordered_set<EMobType> seen;
    for (const CJsonValue& entry : value->AsArray())
    {
        const CJsonValue* mob_value = Field(entry, "mob");
        const CJsonValue* weight_value = Field(entry, "weight");
        EMobType type = EMobType::None;
        const float weight = weight_value && weight_value->IsNumber()
                                 ? static_cast<float>(weight_value->AsNumber())
                                 : -1.f;
        if (!entry.IsObject() || !mob_value || !ParseMob(*mob_value, type) || !std::isfinite(weight) ||
            weight <= 0.f)
        {
            error = context + " contains an invalid mob entry";
            return false;
        }
        if (!seen.insert(type).second)
        {
            error = context + " contains duplicate mob " + mob_value->AsString();
            return false;
        }
        result.push_back({ type, weight });
    }
    if (result.empty())
    {
        error = context + " cannot be empty";
        return false;
    }
    return true;
}

double ForwardDistance(double from, double to)
{
    double result = to - from;
    result = std::fmod(result, 1.0);
    return result < 0.0 ? result + 1.0 : result;
}

float SmoothStep(float value)
{
    value = std::clamp(value, 0.f, 1.f);
    return value * value * (3.f - 2.f * value);
}

float TimeInfluence(double time_of_day, double from, double until, double fade)
{
    const double length = ForwardDistance(from, until);
    if (length <= std::numeric_limits<double>::epsilon()) return 1.f;
    const double position = ForwardDistance(from, time_of_day);
    if (position > length) return 0.f;
    if (fade <= std::numeric_limits<double>::epsilon()) return 1.f;
    const double edge_distance = std::min(position, length - position);
    return SmoothStep(static_cast<float>(edge_distance / std::min(fade, length * 0.5)));
}

float WeatherInfluence(const std::vector<EWeatherType>& accepted, const SWeatherDiurnalSnapshot& environment)
{
    if (accepted.empty()) return 1.f;
    float best = 0.f;
    for (EWeatherType weather : accepted)
    {
        if (!IsKnownWeatherType(weather)) continue;
        best = std::max(best, environment.Intensity(weather));
    }
    return best;
}
} // namespace

void CSpawnRuleSet::Clear()
{
    m_shared_modifiers.clear();
    m_profiles.clear();
    m_cache.clear();
}

bool CSpawnRuleSet::HasProfile(const std::string& name) const { return m_profiles.contains(name); }

bool CSpawnRuleSet::Load(const std::filesystem::path& path, std::string& error)
{
    Clear();
    std::optional<CJsonValue> root = CJsonValue::LoadFromFile(path, &error);
    if (!root || !root->IsObject())
    {
        if (error.empty()) error = "Spawn rules root must be an object";
        return false;
    }
    const CJsonValue* version = Field(*root, "version");
    if (!version || version->AsInt() != 1)
    {
        error = "Unsupported spawn rules version";
        return false;
    }

    auto parse_modifier = [&error](const CJsonValue& value, SSpawnModifier& modifier,
                                   const std::string& context) -> bool {
        if (!value.IsObject())
        {
            error = context + " must be an object";
            return false;
        }
        if (const CJsonValue* id = Field(value, "id"); id && id->IsString()) modifier.id = id->AsString();
        if (const CJsonValue* density = Field(value, "density_multiplier"))
        {
            modifier.density_multiplier = static_cast<float>(density->AsNumber(-1.0));
            if (!std::isfinite(modifier.density_multiplier) || modifier.density_multiplier < 0.f)
            {
                error = context + " has invalid density_multiplier";
                return false;
            }
        }
        if (const CJsonValue* difficulty = Field(value, "difficulty_offset"))
        {
            modifier.difficulty_offset = static_cast<float>(difficulty->AsNumber());
            if (!std::isfinite(modifier.difficulty_offset))
            {
                error = context + " has invalid difficulty_offset";
                return false;
            }
        }

        if (const CJsonValue* when = Field(value, "when"); when && when->IsObject())
        {
            if (const CJsonValue* time = Field(*when, "time"); time && time->IsObject())
            {
                const CJsonValue* from = Field(*time, "from");
                const CJsonValue* until = Field(*time, "until");
                const std::optional<double> parsed_from =
                    from && from->IsString() ? ParseVirtualTime(from->AsString()) : std::nullopt;
                const std::optional<double> parsed_until =
                    until && until->IsString() ? ParseVirtualTime(until->AsString()) : std::nullopt;
                if (!parsed_from || !parsed_until)
                {
                    error = context + " has invalid virtual time window";
                    return false;
                }
                modifier.time.enabled = true;
                modifier.time.from = *parsed_from;
                modifier.time.until = *parsed_until;
                if (const CJsonValue* fade = Field(*time, "fade_minutes"))
                {
                    const double fade_minutes = fade->AsNumber(-1.0);
                    if (!std::isfinite(fade_minutes) || fade_minutes < 0.0)
                    {
                        error = context + " has invalid fade_minutes";
                        return false;
                    }
                    modifier.time.fade = fade_minutes / (24.0 * 60.0);
                }
            }
            if (const CJsonValue* weather = Field(*when, "weather"))
            {
                if (!weather->IsArray())
                {
                    error = context + " weather condition must be an array";
                    return false;
                }
                for (const CJsonValue& weather_value : weather->AsArray())
                {
                    EWeatherType weather_type = EWeatherType::Clear;
                    if (!weather_value.IsString() ||
                        !TryParseWeatherType(weather_value.AsString(), weather_type))
                    {
                        error = context + " contains an unknown weather type";
                        return false;
                    }
                    modifier.weather.push_back(weather_type);
                }
            }
        }

        if (const CJsonValue* multipliers = Field(value, "weight_multipliers"))
        {
            if (!multipliers->IsObject())
            {
                error = context + " weight_multipliers must be an object";
                return false;
            }
            for (const auto& [mob_name, multiplier_value] : multipliers->AsObject())
            {
                EMobType type = EMobType::None;
                const float multiplier = static_cast<float>(multiplier_value.AsNumber(-1.0));
                if (!TryParseZoneMobType(mob_name, type) || !std::isfinite(multiplier) || multiplier < 0.f)
                {
                    error = context + " has invalid weight multiplier for " + mob_name;
                    return false;
                }
                modifier.weight_multipliers[type] = multiplier;
            }
        }
        if (const CJsonValue* additions = Field(value, "add"))
            if (!ParsePool(additions, modifier.additions, error, context + ".add")) return false;
        if (const CJsonValue* disabled = Field(value, "disable"))
        {
            if (!disabled->IsArray())
            {
                error = context + " disable must be an array";
                return false;
            }
            for (const CJsonValue& mob_value : disabled->AsArray())
            {
                EMobType type = EMobType::None;
                if (!ParseMob(mob_value, type))
                {
                    error = context + " contains an invalid disabled mob";
                    return false;
                }
                modifier.disabled.push_back(type);
            }
        }
        return true;
    };

    auto parse_modifiers = [&parse_modifier, &error](const CJsonValue* values,
                                                     std::vector<SSpawnModifier>& result,
                                                     const std::string& context) -> bool {
        if (!values) return true;
        if (!values->IsArray())
        {
            error = context + " must be an array";
            return false;
        }
        for (size_t i = 0; i < values->AsArray().size(); ++i)
        {
            SSpawnModifier modifier;
            if (!parse_modifier(values->AsArray()[i], modifier, context + "[" + std::to_string(i) + "]"))
                return false;
            result.push_back(std::move(modifier));
        }
        return true;
    };

    if (!parse_modifiers(Field(*root, "modifiers"), m_shared_modifiers, "modifiers"))
    {
        Clear();
        return false;
    }

    const CJsonValue* profiles = Field(*root, "profiles");
    if (!profiles || !profiles->IsObject())
    {
        error = "Spawn rules profiles must be an object";
        Clear();
        return false;
    }
    for (const auto& [name, profile_value] : profiles->AsObject())
    {
        SSpawnProfile profile;
        if (name.empty() || !profile_value.IsObject() ||
            !ParsePool(Field(profile_value, "base_pool"), profile.base_pool, error,
                       "profiles." + name + ".base_pool") ||
            !parse_modifiers(Field(profile_value, "modifiers"), profile.modifiers,
                             "profiles." + name + ".modifiers"))
        {
            Clear();
            return false;
        }
        m_profiles.emplace(name, std::move(profile));
    }
    if (m_profiles.empty())
    {
        error = "Spawn rules must define at least one profile";
        return false;
    }
    m_cache.reserve(m_profiles.size());
    return true;
}

const SResolvedSpawnProfile* CSpawnRuleSet::Resolve(const std::string& name,
                                                    const SWeatherDiurnalSnapshot* environment)
{
    const auto profile_it = m_profiles.find(name);
    if (profile_it == m_profiles.end()) return nullptr;

    SCachedProfile& cache = m_cache[name];
    // Time modifiers are smooth, but spawn decisions do not need per-tick cache invalidation.
    constexpr double time_cache_slots_per_day = 24.0 * 60.0;
    const std::uint16_t time_slot = environment
                                        ? static_cast<std::uint16_t>(environment->time_of_day *
                                                                     time_cache_slots_per_day)
                                        : 0;
    // Weather intensities move continuously; quantized signatures keep the cache useful across zones
    // while ensuring material weather changes cannot leave stale mixed weights behind.
    std::uint32_t intensity_signature = 0;
    if (environment)
    {
        for (int value = 0; value < static_cast<int>(EWeatherType::Count); ++value)
        {
            const float intensity = std::clamp(environment->weather_intensity[value], 0.f, 1.f);
            const std::uint32_t bucket =
                static_cast<std::uint32_t>(intensity >= 1.f ? 32.f : std::floor(intensity * 32.f));
            intensity_signature = intensity_signature * 37u + bucket;
        }
    }
    const std::uint64_t revision = environment ? environment->environment_revision : 0;
    if (cache.has_environment == (environment != nullptr) && cache.environment_revision == revision &&
        cache.time_slot == time_slot && cache.weather_intensity_signature == intensity_signature &&
        !cache.resolved.mob_entries.empty())
        return &cache.resolved;

    cache = {};
    cache.has_environment = environment != nullptr;
    cache.environment_revision = revision;
    cache.time_slot = time_slot;
    cache.weather_intensity_signature = intensity_signature;
    cache.resolved.mob_entries = profile_it->second.base_pool;
    if (!environment) return &cache.resolved;

    std::unordered_map<EMobType, float> weights;
    std::vector<EMobType> order;
    for (const SZoneMobEntry& entry : cache.resolved.mob_entries)
    {
        weights[entry.type] = entry.weight;
        order.push_back(entry.type);
    }

    auto apply_modifier = [&](const SSpawnModifier& modifier) {
        float influence = 1.f;
        if (modifier.time.enabled)
            influence *= TimeInfluence(environment->time_of_day, modifier.time.from, modifier.time.until,
                                       modifier.time.fade);
        influence *= WeatherInfluence(modifier.weather, *environment);
        if (influence <= 0.f) return;

        cache.resolved.density_multiplier *=
            std::lerp(1.f, modifier.density_multiplier, influence);
        cache.resolved.difficulty_offset += modifier.difficulty_offset * influence;
        for (const auto& [type, multiplier] : modifier.weight_multipliers)
        {
            auto weight = weights.find(type);
            if (weight != weights.end()) weight->second *= std::lerp(1.f, multiplier, influence);
        }
        for (const SZoneMobEntry& addition : modifier.additions)
        {
            if (!weights.contains(addition.type)) order.push_back(addition.type);
            weights[addition.type] += addition.weight * influence;
        }
        for (EMobType type : modifier.disabled)
        {
            auto weight = weights.find(type);
            if (weight != weights.end()) weight->second *= 1.f - influence;
        }
    };

    for (const SSpawnModifier& modifier : m_shared_modifiers)
        apply_modifier(modifier);
    for (const SSpawnModifier& modifier : profile_it->second.modifiers)
        apply_modifier(modifier);

    cache.resolved.mob_entries.clear();
    for (EMobType type : order)
    {
        const float weight = weights[type];
        if (std::isfinite(weight) && weight > 0.f) cache.resolved.mob_entries.push_back({ type, weight });
    }
    return &cache.resolved;
}
