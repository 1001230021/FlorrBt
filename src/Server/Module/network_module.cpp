#include "network_module.h"
#include "../DevTools/dev_debug_service.h"
#include "../Auth/email_verification_service.h"
#include "../Persistence/account_store.h"
#include "../Persistence/unique_petal_registry.h"
#include "../../Engine/logger.h"
#include "../../Shared/drop_rate.h"
#include "../../Shared/mob_type.h"
#include "../../Shared/network_msg.h"
#include "../../Shared/petal_type.h"
#include "../../Shared/rarity.h"
#include "../../Shared/utf8.h"
#include "../Game/controllers/player_controller.h"
#include "../Game/entities/flower.h"
#include "../Game/entities/petals/petal.h"
#include "../Game/gamecontext.h"
#include "../Game/gamecontrollers/world_controller_factory.h"
#include "../Game/gameworld.h"
#include "../Game/player.h"
#include "../Game/systems/weather_diurnal_system.h"
#include "../Game/talent.h"
#include "../report.h"
#include <SFML/Network.hpp>
#include <algorithm>
#include <array>
#include <charconv>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <condition_variable>
#include <exception>
#include <functional>
#include <limits>
#include <mutex>
#include <sstream>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

class CAccountAuthWorker
{
  public:
    struct SJob
    {
        std::uint64_t request_id = 0;
        std::uint32_t player_id = 0;
        SAccountAuthWork work;
    };

    struct SCompletion
    {
        std::uint64_t request_id = 0;
        std::uint32_t player_id = 0;
        std::string account_name;
        bool register_mode = false;
        SAccountAuthResult result;
    };

    ~CAccountAuthWorker() { ShutDown(); }

    bool Start(std::string& error)
    {
        error.clear();
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (m_running || m_thread.joinable())
            {
                error = "Authentication worker is already running";
                return false;
            }
            m_jobs.clear();
            m_completions.clear();
            m_stop = false;
            m_accepting = true;
            m_running = true;
        }

        try
        {
            m_thread = std::thread(&CAccountAuthWorker::WorkerMain, this);
        } catch (const std::exception& exception)
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_accepting = false;
            m_running = false;
            error = "Failed to start authentication worker: " + std::string(exception.what());
            return false;
        }
        return true;
    }

    bool Submit(SJob job)
    {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (!m_accepting || !m_running) return false;
            m_jobs.push_back(std::move(job));
        }
        m_condition.notify_one();
        return true;
    }

    std::vector<SCompletion> TakeCompletions()
    {
        std::vector<SCompletion> completions;
        std::lock_guard<std::mutex> lock(m_mutex);
        completions.reserve(m_completions.size());
        while (!m_completions.empty())
        {
            completions.push_back(std::move(m_completions.front()));
            m_completions.pop_front();
        }
        return completions;
    }

    void ShutDown()
    {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_accepting = false;
            m_stop = true;
            m_jobs.clear();
        }
        m_condition.notify_all();
        if (m_thread.joinable()) m_thread.join();

        std::lock_guard<std::mutex> lock(m_mutex);
        m_completions.clear();
        m_running = false;
    }

  private:
    void WorkerMain()
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        while (true)
        {
            m_condition.wait(lock, [this]() { return m_stop || !m_jobs.empty(); });
            if (m_stop) break;

            SJob job = std::move(m_jobs.front());
            m_jobs.pop_front();
            lock.unlock();

            SCompletion completion;
            completion.request_id = job.request_id;
            completion.player_id = job.player_id;
            completion.account_name = job.work.account_name;
            completion.register_mode = job.work.register_mode;
            try
            {
                completion.result = CAccountDataStore::ExecuteAuthentication(std::move(job.work));
            } catch (const std::exception& exception)
            {
                completion.result.error = "Authentication worker failed: " + std::string(exception.what());
            } catch (...)
            {
                completion.result.error = "Authentication worker failed";
            }

            lock.lock();
            if (!m_stop) m_completions.push_back(std::move(completion));
        }

        m_running = false;
    }

    std::mutex m_mutex;
    std::condition_variable m_condition;
    std::thread m_thread;
    std::deque<SJob> m_jobs;
    std::deque<SCompletion> m_completions;
    bool m_accepting = false;
    bool m_running = false;
    bool m_stop = false;
};

class CSnapshotBuildWorkerPool
{
  public:
    explicit CSnapshotBuildWorkerPool(size_t worker_count)
    {
        m_workers.reserve(worker_count);
        try
        {
            for (size_t i = 0; i < worker_count; ++i)
                m_workers.emplace_back(&CSnapshotBuildWorkerPool::WorkerMain, this);
        } catch (...)
        {
            ShutDown();
            throw;
        }
    }

    ~CSnapshotBuildWorkerPool() { ShutDown(); }

    size_t WorkerCount() const { return m_workers.size(); }

    void Run(size_t job_count, std::function<void(size_t)> job)
    {
        if (job_count == 0) return;
        if (m_workers.empty())
        {
            for (size_t i = 0; i < job_count; ++i)
                job(i);
            return;
        }

        std::unique_lock lock(m_mutex);
        m_job = std::move(job);
        m_job_count = job_count;
        m_next_job = 0;
        m_workers_pending = m_workers.size();
        m_exception = nullptr;
        ++m_generation;
        m_start_condition.notify_all();
        m_done_condition.wait(lock, [this]() { return m_workers_pending == 0; });
        m_job = {};
        if (m_exception) std::rethrow_exception(m_exception);
    }

  private:
    void ShutDown()
    {
        {
            std::lock_guard lock(m_mutex);
            m_stopping = true;
            ++m_generation;
        }
        m_start_condition.notify_all();
        for (std::thread& worker : m_workers)
            if (worker.joinable()) worker.join();
        m_workers.clear();
    }

    void WorkerMain()
    {
        std::unique_lock lock(m_mutex);
        std::uint64_t observed_generation = 0;
        while (true)
        {
            m_start_condition.wait(lock,
                                   [this, &observed_generation]() {
                                       return m_stopping || m_generation != observed_generation;
                                   });
            if (m_stopping) return;
            observed_generation = m_generation;

            while (m_next_job < m_job_count)
            {
                const size_t job_index = m_next_job++;
                const auto job = m_job;
                lock.unlock();
                try
                {
                    job(job_index);
                } catch (...)
                {
                    lock.lock();
                    if (!m_exception) m_exception = std::current_exception();
                    continue;
                }
                lock.lock();
            }

            if (--m_workers_pending == 0) m_done_condition.notify_one();
        }
    }

    std::mutex m_mutex;
    std::condition_variable m_start_condition;
    std::condition_variable m_done_condition;
    std::vector<std::thread> m_workers;
    std::function<void(size_t)> m_job;
    std::exception_ptr m_exception;
    size_t m_job_count = 0;
    size_t m_next_job = 0;
    size_t m_workers_pending = 0;
    std::uint64_t m_generation = 0;
    bool m_stopping = false;
};

namespace
{
constexpr std::string_view proxy_v1_prefix = "PROXY ";
constexpr size_t proxy_v1_max_header_size = 108;

bool IsLoopbackAddress(std::string_view address)
{
    const auto parsed = sf::IpAddress::fromString(address);
    if (!parsed) return false;
    if (*parsed == sf::IpAddress::LocalHostV4 || *parsed == sf::IpAddress::LocalHostV6) return true;

    if (!parsed->isV6()) return false;
    const auto bytes = parsed->toBytes();
    return std::all_of(bytes.begin(), bytes.begin() + 10, [](std::uint8_t value) { return value == 0; }) &&
           bytes[10] == 0xff && bytes[11] == 0xff && bytes[12] == 127;
}

bool IsProxyPort(std::string_view text)
{
    if (text.empty()) return false;
    unsigned int value = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    return error == std::errc{} && end == text.data() + text.size() && value <= 65535;
}

bool ParseProxyV1Header(std::string_view line, std::string& source_address)
{
    std::istringstream stream{ std::string(line) };
    std::string signature;
    std::string family;
    std::string source;
    std::string destination;
    std::string source_port;
    std::string destination_port;
    std::string extra;
    if (!(stream >> signature >> family >> source >> destination >> source_port >> destination_port) ||
        stream >> extra || signature != "PROXY" || (family != "TCP4" && family != "TCP6") ||
        !IsProxyPort(source_port) || !IsProxyPort(destination_port))
        return false;

    const auto parsed_source = sf::IpAddress::fromString(source);
    const auto parsed_destination = sf::IpAddress::fromString(destination);
    if (!parsed_source || !parsed_destination) return false;

    const bool ipv4 = family == "TCP4";
    if (parsed_source->isV4() != ipv4 || parsed_destination->isV4() != ipv4 ||
        *parsed_source == sf::IpAddress::AnyV4 || *parsed_source == sf::IpAddress::AnyV6)
        return false;

    source_address = parsed_source->toString();
    return true;
}

uint16_t EncodeExpProgressBps(const CPlayerFlower& flower)
{
    const std::int64_t required = flower.ExpRequired();
    if (required <= 0) return 0;

    long double progress =
        static_cast<long double>(std::max<std::int64_t>(0, flower.m_exp)) / static_cast<long double>(required);
    progress = std::clamp(progress, 0.0L, 1.0L);
    return static_cast<uint16_t>(std::clamp<long long>(std::llround(progress * 10000.0L), 0, 10000));
}

size_t PacketLengthAt(const std::vector<uint8_t>& buffer, size_t offset)
{
    if (offset + packet_length_prefix_size > buffer.size()) return 0;
    return static_cast<size_t>(buffer[offset]) | (static_cast<size_t>(buffer[offset + 1]) << 8);
}

void CompactSentPackets(CPlayer& player)
{
    if (player.m_send_offset == 0) return;
    if (player.m_send_offset >= player.m_send_buffer.size())
    {
        player.m_send_buffer.clear();
        player.m_send_offset = 0;
        return;
    }

    size_t erase_until = 0;
    while (erase_until + packet_length_prefix_size <= player.m_send_buffer.size())
    {
        size_t packet_len = PacketLengthAt(player.m_send_buffer, erase_until);
        size_t packet_end = erase_until + packet_length_prefix_size + packet_len;
        if (packet_end > player.m_send_buffer.size() || packet_end > player.m_send_offset) break;
        erase_until = packet_end;
    }

    if (erase_until == 0) return;
    player.m_send_buffer.erase(player.m_send_buffer.begin(), player.m_send_buffer.begin() + erase_until);
    player.m_send_offset -= erase_until;
}

size_t PendingSendBytes(const CPlayer& player)
{
    if (player.m_send_offset >= player.m_send_buffer.size()) return 0;
    return player.m_send_buffer.size() - player.m_send_offset;
}

size_t SnapshotBacklogSkipBytes()
{
    const size_t max_backlog = CSnapshotService::backlog_skip_bytes;
    const size_t min_backlog =
        std::min(std::max<size_t>(1, game_config::network_snapshot_backlog_min_bytes), max_backlog);
    const size_t packet_multiplier = std::max<size_t>(1, game_config::network_snapshot_backlog_packet_multiplier);
    size_t configured = game_config::network_snapshot_backlog_skip_bytes;
    if (configured == 0)
        configured = std::max(min_backlog, game_config::network_snapshot_packet_budget * packet_multiplier);
    return std::clamp(configured, min_backlog, max_backlog);
}

bool IsSlotOperate(const ClientOperate& op)
{
    return op.type == ClientOperate::Type::Equip || op.type == ClientOperate::Type::Unequip;
}

std::string ToLower(std::string text)
{
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return text;
}

std::string Trim(std::string_view text)
{
    size_t begin = 0;
    while (begin < text.size() && std::isspace(static_cast<unsigned char>(text[begin])))
        ++begin;

    size_t end = text.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1])))
        --end;

    return std::string(text.substr(begin, end - begin));
}

std::string LogPriorityName(ELogPriority priority)
{
    switch (priority)
    {
    case ELogPriority::Debug:
        return "DEBUG";
    case ELogPriority::Info:
        return "INFO";
    case ELogPriority::Warning:
        return "WARN";
    case ELogPriority::Error:
        return "ERROR";
    case ELogPriority::Fatal:
        return "FATAL";
    }
    return "UNKNOWN";
}

bool ExtractRconPayload(const std::string& message, std::string& payload)
{
    if (message.empty() || message.front() != '/') return false;

    size_t command_begin = 1;
    size_t command_end = command_begin;
    while (command_end < message.size() && !std::isspace(static_cast<unsigned char>(message[command_end])))
        ++command_end;

    if (ToLower(message.substr(command_begin, command_end - command_begin)) != "rcon") return false;

    payload = Trim(std::string_view(message).substr(command_end));
    return true;
}

void SendPrivateServerMessage(CServer& server, INetworkModule& network, CPlayer& player, const std::string& message)
{
    std::string text = message.empty() ? "(empty)" : message;
    size_t offset = 0;
    while (offset < text.size())
    {
        const std::string_view remaining(text.data() + offset, text.size() - offset);
        std::string part(TruncateUtf8(remaining, max_chat_message_size));
        offset += part.size();
        const CServer::SChatEntry chat = server.SubmitChat(nullptr, { 0.f, 0.f }, EChatFlag::Server, 0, "RCON", part,
                                                    static_cast<int>(player.GetId()));
        if (!chat.message.empty()) network.SendChatToPlayer(player, chat);
    }
}

bool HandleRconCommand(CServer& server, INetworkModule& network, CPlayer& player, const std::string& message)
{
    std::string payload;
    if (!ExtractRconPayload(message, payload)) return false;

    if (!player.IsAuthenticated()) return true;
    network.ValidateRconAuthorization(player);

    if (payload.empty())
    {
        SendPrivateServerMessage(server, network, player,
                                 player.IsRconAuthorized() ? "Usage: /rcon [server command]"
                                                           : "Usage: /rcon [password]");
        return true;
    }

    if (!player.IsRconAuthorized())
    {
        if (game_config::rcon_password.empty())
        {
            SendPrivateServerMessage(server, network, player, "RCON is disabled.");
            return true;
        }

        std::string auth_message;
        if (network.AuthenticateRcon(player, payload, auth_message))
        {
            LOG_INFO("rcon", "Player " + player.GetName() + " authorized from " + player.GetRemoteAddress());
            SendPrivateServerMessage(server, network, player, "RCON authorized.");
        } else
        {
            LOG_WARN("rcon", "Failed RCON login for player " + player.GetName() + " from " + player.GetRemoteAddress());
            SendPrivateServerMessage(server, network, player, auth_message);
        }
        return true;
    }

    if (payload == "logout")
    {
        player.SetRconAuthorized(false);
        SendPrivateServerMessage(server, network, player, "RCON logged out; debug sessions revoked.");
        return true;
    }
    if (payload == "dev_debug" || payload == "debug" || payload == "dev")
    {
        if (!network.QueueDebugTicket(player))
            SendPrivateServerMessage(server, network, player, "Unable to open dev debug; check RCON/session limits.");
        return true;
    }

    LOG_INFO("rcon", "Player " + player.GetName() + " executed: " + payload);

    struct CapturedLogs { std::mutex mutex; std::vector<std::string> lines; };
    auto captured = std::make_shared<CapturedLogs>();
    size_t sink_id =
        CLogger::AddSink([captured](const std::string& sender, ELogPriority priority, const std::string& msg) {
            std::lock_guard<std::mutex> lock(captured->mutex);
            if (captured->lines.size() < 512)
                captured->lines.push_back("[" + sender + "][" + LogPriorityName(priority) + "] " + msg);
        });

    try
    {
        server.GetConsole().ExecuteLine(payload);
    } catch (const std::exception& e)
    {
        std::lock_guard<std::mutex> lock(captured->mutex);
        captured->lines.push_back(std::string("[rcon][ERROR] ") + e.what());
    } catch (...)
    {
        std::lock_guard<std::mutex> lock(captured->mutex);
        captured->lines.push_back("[rcon][ERROR] Unknown exception");
    }
    CLogger::RemoveSink(sink_id);

    std::vector<std::string> captured_lines;
    { std::lock_guard<std::mutex> lock(captured->mutex); captured_lines = captured->lines; }

    if (captured_lines.empty())
    {
        SendPrivateServerMessage(server, network, player, "Executed: " + payload);
    } else
    {
        size_t count = std::min(captured_lines.size(), game_config::network_rcon_max_reply_lines);
        for (size_t i = 0; i < count; ++i)
            SendPrivateServerMessage(server, network, player, captured_lines[i]);
        if (captured_lines.size() > count)
            SendPrivateServerMessage(server, network, player,
                                     "... " + std::to_string(captured_lines.size() - count) + " more lines");
    }
    return true;
}
} // namespace

INetworkModule::INetworkModule(CServer& server, CGameWorld& lobby)
    : m_server(server), m_lobby_world(lobby), m_dev_debug(std::make_unique<CDevDebugService>(server)) {}

INetworkModule::~INetworkModule() = default;

bool INetworkModule::QueueDebugTicket(CPlayer& player)
{
    if (!ValidateRconAuthorization(player)) return false;
    ServerMessage message;
    message.type = ServerMessage::Type::DebugTicket;
    message.debug_ticket = m_dev_debug->IssueTicket(player);
    return !message.debug_ticket.empty() && QueueMessage(player, message);
}

bool INetworkModule::AuthenticateRcon(CPlayer& player, const std::string& password, std::string& message)
{
    // This namespace cannot be a valid (<=32-byte) account name, so logging
    // into an ordinary account can never clear a RCON brute-force lockout.
    const std::string throttle_key(max_auth_name_size + 1, '\0');
    if (!player.IsAuthenticated() || !player.IsConnected() || game_config::rcon_password.empty())
    { message = "RCON is unavailable."; return false; }
    if (IsLoginRateLimited(player, throttle_key))
    { message = "Too many RCON attempts; try again later."; return false; }
    if (password != game_config::rcon_password)
    {
        RecordLoginFailure(player, throttle_key);
        message = "RCON authorization failed."; return false;
    }
    ClearLoginFailures(player, throttle_key); player.SetRconAuthorized(true);
    m_rcon_authorizations.insert_or_assign(player.GetId(), game_config::rcon_password);
    message = "RCON authorized."; return true;
}

bool INetworkModule::ValidateRconAuthorization(CPlayer& player)
{
    const auto auth = m_rcon_authorizations.find(player.GetId());
    if (player.IsConnected() && player.IsAuthenticated() && player.IsRconAuthorized() &&
        auth != m_rcon_authorizations.end() && !game_config::rcon_password.empty() &&
        auth->second == game_config::rcon_password) return true;
    player.SetRconAuthorized(false);
    if (auth != m_rcon_authorizations.end()) m_rcon_authorizations.erase(auth);
    return false;
}

bool INetworkModule::Init()
{
    int port = game_config::port;
    if (m_listener_v6.listen(port, sf::IpAddress::AnyV6) == sf::Socket::Status::Done)
    {
        m_listener_v6.setBlocking(false);
        m_listening_v6 = true;
        LOG_INFO("network", "Listening on IPv6 port " + std::to_string(port));
    } else
    {
        LOG_WARN("network", "IPv6 listen failed on port " + std::to_string(port));
    }

    if (m_listener_v4.listen(port, sf::IpAddress::AnyV4) == sf::Socket::Status::Done)
    {
        m_listener_v4.setBlocking(false);
        m_listening_v4 = true;
        LOG_INFO("network", "Listening on IPv4 port " + std::to_string(port));
    } else
    {
        LOG_WARN("network", "IPv4 listen failed on port " + std::to_string(port));
    }

    if (!m_listening_v4 && !m_listening_v6)
    {
        LOG_FATAL("network", "Failed to listen on port " + std::to_string(port));
        return false;
    }

    m_auth_worker = std::make_unique<CAccountAuthWorker>();
    std::string auth_error;
    if (!m_auth_worker->Start(auth_error))
    {
        m_listener_v6.close();
        m_listener_v4.close();
        m_listening_v4 = false;
        m_listening_v6 = false;
        LOG_FATAL("network", auth_error);
        return false;
    }
    LOG_INFO("network", "Authentication worker started");

    m_email_verification_service = std::make_unique<CEmailVerificationService>();
    std::string email_error;
    if (!m_email_verification_service->Start(email_error))
    {
        m_auth_worker->ShutDown();
        m_listener_v6.close();
        m_listener_v4.close();
        m_listening_v4 = false;
        m_listening_v6 = false;
        LOG_FATAL("email", email_error.empty() ? "Failed to start email verification service" : email_error);
        return false;
    }
    LOG_INFO("email", "Email verification worker started");

    if (game_config::network_parallel_snapshot_build)
    {
        const size_t world_count = m_server.GetWorlds().size();
        const size_t hardware_threads = std::max(1u, std::thread::hardware_concurrency());
        const size_t automatic_workers = std::min(world_count, std::max<size_t>(1, hardware_threads - 1));
        const size_t configured_workers = game_config::network_snapshot_worker_count;
        const size_t worker_count = std::min(world_count, configured_workers > 0 ? configured_workers
                                                                                : automatic_workers);
        if (worker_count > 1)
        {
            try
            {
                m_snapshot_workers = std::make_unique<CSnapshotBuildWorkerPool>(worker_count);
                LOG_INFO("network", "Snapshot builder started with " + std::to_string(worker_count) + " workers");
            } catch (const std::exception& exception)
            {
                m_snapshot_workers.reset();
                LOG_WARN("network", "Parallel snapshot builder unavailable: " + std::string(exception.what()));
            }
        }
    }
    return true;
}

void INetworkModule::Tick(float dt)
{
    for (const auto& player : m_players)
        if (player && player->IsRconAuthorized()) ValidateRconAuthorization(*player);
    ProcessEmailDeliveryResults();
    ProcessAuthenticationResults();
    AcceptConnections();
    ClassifyPendingConnections();
    ProcessMessages();
    m_dev_debug->Tick(dt);
    DispatchAuthenticationRequests();
    BroadcastEnvironmentIfChanged();
    m_player_lifecycle_service.RespawnDeadControlledEntities(m_players, m_lobby_world, *this);
    m_player_lifecycle_service.ProcessDropPickups(m_players, *this);
    TickBans(dt);
    const float snapshot_interval = game_config::network_snapshot_interval;
    if (!std::isfinite(snapshot_interval) || snapshot_interval <= 0.f)
    {
        m_snapshot_timer = 0.f;
        SendSnapshots();
    }
    else
    {
        m_snapshot_timer += std::max(0.f, dt);
        if (m_snapshot_timer >= snapshot_interval)
        {
            m_snapshot_timer = std::fmod(m_snapshot_timer, snapshot_interval);
            SendSnapshots();
        }
    }
    TickTimeouts(dt);
}

void INetworkModule::SendSnapshots()
{
    struct SSnapshotBuildItem
    {
        CPlayer* player = nullptr;
        CSnapshotService::SBuildResult snapshot;
        double build_ms = 0.0;
        bool built = false;
    };
    struct SSnapshotWorldGroup
    {
        CGameWorld* world = nullptr;
        std::vector<size_t> item_indices;
    };

    std::vector<SSnapshotBuildItem> build_items;
    std::vector<SSnapshotWorldGroup> world_groups;
    build_items.reserve(m_players.size());
    world_groups.reserve(m_server.GetWorlds().size());

    for (auto& player_ptr : m_players)
    {
        CPlayer* player = player_ptr.get();
        if (!player || !player->IsConnected() || !player->IsAuthenticated()) continue;

        CEntity* owner = player->GetEntity();
        if (!owner)
        {
            if (player->HasOwnedEntity() && !player->m_logged_missing_entity)
            {
                LOG_WARN("network", "Player " + std::to_string(player->GetId()) + " has no living owner entity");
                player->m_logged_missing_entity = true;
            }
            continue;
        }
        player->m_logged_missing_entity = false;

        auto* owner_mob = dynamic_cast<CMobBase*>(owner);
        if (!owner_mob || !owner_mob->GetFinalStats()) continue;

        DropQueuedSnapshots(*player);
        if (PendingSendBytes(*player) > SnapshotBacklogSkipBytes())
        {
            if ((m_snapshot_id % std::max(1, game_config::network_snapshot_backlog_log_interval)) == 0)
            {
                LOG_WARN("network", "Skipping snapshot for player " + std::to_string(player->GetId()) +
                                        " because output backlog is " + std::to_string(PendingSendBytes(*player)) +
                                        " bytes");
            }
            if (!FlushSendBuffer(*player))
            {
                LOG_INFO("network",
                         "Player " + std::to_string(player->GetId()) + " disconnected while flushing backlog");
                player->DetachSocket();
            }
            continue;
        }

        CGameWorld* world = owner->GameWorld();
        if (!world) continue;
        const size_t item_index = build_items.size();
        build_items.push_back({ player });
        auto group = std::find_if(world_groups.begin(), world_groups.end(),
                                  [world](const SSnapshotWorldGroup& candidate) {
                                      return candidate.world == world;
                                  });
        if (group == world_groups.end())
        {
            world_groups.push_back({ world, {} });
            group = std::prev(world_groups.end());
        }
        group->item_indices.push_back(item_index);
    }

    const auto batch_started = std::chrono::steady_clock::now();
    auto build_group = [this, &build_items, &world_groups](size_t group_index) {
        SSnapshotWorldGroup& group = world_groups[group_index];
        for (size_t item_index : group.item_indices)
        {
            SSnapshotBuildItem& item = build_items[item_index];
            const auto build_started = std::chrono::steady_clock::now();
            item.built = item.player && m_snapshot_service.BuildSnapshot(*item.player, m_snapshot_id, item.snapshot);
            item.build_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - build_started)
                                .count();
            if (item.built) item.snapshot.message.server_tick = m_server.GetTick();
        }
    };
    if (m_snapshot_workers && world_groups.size() > 1)
        m_snapshot_workers->Run(world_groups.size(), build_group);
    else
        for (size_t i = 0; i < world_groups.size(); ++i)
            build_group(i);
    RecordSnapshotBatchTime(
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - batch_started).count());

    for (SSnapshotBuildItem& item : build_items)
    {
        CPlayer* player = item.player;
        if (!player || !item.built) continue;
        RecordSnapshotBuildTime(item.build_ms);
        CSnapshotService::SBuildResult& snapshot = item.snapshot;
        const size_t full_snapshot_size = snapshot.packed_size;
        size_t actual_snapshot_size = full_snapshot_size;
        // Weather intensities ride along with every snapshot (main thread after
        // the worker batch joined), so clients always have fresh weather.
        if (const CWeatherDiurnalSystem* system = m_server.GetWeatherDiurnalSystem())
        {
            const SWeatherDiurnalClientState environment = system->GetClientState();
            for (std::size_t index = 0; index < server_environment_weather_count; ++index)
            {
                const float intensity =
                    index < static_cast<std::size_t>(EWeatherType::Count)
                        ? environment.weather_intensity[index]
                        : 0.f;
                snapshot.message.weather_intensity[index] =
                    static_cast<std::uint8_t>(std::llround(std::clamp(intensity, 0.f, 1.f) * 255.f));
            }
            const SWeatherDiurnalSnapshot environment_snapshot = system->GetWeatherDiurnalSnapshot();
            snapshot.message.wind_direction = static_cast<std::uint8_t>(std::llround(
                std::clamp(environment_snapshot.meteorology.wind_direction_degrees / 360.f, 0.f, 1.f) * 255.f));
            snapshot.message.wind_speed = static_cast<std::uint8_t>(std::llround(
                std::clamp(environment_snapshot.meteorology.wind_speed / 20.f, 0.f, 1.f) * 255.f));
        }
        SSnapshotHistoryEntry history_entry =
            PrepareSnapshotForSend(*player, snapshot.message, full_snapshot_size, actual_snapshot_size);

        if (!QueueMessage(*player, snapshot.message, actual_snapshot_size))
        {
            LOG_WARN("network", "Failed to queue snapshot for player " + std::to_string(player->GetId()) + " (" +
                                    std::to_string(snapshot.message.entities.size()) + " entities, " +
                                    std::to_string(actual_snapshot_size) +
                                    " bytes, backlog " + std::to_string(PendingSendBytes(*player)) + " bytes)");
        } else
        {
            RecordQueuedSnapshot(snapshot.message, full_snapshot_size, actual_snapshot_size);
            RecordSentSnapshot(*player, std::move(history_entry));
            // Feedback is queued on the game thread after all snapshot
            // workers join. No socket I/O or shared mutation in workers.
            if (CEntity* owner = player->GetEntity(); owner && owner->GameWorld())
            {
                ServerMessage feedback;
                feedback.type = ServerMessage::Type::CombatFeedback;
                const float range = snapshot.message.view_radius.value_or(0.f);
                for (const auto& hit : owner->GameWorld()->GetCombatFeedback())
                {
                    if (feedback.combat_hits.size() >= ServerMessage::combat_hit_limit) break;
                    if (DistanceSq(owner->Pos(), hit.pos) <= (range + hit.radius) * (range + hit.radius))
                        feedback.combat_hits.push_back(hit);
                }
                if (!feedback.combat_hits.empty()) QueueMessage(*player, feedback);
            }
        }
        if (!FlushSendBuffer(*player))
        {
            LOG_INFO("network", "Player " + std::to_string(player->GetId()) + " disconnected while flushing snapshot");
            player->DetachSocket();
        }
    }
    m_snapshot_id++;
    for (const auto& world : m_server.GetWorlds()) if (world) world->ClearCombatFeedback();
}

void INetworkModule::ShutDown()
{
    m_rcon_authorizations.clear();
    m_dev_debug->Reset();
    m_pending_connections.clear();
    m_snapshot_workers.reset();
    m_listener_v6.close();
    m_listener_v4.close();
    m_listening_v4 = false;
    m_listening_v6 = false;
    m_pending_auth_requests.clear();
    m_player_auth_requests.clear();
    m_active_auth_accounts.clear();
    m_pending_verified_logins.clear();
    m_email_delivery_requests.clear();
    m_proxy_header_pending.clear();
    m_auth_deadlines.clear();
    m_auth_failures.clear();
    if (m_email_verification_service) m_email_verification_service->ShutDown();
    if (m_auth_worker) m_auth_worker->ShutDown();
    m_players.clear();
    m_client_snapshot_states.clear();
    LOG_INFO("network", "Shut down");
}

void INetworkModule::AcceptConnections()
{
    auto accept_from = [this](sf::TcpListener& listener) {
        sf::TcpSocket new_socket;
        if (listener.accept(new_socket) != sf::Socket::Status::Done) return;

        auto remote = new_socket.getRemoteAddress();
        std::string remote_address = remote ? remote->toString() : "";
        if (IsIpBanned(remote_address))
        {
            LOG_INFO("network", "Rejected banned IP: " + remote_address);
            new_socket.disconnect();
            return;
        }
        if (m_auth_deadlines.size() + m_pending_connections.size() >= unauthenticated_connection_limit)
        {
            LOG_WARN("network", "Rejected connection: unauthenticated connection limit reached");
            new_socket.disconnect();
            return;
        }

        new_socket.setBlocking(false);

        auto pending = std::make_unique<SPendingConnection>();
        pending->socket = std::move(new_socket);
        pending->remote = remote_address;
        pending->proxy_pending = IsLoopbackAddress(remote_address);
        pending->deadline = std::chrono::steady_clock::now() + std::chrono::seconds(initial_auth_timeout_seconds);
        m_pending_connections.push_back(std::move(pending));
    };

    if (m_listening_v4) accept_from(m_listener_v4);
    if (m_listening_v6) accept_from(m_listener_v6);
}

void INetworkModule::ClassifyPendingConnections()
{
    std::array<std::uint8_t, 4096> chunk{};
    for (std::size_t index = 0; index < m_pending_connections.size();)
    {
        auto& pending = *m_pending_connections[index];
        bool reject = std::chrono::steady_clock::now() > pending.deadline;
        std::size_t received = 0;
        const auto status = pending.socket.receive(chunk.data(), chunk.size(), received);
        reject = reject || status == sf::Socket::Status::Disconnected || status == sf::Socket::Status::Error;
        if (received) pending.received.insert(pending.received.end(), chunk.begin(), chunk.begin() + received);
        reject = reject || pending.received.size() > dev_debug::request_limit + proxy_v1_max_header_size;
        auto& bytes = pending.received;
        if (!reject && pending.proxy_pending && !bytes.empty())
        {
            const auto compared = std::min(bytes.size(), proxy_v1_prefix.size());
            if (!std::equal(bytes.begin(), bytes.begin() + compared, proxy_v1_prefix.begin())) pending.proxy_pending = false;
            else if (bytes.size() >= proxy_v1_prefix.size())
            {
                static constexpr std::array<std::uint8_t, 2> terminator{ '\r', '\n' };
                const auto end = std::search(bytes.begin(), bytes.end(), terminator.begin(), terminator.end());
                if (end == bytes.end()) reject = bytes.size() > proxy_v1_max_header_size;
                else
                {
                    const auto size = static_cast<std::size_t>(end - bytes.begin()) + 2;
                    std::string source;
                    reject = size > proxy_v1_max_header_size ||
                        !ParseProxyV1Header(std::string_view(reinterpret_cast<const char*>(bytes.data()), size - 2), source) ||
                        IsIpBanned(source);
                    if (!reject)
                    {
                        pending.remote = std::move(source); pending.proxy_pending = false;
                        bytes.erase(bytes.begin(), bytes.begin() + size);
                    }
                }
            }
        }
        if (!reject && (pending.proxy_pending || bytes.empty())) { ++index; continue; }
        if (!reject && bytes.front() == dev_debug::magic)
        {
            std::size_t payload_size = 0;
            const auto frame = dev_debug::ReadFrameSize(bytes.data(), bytes.size(), dev_debug::hello_limit, payload_size);
            if (frame == dev_debug::FrameStatus::Incomplete) { ++index; continue; }
            if (frame == dev_debug::FrameStatus::Ready)
                m_dev_debug->Accept(std::move(pending.socket), std::move(pending.remote), std::move(bytes));
        }
        else if (!reject && bytes.size() <= game_config::network_max_receive_buffer_size)
        {
            // Only this branch allocates a player ID. Its existing version,
            // account login, reconnect and player-input parser stay intact.
            const int id = GetNewPlayerId();
            auto player = std::make_unique<CPlayer>(std::move(pending.socket), id, "Pending" + std::to_string(id));
            player->SetRemoteAddress(pending.remote); player->m_receive_buffer = std::move(bytes);
            m_auth_deadlines.insert_or_assign(static_cast<std::uint32_t>(id), pending.deadline);
            m_players.push_back(std::move(player));
            LOG_INFO("network", "Game connection pending auth ID: " + std::to_string(id));
        }
        m_pending_connections.erase(m_pending_connections.begin() + index);
    }
}

void INetworkModule::ProcessMessages()
{
    std::vector<uint8_t> buffer(game_config::network_receive_chunk_size);

    for (size_t i = 0; i < m_players.size();)
    {
        auto& player = m_players[i];
        if (!player->IsConnected())
        {
            ++i;
            continue;
        }

        bool disconnected = !FlushSendBuffer(*player);
        if (disconnected)
        {
            LOG_INFO("network", "Player " + std::to_string(player->GetId()) + " disconnected while sending");
            if (!player->IsAuthenticated())
            {
                DropPlayer(i, "unauthenticated connection lost while sending");
                continue;
            }
            player->DetachSocket();
            ++i;
            continue;
        }

        size_t received = 0;
        auto status = player->GetSocket().receive(buffer.data(), buffer.size(), received);

        if ((status == sf::Socket::Status::Done && received > 0) ||
            (status == sf::Socket::Status::NotReady && !player->m_receive_buffer.empty()))
        {
            player->m_receive_buffer.insert(player->m_receive_buffer.end(), buffer.begin(), buffer.begin() + received);
            if (player->m_receive_buffer.size() > game_config::network_max_receive_buffer_size)
            {
                LOG_WARN("network", "Player " + std::to_string(player->GetId()) + " input buffer overflow");
                DropPlayer(i, "input buffer overflow");
                continue;
            }
            EPlayerBufferResult result = ProcessPlayerBuffer(*player);
            if (result == EPlayerBufferResult::RequestedDisconnect)
            {
                LOG_INFO("network", "Player " + std::to_string(player->GetId()) + " requested disconnect");
                DropPlayer(i, "requested disconnect");
                continue;
            }
            if (result == EPlayerBufferResult::RemovePendingPlayer)
            {
                DropPlayer(i, "reconnected through account");
                continue;
            }
            ++i;
        } else if (status == sf::Socket::Status::Disconnected)
        {
            LOG_INFO("network", "Player " + std::to_string(player->GetId()) + " disconnected");
            if (!player->IsAuthenticated())
            {
                DropPlayer(i, "unauthenticated connection closed");
                continue;
            }
            player->DetachSocket();
            ++i;
        } else
        {
            ++i;
        }
    }
}

void INetworkModule::ProcessEmailDeliveryResults()
{
    if (!m_email_verification_service) return;

    for (CEmailVerificationService::SDeliveryResult& result :
         m_email_verification_service->TakeDeliveryResults())
    {
        const auto pending = m_email_delivery_requests.find(result.player_id);
        if (pending == m_email_delivery_requests.end() || pending->second != result.request_id) continue;
        m_email_delivery_requests.erase(pending);

        CPlayer* player = FindPlayerById(result.player_id);
        if (!player || !player->IsConnected()) continue;
        if (result.success)
            QueueAuthResult(*player, EAuthResultCode::VerificationCodeSent, "Verification code sent");
        else
            QueueAuthResult(*player, EAuthResultCode::Failed,
                            result.error.empty() ? "Verification email could not be delivered" : result.error);
    }
}

bool INetworkModule::RequestVerificationCode(CPlayer& player, const std::string& account_name,
                                              const std::string& email, bool registration)
{
    if (!m_email_verification_service)
    {
        QueueAuthResult(player, EAuthResultCode::Failed, "Email verification service is unavailable");
        return false;
    }

    std::uint64_t request_id = 0;
    std::string error;
    const CEmailVerificationService::EPurpose purpose = registration
                                                            ? CEmailVerificationService::EPurpose::Registration
                                                            : CEmailVerificationService::EPurpose::Binding;
    if (!m_email_verification_service->RequestCode(player.GetId(), account_name, email, player.GetRemoteAddress(),
                                                   purpose, request_id, error))
    {
        QueueAuthResult(player, EAuthResultCode::Failed,
                        error.empty() ? "Unable to request verification code" : error);
        return false;
    }

    m_email_delivery_requests.insert_or_assign(player.GetId(), request_id);
    m_auth_deadlines.insert_or_assign(
        player.GetId(), std::chrono::steady_clock::now() +
                            std::chrono::seconds(std::max(1, game_config::email_send_timeout_seconds) +
                                                 std::max(1, game_config::email_verification_ttl_seconds) +
                                                 auth_verification_grace_seconds));
    QueueAuthResult(player, EAuthResultCode::VerificationCodeSending, "Sending verification code");
    return true;
}

void INetworkModule::ClearPendingEmailAuthentication(std::uint32_t player_id)
{
    m_pending_verified_logins.erase(player_id);
    m_email_delivery_requests.erase(player_id);
}

void INetworkModule::ProcessAuthenticationResults()
{
    if (!m_auth_worker) return;

    for (CAccountAuthWorker::SCompletion& completion : m_auth_worker->TakeCompletions())
    {
        m_active_auth_accounts.erase(completion.account_name);

        auto pending = m_player_auth_requests.find(completion.player_id);
        if (pending == m_player_auth_requests.end() || pending->second != completion.request_id) continue;
        m_player_auth_requests.erase(pending);

        CPlayer* player = FindPlayerById(completion.player_id);
        if (!player || !player->IsConnected() || player->IsAuthenticated()) continue;
        if (IsNameBanned(completion.account_name))
        {
            QueueAuthResult(*player, EAuthResultCode::Failed, "Name is banned");
            continue;
        }

        if (!completion.result.success)
        {
            if (!completion.register_mode) RecordLoginFailure(*player, completion.account_name);
            QueueAuthResult(*player, EAuthResultCode::Failed,
                            completion.result.error.empty() ? "Auth failed" : completion.result.error);
            continue;
        }

        std::string commit_error;
        std::string trusted_ip_to_commit;
        if (completion.register_mode)
        {
            if (!CAccountDataStore::CommitRegistration(completion.result, &commit_error))
            {
                QueueAuthResult(*player, EAuthResultCode::Failed,
                                commit_error.empty() ? "Auth failed" : commit_error);
                continue;
            }
        } else
        {
            if (!CAccountDataStore::CommitLogin(completion.result, &commit_error))
            {
                RecordLoginFailure(*player, completion.account_name);
                QueueAuthResult(*player, EAuthResultCode::Failed,
                                commit_error.empty() ? "Auth failed" : commit_error);
                continue;
            }
            ClearLoginFailures(*player, completion.account_name);
            if (completion.result.password_verified) trusted_ip_to_commit = completion.result.client_ip;
        }

        if (game_config::IsEmailBindingRequired() && completion.result.email_verification_required)
        {
            ClearPendingEmailAuthentication(player->GetId());
            const auto expires_at = std::chrono::steady_clock::now() +
                                    std::chrono::seconds(
                                        std::max(1, game_config::email_verification_ttl_seconds));
            m_pending_verified_logins.insert_or_assign(
                player->GetId(), SPendingVerifiedLogin{ completion.account_name, trusted_ip_to_commit,
                                                        completion.register_mode, expires_at });
            m_auth_deadlines.insert_or_assign(
                player->GetId(), expires_at + std::chrono::seconds(auth_verification_grace_seconds));
            QueueAuthResult(*player, EAuthResultCode::EmailBindingRequired,
                            completion.result.email.empty() ? "Bind and verify an email address"
                                                            : "Verify the bound email address");
            continue;
        }
        if (!commit_error.empty()) LOG_WARN("account", commit_error);

        ClearPendingEmailAuthentication(player->GetId());
        EPlayerBufferResult result = ApplyAuthentication(
            *player, completion.account_name, completion.register_mode, EAuthResultCode::Authenticated, {},
            trusted_ip_to_commit);
        if (result == EPlayerBufferResult::Continue) continue;

        for (size_t i = 0; i < m_players.size(); ++i)
        {
            if (!m_players[i] || m_players[i].get() != player) continue;
            DropPlayer(i, result == EPlayerBufferResult::RemovePendingPlayer ? "reconnected through account"
                                                                            : "disconnected after authentication");
            break;
        }
    }
}

void INetworkModule::DispatchAuthenticationRequests()
{
    for (auto request = m_pending_auth_requests.begin(); request != m_pending_auth_requests.end();)
    {
        auto pending = m_player_auth_requests.find(request->player_id);
        CPlayer* player = FindPlayerById(request->player_id);
        if (pending == m_player_auth_requests.end() || pending->second != request->request_id || !player ||
            !player->IsConnected() || player->IsAuthenticated())
        {
            if (pending != m_player_auth_requests.end() && pending->second == request->request_id)
                m_player_auth_requests.erase(pending);
            request = m_pending_auth_requests.erase(request);
            continue;
        }

        if (m_active_auth_accounts.contains(request->account_name))
        {
            ++request;
            continue;
        }
        if (IsNameBanned(request->account_name))
        {
            QueueAuthResult(*player, EAuthResultCode::Failed, "Name is banned");
            m_player_auth_requests.erase(pending);
            request = m_pending_auth_requests.erase(request);
            continue;
        }

        SAccountAuthWork work;
        std::string error;
        if (!CAccountDataStore::PrepareAuthentication(request->account_name, request->password, request->email,
                                                       player->GetRemoteAddress(), request->register_mode, work,
                                                       &error))
        {
            if (!request->register_mode) RecordLoginFailure(*player, request->account_name);
            QueueAuthResult(*player, EAuthResultCode::Failed, error.empty() ? "Auth failed" : error);
            m_player_auth_requests.erase(pending);
            request = m_pending_auth_requests.erase(request);
            continue;
        }

        const std::string account_name = request->account_name;
        m_active_auth_accounts.insert(account_name);
        CAccountAuthWorker::SJob job;
        job.request_id = request->request_id;
        job.player_id = request->player_id;
        job.work = std::move(work);
        if (!m_auth_worker || !m_auth_worker->Submit(std::move(job)))
        {
            m_active_auth_accounts.erase(account_name);
            QueueAuthResult(*player, EAuthResultCode::Failed, "Authentication service is unavailable");
            m_player_auth_requests.erase(pending);
        }
        request = m_pending_auth_requests.erase(request);
    }
}

void INetworkModule::CancelAuthenticationRequest(std::uint32_t player_id)
{
    auto pending = m_player_auth_requests.find(player_id);
    if (pending == m_player_auth_requests.end()) return;

    const std::uint64_t request_id = pending->second;
    m_player_auth_requests.erase(pending);
    m_pending_auth_requests.erase(
        std::remove_if(m_pending_auth_requests.begin(), m_pending_auth_requests.end(),
                       [request_id](const SPendingAuthRequest& request) { return request.request_id == request_id; }),
        m_pending_auth_requests.end());
}

bool INetworkModule::IsLoginRateLimited(const CPlayer& player, const std::string& account_name)
{
    const auto now = std::chrono::steady_clock::now();
    const auto window = std::chrono::seconds(auth_failure_window_seconds);
    for (auto entry = m_auth_failures.begin(); entry != m_auth_failures.end();)
    {
        if (now >= entry->second.blocked_until && now - entry->second.last_failure >= window)
            entry = m_auth_failures.erase(entry);
        else
            ++entry;
    }

    std::string key = player.GetRemoteAddress();
    key.push_back('\n');
    key += account_name;
    const auto found = m_auth_failures.find(key);
    if (found != m_auth_failures.end()) return now < found->second.blocked_until;
    return m_auth_failures.size() >= auth_failure_state_limit;
}

void INetworkModule::RecordLoginFailure(const CPlayer& player, const std::string& account_name)
{
    std::string key = player.GetRemoteAddress();
    key.push_back('\n');
    key += account_name;

    const auto now = std::chrono::steady_clock::now();
    SAuthFailureState& state = m_auth_failures[key];
    if (state.failures == 0 || now - state.last_failure >= std::chrono::seconds(auth_failure_window_seconds))
        state = {};
    state.last_failure = now;
    ++state.failures;
    if (state.failures < auth_failure_limit) return;

    const int exponent = std::clamp(state.failures - auth_failure_limit, 0, 3);
    state.blocked_until = now + std::chrono::seconds(auth_failure_lock_seconds * (1 << exponent));
}

void INetworkModule::ClearLoginFailures(const CPlayer& player, const std::string& account_name)
{
    std::string key = player.GetRemoteAddress();
    key.push_back('\n');
    key += account_name;
    m_auth_failures.erase(key);
}

bool INetworkModule::FlushSendBuffer(CPlayer& player)
{
    CompactSentPackets(player);
    while (!player.m_send_buffer.empty())
    {
        size_t sent = 0;
        auto status = player.GetSocket().send(player.m_send_buffer.data() + player.m_send_offset,
                                              player.m_send_buffer.size() - player.m_send_offset, sent);
        if (sent > 0)
        {
            player.m_send_offset += sent;
            CompactSentPackets(player);
        }

        if (status == sf::Socket::Status::Done)
        {
            if (sent == 0 && !player.m_send_buffer.empty()) return true;
            continue;
        }
        if (status == sf::Socket::Status::Partial || status == sf::Socket::Status::NotReady) return true;
        if (status == sf::Socket::Status::Disconnected) return false;
        LOG_WARN("network", "Send failed for player " + std::to_string(player.GetId()));
        return false;
    }
    return true;
}

void INetworkModule::DropQueuedSnapshots(CPlayer& player)
{
    CompactSentPackets(player);
    if (player.m_send_buffer.empty()) return;

    size_t scan_offset = 0;
    if (player.m_send_offset > 0)
    {
        size_t first_len = PacketLengthAt(player.m_send_buffer, 0);
        size_t first_end = packet_length_prefix_size + first_len;
        if (first_end > player.m_send_buffer.size()) return;
        scan_offset = first_end;
    }

    std::vector<uint8_t> kept;
    kept.reserve(player.m_send_buffer.size());
    kept.insert(kept.end(), player.m_send_buffer.begin(), player.m_send_buffer.begin() + scan_offset);

    size_t dropped_packets = 0;
    size_t offset = scan_offset;
    while (offset + packet_length_prefix_size <= player.m_send_buffer.size())
    {
        size_t packet_len = PacketLengthAt(player.m_send_buffer, offset);
        size_t packet_payload = offset + packet_length_prefix_size;
        size_t packet_end = packet_payload + packet_len;
        if (packet_len == 0 || packet_end > player.m_send_buffer.size())
        {
            kept.insert(kept.end(), player.m_send_buffer.begin() + offset, player.m_send_buffer.end());
            offset = player.m_send_buffer.size();
            break;
        }

        uint8_t type = player.m_send_buffer[packet_payload];
        if (type == static_cast<uint8_t>(ServerMessage::Type::Snapshot))
        {
            dropped_packets++;
        } else
        {
            kept.insert(kept.end(), player.m_send_buffer.begin() + offset, player.m_send_buffer.begin() + packet_end);
        }
        offset = packet_end;
    }

    if (offset < player.m_send_buffer.size())
    {
        kept.insert(kept.end(), player.m_send_buffer.begin() + offset, player.m_send_buffer.end());
    }
    if (dropped_packets == 0) return;

    player.m_send_buffer.swap(kept);
}

bool INetworkModule::QueueMessage(CPlayer& player, const ServerMessage& msg, size_t packed_size)
{
    CompactSentPackets(player);
    const size_t len = packed_size > 0 ? packed_size : ServerMessage::GetPackedSize(msg);
    if (len == 0 || len > UINT16_MAX)
    {
        if (msg.type == ServerMessage::Type::Snapshot)
        {
            LOG_WARN("network", "Snapshot packet size is invalid: " + std::to_string(len) + " bytes");
        }
        return false;
    }
    if (PendingSendBytes(player) + len + packet_length_prefix_size > game_config::network_max_send_buffer_size)
    {
        LOG_WARN("network", "Player " + std::to_string(player.GetId()) + " output buffer overflow");
        return false;
    }

    thread_local std::vector<uint8_t> payload(UINT16_MAX);
    if (payload.size() < UINT16_MAX) payload.resize(UINT16_MAX);
    size_t packed_len = ServerMessage::pack(msg, payload.data());
    if (packed_len == 0 || packed_len > UINT16_MAX || packed_len != len) return false;
    if (PendingSendBytes(player) + packed_len + packet_length_prefix_size > game_config::network_max_send_buffer_size)
    {
        LOG_WARN("network", "Player " + std::to_string(player.GetId()) + " output buffer overflow");
        return false;
    }

    const size_t begin = player.m_send_buffer.size();
    uint16_t packet_len = static_cast<uint16_t>(packed_len);
    player.m_send_buffer.resize(begin + packet_length_prefix_size);
    player.m_send_buffer[begin] = static_cast<uint8_t>(packet_len & 0xff);
    player.m_send_buffer[begin + 1] = static_cast<uint8_t>((packet_len >> 8) & 0xff);
    player.m_send_buffer.insert(player.m_send_buffer.end(), payload.begin(), payload.begin() + packed_len);
    return true;
}

void INetworkModule::HandleSnapshotAck(CPlayer& player, const ClientSnapshotAck& ack)
{
    if (ack.snapshot_id == full_snapshot_base_id)
    {
        ResetSnapshotState(player);
        return;
    }

    auto state_it = m_client_snapshot_states.find(player.GetId());
    if (state_it == m_client_snapshot_states.end()) return;
    SClientSnapshotState& state = state_it->second;
    if (state.acked_snapshot_id && ack.snapshot_id < *state.acked_snapshot_id) return;

    auto history_it = std::find_if(state.history.begin(), state.history.end(), [&](const auto& entry) {
        return entry.snapshot_id == ack.snapshot_id;
    });
    if (history_it == state.history.end())
    {
        state.acked_snapshot_id.reset();
        return;
    }

    state.acked_snapshot_id = ack.snapshot_id;
    while (!state.history.empty() && state.history.front().snapshot_id < ack.snapshot_id)
        state.history.pop_front();
}

void INetworkModule::HandleInputFrame(CPlayer& player, const ClientInputFrame& frame)
{
    const std::uint64_t current_tick = m_server.GetTick();
    std::uint32_t delay_ticks = CPlayerController::MIN_INPUT_DELAY_TICKS;
    if (frame.target_server_tick > current_tick)
    {
        const std::uint64_t requested_delay = frame.target_server_tick - current_tick;
        delay_ticks = static_cast<std::uint32_t>(
            std::min<std::uint64_t>(requested_delay, CPlayerController::MAX_INPUT_DELAY_TICKS));
    }
    player.HandleScheduledOperate(frame.ToOperate(), delay_ticks, frame.sequence);
}

INetworkModule::SSnapshotHistoryEntry INetworkModule::PrepareSnapshotForSend(CPlayer& player, ServerMessage& msg,
                                                                              size_t full_size,
                                                                              size_t& actual_size)
{
    msg.base_snapshot_id = full_snapshot_base_id;
    msg.removed_entity_ids.clear();
    actual_size = full_size;

    SSnapshotHistoryEntry full_entry;
    full_entry.snapshot_id = msg.snapshot_id.value_or(0);
    full_entry.server_tick = msg.server_tick.value_or(0);
    full_entry.entities.reserve(msg.entities.size());
    for (const ServerEntitySnap& snap : msg.entities) full_entry.entities.insert_or_assign(snap.entity_id, snap);

    auto state_it = m_client_snapshot_states.find(player.GetId());
    if (state_it == m_client_snapshot_states.end() || !state_it->second.acked_snapshot_id) return full_entry;

    const std::uint32_t base_id = *state_it->second.acked_snapshot_id;
    auto base_it = std::find_if(state_it->second.history.begin(), state_it->second.history.end(),
                                [base_id](const auto& entry) { return entry.snapshot_id == base_id; });
    if (base_it == state_it->second.history.end())
    {
        state_it->second.acked_snapshot_id.reset();
        return full_entry;
    }

    ServerMessage delta;
    delta.type = ServerMessage::Type::Snapshot;
    delta.snapshot_id = msg.snapshot_id;
    delta.base_snapshot_id = base_id;
    delta.server_tick = msg.server_tick;
    delta.owner_entity_id = msg.owner_entity_id;
    delta.view_radius = msg.view_radius;
    std::copy_n(msg.weather_intensity, server_environment_weather_count, delta.weather_intensity);
    delta.wind_direction = msg.wind_direction;
    delta.wind_speed = msg.wind_speed;

    for (const ServerEntitySnap& snap : msg.entities)
    {
        auto old_it = base_it->entities.find(snap.entity_id);
        if (old_it == base_it->entities.end() || !SameServerEntitySnap(snap, old_it->second))
            delta.entities.push_back(snap);
    }

    for (const auto& [entity_id, old_snap] : base_it->entities)
    {
        (void)old_snap;
        if (full_entry.entities.find(entity_id) == full_entry.entities.end())
            delta.removed_entity_ids.push_back(entity_id);
    }
    std::sort(delta.removed_entity_ids.begin(), delta.removed_entity_ids.end());

    const size_t delta_size = ServerMessage::GetPackedSize(delta);
    if (delta_size < full_size)
    {
        msg = std::move(delta);
        actual_size = delta_size;
    }
    return full_entry;
}

void INetworkModule::RecordSentSnapshot(CPlayer& player, SSnapshotHistoryEntry entry)
{
    SClientSnapshotState& state = m_client_snapshot_states[player.GetId()];
    state.history.push_back(std::move(entry));
    while (state.history.size() > snapshot_history_limit) state.history.pop_front();
    if (state.acked_snapshot_id &&
        std::none_of(state.history.begin(), state.history.end(), [&](const auto& history_entry) {
            return history_entry.snapshot_id == *state.acked_snapshot_id;
        }))
        state.acked_snapshot_id.reset();
}

void INetworkModule::RecordSnapshotBuildTime(double elapsed_ms)
{
    SSnapshotTelemetry& telemetry = m_snapshot_telemetry;
    telemetry.build_count++;
    telemetry.last_build_ms = elapsed_ms;
    m_snapshot_build_total_ms += elapsed_ms;
    telemetry.average_build_ms = m_snapshot_build_total_ms / static_cast<double>(telemetry.build_count);
    telemetry.max_build_ms = std::max(telemetry.max_build_ms, elapsed_ms);
}

void INetworkModule::RecordSnapshotBatchTime(double elapsed_ms)
{
    SSnapshotTelemetry& telemetry = m_snapshot_telemetry;
    telemetry.batch_count++;
    telemetry.last_batch_ms = elapsed_ms;
    m_snapshot_batch_total_ms += elapsed_ms;
    telemetry.average_batch_ms = m_snapshot_batch_total_ms / static_cast<double>(telemetry.batch_count);
    telemetry.max_batch_ms = std::max(telemetry.max_batch_ms, elapsed_ms);
}

void INetworkModule::RecordQueuedSnapshot(const ServerMessage& msg, size_t full_size, size_t actual_size)
{
    SSnapshotTelemetry& telemetry = m_snapshot_telemetry;
    if (msg.base_snapshot_id.value_or(full_snapshot_base_id) == full_snapshot_base_id)
        telemetry.full_snapshots_queued++;
    else
        telemetry.delta_snapshots_queued++;
    telemetry.full_snapshot_bytes += full_size;
    telemetry.actual_snapshot_bytes += actual_size;
    telemetry.changed_entities += msg.entities.size();
    telemetry.removed_entities += msg.removed_entity_ids.size();
}

void INetworkModule::ResetSnapshotState(CPlayer& player) { m_client_snapshot_states.erase(player.GetId()); }

bool INetworkModule::QueueAuthResult(CPlayer& player, EAuthResultCode code, const std::string& message)
{
    ServerMessage msg;
    msg.type = ServerMessage::Type::AuthResult;
    msg.auth_result_code = code;
    msg.auth_message = message;
    msg.email_policy = static_cast<EEmailPolicy>(game_config::GetEmailPolicy());
    msg.email_bound = false;
    if (player.IsAuthenticated())
    {
        SAccountEmailState email_state;
        if (CAccountDataStore::GetEmailState(player.GetAccountName(), email_state))
            msg.email_bound = email_state.verified;
    }
    return QueueMessage(player, msg);
}

bool INetworkModule::QueueWelcome(CPlayer& player)
{
    ResetSnapshotState(player);
    ServerMessage msg;
    msg.type = ServerMessage::Type::Welcome;
    msg.player_id = static_cast<uint16_t>(player.GetId());
    msg.tick_rate = static_cast<uint8_t>(std::round(1.f / game_config::server_fixed_dt));

    if (CEntity* entity = player.GetEntity())
    {
        msg.owner_entity_id = static_cast<net_entity_id>(entity->Id());
        if (CGameWorld* world = entity->GameWorld()) msg.map_name = world->GetMapPath();
    } else
    {
        msg.owner_entity_id = 0;
        msg.map_name = m_lobby_world.GetMapPath();
    }
    msg.weather_enabled = map_declarations::DeclaresWeatherDiurnal(msg.map_name) ? 1 : 0;

    if (!QueueMessage(player, msg))
    {
        LOG_WARN("network", "QueueWelcome: Welcome message queue failed for player " + std::to_string(player.GetId()));
        return false;
    }
    const bool environment_queued = QueueEnvironment(player);
    if (!environment_queued)
    {
        LOG_WARN("network", "QueueWelcome: Environment queue failed for player " + std::to_string(player.GetId()));
        return false;
    }
    QueueCodexFull(player);
    QueueCodexLoot(player);
    report::SyncSquadMetadata(player);
    return true;
}

bool INetworkModule::QueueEnvironment(CPlayer& player)
{
    const CWeatherDiurnalSystem* system = m_server.GetWeatherDiurnalSystem();
    if (!system) return false;

    const SWeatherDiurnalClientState environment = system->GetClientState();
    ServerMessage msg;
    msg.type = ServerMessage::Type::Environment;
    msg.environment_anchor_tick = m_server.GetTick();
    msg.time_of_day = static_cast<std::uint16_t>(
        std::llround(std::clamp(environment.time_of_day, 0.0, 1.0) * static_cast<double>(UINT16_MAX)));
    msg.day_duration_seconds = static_cast<float>(system->DayDurationSeconds());
    msg.fixed_dt_seconds = m_server.GetFixedDtSeconds();
    for (std::size_t index = 0; index < server_environment_weather_count; ++index)
    {
        const float intensity =
            index < static_cast<std::size_t>(EWeatherType::Count) ? environment.weather_intensity[index] : 0.f;
        msg.weather_intensity[index] =
            static_cast<std::uint8_t>(std::llround(std::clamp(intensity, 0.f, 1.f) * 255.f));
    }
    const SWeatherDiurnalSnapshot snapshot = system->GetWeatherDiurnalSnapshot();
    msg.wind_direction = static_cast<std::uint8_t>(std::llround(
        std::clamp(snapshot.meteorology.wind_direction_degrees / 360.f, 0.f, 1.f) * 255.f));
    msg.wind_speed = static_cast<std::uint8_t>(
        std::llround(std::clamp(snapshot.meteorology.wind_speed / 20.f, 0.f, 1.f) * 255.f));
    return QueueMessage(player, msg);
}

void INetworkModule::QueueCodexFull(CPlayer& player)
{
    // Full codex progress: every mob type x rarity combination, including
    // zero-count entries, so the client renders the complete bestiary grid.
    ServerMessage msg;
    msg.type = ServerMessage::Type::CodexFull;
    const auto& codex = player.GetCodex();
    msg.codex_entries.reserve(
        (mob_type_names.size() - 1) * (static_cast<int>(ERarity::Primordial) - static_cast<int>(ERarity::Common) + 1));
    for (std::size_t mob = 1; mob < mob_type_names.size(); ++mob)
    {
        for (int rarity = static_cast<int>(ERarity::Common); rarity <= static_cast<int>(ERarity::Primordial); ++rarity)
        {
            const uint32_t key = (static_cast<uint32_t>(mob) << 8) | static_cast<uint32_t>(rarity);
            uint32_t count = 0;
            const auto it = codex.find(key);
            if (it != codex.end()) count = it->second;
            msg.codex_entries.push_back(
                { static_cast<uint8_t>(mob), static_cast<uint8_t>(rarity), count });
        }
    }
    if (!QueueMessage(player, msg))
    {
        LOG_WARN("network", "QueueCodexFull failed (" + std::to_string(msg.codex_entries.size()) + " entries)");
        return;
    }
    LOG_INFO("network", "CodexFull queued for player " + std::to_string(player.GetId()) + " (" +
                            std::to_string(msg.codex_entries.size()) + " entries)");
}

void INetworkModule::QueueCodexLoot(CPlayer& player)
{
    ServerMessage msg;
    msg.type = ServerMessage::Type::CodexLoot;
    for (const auto& [key, drops] : GetDropRateTable())
    {
        for (const SDropRate& drop : drops)
        {
            msg.codex_loot_entries.push_back(
                { static_cast<uint8_t>(key.mob_type), static_cast<uint8_t>(key.rarity),
                  static_cast<uint8_t>(drop.type), static_cast<uint8_t>(drop.rarity),
                  static_cast<uint16_t>(std::clamp(drop.drop_rate, 0.0, 1.0) * 10000.0) });
        }
    }
    QueueMessage(player, msg);
}

void INetworkModule::SendCodexDelta(CPlayer& player, uint8_t mob_type, uint8_t rarity, uint32_t count)
{
    if (!player.IsConnected() || !player.IsAuthenticated()) return;
    ServerMessage msg;
    msg.type = ServerMessage::Type::CodexDelta;
    msg.codex_delta_entries.push_back({ mob_type, rarity, count });
    QueueMessage(player, msg);
}

void INetworkModule::BroadcastLightningChain(CGameWorld& world, const std::vector<florrbt::Vector2f>& points)
{
    ServerMessage msg;
    msg.type = ServerMessage::Type::LightningChain;
    msg.lightning_points = points;
    for (const auto& player : m_players)
    {
        if (!player || !player->IsConnected() || !player->IsAuthenticated()) continue;
        CEntity* entity = player->GetEntity();
        if (!entity || entity->GameWorld() != &world) continue;
        QueueMessage(*player, msg);
    }
}

void INetworkModule::SendTerrainDelta(CPlayer& player, uint8_t layer, uint16_t row, uint16_t col, uint32_t gid)
{
    ServerMessage msg;
    msg.type = ServerMessage::Type::TerrainDelta;
    msg.terrain_layer = layer;
    msg.terrain_row = row;
    msg.terrain_col = col;
    msg.terrain_gid = gid;
    QueueMessage(player, msg);
}

void INetworkModule::BroadcastTerrainDelta(CGameWorld& world, uint8_t layer, uint16_t row, uint16_t col, uint32_t gid)
{
    m_dev_debug->BroadcastTerrain(world, layer, row, col, gid);
    for (const auto& player : m_players)
    {
        if (!player || !player->IsConnected() || !player->IsAuthenticated()) continue;
        CEntity* entity = player->GetEntity();
        if (!entity || entity->GameWorld() != &world) continue;
        SendTerrainDelta(*player, layer, row, col, gid);
    }
}

void INetworkModule::SendTerrainWallRemove(CPlayer& player, florrbt::Vector2f min_pos, florrbt::Vector2f max_pos)
{
    ServerMessage msg;
    msg.type = ServerMessage::Type::TerrainWallRemove;
    msg.wall_remove_min = min_pos;
    msg.wall_remove_max = max_pos;
    QueueMessage(player, msg);
}

void INetworkModule::BroadcastTerrainWallRemove(CGameWorld& world, florrbt::Vector2f min_pos, florrbt::Vector2f max_pos)
{
    m_dev_debug->BroadcastWallRemoval(world, min_pos.x, min_pos.y, max_pos.x, max_pos.y);
    for (const auto& player : m_players)
    {
        if (!player || !player->IsConnected() || !player->IsAuthenticated()) continue;
        CEntity* entity = player->GetEntity();
        if (!entity || entity->GameWorld() != &world) continue;
        SendTerrainWallRemove(*player, min_pos, max_pos);
    }
}

void INetworkModule::BroadcastEnvironmentIfChanged()
{
    const CWeatherDiurnalSystem* system = m_server.GetWeatherDiurnalSystem();
    if (!system) return;
    // Time-of-day is extrapolated client-side from the anchor tick, and weather
    // rides along with every snapshot. The Environment message is therefore only
    // re-sent on connect (QueueEnvironment), when the day length changes, or when
    // the virtual time jumps (e.g. the `weather time` command), so clients do not
    // stay anchored to a stale time-of-day.
    const SWeatherDiurnalSnapshot snapshot = system->GetWeatherDiurnalSnapshot();
    const float day_duration = static_cast<float>(system->DayDurationSeconds());
    const bool day_changed = day_duration != m_last_environment_day_duration;
    const double raw_jump = std::abs(snapshot.time_of_day - m_last_environment_time_of_day);
    const double time_jump = std::min(raw_jump, 1.0 - raw_jump);
    // Natural advance is ~2e-6 per tick; a command jump is orders of magnitude larger.
    const bool time_jumped = time_jump > 0.002;
    if (!day_changed && !time_jumped) return;
    m_last_environment_day_duration = day_duration;
    m_last_environment_time_of_day = snapshot.time_of_day;

    // Queue only: the network thread owns socket sends, so flushing here would
    // race with it and disconnect players on send failure.
    for (const auto& player : m_players)
    {
        if (!player || !player->IsConnected() || !player->IsAuthenticated()) continue;
        QueueEnvironment(*player);
    }
}

bool INetworkModule::QueueOwnerState(CPlayer& player)
{
    ServerMessage msg;
    msg.type = ServerMessage::Type::OwnerState;
    msg.level = 1;
    msg.flags = 0;
    msg.exp_progress_bps = 0;
    msg.talent_points = static_cast<uint16_t>(std::clamp(player.GetTalentPoints(), 0, static_cast<int>(UINT16_MAX)));
    for (const ITalent* talent : player.GetTalents())
    {
        if (!talent) continue;
        msg.talents.push_back({ talent->m_id, static_cast<uint8_t>(talent->m_rarity),
                                static_cast<uint8_t>(std::clamp(
                                    talent->m_rank, 0, static_cast<int>(std::numeric_limits<uint8_t>::max()))) });
    }

    auto* player_flower = dynamic_cast<CPlayerFlower*>(player.GetEntity());
    if (player_flower)
    {
        msg.level = static_cast<uint8_t>(
            std::clamp(player_flower->m_level, 0, static_cast<int>(std::numeric_limits<uint8_t>::max())));
        msg.exp_progress_bps = EncodeExpProgressBps(*player_flower);
    }

    auto* flower = dynamic_cast<CFlower*>(player.GetEntity());
    if (flower)
    {
        msg.owner_slots = CSnapshotService::BuildPrimarySlots(*flower);
        const IGameController* controller = flower->GameWorld() ? flower->GameWorld()->GetController() : nullptr;
        if (controller && controller->LocksPetalLoadout()) msg.flags = owner_state_loadout_locked;
    }

    // Suppress the account's saved side slots only on the wire; never erase
    // them or return temporary controller cards to persistent inventory.
    const bool locked = (msg.flags.value_or(0) & owner_state_loadout_locked) != 0;
    std::vector<SInventoryItem> secondary_slots = locked ? std::vector<SInventoryItem>{}
        : CAccountDataStore::GetSecondarySlots(player.GetAccountName());
    if (locked) msg.secondary_slots.resize(msg.owner_slots.size());
    msg.secondary_slots.reserve(secondary_slots.size());
    for (const SInventoryItem& item : secondary_slots)
    {
        SOwnerPetalSlot slot;
        if (item.petal_type != 0 && item.rarity != 0)
        {
            slot.petal_type = item.petal_type;
            slot.rarity = item.rarity;
        }
        msg.secondary_slots.push_back(slot);
    }

    msg.petal_slots = static_cast<uint8_t>(std::min<size_t>(msg.owner_slots.size(), UINT8_MAX));
    msg.secondary_slots_count = static_cast<uint8_t>(std::min<size_t>(msg.secondary_slots.size(), UINT8_MAX));
    return QueueMessage(player, msg);
}

bool INetworkModule::QueueOwnerStateUpdate(CPlayer& player) { return QueueOwnerState(player); }

bool INetworkModule::QueueInventoryUpdate(CPlayer& player) { return QueueInventory(player); }

bool INetworkModule::QueueInventory(CPlayer& player)
{
    ServerMessage msg;
    msg.type = ServerMessage::Type::Inventory;
    msg.inventory = CAccountDataStore::GetInventory(player.GetAccountName());
    return QueueMessage(player, msg);
}

bool INetworkModule::QueueCraftResult(CPlayer& player, const SCraftResult& result)
{
    ServerMessage msg;
    msg.type = ServerMessage::Type::CraftResult;
    msg.craft_success = result.successes > 0;
    msg.craft_petal_type = result.petal_type;
    msg.craft_rarity = result.rarity;
    msg.craft_consumed = result.consumed;
    msg.craft_items = result.items;
    return QueueMessage(player, msg);
}

bool INetworkModule::QueueChat(CPlayer& player, const CServer::SChatEntry& chat)
{
    ServerMessage msg;
    msg.type = ServerMessage::Type::Chat;
    msg.chat.flag = chat.flag;
    msg.chat.player_id = static_cast<uint16_t>(std::min<uint32_t>(chat.player_id, UINT16_MAX));
    msg.chat.time = chat.system_time;
    msg.chat.player_name = chat.player_name;
    msg.chat.message = chat.message;
    return QueueMessage(player, msg);
}

bool INetworkModule::SendChatToPlayer(CPlayer& player, const CServer::SChatEntry& chat)
{
    if (!player.IsConnected() || !player.IsAuthenticated()) return false;
    return QueueChat(player, chat);
}

void INetworkModule::BroadcastChat(const CServer::SChatEntry& chat)
{
    for (auto& player : m_players)
    {
        if (!player || !player->IsConnected() || !player->IsAuthenticated()) continue;

        if (chat.flag == EChatFlag::Server)
        {
            if (chat.target_player_id >= 0 && static_cast<int>(player->GetId()) != chat.target_player_id) continue;
            if (chat.world)
            {
                CEntity* entity = player->GetEntity();
                if (!entity || entity->IsMarkedForDes() || entity->GameWorld() != chat.world) continue;
            }
            QueueChat(*player, chat);
            continue;
        }

        if (chat.flag == EChatFlag::Whisper)
        {
            if (static_cast<int>(player->GetId()) == chat.target_player_id || player->GetId() == chat.player_id)
                QueueChat(*player, chat);
            continue;
        }

        // Squad recipients are selected explicitly by the squad controller.
        if (chat.flag == EChatFlag::Squad) continue;

        if (chat.flag == EChatFlag::Global)
        {
            QueueChat(*player, chat);
            continue;
        }

        CEntity* entity = player->GetEntity();
        if (!entity || entity->IsMarkedForDes() || entity->GameWorld() != chat.world) continue;

        auto* mob = dynamic_cast<CMobBase*>(entity);
        const SMobStats* stats = mob ? mob->GetFinalStats() : nullptr;
        float range = (stats ? stats->horizon : game_config::default_horizon) * 2.f;
        if (DistanceSq(entity->Pos(), chat.pos) <= range * range) QueueChat(*player, chat);
    }
}

INetworkModule::EProxyHeaderResult INetworkModule::ResolveTrustedProxyHeader(CPlayer& player)
{
    const auto pending = m_proxy_header_pending.find(player.GetId());
    if (pending == m_proxy_header_pending.end()) return EProxyHeaderResult::Resolved;

    auto& buffer = player.m_receive_buffer;
    const size_t compared = std::min(buffer.size(), proxy_v1_prefix.size());
    if (!std::equal(buffer.begin(), buffer.begin() + compared, proxy_v1_prefix.begin()))
    {
        m_proxy_header_pending.erase(pending);
        return EProxyHeaderResult::Resolved;
    }
    if (buffer.size() < proxy_v1_prefix.size()) return EProxyHeaderResult::NeedMoreData;

    static constexpr std::array<std::uint8_t, 2> terminator{ '\r', '\n' };
    const auto line_end = std::search(buffer.begin() + proxy_v1_prefix.size(), buffer.end(), terminator.begin(),
                                      terminator.end());
    if (line_end == buffer.end())
    {
        if (buffer.size() <= proxy_v1_max_header_size) return EProxyHeaderResult::NeedMoreData;
        m_proxy_header_pending.erase(pending);
        return EProxyHeaderResult::Rejected;
    }

    const size_t header_size = static_cast<size_t>(std::distance(buffer.begin(), line_end)) + terminator.size();
    if (header_size > proxy_v1_max_header_size)
    {
        m_proxy_header_pending.erase(pending);
        return EProxyHeaderResult::Rejected;
    }

    const std::string_view line(reinterpret_cast<const char*>(buffer.data()), header_size - terminator.size());
    std::string source_address;
    m_proxy_header_pending.erase(pending);
    if (!ParseProxyV1Header(line, source_address)) return EProxyHeaderResult::Rejected;
    if (IsIpBanned(source_address))
    {
        LOG_INFO("network", "Rejected banned proxied IP: " + source_address);
        return EProxyHeaderResult::Rejected;
    }

    player.SetRemoteAddress(source_address);
    buffer.erase(buffer.begin(), buffer.begin() + header_size);
    return EProxyHeaderResult::Resolved;
}

INetworkModule::EPlayerBufferResult INetworkModule::ProcessPlayerBuffer(CPlayer& player)
{
    const EProxyHeaderResult proxy_result = ResolveTrustedProxyHeader(player);
    if (proxy_result == EProxyHeaderResult::NeedMoreData) return EPlayerBufferResult::Continue;
    if (proxy_result == EProxyHeaderResult::Rejected) return EPlayerBufferResult::RequestedDisconnect;

    CAccountDataStore::CSaveBatch account_save_batch;
    bool queue_slot_state = false;
    auto flush_slot_state = [&]() -> EPlayerBufferResult {
        if (!queue_slot_state) return EPlayerBufferResult::Continue;
        QueueInventory(player);
        QueueOwnerState(player);
        queue_slot_state = false;
        if (!FlushSendBuffer(player)) return EPlayerBufferResult::RequestedDisconnect;
        return EPlayerBufferResult::Continue;
    };

    while (player.m_receive_buffer.size() >= 1)
    {
        // Version gate: the first packet from a client must be the wire version
        // triple. Old clients (or anything else) are rejected deterministically
        // before any other packet is parsed, so protocol changes can never be
        // misparsed by a stale client.
        if (!player.IsAuthenticated() && !player.IsVersionVerified())
        {
            if (player.m_receive_buffer[0] == client_version_packet_type)
            {
                if (player.m_receive_buffer.size() < client_version_packet_size) return flush_slot_state();

                bool version_ok = false;
                ClientVersionPacket version_packet =
                    ClientVersionPacket::parse(player.m_receive_buffer.data(), client_version_packet_size, &version_ok);
                player.m_receive_buffer.erase(player.m_receive_buffer.begin(),
                                              player.m_receive_buffer.begin() + client_version_packet_size);
                if (version_ok && version_packet.MatchesServer())
                {
                    player.SetVersionVerified(true);
                    continue;
                }
                LOG_WARN("network", "Player " + std::to_string(player.GetId()) +
                                        " rejected: client version mismatch (" +
                                        std::to_string(version_packet.major) + "." + std::to_string(version_packet.minor) +
                                        "." + std::to_string(version_packet.patch) + ")");
                QueueAuthResult(player, EAuthResultCode::Failed,
                                "Outdated client version; refresh the page to reconnect");
                FlushSendBuffer(player);
                return EPlayerBufferResult::RequestedDisconnect;
            }
            LOG_WARN("network", "Player " + std::to_string(player.GetId()) +
                                    " rejected: first packet is not the version handshake");
            QueueAuthResult(player, EAuthResultCode::Failed,
                            "Outdated client version; refresh the page to reconnect");
            FlushSendBuffer(player);
            return EPlayerBufferResult::RequestedDisconnect;
        }

        if (ClientAuthRequest::IsPacketStart(player.m_receive_buffer[0]))
        {
            size_t packet_size =
                ClientAuthRequest::GetPacketSize(player.m_receive_buffer.data(), player.m_receive_buffer.size());
            if (packet_size == 0 || player.m_receive_buffer.size() < packet_size) return flush_slot_state();

            bool ok = false;
            ClientAuthRequest request = ClientAuthRequest::parse(player.m_receive_buffer.data(), packet_size, &ok);
            player.m_receive_buffer.erase(player.m_receive_buffer.begin(),
                                          player.m_receive_buffer.begin() + packet_size);
            if (!ok)
            {
                LOG_WARN("network", "Player " + std::to_string(player.GetId()) + " sent a malformed auth packet");
                return EPlayerBufferResult::RequestedDisconnect;
            }

            EPlayerBufferResult result = HandleAuthRequest(player, request);
            if (result != EPlayerBufferResult::Continue) return result;
            continue;
        }

        if (ClientChatRequest::IsPacketStart(player.m_receive_buffer[0]))
        {
            size_t packet_size =
                ClientChatRequest::GetPacketSize(player.m_receive_buffer.data(), player.m_receive_buffer.size());
            if (packet_size == 0 || player.m_receive_buffer.size() < packet_size) return flush_slot_state();

            bool ok = false;
            ClientChatRequest request = ClientChatRequest::parse(player.m_receive_buffer.data(), packet_size, &ok);
            player.m_receive_buffer.erase(player.m_receive_buffer.begin(),
                                          player.m_receive_buffer.begin() + packet_size);
            if (!player.IsAuthenticated())
            {
                QueueAuthResult(player, EAuthResultCode::Failed, "Please login first");
                return EPlayerBufferResult::RequestedDisconnect;
            }
            if (ok)
            {
                HandleChatRequest(player, request);
            } else
            {
                LOG_WARN("network", "Player " + std::to_string(player.GetId()) + " sent a malformed chat packet");
                return EPlayerBufferResult::RequestedDisconnect;
            }
            continue;
        }

        if (ClientSecondarySlotRequest::IsPacketStart(player.m_receive_buffer[0]))
        {
            size_t packet_size = ClientSecondarySlotRequest::GetPacketSize(player.m_receive_buffer.data(),
                                                                           player.m_receive_buffer.size());
            if (packet_size == 0 || player.m_receive_buffer.size() < packet_size) return flush_slot_state();

            bool ok = false;
            ClientSecondarySlotRequest request =
                ClientSecondarySlotRequest::parse(player.m_receive_buffer.data(), packet_size, &ok);
            player.m_receive_buffer.erase(player.m_receive_buffer.begin(),
                                          player.m_receive_buffer.begin() + packet_size);
            if (!player.IsAuthenticated())
            {
                QueueAuthResult(player, EAuthResultCode::Failed, "Please login first");
                return EPlayerBufferResult::RequestedDisconnect;
            }
            if (ok)
            {
                HandleSecondarySlotRequest(player, request);
                queue_slot_state = true;
            } else
            {
                LOG_WARN("network", "Player " + std::to_string(player.GetId()) + " sent a malformed slot packet");
                return EPlayerBufferResult::RequestedDisconnect;
            }
            continue;
        }

        if (ClientCraftRequest::IsPacketStart(player.m_receive_buffer[0]))
        {
            size_t packet_size =
                ClientCraftRequest::GetPacketSize(player.m_receive_buffer.data(), player.m_receive_buffer.size());
            if (packet_size == 0 || player.m_receive_buffer.size() < packet_size) return flush_slot_state();

            bool ok = false;
            ClientCraftRequest request = ClientCraftRequest::parse(player.m_receive_buffer.data(), packet_size, &ok);
            player.m_receive_buffer.erase(player.m_receive_buffer.begin(),
                                          player.m_receive_buffer.begin() + packet_size);
            if (!player.IsAuthenticated())
            {
                QueueAuthResult(player, EAuthResultCode::Failed, "Please login first");
                return EPlayerBufferResult::RequestedDisconnect;
            }
            if (ok)
            {
                HandleCraftRequest(player, request);
            } else
            {
                LOG_WARN("network", "Player " + std::to_string(player.GetId()) + " sent a malformed craft packet");
                return EPlayerBufferResult::RequestedDisconnect;
            }
            continue;
        }

        if (ClientForgeRequest::IsPacketStart(player.m_receive_buffer[0]))
        {
            size_t packet_size =
                ClientForgeRequest::GetPacketSize(player.m_receive_buffer.data(), player.m_receive_buffer.size());
            if (packet_size == 0 || player.m_receive_buffer.size() < packet_size) return flush_slot_state();

            bool ok = false;
            ClientForgeRequest request = ClientForgeRequest::parse(player.m_receive_buffer.data(), packet_size, &ok);
            player.m_receive_buffer.erase(player.m_receive_buffer.begin(),
                                          player.m_receive_buffer.begin() + packet_size);
            if (!player.IsAuthenticated())
            {
                QueueAuthResult(player, EAuthResultCode::Failed, "Please login first");
                return EPlayerBufferResult::RequestedDisconnect;
            }
            if (ok)
            {
                HandleForgeRequest(player, request);
            } else
            {
                LOG_WARN("network", "Player " + std::to_string(player.GetId()) + " sent a malformed forge packet");
                return EPlayerBufferResult::RequestedDisconnect;
            }
            continue;
        }

        if (ClientTalentRequest::IsPacketStart(player.m_receive_buffer[0]))
        {
            size_t packet_size =
                ClientTalentRequest::GetPacketSize(player.m_receive_buffer.data(), player.m_receive_buffer.size());
            if (packet_size == 0 || player.m_receive_buffer.size() < packet_size) return flush_slot_state();

            bool ok = false;
            ClientTalentRequest request = ClientTalentRequest::parse(player.m_receive_buffer.data(), packet_size, &ok);
            player.m_receive_buffer.erase(player.m_receive_buffer.begin(),
                                          player.m_receive_buffer.begin() + packet_size);
            if (!player.IsAuthenticated())
            {
                QueueAuthResult(player, EAuthResultCode::Failed, "Please login first");
                return EPlayerBufferResult::RequestedDisconnect;
            }
            if (ok)
            {
                HandleTalentRequest(player, request);
            } else
            {
                LOG_WARN("network", "Player " + std::to_string(player.GetId()) + " sent a malformed talent packet");
                return EPlayerBufferResult::RequestedDisconnect;
            }
            continue;
        }

        if (ClientInputFrame::IsPacketStart(player.m_receive_buffer[0]))
        {
            size_t packet_size =
                ClientInputFrame::GetPacketSize(player.m_receive_buffer.data(), player.m_receive_buffer.size());
            if (packet_size == 0 || player.m_receive_buffer.size() < packet_size) return flush_slot_state();

            bool ok = false;
            ClientInputFrame frame = ClientInputFrame::parse(player.m_receive_buffer.data(), packet_size, &ok);
            player.m_receive_buffer.erase(player.m_receive_buffer.begin(),
                                          player.m_receive_buffer.begin() + packet_size);
            if (!player.IsAuthenticated())
            {
                QueueAuthResult(player, EAuthResultCode::Failed, "Please login first");
                return EPlayerBufferResult::RequestedDisconnect;
            }
            if (ok)
            {
                HandleInputFrame(player, frame);
            } else
            {
                LOG_WARN("network", "Player " + std::to_string(player.GetId()) + " sent a malformed input frame");
                return EPlayerBufferResult::RequestedDisconnect;
            }
            continue;
        }

        if (ClientSnapshotAck::IsPacketStart(player.m_receive_buffer[0]))
        {
            size_t packet_size =
                ClientSnapshotAck::GetPacketSize(player.m_receive_buffer.data(), player.m_receive_buffer.size());
            if (packet_size == 0 || player.m_receive_buffer.size() < packet_size) return flush_slot_state();

            bool ok = false;
            ClientSnapshotAck ack = ClientSnapshotAck::parse(player.m_receive_buffer.data(), packet_size, &ok);
            player.m_receive_buffer.erase(player.m_receive_buffer.begin(),
                                          player.m_receive_buffer.begin() + packet_size);
            if (!player.IsAuthenticated())
            {
                QueueAuthResult(player, EAuthResultCode::Failed, "Please login first");
                return EPlayerBufferResult::RequestedDisconnect;
            }
            if (ok)
            {
                HandleSnapshotAck(player, ack);
            } else
            {
                LOG_WARN("network", "Player " + std::to_string(player.GetId()) + " sent a malformed snapshot ack");
                return EPlayerBufferResult::RequestedDisconnect;
            }
            continue;
        }

        if (player.m_receive_buffer[0] == client_state_request_packet_type)
        {
            player.m_receive_buffer.erase(player.m_receive_buffer.begin());
            if (!player.IsAuthenticated())
            {
                QueueAuthResult(player, EAuthResultCode::Failed, "Please login first");
                return EPlayerBufferResult::RequestedDisconnect;
            }
            QueueInventory(player);
            QueueOwnerState(player);
            queue_slot_state = false;
            if (!FlushSendBuffer(player)) return EPlayerBufferResult::RequestedDisconnect;
            continue;
        }

        if (!player.IsAuthenticated())
        {
            QueueAuthResult(player, EAuthResultCode::Failed, "Please login first");
            return EPlayerBufferResult::RequestedDisconnect;
        }

        uint8_t type = player.m_receive_buffer[0] & client_type_mask;
        size_t packet_size = (type == static_cast<uint8_t>(ClientOperate::Type::Input) ||
                              type == static_cast<uint8_t>(ClientOperate::Type::Equip))
                                 ? client_extended_packet_size
                                 : client_compact_packet_size;
        if (player.m_receive_buffer.size() < packet_size) return flush_slot_state();

        ClientOperate op = ClientOperate::parse(player.m_receive_buffer.data(), packet_size);
        if (op.type != ClientOperate::Type::Unknown)
        {
            bool op_queues_slot_state = IsSlotOperate(op);
            if (op.disconnect.value_or(false)) return EPlayerBufferResult::RequestedDisconnect;
            else if (op.agree.value_or(false))
            {
                auto* flower = dynamic_cast<CPlayerFlower*>(player.GetEntity());
                if (!player.GetEntity() || (flower && flower->m_is_dead))
                {
                    CGameWorld* respawn_world = player.GetEntity() ? player.GetEntity()->GameWorld() : nullptr;
                    if (!respawn_world) respawn_world = &m_lobby_world;
                    if (m_player_lifecycle_service.SpawnPlayer(player, *respawn_world,
                                                               EPlayerSpawnReason::Respawn))
                        m_player_lifecycle_service.NotifyPlayerWorldChanged(player, *this);
                } else player.HandleOperate(op);
            } else
            {
                auto* flower = dynamic_cast<CPlayerFlower*>(player.GetEntity());
                if (!flower || !flower->m_is_dead) player.HandleOperate(op);
            }
            queue_slot_state = queue_slot_state || op_queues_slot_state;
        }
        player.m_receive_buffer.erase(player.m_receive_buffer.begin(), player.m_receive_buffer.begin() + packet_size);
    }
    return flush_slot_state();
}

void INetworkModule::HandleChatRequest(CPlayer& player, const ClientChatRequest& request)
{
    if (Trim(request.message) == "/dev_debug")
    {
        if (!ValidateRconAuthorization(player))
            SendPrivateServerMessage(m_server, *this, player, "Use /rcon <password> before dev_debug.");
        else if (!QueueDebugTicket(player))
            SendPrivateServerMessage(m_server, *this, player, "Unable to issue a debug ticket; close another observer and retry.");
        return;
    }
    if (HandleRconCommand(m_server, *this, player, request.message)) return;

    CEntity* entity = player.GetEntity();
    if (!entity || entity->IsMarkedForDes()) return;

    if (!request.message.empty() && request.message.front() == '/')
    {
        report::HandleServerCommand(player, request.message);
        return;
    }

    if (player.IsMuted())
    {
        CServer::SChatEntry entry;
        entry.flag = EChatFlag::Server;
        entry.target_player_id = static_cast<int>(player.GetId());
        entry.player_name = "Server";
        entry.message =
            "You are muted for " + std::to_string(static_cast<int>(std::ceil(player.GetMuteTimer()))) + "s.";
        entry.system_time = static_cast<uint32_t>(
            std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch())
                .count());
        SendChatToPlayer(player, entry);
        return;
    }

    EChatFlag flag = request.flag == EChatFlag::Local ? EChatFlag::Local : EChatFlag::Global;
    const CServer::SChatEntry chat = m_server.SubmitChat(entity->GameWorld(), entity->Pos(), flag, player.GetId(),
                                                     player.GetName(), request.message);
    if (!chat.message.empty()) BroadcastChat(chat);
}

void INetworkModule::HandleSecondarySlotRequest(CPlayer& player, const ClientSecondarySlotRequest& request)
{
    if (request.slot_index >= game_config::default_flower_petal_num_max) return;

    CEntity* entity = player.GetEntity();
    CGameWorld* world = entity ? entity->GameWorld() : nullptr;
    IGameController* controller = world ? world->GetController() : nullptr;
    if (controller && controller->LocksPetalLoadout()) return;

    const std::vector<SInventoryItem> current_slots =
        CAccountDataStore::GetSecondarySlots(player.GetAccountName());
    if (request.slot_index < current_slots.size())
    {
        const SInventoryItem& old_item = current_slots[request.slot_index];
        if (old_item.petal_type != 0 && old_item.rarity != 0 && controller &&
            !controller->CanUnequipPetalType(static_cast<EPetalType>(old_item.petal_type)))
            return;
    }

    if (request.petal_type == 0 || request.rarity == 0)
    {
        CAccountDataStore::ClearSecondarySlot(player.GetAccountName(), request.slot_index);
        return;
    }

    const EPetalType requested_type = static_cast<EPetalType>(request.petal_type);
    if (!IsKnownPetalTypeId(request.petal_type) || !IsKnownRarity(static_cast<ERarity>(request.rarity))) return;
    const CPetalPrototype* proto = FindPetalPrototype(requested_type);
    if (!proto || !proto->m_p_behavior) return;
    if (controller && !controller->CanEquipPetalType(requested_type)) return;

    CAccountDataStore::SetSecondarySlot(player.GetAccountName(), request.slot_index, request.petal_type,
                                        request.rarity);
}

void INetworkModule::HandleCraftRequest(CPlayer& player, const ClientCraftRequest& request)
{
    SCraftResult result;
    if (player.TryCraftPetal(request.petal_type, request.rarity, request.count, &result))
    {
        QueueCraftResult(player, result);
        QueueInventory(player);
        QueueOwnerStateUpdate(player);
    }
}

void INetworkModule::HandleForgeRequest(CPlayer& player, const ClientForgeRequest& request)
{
    constexpr uint32_t forge_cost = 5;
    constexpr uint8_t super_rarity = static_cast<uint8_t>(ERarity::Super);

    SCraftResult result;
    result.petal_type = request.petal_type;
    result.rarity = request.rarity;
    result.consumed = forge_cost;

    const bool valid_request = IsKnownPetalTypeId(request.petal_type) && request.rarity == super_rarity &&
                               request.count == forge_cost;
    if (valid_request) result.items.push_back({ request.petal_type, super_rarity, forge_cost });

    auto reject = [&]() { QueueCraftResult(player, result); };
    if (!valid_request)
    {
        reject();
        return;
    }

    CEntity* player_entity = player.GetEntity();
    CGameWorld* world = player_entity ? player_entity->GameWorld() : nullptr;
    if (!player_entity || !world || player_entity->IsMarkedForDes() || player_entity->IsDead())
    {
        reject();
        return;
    }

    auto* forge_titan = dynamic_cast<CTitanFlower*>(world->FindClosestEntityByEdge(
        player_entity->Pos(), game_config::titan_forge_range, [](const CEntity* candidate) {
            const auto* titan = dynamic_cast<const CTitanFlower*>(candidate);
            return titan && !titan->IsMarkedForDes() && !titan->IsDead();
        }));
    const EPetalType petal_type = static_cast<EPetalType>(request.petal_type);
    if (!forge_titan || !m_server.CanTitanForgePetal(petal_type) ||
        !CAccountDataStore::HasItem(player.GetAccountName(), request.petal_type, super_rarity, forge_cost))
    {
        reject();
        return;
    }

    IUniquePetalRegistry* registry = m_server.GetUniquePetalRegistry();
    if (!registry || !registry->GetUnique(petal_type, forge_cost, player.GetAccountName()))
    {
        reject();
        return;
    }

    forge_titan->ReplaceForgedUniquePetal(petal_type);
    m_server.StartTitanForgeCooldown(petal_type);

    result.changed = true;
    result.successes = 1;
    result.result_rarity = static_cast<uint8_t>(ERarity::Unique);
    result.items = { { request.petal_type, result.result_rarity, 1 } };
    QueueCraftResult(player, result);
}

void INetworkModule::HandleTalentRequest(CPlayer& player, const ClientTalentRequest& request)
{
    bool changed = false;
    for (const STalentPacketItem& item : request.talents)
    {
        ERarity rarity = static_cast<ERarity>(item.rarity);
        int rank = static_cast<int>(item.rank);
        if (request.action == ClientTalentAction::Remove)
            changed = player.RemoveTalent(item.id, rarity, rank) || changed;
        else changed = player.AddTalent(item.id, rarity, rank) || changed;
    }

    QueueOwnerState(player);
    if (changed) QueueInventory(player);
}

INetworkModule::EPlayerBufferResult INetworkModule::HandleAuthRequest(CPlayer& player, const ClientAuthRequest& request)
{
    const bool binding_request = request.mode == ClientAuthRequest::Mode::RequestBindingCode ||
                                 request.mode == ClientAuthRequest::Mode::ConfirmBinding;
    if (player.IsAuthenticated() && !binding_request)
    {
        QueueAuthResult(player, EAuthResultCode::Failed, "Already authenticated");
        return EPlayerBufferResult::Continue;
    }
    if (binding_request && !game_config::IsEmailBindingEnabled())
    {
        QueueAuthResult(player, EAuthResultCode::Failed, "Email binding is disabled");
        return EPlayerBufferResult::Continue;
    }

    if (m_player_auth_requests.contains(player.GetId()))
    {
        QueueAuthResult(player, EAuthResultCode::Failed, "Authentication already in progress");
        return EPlayerBufferResult::Continue;
    }

    if (!IsValidUtf8(request.name))
    {
        LOG_WARN("network", "Player " + std::to_string(player.GetId()) + " sent an invalid-UTF-8 account name");
        QueueAuthResult(player, EAuthResultCode::Failed, "Invalid name encoding");
        return EPlayerBufferResult::Continue;
    }

    if (IsNameBanned(request.name))
    {
        QueueAuthResult(player, EAuthResultCode::Failed, "Name is banned");
        return EPlayerBufferResult::Continue;
    }
    if (request.mode == ClientAuthRequest::Mode::Login && IsLoginRateLimited(player, request.name))
    {
        QueueAuthResult(player, EAuthResultCode::Failed, "Too many login attempts; try again later");
        return EPlayerBufferResult::Continue;
    }

    const bool queues_authentication = request.mode == ClientAuthRequest::Mode::Login ||
                                       request.mode == ClientAuthRequest::Mode::Register;
    if (queues_authentication && m_player_auth_requests.size() >= auth_queue_limit)
    {
        QueueAuthResult(player, EAuthResultCode::Failed, "Authentication service is busy");
        return EPlayerBufferResult::Continue;
    }

    if (request.mode == ClientAuthRequest::Mode::RequestRegistrationCode)
    {
        QueueAuthResult(player, EAuthResultCode::Failed, "Registration no longer requires an email code");
        return EPlayerBufferResult::Continue;
    }

    auto pending_login = m_pending_verified_logins.find(player.GetId());
    if (pending_login != m_pending_verified_logins.end() &&
        std::chrono::steady_clock::now() >= pending_login->second.expires_at)
    {
        ClearPendingEmailAuthentication(player.GetId());
        pending_login = m_pending_verified_logins.end();
    }
    const std::string binding_account_name = player.IsAuthenticated()
                                                 ? player.GetAccountName()
                                                 : pending_login != m_pending_verified_logins.end()
                                                       ? pending_login->second.account_name
                                                       : std::string{};
    if (request.mode == ClientAuthRequest::Mode::RequestBindingCode)
    {
        if (binding_account_name.empty() || binding_account_name != request.name)
        {
            QueueAuthResult(player, EAuthResultCode::Failed, "Login again before binding an email address");
            return EPlayerBufferResult::Continue;
        }

        std::string error;
        if (!CAccountDataStore::ValidateEmailForBinding(request.name, request.email, &error))
        {
            QueueAuthResult(player, EAuthResultCode::Failed,
                            error.empty() ? "Unable to bind email address" : error);
            return EPlayerBufferResult::Continue;
        }
        RequestVerificationCode(player, request.name, request.email, false);
        return EPlayerBufferResult::Continue;
    }

    if (request.mode == ClientAuthRequest::Mode::ConfirmBinding)
    {
        if (binding_account_name.empty() || binding_account_name != request.name)
        {
            QueueAuthResult(player, EAuthResultCode::Failed, "Login again before binding an email address");
            return EPlayerBufferResult::Continue;
        }
        if (!m_email_verification_service)
        {
            QueueAuthResult(player, EAuthResultCode::Failed, "Email verification service is unavailable");
            return EPlayerBufferResult::Continue;
        }

        std::string error;
        if (!m_email_verification_service->VerifyCode(request.name, request.email,
                                                       CEmailVerificationService::EPurpose::Binding,
                                                       request.code, error))
        {
            QueueAuthResult(player, EAuthResultCode::Failed,
                            error.empty() ? "Email verification failed" : error);
            return EPlayerBufferResult::Continue;
        }
        if (!CAccountDataStore::ConfirmEmail(request.name, request.email, &error))
        {
            QueueAuthResult(player, EAuthResultCode::Failed,
                            error.empty() ? "Unable to confirm email address" : error);
            return EPlayerBufferResult::Continue;
        }
        m_email_verification_service->ForgetChallenge(request.name, request.email,
                                                      CEmailVerificationService::EPurpose::Binding);

        if (player.IsAuthenticated())
        {
            ClearPendingEmailAuthentication(player.GetId());
            QueueAuthResult(player, EAuthResultCode::EmailBound, "Email bound");
            return EPlayerBufferResult::Continue;
        }

        const std::string account_name = pending_login->second.account_name;
        const std::string trusted_ip_to_commit = pending_login->second.trusted_ip_to_commit;
        const bool register_mode = pending_login->second.register_mode;
        ClearPendingEmailAuthentication(player.GetId());
        return ApplyAuthentication(player, account_name, register_mode, EAuthResultCode::EmailBound, "Email bound",
                                   trusted_ip_to_commit);
    }

    const bool register_mode = request.mode == ClientAuthRequest::Mode::Register;
    if (register_mode)
    {
        if (m_active_auth_accounts.contains(request.name))
        {
            QueueAuthResult(player, EAuthResultCode::Failed, "Authentication already in progress");
            return EPlayerBufferResult::Continue;
        }

        SAccountAuthWork validation;
        std::string error;
        if (!CAccountDataStore::PrepareAuthentication(request.name, request.password, request.email,
                                                       player.GetRemoteAddress(), true, validation, &error))
        {
            QueueAuthResult(player, EAuthResultCode::Failed,
                            error.empty() ? "Invalid registration request" : error);
            return EPlayerBufferResult::Continue;
        }
    } else if (request.mode != ClientAuthRequest::Mode::Login)
    {
        QueueAuthResult(player, EAuthResultCode::Failed, "Unsupported authentication mode");
        return EPlayerBufferResult::Continue;
    }

    if (m_next_auth_request_id == 0) m_next_auth_request_id = 1;
    SPendingAuthRequest pending;
    pending.request_id = m_next_auth_request_id++;
    pending.player_id = player.GetId();
    pending.account_name = request.name;
    pending.password = request.password;
    pending.email = request.email;
    pending.register_mode = register_mode;
    m_player_auth_requests.emplace(pending.player_id, pending.request_id);
    m_pending_auth_requests.push_back(std::move(pending));
    return EPlayerBufferResult::Continue;
}

INetworkModule::EPlayerBufferResult INetworkModule::ApplyAuthentication(CPlayer& player,
                                                                        const std::string& account_name,
                                                                        bool register_mode,
                                                                        EAuthResultCode result_code,
                                                                        const std::string& result_message,
                                                                        const std::string& trusted_ip_to_commit)
{
    for (const auto& existing_player : m_players)
    {
        if (!existing_player || existing_player.get() == &player) continue;
        if (!existing_player->IsAuthenticated()) continue;
        if (existing_player->GetAccountName() != account_name) continue;
        if (existing_player->IsConnected())
        {
            QueueAuthResult(player, EAuthResultCode::Failed, "Account is already online");
            return EPlayerBufferResult::Continue;
        }
    }

    auto commit_trusted_ip = [&](CPlayer& response_player) {
        if (trusted_ip_to_commit.empty()) return true;
        std::string error;
        if (CAccountDataStore::CommitTrustedIp(account_name, trusted_ip_to_commit, &error)) return true;

        QueueAuthResult(response_player, EAuthResultCode::Failed, "Failed to update trusted IP");
        FlushSendBuffer(response_player);
        LOG_ERROR("account", "Failed to update trusted IP for " + account_name +
                                 (error.empty() ? std::string{} : ": " + error));
        return false;
    };

    if (CPlayer* reconnect_player = FindReconnectablePlayer(account_name, &player))
    {
        reconnect_player->SetRemoteAddress(player.GetRemoteAddress());
        reconnect_player->AttachSocket(std::move(player.GetSocket()));
        if (!reconnect_player->GetEntity() &&
            !m_player_lifecycle_service.SpawnPlayer(*reconnect_player, m_lobby_world, EPlayerSpawnReason::Login))
        {
            QueueAuthResult(*reconnect_player, EAuthResultCode::Failed, "Failed to enter world");
            FlushSendBuffer(*reconnect_player);
            reconnect_player->DetachSocket();
            LOG_ERROR("network", "Failed to restore an entity for account " + account_name);
            return EPlayerBufferResult::RemovePendingPlayer;
        }
        if (!commit_trusted_ip(*reconnect_player))
        {
            reconnect_player->DetachSocket();
            return EPlayerBufferResult::RemovePendingPlayer;
        }
        m_auth_deadlines.erase(player.GetId());
        QueueAuthResult(*reconnect_player, result_code,
                        result_message.empty() ? "Reconnected" : result_message);
        m_player_lifecycle_service.NotifyPlayerLogin(*reconnect_player, *this);
        if (!FlushSendBuffer(*reconnect_player))
        {
            reconnect_player->DetachSocket();
            return EPlayerBufferResult::RemovePendingPlayer;
        }
        LOG_INFO("network",
                 "Account " + account_name + " reconnected as player " + std::to_string(reconnect_player->GetId()));
        return EPlayerBufferResult::RemovePendingPlayer;
    }

    player.Authenticate(account_name);
    player.SetUseNewPlayerSpawn(register_mode);
    if (!m_player_lifecycle_service.SpawnPlayer(player, m_lobby_world, EPlayerSpawnReason::Login))
    {
        QueueAuthResult(player, EAuthResultCode::Failed, "Failed to enter world");
        FlushSendBuffer(player);
        LOG_ERROR("network", "Failed to create an entity for account " + account_name);
        return EPlayerBufferResult::RequestedDisconnect;
    }
    if (!commit_trusted_ip(player)) return EPlayerBufferResult::RequestedDisconnect;

    m_auth_deadlines.erase(player.GetId());
    QueueAuthResult(player, result_code,
                    result_message.empty() ? (register_mode ? "Registered" : "Logged in") : result_message);
    m_player_lifecycle_service.NotifyPlayerLogin(player, *this);
    if (!FlushSendBuffer(player))
    {
        player.DetachSocket();
        return EPlayerBufferResult::RequestedDisconnect;
    }
    LOG_INFO("network", "Account " + account_name + " authenticated as player " + std::to_string(player.GetId()));
    return EPlayerBufferResult::Continue;
}

CPlayer* INetworkModule::FindPlayerById(uint32_t player_id) const
{
    for (const auto& player : m_players)
    {
        if (player && player->GetId() == player_id) return player.get();
    }
    return nullptr;
}

void INetworkModule::ClearPlayersForRestore()
{
    m_rcon_authorizations.clear();
    m_dev_debug->Reset();
    m_pending_connections.clear();
    for (const auto& player : m_players)
        if (player) ClearPendingEmailAuthentication(player->GetId());
    m_pending_auth_requests.clear();
    m_player_auth_requests.clear();
    m_active_auth_accounts.clear();
    m_pending_verified_logins.clear();
    m_email_delivery_requests.clear();
    m_proxy_header_pending.clear();
    m_auth_deadlines.clear();
    m_auth_failures.clear();
    m_players.clear();
    m_client_snapshot_states.clear();
    m_free_player_ids.clear();
    m_next_player_id = 1;
}

bool INetworkModule::InsertRestoredPlayer(std::unique_ptr<CPlayer> player)
{
    if (!player || player->GetId() == 0 || FindPlayerById(player->GetId())) return false;
    m_next_player_id = std::max(m_next_player_id, static_cast<int>(player->GetId()) + 1);
    m_players.push_back(std::move(player));
    return true;
}

void INetworkModule::FinalizePlayerRestore()
{
    m_free_player_ids.clear();
    for (int id = 1; id < m_next_player_id; ++id)
    {
        if (!FindPlayerById(static_cast<std::uint32_t>(id))) m_free_player_ids.insert(id);
    }
}

bool INetworkModule::KickPlayer(uint32_t player_id, const std::string& reason)
{
    for (size_t i = 0; i < m_players.size(); ++i)
    {
        if (!m_players[i] || m_players[i]->GetId() != player_id) continue;
        DropPlayer(i, reason);
        return true;
    }
    return false;
}

void INetworkModule::BanPlayerIp(uint32_t player_id, float seconds)
{
    CPlayer* player = FindPlayerById(player_id);
    if (!player || player->GetRemoteAddress().empty()) return;

    m_ip_bans.push_back({ player->GetRemoteAddress(), seconds });
    KickPlayer(player_id, "IP banned");
}

void INetworkModule::BanName(const std::string& name, float seconds)
{
    std::string target = ToLower(name);
    m_name_bans.push_back({ target, seconds });
    for (size_t i = 0; i < m_players.size();)
    {
        CPlayer* player = m_players[i].get();
        if (player && ToLower(player->GetAccountName()) == target)
        {
            DropPlayer(i, "name banned");
            continue;
        }
        ++i;
    }
}

bool INetworkModule::IsIpBanned(const std::string& ip) const
{
    for (const auto& [banned_ip, timer] : m_ip_bans)
    {
        if (banned_ip == ip && (timer < 0.f || timer > 0.f)) return true;
    }
    return false;
}

bool INetworkModule::IsNameBanned(const std::string& name) const
{
    std::string target = ToLower(name);
    for (const auto& [banned_name, timer] : m_name_bans)
    {
        if (banned_name == target && (timer < 0.f || timer > 0.f)) return true;
    }
    return false;
}

bool INetworkModule::AssignPlayerEntity(uint32_t player_id, CGameWorld& world, int entity_id)
{
    CPlayer* player = FindPlayerById(player_id);
    if (!player || !player->IsAuthenticated()) return false;

    CEntity* entity = world.GetEntity(entity_id);
    auto* mob = dynamic_cast<CMobBase*>(entity);
    if (!mob || mob->IsDead()) return false;

    CEntity* source_entity = player->GetEntity();
    CGameWorld* source_world = source_entity ? source_entity->GameWorld() : nullptr;
    const bool entity_changed = source_entity != mob;
    player->ResetControlledMob();
    mob->SetController(std::make_unique<CPlayerController>());
    if (entity_changed && source_world)
        if (IGameController* controller = source_world->GetController())
            controller->OnPlayerLeftWorld(*source_world, *player);
    player->SetOwnedEntity(mob);
    if (entity_changed)
        if (IGameController* controller = world.GetController()) controller->OnPlayerEnteredWorld(world, *player);
    player->m_logged_missing_entity = false;
    m_player_lifecycle_service.NotifyPlayerWorldChanged(*player, *this);
    LOG_INFO("network", "Player " + std::to_string(player_id) + " now controls entity " +
                            std::to_string(entity_id) + " in world " + std::to_string(world.GetId()));
    return true;
}

CGameContext* INetworkModule::GameContext() const { return m_lobby_world.GameContext(); }

void INetworkModule::NotifyPlayerWorldChanged(CPlayer& player)
{
    m_player_lifecycle_service.NotifyPlayerWorldChanged(player, *this);
}

CPlayer* INetworkModule::FindReconnectablePlayer(const std::string& account_name, const CPlayer* pending_player) const
{
    for (const auto& player : m_players)
    {
        if (!player || player.get() == pending_player) continue;
        if (!player->IsAuthenticated()) continue;
        if (player->GetAccountName() != account_name) continue;
        if (!player->IsConnected() && !player->IsTimedOut()) return player.get();
    }
    return nullptr;
}

void INetworkModule::TickTimeouts(float dt)
{
    const auto now = std::chrono::steady_clock::now();
    for (size_t i = 0; i < m_players.size();)
    {
        CPlayer* player = m_players[i].get();
        if (!player)
        {
            m_players.erase(m_players.begin() + i);
            continue;
        }

        if (player->IsConnected() && !player->IsAuthenticated())
        {
            const auto deadline = m_auth_deadlines.find(player->GetId());
            if (deadline != m_auth_deadlines.end() && now >= deadline->second)
            {
                DropPlayer(i, "authentication timed out");
                continue;
            }
        }

        player->TickTimeout(dt);
        if (player->IsTimedOut())
        {
            DropPlayer(i, "timed out");
            continue;
        }
        ++i;
    }
}

void INetworkModule::TickBans(float dt)
{
    auto tick = [dt](auto& bans) {
        for (auto& [key, timer] : bans)
        {
            (void)key;
            if (timer > 0.f) timer -= dt;
        }
        bans.erase(std::remove_if(
                       bans.begin(), bans.end(),
                       [](const auto& ban) { return ban.second == 0.f || (ban.second > -1.f && ban.second <= 0.f); }),
                   bans.end());
    };

    tick(m_ip_bans);
    tick(m_name_bans);
}

void INetworkModule::DropPlayer(size_t index, const std::string& reason)
{
    if (index >= m_players.size() || !m_players[index]) return;

    CPlayer& player = *m_players[index];
    m_rcon_authorizations.erase(player.GetId());
    LOG_INFO("network", "Player " + std::to_string(player.GetId()) + " dropped: " + reason);
    CancelAuthenticationRequest(player.GetId());
    ClearPendingEmailAuthentication(player.GetId());
    ResetSnapshotState(player);
    m_proxy_header_pending.erase(player.GetId());
    m_auth_deadlines.erase(player.GetId());
    CEntity* entity = player.GetEntity();
    CGameWorld* player_world = entity ? entity->GameWorld() : nullptr;
    if (player.IsAuthenticated() && player_world)
        if (IGameController* controller = player_world->GetController())
            controller->OnPlayerLeftWorld(*player_world, player);
    if (auto* flower = dynamic_cast<CPlayerFlower*>(entity))
        flower->PrepareRespawnDestroy(EEntityRemovalReason::Despawned);
    else if (entity) entity->MarkForDestroy(EEntityRemovalReason::Despawned);
    FreePlayerId(player.GetId());
    m_players.erase(m_players.begin() + index);
}

int INetworkModule::GetNewPlayerId()
{
    if (!m_free_player_ids.empty())
    {
        int id = *m_free_player_ids.begin();
        m_free_player_ids.erase(m_free_player_ids.begin());
        return id;
    }
    return m_next_player_id++;
}

void INetworkModule::FreePlayerId(int id) { m_free_player_ids.insert(id); }
