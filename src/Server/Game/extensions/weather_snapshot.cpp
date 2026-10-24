#include "weather_snapshot.h"
#include "weather_extension.h"
#include "../../../Shared/game_config.h"

CJsonOwner CaptureWeatherSnapshot(const CWeatherExtension& system)
{
    const SWeatherDiurnalPersistentState state = system.CaptureState();
    const SWeatherDiurnalSnapshot& snapshot = state.snapshot;
    CJsonOwner result = MakeJsonObject();
    CSnapshotWriter writer(result.get());
    writer.UInt64("revision", snapshot.revision);
    writer.UInt64("simulation_tick", snapshot.simulation_tick);
    writer.UInt64("day_index", snapshot.day_index);
    writer.Field("simulation_seconds", snapshot.simulation_seconds);
    writer.Field("time_of_day", snapshot.time_of_day);
    for (int value = 0; value < static_cast<int>(EWeatherType::Count); ++value)
        writer.Field("weather_intensity_" + std::to_string(value), snapshot.weather_intensity[value]);
    writer.Field("day_duration_seconds", state.day_duration_seconds);
    writer.UInt64("environment_revision", snapshot.environment_revision);
    writer.Field("humidity", snapshot.meteorology.humidity);
    writer.Field("wind_direction_degrees", snapshot.meteorology.wind_direction_degrees);
    writer.Field("wind_speed", snapshot.meteorology.wind_speed);
    writer.Field("temperature_celsius", snapshot.meteorology.temperature_celsius);
    writer.Field("has_forced_weather", state.forced_weather.has_value());
    if (state.forced_weather)
        writer.Field("forced_weather", static_cast<int>(*state.forced_weather));
    writer.Field("has_forced_intensity", state.forced_intensity.has_value());
    if (state.forced_intensity)
        writer.Field("forced_intensity", *state.forced_intensity);
    writer.Field("has_meteorology_override", state.meteorology_override.has_value());
    if (state.meteorology_override)
    {
        writer.Field("override_humidity", state.meteorology_override->humidity);
        writer.Field("override_wind_direction_degrees", state.meteorology_override->wind_direction_degrees);
        writer.Field("override_wind_speed", state.meteorology_override->wind_speed);
        writer.Field("override_temperature_celsius", state.meteorology_override->temperature_celsius);
    }

    return result;
}

bool RestoreWeatherSnapshot(CWeatherExtension& system, const json_t* data, std::uint32_t version, std::string& error)
{
    if (version < 1 || version > 4) { error = "Unsupported weather extension state"; return false; }
    CSnapshotReader reader(data);
    if (!reader.IsValid())
    {
        error = "Weather-diurnal section must be an object";
        return false;
    }

    SWeatherDiurnalPersistentState state;
    state.snapshot.revision = reader.UInt64("revision");
    state.snapshot.simulation_tick = reader.UInt64("simulation_tick");
    state.snapshot.day_index = reader.UInt64("day_index");
    state.snapshot.simulation_seconds = reader.Double("simulation_seconds");
    state.snapshot.time_of_day = reader.Double("time_of_day");
    if (version >= 4)
    {
        for (int value = 0; value < static_cast<int>(EWeatherType::Count); ++value)
            state.snapshot.weather_intensity[value] =
                reader.Float("weather_intensity_" + std::to_string(value), 0.f);
    }
    state.day_duration_seconds = reader.Double(
        "day_duration_seconds", static_cast<double>(game_config::weather_diurnal_day_duration));
    if (version >= 2)
    {
        state.snapshot.environment_revision = reader.UInt64("environment_revision", 1);
        state.snapshot.meteorology.humidity = reader.Float("humidity", state.snapshot.meteorology.humidity);
        state.snapshot.meteorology.wind_direction_degrees =
            reader.Float("wind_direction_degrees", state.snapshot.meteorology.wind_direction_degrees);
        state.snapshot.meteorology.wind_speed = reader.Float("wind_speed", state.snapshot.meteorology.wind_speed);
        state.snapshot.meteorology.temperature_celsius =
            reader.Float("temperature_celsius", state.snapshot.meteorology.temperature_celsius);
        if (reader.Bool("has_forced_weather"))
            state.forced_weather = static_cast<EWeatherType>(reader.Int("forced_weather", -1));
        if (version >= 4 && reader.Bool("has_forced_intensity"))
            state.forced_intensity = reader.Float("forced_intensity", 1.f);
        if (reader.Bool("has_meteorology_override"))
        {
            SMeteorology override;
            override.humidity = reader.Float("override_humidity", override.humidity);
            override.wind_direction_degrees =
                reader.Float("override_wind_direction_degrees", override.wind_direction_degrees);
            override.wind_speed = reader.Float("override_wind_speed", override.wind_speed);
            override.temperature_celsius =
                reader.Float("override_temperature_celsius", override.temperature_celsius);
            state.meteorology_override = override;
        }
    } else
    {
        state.snapshot.environment_revision = 1;
    }
    return system.RestoreState(state, error);
}
