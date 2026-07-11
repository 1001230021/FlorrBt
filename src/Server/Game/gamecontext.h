#pragma once
#include <memory>
#include <vector>

class CEntity;
class CGameWorld;
class CPlayer;
class INetworkModule;

class CGameContext
{
  public:
    CGameContext(CGameWorld& world, INetworkModule& network);

    CGameWorld& World() { return m_world; }
    const CGameWorld& World() const { return m_world; }

    INetworkModule& Network() { return m_network; }
    const INetworkModule& Network() const { return m_network; }

    const std::vector<std::unique_ptr<CPlayer>>& Players() const;
    CPlayer* FindPlayerFromEntity(CEntity* entity) const;

  private:
    CGameWorld& m_world;
    INetworkModule& m_network;
};
