#pragma once
#include "../Game/player.h"
#include "module.h"
#include <SFML/Network/TcpListener.hpp>
#include <memory>
#include <set>
#include <vector>

class CGameWorld;

class INetworkModule : public IModule
{
  public:
    explicit INetworkModule(CGameWorld& lobby) : m_lobby_world(lobby) {}
    ~INetworkModule() = default;

    bool Init() override;
    void Tick(float dt) override;
    void ShutDown() override;

  private:
    void AcceptConnections();
    void ProcessMessages();

    int GetNewPlayerId();
    void FreePlayerId(int id);

    CGameWorld& m_lobby_world;
    sf::TcpListener m_listener;
    std::vector<std::unique_ptr<CPlayer>> m_players;
    std::set<int> m_free_player_ids;
    int m_next_player_id = 1;
};