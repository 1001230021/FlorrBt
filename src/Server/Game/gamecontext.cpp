#include "gamecontext.h"
#include "../../Shared/tools.h"
#include "../Module/network_module.h"

CGameContext::CGameContext(CGameWorld& world, INetworkModule& network) : m_world(world), m_network(network) {}

const std::vector<std::unique_ptr<CPlayer>>& CGameContext::Players() const
{
    return m_network.GetPlayers();
}

CPlayer* CGameContext::FindPlayerFromEntity(CEntity* entity) const
{
    return ::FindPlayerFromEntity(entity, Players());
}
