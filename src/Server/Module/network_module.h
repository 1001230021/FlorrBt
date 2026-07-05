#pragma once
#include "../Game/player.h"
#include "module.h"
#include <SFML/Network/TcpListener.hpp>
#include <memory>
#include <set>
#include <vector>

class CGameWorld;
struct ServerSnapshot;

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
    void BroadcastSnapshots(float dt);
    void QueueSnapshot(CPlayer& player, const ServerSnapshot& snapshot);
    void FlushOutgoing();
    ServerSnapshot BuildSnapshot(const CPlayer& player) const;

    int GetNewPlayerId();
    void FreePlayerId(int id);

    CGameWorld& m_lobby_world;
    sf::TcpListener m_listener;
    std::vector<std::unique_ptr<CPlayer>> m_players;
    std::set<int> m_free_player_ids;
    int m_next_player_id = 1;
    float m_snapshot_timer = 0.f;
    uint32_t m_snapshot_sequence = 0;
};
