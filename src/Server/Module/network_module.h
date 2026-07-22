#pragma once
#include "../server.h"
#include "../Game/player.h"
#include "../Game/systems/player_lifecycle_service.h"
#include "../Game/systems/snapshot_service.h"
#include "module.h"
#include <SFML/Network/TcpListener.hpp>
#include <memory>
#include <set>
#include <string>
#include <vector>

class CGameWorld;
class CGameContext;
class CEntity;
class CPlayer;
struct ClientAuthRequest;
struct ClientChatRequest;
struct ClientCraftRequest;
struct ClientSecondarySlotRequest;
struct ClientTalentRequest;
struct SCraftResult;
struct ServerMessage;

class INetworkModule : public IModule, public IPlayerLifecycleNotifier
{
  public:
    explicit INetworkModule(CGameWorld& lobby) : m_lobby_world(lobby) {}
    ~INetworkModule() = default;

    bool Init() override;
    void Tick(float dt) override;
    void ShutDown() override;
    CPlayer* FindPlayerById(uint32_t player_id) const;
    bool AssignPlayerEntity(uint32_t player_id, int entity_id);
    bool KickPlayer(uint32_t player_id, const std::string& reason);
    void BanPlayerIp(uint32_t player_id, float seconds);
    void BanName(const std::string& name, float seconds);
    const std::vector<std::unique_ptr<CPlayer>>& GetPlayers() const { return m_players; }
    CGameContext* GameContext() const;
    void BroadcastChat(const CServer::SChatEntry& chat);
    bool SendChatToPlayer(CPlayer& player, const CServer::SChatEntry& chat);
    bool QueueWelcome(CPlayer& player) override;
    bool QueueOwnerStateUpdate(CPlayer& player) override;
    bool QueueInventoryUpdate(CPlayer& player);
    void NotifyPlayerWorldChanged(CPlayer& player);

  private:
    enum class EPlayerBufferResult
    {
        Continue,
        RequestedDisconnect,
        RemovePendingPlayer,
    };

    void AcceptConnections();
    void ProcessMessages();
    void SendSnapshots();
    void TickTimeouts(float dt);
    void TickBans(float dt);
    bool IsIpBanned(const std::string& ip) const;
    bool IsNameBanned(const std::string& name) const;
    bool FlushSendBuffer(CPlayer& player);
    void DropQueuedSnapshots(CPlayer& player);
    bool QueueMessage(CPlayer& player, const ServerMessage& msg);
    bool QueueAuthResult(CPlayer& player, bool success, const std::string& message);
    bool QueueOwnerState(CPlayer& player);
    bool QueueInventory(CPlayer& player) override;
    bool QueueCraftResult(CPlayer& player, const SCraftResult& result);
    bool QueueChat(CPlayer& player, const CServer::SChatEntry& chat);
    EPlayerBufferResult ProcessPlayerBuffer(CPlayer& player);
    EPlayerBufferResult HandleAuthRequest(CPlayer& player, const ClientAuthRequest& request);
    void HandleChatRequest(CPlayer& player, const ClientChatRequest& request);
    void HandleCraftRequest(CPlayer& player, const ClientCraftRequest& request);
    void HandleSecondarySlotRequest(CPlayer& player, const ClientSecondarySlotRequest& request);
    void HandleTalentRequest(CPlayer& player, const ClientTalentRequest& request);
    CPlayer* FindReconnectablePlayer(const std::string& account_name, const CPlayer* pending_player = nullptr) const;
    void DropPlayer(size_t index, const std::string& reason);

    int GetNewPlayerId();
    void FreePlayerId(int id);

    CGameWorld& m_lobby_world;
    sf::TcpListener m_listener_v6;
    sf::TcpListener m_listener_v4;
    bool m_listening_v4 = false;
    bool m_listening_v6 = false;
    std::vector<std::unique_ptr<CPlayer>> m_players;
    std::set<int> m_free_player_ids;
    int m_next_player_id = 1;
    uint32_t m_snapshot_id = 0;
    float m_snapshot_timer = 0.f;
    CPlayerLifecycleService m_player_lifecycle_service;
    CSnapshotService m_snapshot_service;
    std::vector<std::pair<std::string, float>> m_ip_bans;
    std::vector<std::pair<std::string, float>> m_name_bans;
};
