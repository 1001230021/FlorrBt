#include "gamecontext.h"
#include "../../Shared/tools.h"
#include "../Module/network_module.h"

CGameContext::CGameContext(CGameWorld& world, INetworkModule& network, IGameEventSink& events,
                           IGameWorldResolver& worlds)
    : m_world(world), m_network(network), m_events(events), m_worlds(worlds)
{
}

const std::vector<std::unique_ptr<CPlayer>>& CGameContext::Players() const { return m_network.GetPlayers(); }

CPlayer* CGameContext::FindPlayerFromEntity(CEntity* entity) const { return ::FindPlayerFromEntity(entity, Players()); }
