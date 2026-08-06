#pragma once
#include "../Game/player.h"
#include "../Game/systems/player_lifecycle_service.h"
#include "../Game/systems/snapshot_service.h"
#include "../server.h"
#include "module.h"
#include <SFML/Network/TcpListener.hpp>
#include <chrono>
#include <deque>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

class CGameWorld;
class CGameContext;
class CEntity;
class CPlayer;
class CAccountAuthWorker;
class CEmailVerificationService;
struct ClientAuthRequest;
struct ClientChatRequest;
struct ClientCraftRequest;
struct ClientForgeRequest;
struct ClientInputFrame;
struct ClientSecondarySlotRequest;
struct ClientSnapshotAck;
struct ClientTalentRequest;
struct SCraftResult;
struct ServerMessage;

class INetworkModule : public IModule, public IPlayerLifecycleNotifier
{
  public:
    struct SSnapshotTelemetry
    {
        std::uint64_t full_snapshots_queued = 0;
        std::uint64_t delta_snapshots_queued = 0;
        std::uint64_t full_snapshot_bytes = 0;
        std::uint64_t actual_snapshot_bytes = 0;
        std::uint64_t changed_entities = 0;
        std::uint64_t removed_entities = 0;
        std::uint64_t build_count = 0;
        double last_build_ms = 0.0;
        double average_build_ms = 0.0;
        double max_build_ms = 0.0;
    };

    INetworkModule(CServer& server, CGameWorld& lobby);
    ~INetworkModule();

    bool Init() override;
    void Tick(float dt) override;
    void ShutDown() override;
    CPlayer* FindPlayerById(uint32_t player_id) const;
    bool AssignPlayerEntity(uint32_t player_id, CGameWorld& world, int entity_id);
    bool KickPlayer(uint32_t player_id, const std::string& reason);
    void BanPlayerIp(uint32_t player_id, float seconds);
    void BanName(const std::string& name, float seconds);
    const std::vector<std::unique_ptr<CPlayer>>& GetPlayers() const { return m_players; }
    void ClearPlayersForRestore();
    bool InsertRestoredPlayer(std::unique_ptr<CPlayer> player);
    void FinalizePlayerRestore();
    CGameContext* GameContext() const;
    void BroadcastChat(const CServer::SChatEntry& chat);
    bool SendChatToPlayer(CPlayer& player, const CServer::SChatEntry& chat);
    bool QueueWelcome(CPlayer& player) override;
    bool QueueOwnerStateUpdate(CPlayer& player) override;
    bool QueueInventoryUpdate(CPlayer& player);
    void NotifyPlayerWorldChanged(CPlayer& player);
    const SSnapshotTelemetry& GetSnapshotTelemetry() const { return m_snapshot_telemetry; }

  private:
    static constexpr size_t auth_queue_limit = 128;
    static constexpr size_t unauthenticated_connection_limit = 256;
    static constexpr int initial_auth_timeout_seconds = 15;
    static constexpr int auth_verification_grace_seconds = 30;
    static constexpr int auth_failure_window_seconds = 300;
    static constexpr int auth_failure_limit = 5;
    static constexpr int auth_failure_lock_seconds = 30;
    static constexpr size_t auth_failure_state_limit = 4096;
    static constexpr size_t snapshot_history_limit = 90;

    struct SPendingAuthRequest
    {
        std::uint64_t request_id = 0;
        std::uint32_t player_id = 0;
        std::string account_name;
        std::string password;
        std::string email;
        bool register_mode = false;
    };

    struct SPendingVerifiedLogin
    {
        std::string account_name;
        std::string trusted_ip_to_commit;
        std::chrono::steady_clock::time_point expires_at{};
    };

    struct SAuthFailureState
    {
        int failures = 0;
        std::chrono::steady_clock::time_point last_failure{};
        std::chrono::steady_clock::time_point blocked_until{};
    };

    struct SSnapshotHistoryEntry
    {
        std::uint32_t snapshot_id = 0;
        std::uint64_t server_tick = 0;
        std::unordered_map<net_entity_id, ServerEntitySnap> entities;
    };

    struct SClientSnapshotState
    {
        std::optional<std::uint32_t> acked_snapshot_id;
        std::deque<SSnapshotHistoryEntry> history;
    };

    enum class EPlayerBufferResult
    {
        Continue,
        RequestedDisconnect,
        RemovePendingPlayer,
    };

    enum class EProxyHeaderResult
    {
        Resolved,
        NeedMoreData,
        Rejected,
    };

    void AcceptConnections();
    void ProcessMessages();
    void ProcessAuthenticationResults();
    void ProcessEmailDeliveryResults();
    void DispatchAuthenticationRequests();
    void CancelAuthenticationRequest(std::uint32_t player_id);
    bool IsLoginRateLimited(const CPlayer& player, const std::string& account_name);
    void RecordLoginFailure(const CPlayer& player, const std::string& account_name);
    void ClearLoginFailures(const CPlayer& player, const std::string& account_name);
    void SendSnapshots();
    void TickTimeouts(float dt);
    void TickBans(float dt);
    bool IsIpBanned(const std::string& ip) const;
    bool IsNameBanned(const std::string& name) const;
    bool FlushSendBuffer(CPlayer& player);
    void DropQueuedSnapshots(CPlayer& player);
    bool QueueMessage(CPlayer& player, const ServerMessage& msg, size_t packed_size = 0);
    void HandleInputFrame(CPlayer& player, const ClientInputFrame& frame);
    void HandleSnapshotAck(CPlayer& player, const ClientSnapshotAck& ack);
    SSnapshotHistoryEntry PrepareSnapshotForSend(CPlayer& player, ServerMessage& msg, size_t full_size,
                                                 size_t& actual_size);
    void RecordSentSnapshot(CPlayer& player, SSnapshotHistoryEntry entry);
    void RecordSnapshotBuildTime(double elapsed_ms);
    void RecordQueuedSnapshot(const ServerMessage& msg, size_t full_size, size_t actual_size);
    void ResetSnapshotState(CPlayer& player);
    bool QueueAuthResult(CPlayer& player, EAuthResultCode code, const std::string& message);
    bool QueueOwnerState(CPlayer& player);
    bool QueueInventory(CPlayer& player) override;
    bool QueueCraftResult(CPlayer& player, const SCraftResult& result);
    bool QueueChat(CPlayer& player, const CServer::SChatEntry& chat);
    EProxyHeaderResult ResolveTrustedProxyHeader(CPlayer& player);
    EPlayerBufferResult ProcessPlayerBuffer(CPlayer& player);
    EPlayerBufferResult HandleAuthRequest(CPlayer& player, const ClientAuthRequest& request);
    EPlayerBufferResult ApplyAuthentication(CPlayer& player, const std::string& account_name, bool register_mode,
                                            EAuthResultCode result_code = EAuthResultCode::Authenticated,
                                            const std::string& result_message = {},
                                            const std::string& trusted_ip_to_commit = {});
    bool RequestVerificationCode(CPlayer& player, const std::string& account_name, const std::string& email,
                                 bool registration);
    void ClearPendingEmailAuthentication(std::uint32_t player_id);
    void HandleChatRequest(CPlayer& player, const ClientChatRequest& request);
    void HandleCraftRequest(CPlayer& player, const ClientCraftRequest& request);
    void HandleForgeRequest(CPlayer& player, const ClientForgeRequest& request);
    void HandleSecondarySlotRequest(CPlayer& player, const ClientSecondarySlotRequest& request);
    void HandleTalentRequest(CPlayer& player, const ClientTalentRequest& request);
    CPlayer* FindReconnectablePlayer(const std::string& account_name, const CPlayer* pending_player = nullptr) const;
    void DropPlayer(size_t index, const std::string& reason);

    int GetNewPlayerId();
    void FreePlayerId(int id);

    CServer& m_server;
    CGameWorld& m_lobby_world;
    sf::TcpListener m_listener_v6;
    sf::TcpListener m_listener_v4;
    bool m_listening_v4 = false;
    bool m_listening_v6 = false;
    std::vector<std::unique_ptr<CPlayer>> m_players;
    std::set<std::uint32_t> m_proxy_header_pending;
    std::unordered_map<std::uint32_t, std::chrono::steady_clock::time_point> m_auth_deadlines;
    std::set<int> m_free_player_ids;
    int m_next_player_id = 1;
    uint32_t m_snapshot_id = 0;
    float m_snapshot_timer = 0.f;
    std::unique_ptr<CAccountAuthWorker> m_auth_worker;
    std::unique_ptr<CEmailVerificationService> m_email_verification_service;
    std::deque<SPendingAuthRequest> m_pending_auth_requests;
    std::unordered_map<std::uint32_t, std::uint64_t> m_player_auth_requests;
    std::set<std::string> m_active_auth_accounts;
    std::unordered_map<std::uint32_t, SPendingVerifiedLogin> m_pending_verified_logins;
    std::unordered_map<std::uint32_t, std::uint64_t> m_email_delivery_requests;
    std::unordered_map<std::string, SAuthFailureState> m_auth_failures;
    std::uint64_t m_next_auth_request_id = 1;
    CPlayerLifecycleService m_player_lifecycle_service;
    CSnapshotService m_snapshot_service;
    SSnapshotTelemetry m_snapshot_telemetry;
    double m_snapshot_build_total_ms = 0.0;
    std::unordered_map<std::uint32_t, SClientSnapshotState> m_client_snapshot_states;
    std::vector<std::pair<std::string, float>> m_ip_bans;
    std::vector<std::pair<std::string, float>> m_name_bans;
};
