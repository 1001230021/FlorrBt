#include "weather_extension.h"
#include "../../../Shared/game_config.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <limits>

namespace
{
constexpr double minimum_duration = 0.001;

double NormalizeTimeOfDay(double value)
{
    if (!std::isfinite(value)) return 0.0;
    value = std::fmod(value, 1.0);
    return value < 0.0 ? value + 1.0 : value;
}

EDiurnalPhase ResolveDiurnalPhase(double time_of_day)
{
    if (time_of_day >= 0.20 && time_of_day < 0.30) return EDiurnalPhase::Dawn;
    if (time_of_day >= 0.30 && time_of_day < 0.70) return EDiurnalPhase::Day;
    if (time_of_day >= 0.70 && time_of_day < 0.80) return EDiurnalPhase::Dusk;
    return EDiurnalPhase::Night;
}

float ResolveDaylight(double time_of_day)
{
    const double angle = 2.0 * static_cast<double>(game_config::pi) * (time_of_day - 0.25);
    return static_cast<float>(std::clamp(std::sin(angle), 0.0, 1.0));
}

float NormalizeWindDirection(float degrees)
{
    if (!std::isfinite(degrees)) return 0.f;
    degrees = std::fmod(degrees, 360.f);
    return degrees < 0.f ? degrees + 360.f : degrees;
}

bool ValidMeteorology(const SMeteorology& value)
{
    return std::isfinite(value.humidity) && value.humidity >= 0.f && value.humidity <= 1.f &&
           std::isfinite(value.wind_direction_degrees) && std::isfinite(value.wind_speed) &&
           value.wind_speed >= 0.f && std::isfinite(value.temperature_celsius);
}

float Approach(float value, float target, float max_delta)
{
    return value < target ? std::min(value + max_delta, target) : std::max(value - max_delta, target);
}

// Maps a score to an intensity in [0, 1]: nothing below `onset`, full intensity at or above `full`.
float RampIntensity(float score, float onset, float full)
{
    if (score <= onset) return 0.f;
    if (score >= full) return 1.f;
    return (score - onset) / (full - onset);
}
} // namespace

std::string_view WeatherTypeName(EWeatherType weather)
{
    switch (weather)
    {
    case EWeatherType::Clear: return "clear";
    case EWeatherType::Cloudy: return "cloudy";
    case EWeatherType::Rain: return "rain";
    case EWeatherType::Fog: return "fog";
    default: return "unknown";
    }
}

bool TryParseWeatherType(std::string_view text, EWeatherType& weather)
{
    std::string lowered(text);
    std::transform(lowered.begin(), lowered.end(), lowered.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    for (int value = static_cast<int>(EWeatherType::Clear); value < static_cast<int>(EWeatherType::Count); ++value)
    {
        const EWeatherType candidate = static_cast<EWeatherType>(value);
        if (lowered == WeatherTypeName(candidate))
        {
            weather = candidate;
            return true;
        }
    }
    return false;
}

bool CWeatherExtension::Init()
{
    Reset();
    return true;
}

void CWeatherExtension::Reset()
{
    m_snapshot = {};
    m_snapshot.revision = 1;
    m_snapshot.time_of_day = NormalizeTimeOfDay(game_config::weather_diurnal_start_time);
    m_snapshot.meteorology = {};
    m_snapshot.environment_revision = 1;
    std::fill(std::begin(m_published_intensity), std::end(m_published_intensity), 0.f);
    m_forced_weather.reset();
    m_forced_intensity.reset();
    m_meteorology_override.reset();
    const double configured_day_duration = static_cast<double>(game_config::weather_diurnal_day_duration);
    m_day_duration_seconds = std::isfinite(configured_day_duration) && configured_day_duration > 0.0
                                 ? configured_day_duration
                                 : 6.0 * 60.0 * 60.0;
    PublishDerivedState();
}

void CWeatherExtension::Tick(float dt)
{
    if (!std::isfinite(dt) || dt <= 0.f) return;

    const double seconds = static_cast<double>(dt);
    const double configured_day_duration = static_cast<double>(game_config::weather_diurnal_day_duration);
    if (std::isfinite(configured_day_duration) && configured_day_duration > 0.0 &&
        configured_day_duration != m_day_duration_seconds)
    {
        m_day_duration_seconds = configured_day_duration;
        ++m_snapshot.environment_revision;
    }
    ++m_snapshot.simulation_tick;
    ++m_snapshot.revision;
    m_snapshot.simulation_seconds += seconds;
    AdvanceTime(seconds);
    if (!game_config::weather_diurnal_enabled)
    {
        std::fill(std::begin(m_snapshot.weather_intensity), std::end(m_snapshot.weather_intensity), 0.f);
        PublishDerivedState();
        return;
    }
    AdvanceMeteorology(seconds);
    AdvanceIntensity(seconds);
    PublishDerivedState();
}

void CWeatherExtension::AdvanceMeteorology(double seconds)
{
    if (m_meteorology_override)
    {
        m_snapshot.meteorology = *m_meteorology_override;
        return;
    }

    const double phase = m_snapshot.time_of_day * 2.0 * static_cast<double>(game_config::pi);
    const float humidity_target = static_cast<float>(
        std::clamp(0.55 + 0.22 * std::cos(phase) + 0.08 * std::sin(m_snapshot.simulation_seconds / 173.0),
                   0.12, 0.98));
    const float temperature_target = static_cast<float>(
        19.0 + 8.0 * std::sin(phase - static_cast<double>(game_config::pi) * 0.5) +
        1.5 * std::sin(m_snapshot.simulation_seconds / 257.0));
    const float wind_target = static_cast<float>(
        std::clamp(3.0 + 2.5 * std::sin(m_snapshot.simulation_seconds / 211.0) +
                       1.2 * std::sin(m_snapshot.simulation_seconds / 47.0),
                   0.0, 12.0));

    const float dt = static_cast<float>(std::min(seconds, 1.0));
    m_snapshot.meteorology.humidity =
        Approach(m_snapshot.meteorology.humidity, humidity_target, 0.008f * dt);
    m_snapshot.meteorology.temperature_celsius =
        Approach(m_snapshot.meteorology.temperature_celsius, temperature_target, 0.05f * dt);
    m_snapshot.meteorology.wind_speed =
        Approach(m_snapshot.meteorology.wind_speed, wind_target, 0.035f * dt);
    m_snapshot.meteorology.wind_direction_degrees = NormalizeWindDirection(
        m_snapshot.meteorology.wind_direction_degrees + static_cast<float>(seconds * 0.35));
}

void CWeatherExtension::AdvanceTime(double seconds)
{
    const double day_duration = std::max(minimum_duration, m_day_duration_seconds);
    const double advanced_time = m_snapshot.time_of_day + seconds / day_duration;
    const double completed_days = std::floor(advanced_time);
    if (completed_days > 0.0)
        m_snapshot.day_index += static_cast<std::uint64_t>(completed_days);
    m_snapshot.time_of_day = NormalizeTimeOfDay(advanced_time);
}

void CWeatherExtension::AdvanceIntensity(double seconds)
{
    float targets[weather_type_count] = {};
    if (m_forced_weather && m_forced_intensity)
    {
        targets[static_cast<int>(*m_forced_weather)] = std::clamp(*m_forced_intensity, 0.f, 1.f);
    } else
    {
        DeriveIntensities(targets);
    }

    const double full_duration = std::max(1.0, static_cast<double>(game_config::weather_diurnal_transition_duration));
    const float max_delta = static_cast<float>(std::min(1.0, seconds / full_duration));
    for (int value = 0; value < static_cast<int>(EWeatherType::Count); ++value)
        m_snapshot.weather_intensity[value] = Approach(m_snapshot.weather_intensity[value], targets[value], max_delta);
}

void CWeatherExtension::DeriveIntensities(float out[weather_type_count]) const
{
    std::fill(out, out + weather_type_count, 0.f);
    const SMeteorology& value = m_snapshot.meteorology;
    const float wind_direction_radians = value.wind_direction_degrees * game_config::pi / 180.f;
    const float moisture_direction = 0.5f + 0.5f * std::sin(wind_direction_radians);
    const float wind = std::clamp(value.wind_speed / 14.f, 0.f, 1.f);
    const float warmth = std::clamp((value.temperature_celsius + 5.f) / 40.f, 0.f, 1.f);
    const float condensation = std::clamp((22.f - value.temperature_celsius) / 25.f, 0.f, 1.f);

    // Every score deliberately includes humidity, wind direction, wind speed, and temperature.
    // Rain absorbs the old storm weighting: stronger wind drives heavier rain.
    const float fog_score = value.humidity * 0.55f + (1.f - wind) * 0.18f +
                            condensation * 0.17f + (1.f - moisture_direction) * 0.10f;
    const float rain_score = value.humidity * 0.55f + wind * 0.15f +
                             moisture_direction * 0.15f + condensation * 0.15f;
    const float cloud_score = value.humidity * 0.58f + wind * 0.17f +
                              moisture_direction * 0.10f + condensation * 0.15f;

    // Intensities ramp from the onset score to full at an extreme score, so the more extreme
    // the driving attributes, the stronger each weather. Several weathers may be active at once.
    const float cloudy = RampIntensity(cloud_score, 0.45f, 0.66f);
    const float rain = RampIntensity(rain_score, 0.58f, 0.76f);
    const float fog = RampIntensity(fog_score, 0.72f, 0.85f);

    out[static_cast<int>(EWeatherType::Clear)] = 1.f - std::max({ cloudy, rain, fog });
    out[static_cast<int>(EWeatherType::Cloudy)] = cloudy;
    out[static_cast<int>(EWeatherType::Rain)] = rain;
    out[static_cast<int>(EWeatherType::Fog)] = fog;
}

void CWeatherExtension::PublishDerivedState()
{
    m_snapshot.time_of_day = NormalizeTimeOfDay(m_snapshot.time_of_day);
    m_snapshot.phase = ResolveDiurnalPhase(m_snapshot.time_of_day);
    m_snapshot.daylight = ResolveDaylight(m_snapshot.time_of_day);
    float max_delta = 0.f;
    for (int value = 0; value < static_cast<int>(EWeatherType::Count); ++value)
    {
        m_snapshot.weather_intensity[value] = std::clamp(m_snapshot.weather_intensity[value], 0.f, 1.f);
        max_delta = std::max(max_delta, std::abs(m_snapshot.weather_intensity[value] - m_published_intensity[value]));
    }
    if (max_delta > 0.02f)
    {
        std::copy(std::begin(m_snapshot.weather_intensity), std::end(m_snapshot.weather_intensity),
                  std::begin(m_published_intensity));
        ++m_snapshot.environment_revision;
    }
}

SWeatherDiurnalPersistentState CWeatherExtension::CaptureState() const
{
    return { m_snapshot, m_day_duration_seconds, m_forced_weather, m_forced_intensity, m_meteorology_override };
}

SWeatherDiurnalClientState CWeatherExtension::GetClientState() const
{
    SWeatherDiurnalClientState state;
    state.time_of_day = m_snapshot.time_of_day;
    std::copy(std::begin(m_snapshot.weather_intensity), std::end(m_snapshot.weather_intensity),
              std::begin(state.weather_intensity));
    state.revision = m_snapshot.environment_revision;
    return state;
}

bool CWeatherExtension::RestoreState(const SWeatherDiurnalPersistentState& state, std::string& error)
{
    const auto& snapshot = state.snapshot;
    if (!std::isfinite(snapshot.simulation_seconds) || snapshot.simulation_seconds < 0.0 ||
        !std::isfinite(snapshot.time_of_day) || !std::isfinite(snapshot.daylight) ||
        !std::isfinite(state.day_duration_seconds) || state.day_duration_seconds < minimum_duration ||
        state.day_duration_seconds > static_cast<double>(std::numeric_limits<float>::max()) ||
        !ValidMeteorology(snapshot.meteorology) ||
        (state.forced_weather && !IsKnownWeatherType(*state.forced_weather)) ||
        (state.forced_intensity &&
         (!std::isfinite(*state.forced_intensity) || *state.forced_intensity < 0.f ||
          *state.forced_intensity > 1.f)) ||
        (state.meteorology_override && !ValidMeteorology(*state.meteorology_override)))
    {
        error = "Weather-diurnal snapshot contains invalid state";
        return false;
    }
    for (int value = 0; value < static_cast<int>(EWeatherType::Count); ++value)
    {
        const float intensity = snapshot.weather_intensity[value];
        if (!std::isfinite(intensity) || intensity < 0.f || intensity > 1.f)
        {
            error = "Weather-diurnal snapshot contains invalid weather intensity";
            return false;
        }
    }

    m_snapshot = snapshot;
    const float restored_day_duration = static_cast<float>(state.day_duration_seconds);
    m_day_duration_seconds = static_cast<double>(restored_day_duration);
    if (game_config::weather_diurnal_day_duration != restored_day_duration)
    {
        game_config::weather_diurnal_day_duration = restored_day_duration;
        game_config::AdvanceConfigRevision();
    }
    m_snapshot.environment_revision = std::max<std::uint64_t>(1, m_snapshot.environment_revision);
    m_snapshot.time_of_day = NormalizeTimeOfDay(m_snapshot.time_of_day);
    for (int value = 0; value < static_cast<int>(EWeatherType::Count); ++value)
    {
        m_snapshot.weather_intensity[value] = std::clamp(m_snapshot.weather_intensity[value], 0.f, 1.f);
        m_published_intensity[value] = m_snapshot.weather_intensity[value];
    }
    m_forced_weather = state.forced_weather;
    m_forced_intensity = state.forced_intensity;
    m_meteorology_override = state.meteorology_override;
    PublishDerivedState();
    return true;
}

bool CWeatherExtension::ForceWeather(EWeatherType weather, float intensity, std::string& error)
{
    if (!IsKnownWeatherType(weather) || !std::isfinite(intensity) || intensity < 0.f || intensity > 1.f)
    {
        error = "Invalid forced weather or intensity (must be in [0, 1])";
        return false;
    }
    m_forced_weather = weather;
    m_forced_intensity = intensity;
    for (int value = 0; value < static_cast<int>(EWeatherType::Count); ++value)
    {
        const float assigned = value == static_cast<int>(weather) ? intensity : 0.f;
        m_snapshot.weather_intensity[value] = assigned;
        m_published_intensity[value] = assigned;
    }
    ++m_snapshot.environment_revision;
    error.clear();
    return true;
}

bool CWeatherExtension::SetMeteorologyOverride(const SMeteorology& meteorology, std::string& error)
{
    if (!ValidMeteorology(meteorology))
    {
        error = "Meteorology requires humidity [0,1], finite direction/temperature, and non-negative wind speed";
        return false;
    }
    SMeteorology normalized = meteorology;
    normalized.wind_direction_degrees = NormalizeWindDirection(normalized.wind_direction_degrees);
    m_meteorology_override = normalized;
    m_snapshot.meteorology = normalized;
    error.clear();
    return true;
}

bool CWeatherExtension::ResumeDerivedWeather(std::string& error)
{
    if (!m_meteorology_override)
    {
        error = "No manual meteorology is set; use weather env first";
        return false;
    }
    m_forced_weather.reset();
    m_forced_intensity.reset();
    error.clear();
    return true;
}

bool CWeatherExtension::ResumeAutomaticWeather(std::string& error)
{
    m_forced_weather.reset();
    m_forced_intensity.reset();
    m_meteorology_override.reset();
    AdvanceMeteorology(1.0);
    error.clear();
    return true;
}

bool CWeatherExtension::SetVirtualTime(double normalized_time, std::string& error)
{
    if (!std::isfinite(normalized_time))
    {
        error = "Virtual time must be finite";
        return false;
    }
    m_snapshot.time_of_day = NormalizeTimeOfDay(normalized_time);
    ++m_snapshot.revision;
    ++m_snapshot.environment_revision;
    PublishDerivedState();
    error.clear();
    return true;
}
