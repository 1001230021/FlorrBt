#pragma once
#include "game_services.h"
#include <memory>
#include <vector>

class CEntity;
class CGameWorld;
class CPlayer;
class INetworkModule;

class CGameContext
{
  public:
    CGameContext(CGameWorld& world, INetworkModule& network, IGameEventSink& events, IGameWorldResolver& worlds);

    CGameWorld& World() { return m_world; }
    const CGameWorld& World() const { return m_world; }

    INetworkModule& Network() { return m_network; }
    const INetworkModule& Network() const { return m_network; }

    IGameEventSink& Events() { return m_events; }
    const IGameEventSink& Events() const { return m_events; }

    IGameWorldResolver& Worlds() { return m_worlds; }
    const IGameWorldResolver& Worlds() const { return m_worlds; }

    const std::vector<std::unique_ptr<CPlayer>>& Players() const;
    CPlayer* FindPlayerFromEntity(CEntity* entity) const;

  private:
    CGameWorld& m_world;
    INetworkModule& m_network;
    IGameEventSink& m_events;
    IGameWorldResolver& m_worlds;
};
