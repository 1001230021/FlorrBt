#include "test_framework.h"
#include "Server/Game/extensions/weather_extension.h"
#include "Shared/game_config.h"
#include <limits>

namespace
{
constexpr double tolerance = 0.02;

CWeatherExtension MakeSystem()
{
    CWeatherExtension system;
    CHECK(system.Init());
    return system;
}

SWeatherDiurnalSnapshot SettledSnapshot(CWeatherExtension& system, const SMeteorology& meteorology,
                                        double settle_seconds)
{
    std::string error;
    CHECK(system.SetMeteorologyOverride(meteorology, error));
    system.Tick(static_cast<float>(settle_seconds));
    return system.GetWeatherDiurnalSnapshot();
}

SWeatherDiurnalPersistentState StateWithInvalidMeteorology()
{
    SWeatherDiurnalPersistentState state;
    state.snapshot.simulation_seconds = 1.0;
    state.snapshot.time_of_day = 0.5;
    state.snapshot.daylight = 1.f;
    state.snapshot.meteorology.humidity = 1.5f; // Out of range.
    state.day_duration_seconds = 21600.0;
    return state;
}
} // namespace

TEST(InitialStateMatchesConfig)
{
    CWeatherExtension system = MakeSystem();
    const SWeatherDiurnalSnapshot snapshot = system.GetWeatherDiurnalSnapshot();
    CHECK_NEAR(snapshot.time_of_day, game_config::weather_diurnal_start_time, tolerance);
    CHECK(snapshot.day_index == 0);
    CHECK(snapshot.phase == EDiurnalPhase::Dawn);
    CHECK(snapshot.daylight >= 0.f && snapshot.daylight < 1.f);
    CHECK(snapshot.environment_revision >= 1);
    for (int value = 0; value < static_cast<int>(EWeatherType::Count); ++value)
        CHECK(snapshot.weather_intensity[value] == 0.f);
}

TEST(TimeAdvancesAtConfiguredRate)
{
    CWeatherExtension system = MakeSystem();
    // One real hour is 1/24 of the six-hour virtual day.
    system.Tick(3600.f);
    const SWeatherDiurnalSnapshot snapshot = system.GetWeatherDiurnalSnapshot();
    CHECK_NEAR(snapshot.time_of_day, 0.25 + 3600.0 / 21600.0, tolerance);
    CHECK(snapshot.day_index == 0);
}

TEST(DayRollsOver)
{
    CWeatherExtension system = MakeSystem();
    system.Tick(21600.f * 2.5f);
    const SWeatherDiurnalSnapshot snapshot = system.GetWeatherDiurnalSnapshot();
    CHECK(snapshot.day_index == 2);
    CHECK_NEAR(snapshot.time_of_day, 0.25 + 0.5, tolerance);
}

TEST(DiurnalPhaseBoundaries)
{
    CWeatherExtension system = MakeSystem();
    std::string error;

    CHECK(system.SetVirtualTime(0.25, error));
    CHECK(system.GetWeatherDiurnalSnapshot().phase == EDiurnalPhase::Dawn);
    CHECK(system.SetVirtualTime(0.35, error));
    CHECK(system.GetWeatherDiurnalSnapshot().phase == EDiurnalPhase::Day);
    CHECK(system.SetVirtualTime(0.75, error));
    CHECK(system.GetWeatherDiurnalSnapshot().phase == EDiurnalPhase::Dusk);
    CHECK(system.SetVirtualTime(0.90, error));
    CHECK(system.GetWeatherDiurnalSnapshot().phase == EDiurnalPhase::Night);
}

TEST(DaylightPeaksAtNoonAndZeroAtMidnight)
{
    CWeatherExtension system = MakeSystem();
    std::string error;
    CHECK(system.SetVirtualTime(0.5, error));
    CHECK_NEAR(system.GetWeatherDiurnalSnapshot().daylight, 1.f, 0.01);
    CHECK(system.SetVirtualTime(0.0, error));
    CHECK_NEAR(system.GetWeatherDiurnalSnapshot().daylight, 0.f, 0.01);
}

TEST(SetVirtualTimeNormalizes)
{
    CWeatherExtension system = MakeSystem();
    std::string error;
    CHECK(system.SetVirtualTime(1.2, error));
    CHECK_NEAR(system.GetWeatherDiurnalSnapshot().time_of_day, 0.2, tolerance);
    CHECK(system.SetVirtualTime(-0.1, error));
    CHECK_NEAR(system.GetWeatherDiurnalSnapshot().time_of_day, 0.9, tolerance);
    CHECK(!system.SetVirtualTime(std::numeric_limits<double>::quiet_NaN(), error));
}

TEST(ExtremeWindRainYieldsHighIntensity)
{
    CWeatherExtension system = MakeSystem();
    const SMeteorology stormy_rain{ 0.98f, 180.f, 12.f, 30.f };
    const SWeatherDiurnalSnapshot snapshot = SettledSnapshot(system, stormy_rain, 30.0);
    CHECK(snapshot.Intensity(EWeatherType::Rain) > 0.9f);
}

TEST(HumidCalmColdYieldsFog)
{
    CWeatherExtension system = MakeSystem();
    const SMeteorology foggy{ 0.92f, 180.f, 0.5f, 5.f };
    const SWeatherDiurnalSnapshot snapshot = SettledSnapshot(system, foggy, 30.0);
    CHECK(snapshot.Intensity(EWeatherType::Fog) > 0.85f);
}

TEST(MildDryWarmYieldsClear)
{
    CWeatherExtension system = MakeSystem();
    const SMeteorology clear_sky{ 0.20f, 180.f, 3.f, 25.f };
    const SWeatherDiurnalSnapshot snapshot = SettledSnapshot(system, clear_sky, 30.0);
    CHECK(snapshot.Intensity(EWeatherType::Clear) > 0.85f);
    CHECK(snapshot.Intensity(EWeatherType::Rain) < 0.5f);
}

TEST(WeathersCanStack)
{
    CWeatherExtension system = MakeSystem();
    // Humid, calm, cool: rain and fog and cloud all present at once.
    const SMeteorology stacked{ 0.90f, 90.f, 0.5f, 10.f };
    const SWeatherDiurnalSnapshot snapshot = SettledSnapshot(system, stacked, 30.0);
    CHECK(snapshot.Intensity(EWeatherType::Rain) > 0.5f);
    CHECK(snapshot.Intensity(EWeatherType::Fog) > 0.1f);
    CHECK(snapshot.Intensity(EWeatherType::Cloudy) > 0.5f);
}

TEST(IntensityRisesWithExtremeness)
{
    CWeatherExtension mild = MakeSystem();
    CWeatherExtension extreme = MakeSystem();
    const SMeteorology base{ 0.f, 90.f, 6.f, 10.f };
    SMeteorology mild_rain = base;
    mild_rain.humidity = 0.70f;
    SMeteorology heavy_rain = base;
    heavy_rain.humidity = 0.98f;

    const float mild_intensity = SettledSnapshot(mild, mild_rain, 30.0).Intensity(EWeatherType::Rain);
    const float extreme_intensity = SettledSnapshot(extreme, heavy_rain, 30.0).Intensity(EWeatherType::Rain);
    CHECK(extreme_intensity > mild_intensity);
    CHECK(mild_intensity > 0.1f);
    CHECK(extreme_intensity > 0.9f);
}

TEST(ForceWeatherIsImmediate)
{
    CWeatherExtension system = MakeSystem();
    std::string error;
    CHECK(system.ForceWeather(EWeatherType::Fog, 1.f, error));
    const SWeatherDiurnalSnapshot snapshot = system.GetWeatherDiurnalSnapshot();
    CHECK_NEAR(snapshot.Intensity(EWeatherType::Fog), 1.f, 0.001);
    CHECK_NEAR(snapshot.Intensity(EWeatherType::Clear), 0.f, 0.001);
}

TEST(ForceWeatherValidatesArguments)
{
    CWeatherExtension system = MakeSystem();
    std::string error;
    CHECK(!system.ForceWeather(EWeatherType::Count, 1.f, error));
    CHECK(!system.ForceWeather(EWeatherType::Clear, -0.1f, error));
    CHECK(!system.ForceWeather(EWeatherType::Clear, 1.5f, error));
    CHECK(!system.ForceWeather(EWeatherType::Clear, std::numeric_limits<float>::quiet_NaN(), error));
    CHECK(system.ForceWeather(EWeatherType::Clear, 0.5f, error));
    CHECK_NEAR(system.GetWeatherDiurnalSnapshot().Intensity(EWeatherType::Clear), 0.5f, 0.001);
}

TEST(ResumeAutomaticSmoothsBack)
{
    CWeatherExtension system = MakeSystem();
    std::string error;
    CHECK(system.ForceWeather(EWeatherType::Rain, 1.f, error));
    CHECK(system.ResumeAutomaticWeather(error));
    system.Tick(1.f);
    const float after_one_second = system.GetWeatherDiurnalSnapshot().Intensity(EWeatherType::Rain);
    CHECK(after_one_second < 1.f);
    CHECK(after_one_second > 0.f);
}

TEST(IntensityTransitionsGradually)
{
    CWeatherExtension system = MakeSystem();
    std::string error;
    CHECK(system.ForceWeather(EWeatherType::Fog, 1.f, error));
    CHECK(system.ResumeAutomaticWeather(error));
    // Half the transition duration -> roughly halfway toward the derived target.
    system.Tick(10.f);
    const float halfway = system.GetWeatherDiurnalSnapshot().Intensity(EWeatherType::Fog);
    CHECK(halfway > 0.05f);
    CHECK(halfway < 0.95f);
    system.Tick(20.f);
    const float settled = system.GetWeatherDiurnalSnapshot().Intensity(EWeatherType::Fog);
    CHECK(settled < 0.05f); // Derived target for the default dry start weather is no fog.
}

TEST(CaptureRestoreRoundTrip)
{
    CWeatherExtension system = MakeSystem();
    std::string error;
    const SMeteorology manual{ 0.85f, 200.f, 6.f, 18.f };
    CHECK(system.SetMeteorologyOverride(manual, error));
    system.Tick(120.f);
    system.SetVirtualTime(0.42, error);
    CHECK(system.ForceWeather(EWeatherType::Cloudy, 0.8f, error));

    const SWeatherDiurnalPersistentState captured = system.CaptureState();
    CWeatherExtension restored = MakeSystem();
    CHECK(restored.RestoreState(captured, error));

    const SWeatherDiurnalSnapshot expected = system.GetWeatherDiurnalSnapshot();
    const SWeatherDiurnalSnapshot actual = restored.GetWeatherDiurnalSnapshot();
    CHECK(actual.day_index == expected.day_index);
    CHECK_NEAR(actual.time_of_day, expected.time_of_day, tolerance);
    CHECK(actual.simulation_tick == expected.simulation_tick);
    CHECK(actual.revision == expected.revision);
    for (int value = 0; value < static_cast<int>(EWeatherType::Count); ++value)
        CHECK_NEAR(actual.weather_intensity[value], expected.weather_intensity[value], tolerance);
    CHECK_NEAR(actual.meteorology.humidity, expected.meteorology.humidity, tolerance);
    CHECK_NEAR(actual.meteorology.wind_speed, expected.meteorology.wind_speed, tolerance);
    CHECK(restored.IsWeatherForced());
}

TEST(RestoreRejectsInvalidState)
{
    CWeatherExtension system = MakeSystem();
    std::string error;
    CHECK(!system.RestoreState(StateWithInvalidMeteorology(), error));
    CHECK(!error.empty());

    SWeatherDiurnalPersistentState unknown_weather = StateWithInvalidMeteorology();
    unknown_weather.snapshot.meteorology.humidity = 0.5f;
    unknown_weather.forced_weather = EWeatherType::Count;
    CHECK(!system.RestoreState(unknown_weather, error));

    SWeatherDiurnalPersistentState bad_intensity = StateWithInvalidMeteorology();
    bad_intensity.snapshot.meteorology.humidity = 0.5f;
    bad_intensity.snapshot.weather_intensity[static_cast<int>(EWeatherType::Rain)] = 2.f;
    CHECK(!system.RestoreState(bad_intensity, error));
}

TEST(ClientStateMatchesSnapshot)
{
    CWeatherExtension system = MakeSystem();
    std::string error;
    CHECK(system.ForceWeather(EWeatherType::Cloudy, 1.f, error));
    const SWeatherDiurnalSnapshot snapshot = system.GetWeatherDiurnalSnapshot();
    const SWeatherDiurnalClientState client = system.GetClientState();
    CHECK_NEAR(client.time_of_day, snapshot.time_of_day, tolerance);
    for (int value = 0; value < static_cast<int>(EWeatherType::Count); ++value)
        CHECK_NEAR(client.weather_intensity[value], snapshot.weather_intensity[value], tolerance);
    CHECK(client.revision == snapshot.environment_revision);
}

TEST(WeatherNamesRoundTrip)
{
    for (int value = 0; value < static_cast<int>(EWeatherType::Count); ++value)
    {
        const EWeatherType type = static_cast<EWeatherType>(value);
        EWeatherType parsed = EWeatherType::Clear;
        CHECK(TryParseWeatherType(WeatherTypeName(type), parsed));
        CHECK(parsed == type);
    }
    EWeatherType parsed = EWeatherType::Clear;
    CHECK(!TryParseWeatherType("typhoon", parsed));
}
