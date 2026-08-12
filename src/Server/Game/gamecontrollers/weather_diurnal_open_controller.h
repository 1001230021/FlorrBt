#pragma once

#include "../systems/weather_diurnal_system.h"
#include "opencontroller.h"

class CWeatherDiurnalOpenController : public COpenController
{
  public:
    void OnActivate(CGameWorld& world) override;
    void OnTick(CGameWorld& world, float dt) override;
    std::string_view SnapshotKey() const override { return "world_controller.weather_diurnal_open"; }
    bool CanRestoreSnapshotKey(std::string_view key) const override
    {
        return key == SnapshotKey() || key == "world_controller.open";
    }

    const SWeatherDiurnalSnapshot& WeatherDiurnalSnapshot() const { return m_weather_diurnal; }

  protected:
    virtual void OnWeatherDiurnalTick(CGameWorld&, const SWeatherDiurnalSnapshot&, float) {}
    const SWeatherDiurnalSnapshot* SpawnEnvironment() const override { return &m_weather_diurnal; }

  private:
    void RefreshWeatherDiurnalSnapshot(CGameWorld& world);

    SWeatherDiurnalSnapshot m_weather_diurnal;
};
