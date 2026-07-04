#pragma once
#include "../Game/player.h"
#include "module.h"
#include <SFML/Network/TcpListener.hpp>
#include <memory>
#include <set>
#include <vector>

class CGameWorld;
class CEntity;
class CPlayer;
struct ServerEntitySnap;
struct ServerMessage;

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
    void SendSnapshots();
    void TickTimeouts(float dt);
    bool FlushSendBuffer(CPlayer& player);
    bool QueueMessage(CPlayer& player, const ServerMessage& msg);
    bool ProcessPlayerBuffer(CPlayer& player);
    void Respawn(CPlayer& player);
    CPlayer* FindReconnectablePlayer() const;
    void DropPlayer(size_t index, const std::string& reason);
    ServerEntitySnap BuildEntitySnap(const CEntity& entity, const CEntity& owner) const;

    int GetNewPlayerId();
    void FreePlayerId(int id);

    CGameWorld& m_lobby_world;
    sf::TcpListener m_listener;
    std::vector<std::unique_ptr<CPlayer>> m_players;
    std::set<int> m_free_player_ids;
    int m_next_player_id = 1;
    uint32_t m_snapshot_id = 0;
};
