#pragma once
#include "game_services.h"
#include <memory>
#include <vector>

class CEntity;
class CGameWorld;
class CPlayer;
class INetworkModule;
class CExtensionRuntime;

class CGameContext
{
  public:
    CGameContext(INetworkModule& network, IGameEventSink& events, IGameWorldResolver& worlds,
                 CExtensionRuntime& extensions);

    CGameWorld* MainWorld() const { return m_worlds.GetMainWorld(); }

    INetworkModule& Network() { return m_network; }
    const INetworkModule& Network() const { return m_network; }

    IGameEventSink& Events() { return m_events; }
    const IGameEventSink& Events() const { return m_events; }

    IGameWorldResolver& Worlds() { return m_worlds; }
    const IGameWorldResolver& Worlds() const { return m_worlds; }

    CExtensionRuntime& Extensions() const { return m_extensions; }

    const std::vector<std::unique_ptr<CPlayer>>& Players() const;
    CPlayer* FindPlayerFromEntity(CEntity* entity) const;

  private:
    INetworkModule& m_network;
    IGameEventSink& m_events;
    IGameWorldResolver& m_worlds;
    CExtensionRuntime& m_extensions;
};
