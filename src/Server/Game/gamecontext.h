#pragma once
#include "game_services.h"
#include <memory>
#include <vector>

class CEntity;
class CGameWorld;
class CPlayer;
class INetworkModule;
class IWeatherDiurnalView;

class CGameContext
{
  public:
    CGameContext(CGameWorld& world, INetworkModule& network, IGameEventSink& events, IGameWorldResolver& worlds,
                 IWeatherDiurnalView& weather_diurnal);

    CGameWorld& World() { return m_world; }
    const CGameWorld& World() const { return m_world; }

    INetworkModule& Network() { return m_network; }
    const INetworkModule& Network() const { return m_network; }

    IGameEventSink& Events() { return m_events; }
    const IGameEventSink& Events() const { return m_events; }

    IGameWorldResolver& Worlds() { return m_worlds; }
    const IGameWorldResolver& Worlds() const { return m_worlds; }

    IWeatherDiurnalView& WeatherDiurnal() { return m_weather_diurnal; }
    const IWeatherDiurnalView& WeatherDiurnal() const { return m_weather_diurnal; }

    const std::vector<std::unique_ptr<CPlayer>>& Players() const;
    CPlayer* FindPlayerFromEntity(CEntity* entity) const;

  private:
    CGameWorld& m_world;
    INetworkModule& m_network;
    IGameEventSink& m_events;
    IGameWorldResolver& m_worlds;
    IWeatherDiurnalView& m_weather_diurnal;
};
