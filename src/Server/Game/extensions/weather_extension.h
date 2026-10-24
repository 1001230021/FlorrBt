#pragma once
#include "../extension.h"
#include "weather_view.h"

class CWeatherExtension : public CExtension, public ITickExtension, public IWeatherCommands
{
  public:
    const SExtensionDescriptor& Descriptor() const override
    {
        static const SExtensionDescriptor descriptor{"extension.weather", EExtensionScope::Server, 4};
        return descriptor;
    }
    bool OnAttach(CExtensionContext&, std::string&) override { return Init(); }
    bool Init();
    void Tick(float dt) override;

    SWeatherDiurnalSnapshot GetWeatherDiurnalSnapshot() const override { return m_snapshot; }
    SWeatherDiurnalClientState GetClientState() const override;
    SWeatherDiurnalPersistentState CaptureState() const;
    bool RestoreState(const SWeatherDiurnalPersistentState& state, std::string& error);
    bool ForceWeather(EWeatherType weather, float intensity, std::string& error) override;
    bool SetMeteorologyOverride(const SMeteorology& meteorology, std::string& error) override;
    bool ResumeDerivedWeather(std::string& error) override;
    bool ResumeAutomaticWeather(std::string& error) override;
    bool SetVirtualTime(double normalized_time, std::string& error) override;
    bool IsWeatherForced() const override { return m_forced_weather.has_value(); }
    bool HasMeteorologyOverride() const override { return m_meteorology_override.has_value(); }
    double DayDurationSeconds() const override { return m_day_duration_seconds; }

  private:
    void Reset();
    void AdvanceTime(double seconds);
    void AdvanceMeteorology(double seconds);
    void AdvanceIntensity(double seconds);
    void DeriveIntensities(float out[weather_type_count]) const;
    void PublishDerivedState();

    SWeatherDiurnalSnapshot m_snapshot;
    // Last intensities sent to clients, for change-quantized environment revision bumps.
    float m_published_intensity[weather_type_count] = {};
    std::optional<EWeatherType> m_forced_weather;
    std::optional<float> m_forced_intensity;
    std::optional<SMeteorology> m_meteorology_override;
    double m_day_duration_seconds = 6.0 * 60.0 * 60.0;
};
