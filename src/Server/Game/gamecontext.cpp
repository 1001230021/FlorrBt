#include "gamecontext.h"
#include "../../Shared/tools.h"
#include "../Module/network_module.h"

CGameContext::CGameContext(INetworkModule& network, IGameEventSink& events,
                           IGameWorldResolver& worlds, CExtensionRuntime& extensions)
    : m_network(network), m_events(events), m_worlds(worlds), m_extensions(extensions)
{
}

const std::vector<std::unique_ptr<CPlayer>>& CGameContext::Players() const { return m_network.GetPlayers(); }

CPlayer* CGameContext::FindPlayerFromEntity(CEntity* entity) const { return ::FindPlayerFromEntity(entity, Players()); }
