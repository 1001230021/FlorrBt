#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

enum class EWeatherType : std::uint8_t
{
    Clear = 0,
    Cloudy,
    Rain,
    Fog,
    Count,
};

enum class EDiurnalPhase : std::uint8_t
{
    Night = 0,
    Dawn,
    Day,
    Dusk,
};

constexpr bool IsKnownWeatherType(EWeatherType weather)
{
    return weather >= EWeatherType::Clear && weather < EWeatherType::Count;
}

constexpr int weather_type_count = static_cast<int>(EWeatherType::Count);

std::string_view WeatherTypeName(EWeatherType weather);
bool TryParseWeatherType(std::string_view text, EWeatherType& weather);

struct SMeteorology
{
    float humidity = 0.45f;              // Relative humidity in [0, 1].
    float wind_direction_degrees = 0.f; // Clockwise degrees in [0, 360).
    float wind_speed = 2.f;              // Abstract world wind-speed units, non-negative.
    float temperature_celsius = 20.f;
};

struct SWeatherDiurnalSnapshot
{
    // This clock advances by fixed server simulation dt; host wall-clock changes do not affect it.
    static constexpr double virtual_hours_per_day = 24.0;
    static constexpr double virtual_minutes_per_hour = 60.0;
    static constexpr double virtual_seconds_per_minute = 60.0;

    std::uint64_t revision = 0;
    std::uint64_t simulation_tick = 0;
    std::uint64_t day_index = 0;
    // Elapsed server simulation time. It is deliberately independent of the host system clock.
    double simulation_seconds = 0.0;
    double time_of_day = 0.0; // Normalized to [0, 1), where 0 is midnight and 0.5 is noon.
    float daylight = 0.f;
    EDiurnalPhase phase = EDiurnalPhase::Night;
    // Per-weather-type intensity in [0, 1], indexed by EWeatherType. Weathers can be active at once;
    // the more extreme the driving meteorology, the higher each intensity.
    float weather_intensity[weather_type_count] = {};
    SMeteorology meteorology;
    // Changes only when a client-visible clock anchor or weather target changes.
    std::uint64_t environment_revision = 0;

    float Intensity(EWeatherType weather) const
    {
        const int index = static_cast<int>(weather);
        return index >= 0 && index < static_cast<int>(EWeatherType::Count) ? weather_intensity[index] : 0.f;
    }

    EWeatherType DominantWeather() const
    {
        EWeatherType best = EWeatherType::Clear;
        for (int value = 0; value < static_cast<int>(EWeatherType::Count); ++value)
        {
            if (weather_intensity[value] > weather_intensity[static_cast<int>(best)])
                best = static_cast<EWeatherType>(value);
        }
        return best;
    }

    double VirtualSecondsOfDay() const
    {
        constexpr double virtual_seconds_per_day =
            virtual_hours_per_day * virtual_minutes_per_hour * virtual_seconds_per_minute;
        const double seconds = time_of_day * virtual_seconds_per_day;
        return seconds < 0.0 ? 0.0 : seconds >= virtual_seconds_per_day ? 0.0 : seconds;
    }
    double VirtualHour() const
    {
        return VirtualSecondsOfDay() / (virtual_minutes_per_hour * virtual_seconds_per_minute);
    }
    std::uint32_t VirtualHourOfDay() const
    {
        return static_cast<std::uint32_t>(
            VirtualSecondsOfDay() / (virtual_minutes_per_hour * virtual_seconds_per_minute));
    }
    std::uint32_t VirtualMinuteOfHour() const
    {
        const auto total_seconds = static_cast<std::uint32_t>(VirtualSecondsOfDay());
        return (total_seconds / static_cast<std::uint32_t>(virtual_seconds_per_minute)) %
               static_cast<std::uint32_t>(virtual_minutes_per_hour);
    }
    std::uint32_t VirtualSecondOfMinute() const
    {
        return static_cast<std::uint32_t>(VirtualSecondsOfDay()) %
               static_cast<std::uint32_t>(virtual_seconds_per_minute);
    }
    double VirtualSecondsSinceEpoch() const
    {
        constexpr double virtual_seconds_per_day =
            virtual_hours_per_day * virtual_minutes_per_hour * virtual_seconds_per_minute;
        return (static_cast<double>(day_index) + time_of_day) * virtual_seconds_per_day;
    }
};

struct SWeatherDiurnalClientState
{
    double time_of_day = 0.0;
    float weather_intensity[weather_type_count] = {};
    std::uint64_t revision = 0;
};

class IWeatherDiurnalView
{
  public:
    virtual ~IWeatherDiurnalView() = default;
    virtual SWeatherDiurnalSnapshot GetWeatherDiurnalSnapshot() const = 0;
    virtual SWeatherDiurnalClientState GetClientState() const = 0;
    virtual double DayDurationSeconds() const = 0;
};

class IWeatherCommands : public IWeatherDiurnalView
{
  public:
    virtual bool ForceWeather(EWeatherType, float, std::string&) = 0;
    virtual bool SetMeteorologyOverride(const SMeteorology&, std::string&) = 0;
    virtual bool ResumeDerivedWeather(std::string&) = 0;
    virtual bool ResumeAutomaticWeather(std::string&) = 0;
    virtual bool SetVirtualTime(double, std::string&) = 0;
    virtual bool IsWeatherForced() const = 0;
    virtual bool HasMeteorologyOverride() const = 0;
};

struct SWeatherDiurnalPersistentState
{
    SWeatherDiurnalSnapshot snapshot;
    double day_duration_seconds = 6.0 * 60.0 * 60.0;
    std::optional<EWeatherType> forced_weather;
    std::optional<float> forced_intensity;
    std::optional<SMeteorology> meteorology_override;
};
