#include "server.h"
#include "../Engine/logger.h"
#include "../Shared/drop_rate.h"
#include "../Shared/game_config.h"
#include "../Shared/version.h"
#include "Game/entities/mob.h"
#include "Game/entities/petals/petals_behavior.h"
#include "Game/gamecontext.h"
#include "HotReload/hot_reload_snapshot.h"
#include "HotReload/snapshot_archive.h"
#include "Module/console_module.h"
#include "Module/network_module.h"
#include "Module/server_gui_module.h"
#include "Module/world_module.h"
#include "Persistence/server_data_store.h"
#include "Persistence/unique_petal_registry.h"
#include "report.h"
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <thread>

namespace
{
using server_clock = std::chrono::steady_clock;

constexpr std::uint64_t max_ticks_per_batch = 4;
constexpr auto performance_sample_interval = std::chrono::seconds(10);

bool IsForgeablePetalType(EPetalType type)
{
    const int value = static_cast<int>(type);
    return value > static_cast<int>(EPetalType::None) && value < static_cast<int>(petal_type_names.size());
}

struct SPerformanceWindow
{
    server_clock::time_point start;
    std::uint64_t ticks = 0;
    std::uint64_t overruns = 0;
    std::uint64_t slow_ticks = 0;
    std::uint64_t catch_up_ticks = 0;
    std::uint64_t dropped_ticks = 0;
    double tick_ms_sum = 0.0;
    double max_tick_ms = 0.0;
    double max_schedule_lag_ms = 0.0;

    void Reset(server_clock::time_point now)
    {
        *this = {};
        start = now;
    }
};

std::string GenerateRconPassword()
{
    static const std::string upper = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    static const std::string lower = "abcdefghijklmnopqrstuvwxyz";
    static const std::string digits = "0123456789";
    static const std::string all = upper + lower + digits;

    std::random_device random_device;
    std::mt19937 rng(random_device());
    auto pick = [&rng](const std::string& pool) {
        std::uniform_int_distribution<size_t> dist(0, pool.size() - 1);
        return pool[dist(rng)];
    };

    const size_t password_length = std::max<size_t>(3, game_config::rcon_generated_password_length);
    std::string password;
    password.reserve(password_length);
    password.push_back(pick(upper));
    password.push_back(pick(lower));
    password.push_back(pick(digits));
    while (password.size() < password_length)
        password.push_back(pick(all));
    std::shuffle(password.begin(), password.end(), rng);
    return password;
}

void EnsureRconPasswordInitialized()
{
    if (!game_config::rcon_password.empty()) return;

    game_config::rcon_password = GenerateRconPassword();
    LOG_INFO("rcon", "Generated RCON password: " + game_config::rcon_password);
}

std::string ReportArticle(std::string_view word)
{
    if (word.empty()) return "A";

    std::string lowered(word.begin(), word.end());
    std::transform(lowered.begin(), lowered.end(), lowered.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });

    if (lowered.rfind("uni", 0) == 0) return "A";

    const char first = lowered.front();
    return first == 'a' || first == 'e' || first == 'i' || first == 'o' || first == 'u' ? "An" : "A";
}

std::string RarityReportArticle(ERarity rarity) { return ReportArticle(GetRarityName(rarity)); }

} // namespace

CServer* CServer::s_p_instance = nullptr;

CServer::CServer()
{
    s_p_instance = this;
    m_p_data_store = CreateServerDataStore();
    m_modules.emplace_back(std::make_unique<IConsoleModule>(m_console));
    m_modules.emplace_back(std::make_unique<IServerGuiModule>(m_console));

    auto world_mod = std::make_unique<IWorldModule>();
    m_p_world_module = world_mod.get();
    auto& worlds = world_mod->GetWorlds();

    CGameWorld& world = *worlds[0];
    m_p_main_world = &world;
    m_modules.emplace_back(std::move(world_mod));
    auto network_mod = std::make_unique<INetworkModule>(*this, world);
    m_p_network_module = network_mod.get();
    m_modules.emplace_back(std::move(network_mod));
    m_p_game_context = std::make_unique<CGameContext>(world, *m_p_network_module, *this, *this);
    for (const auto& game_world : worlds)
        if (game_world) game_world->SetGameContext(m_p_game_context.get());
}

CServer::~CServer()
{
    if (m_log_sink_id != 0)
    {
        CLogger::RemoveSink(m_log_sink_id);
        m_log_sink_id = 0;
    }
    if (m_log_file && m_log_file->is_open()) m_log_file->flush();
    s_p_instance = nullptr;
}

void CServer::Init()
{
    std::string petal_registry_error;
    std::string mob_registry_error;
    const bool petals_registered = RegisterPetals(petal_registry_error);
    const bool mobs_registered = RegisterMobs(mob_registry_error);
    if (!petals_registered || !mobs_registered)
    {
        std::string error = std::move(petal_registry_error);
        if (!mob_registry_error.empty())
        {
            if (!error.empty()) error += '\n';
            error += mob_registry_error;
        }
        LOG_FATAL("content", error.empty() ? "Failed to initialize game content registries" : error);
        m_running = false;
        return;
    }

    RegisterDropRates();
    m_console.InstallCommands();
    ExecuteStartupCommands();
    InstallLogSink();
    LOG_INFO("server", std::string(florrbt::version_label) + " starting");
    EnsureRconPasswordInitialized();

    std::string persistence_error;
    if (!m_p_data_store || !m_p_data_store->Init(persistence_error))
    {
        LOG_FATAL("persistence", persistence_error.empty() ? "Failed to initialize server data store"
                                                            : persistence_error);
        m_running = false;
        return;
    }

    for (auto& module : m_modules)
    {
        if (!module->Init())
        {
            m_running = false;
            return;
        }
    }

    if (!RestoreConfiguredSnapshot())
    {
        m_exit_code = restore_failed_exit_code;
        m_running = false;
        return;
    }
    if (!PublishReadyFile())
    {
        m_exit_code = 1;
        m_running = false;
    }
}

void CServer::RequestHotReload(std::string candidate)
{
    if (m_hot_reload_requested)
    {
        LOG_WARN("hot_reload", "A hot reload is already pending");
        return;
    }
    m_hot_reload_candidate = candidate.empty() ? game_config::hot_reload_default_candidate : std::move(candidate);
    m_hot_reload_requested = true;
    LOG_INFO("hot_reload", "Hot reload scheduled at the next completed tick");
}

bool CServer::RestoreConfiguredSnapshot()
{
    if (m_restore_snapshot_path.empty()) return true;
    std::string error;
    if (!CHotReloadSnapshotService::Restore(*this, m_restore_snapshot_path, error))
    {
        LOG_FATAL("hot_reload", "Failed to restore snapshot " + m_restore_snapshot_path.string() + ": " + error);
        return false;
    }
    LOG_INFO("hot_reload", "Restored snapshot " + m_restore_snapshot_path.string() + " at tick " +
                               std::to_string(m_tick));
    return true;
}

bool CServer::PublishReadyFile()
{
    if (m_ready_file_path.empty()) return true;
    CJsonOwner ready = MakeJsonObject();
    CSnapshotWriter writer(ready.get());
    writer.Field("status", "ready");
    writer.Field("app_version", florrbt::version);
    writer.UInt64("tick", m_tick);
    writer.Field("restored", !m_restore_snapshot_path.empty());
    std::string error;
    if (!WriteJsonAtomically(m_ready_file_path, ready.get(), error))
    {
        LOG_FATAL("hot_reload", "Failed to publish readiness file: " + error);
        return false;
    }
    return true;
}

void CServer::ProcessHotReloadRequest()
{
    if (!m_hot_reload_requested) return;
    m_hot_reload_requested = false;

    if (m_p_network_module)
    {
        if (const SChatEntry* chat = SubmitServerChat("Server is reloading")) m_p_network_module->BroadcastChat(*chat);
    }

    const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
                         std::chrono::system_clock::now().time_since_epoch())
                         .count();
    const std::filesystem::path directory(game_config::hot_reload_directory);
    const std::filesystem::path snapshot_path =
        directory / ("snapshot-" + std::to_string(now) + "-" + std::to_string(m_tick) + ".json");

    std::string error;
    if (!CHotReloadSnapshotService::Capture(*this, snapshot_path, error))
    {
        LOG_ERROR("hot_reload", "Snapshot capture failed; server will continue running: " + error);
        return;
    }

    CJsonOwner request = MakeJsonObject();
    CSnapshotWriter writer(request.get());
    std::error_code path_error;
    const std::filesystem::path absolute_snapshot = std::filesystem::absolute(snapshot_path, path_error);
    writer.Field("snapshot", (path_error ? snapshot_path : absolute_snapshot).generic_string());
    writer.Field("candidate", m_hot_reload_candidate);
    writer.Field("app_version", florrbt::version);
    writer.UInt64("tick", m_tick);
    if (!WriteJsonAtomically(game_config::hot_reload_request_path, request.get(), error))
    {
        LOG_ERROR("hot_reload", "Failed to publish reload request; server will continue running: " + error);
        return;
    }

    LOG_INFO("hot_reload", "Snapshot ready at " + snapshot_path.string() + "; handing control to launcher");
    m_exit_code = hot_reload_exit_code;
    m_running = false;
}

std::vector<CGameWorld*> CServer::FindWorldsByMapName(const std::string& map_name) const
{
    return m_p_world_module ? m_p_world_module->FindWorldsByMapName(map_name) : std::vector<CGameWorld*>{};
}

CGameWorld* CServer::FindWorldById(std::uint32_t world_id) const
{
    return m_p_world_module ? m_p_world_module->FindWorldById(world_id) : nullptr;
}

IUniquePetalRegistry* CServer::GetUniquePetalRegistry() const
{
    return m_p_data_store ? &m_p_data_store->UniquePetals() : nullptr;
}

bool CServer::CanTitanForgePetal(EPetalType type) const
{
    return IsForgeablePetalType(type) && m_titan_forge_cooldowns[static_cast<std::size_t>(type)] <= 0.f;
}

void CServer::StartTitanForgeCooldown(EPetalType type)
{
    if (!IsForgeablePetalType(type)) return;
    m_titan_forge_cooldowns[static_cast<std::size_t>(type)] =
        std::max(0.f, game_config::titan_forge_cooldown_seconds);
}

void CServer::RestoreTitanForgeCooldowns(TTitanForgeCooldowns cooldowns)
{
    cooldowns[static_cast<std::size_t>(EPetalType::None)] = 0.f;
    for (float& cooldown : cooldowns)
        cooldown = std::isfinite(cooldown) ? std::max(0.f, cooldown) : 0.f;
    m_titan_forge_cooldowns = std::move(cooldowns);
}

void CServer::MergeLegacyTitanForgeCooldownForRestore(float cooldown)
{
    if (!std::isfinite(cooldown) || cooldown <= 0.f) return;
    for (std::size_t i = static_cast<std::size_t>(EPetalType::None) + 1; i < m_titan_forge_cooldowns.size(); ++i)
        m_titan_forge_cooldowns[i] = std::max(m_titan_forge_cooldowns[i], cooldown);
}

void CServer::TickTitanForgeCooldowns(float dt)
{
    if (!std::isfinite(dt) || dt <= 0.f) return;
    for (std::size_t i = static_cast<std::size_t>(EPetalType::None) + 1; i < m_titan_forge_cooldowns.size(); ++i)
        m_titan_forge_cooldowns[i] = std::max(0.f, m_titan_forge_cooldowns[i] - dt);
}

std::vector<CGameWorld*> CServer::GetWorlds() const
{
    std::vector<CGameWorld*> result;
    if (!m_p_world_module) return result;
    result.reserve(m_p_world_module->GetWorlds().size());
    for (const auto& world : m_p_world_module->GetWorlds())
        if (world) result.push_back(world.get());
    return result;
}

CGameWorld* CServer::FindRandomWorldByMapName(const std::string& map_name) const
{
    return m_p_world_module ? m_p_world_module->FindRandomWorldByMapName(map_name) : nullptr;
}

void CServer::InstallLogSink()
{
    if (m_log_sink_id != 0) return;

    std::filesystem::path log_path(game_config::server_log_path);
    if (!log_path.parent_path().empty())
    {
        std::error_code ec;
        std::filesystem::create_directories(log_path.parent_path(), ec);
        if (ec)
        {
            LOG_WARN("server",
                     "Failed to create log directory: " + log_path.parent_path().string() + " (" + ec.message() + ")");
        }
    }

    auto file = std::make_shared<std::ofstream>(log_path, std::ios::app | std::ios::binary);
    if (!file->is_open())
    {
        LOG_WARN("server", "Failed to open server log file: " + log_path.string());
        return;
    }

    auto mutex = std::make_shared<std::mutex>();
    *file << "==== FlorrBt server log started ====" << std::endl;
    file->flush();

    m_log_file = file;
    m_log_mutex = mutex;
    m_log_sink_id =
        CLogger::AddSink([file, mutex](const std::string& sender, ELogPriority priority, const std::string& msg) {
            std::lock_guard<std::mutex> lock(*mutex);
            if (!file->is_open()) return;
            *file << CLogger::FormatLine(sender, priority, msg) << std::endl;
            file->flush();
        });

    LOG_INFO("server", "Saving server log to " + log_path.string());
}

void CServer::ExecuteStartupCommands()
{
    std::ifstream file(game_config::startup_commands_path);
    if (!file) return;

    std::string line;
    while (std::getline(file, line))
    {
        if (!line.empty() && line.back() == '\r') line.pop_back();

        size_t begin = 0;
        while (begin < line.size() && std::isspace(static_cast<unsigned char>(line[begin])))
            ++begin;
        if (begin >= line.size()) continue;
        if (line.compare(begin, 2, "//") == 0) continue;

        m_console.ExecuteLine(line.substr(begin));
    }
}

const CServer::SChatEntry* CServer::SubmitChat(CGameWorld* world, sf::Vector2f pos, EChatFlag flag, uint32_t player_id,
                                               const std::string& player_name, const std::string& message,
                                               int target_player_id)
{
    if (message.empty()) return nullptr;

    SChatEntry entry;
    entry.world = world;
    entry.pos = pos;
    entry.flag = flag;
    entry.player_id = player_id;
    entry.target_player_id = target_player_id;
    entry.player_name = player_name;
    entry.message = message.substr(0, max_chat_message_size);
    entry.system_time = static_cast<uint32_t>(
        std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count());

    m_chats.push_back(std::move(entry));
    const size_t max_saved_chats = std::max<size_t>(1, game_config::server_max_saved_chats);
    if (m_chats.size() > max_saved_chats)
        m_chats.erase(m_chats.begin(), m_chats.begin() + static_cast<std::ptrdiff_t>(m_chats.size() - max_saved_chats));

    return &m_chats.back();
}

const CServer::SChatEntry* CServer::SubmitServerChat(const std::string& message)
{
    return SubmitChat(nullptr, { 0.f, 0.f }, EChatFlag::Server, 0, "Server", message);
}

bool CServer::MeetsPetalReportRarity(ERarity rarity, int min_rarity)
{
    if (!IsKnownRarity(rarity)) return false;
    if (min_rarity <= 0) return true;

    ERarity min = static_cast<ERarity>(min_rarity);
    if (!IsKnownRarity(min)) return false;
    return GetRaritySortRank(rarity) >= GetRaritySortRank(min);
}

bool CServer::ShouldReportRarity(ERarity rarity, int minimum_rarity) const
{
    return MeetsPetalReportRarity(rarity, minimum_rarity);
}

bool CServer::ReportMob(std::string_view action, ERarity rarity, std::string_view mob_name)
{
    return BroadcastMobReport(action, rarity, mob_name);
}

bool CServer::ReportMobSpawn(CGameWorld& source_world, std::string_view action, ERarity rarity,
                             std::string_view mob_name)
{
    return BroadcastMobSpawnReport(source_world, action, rarity, mob_name);
}

bool CServer::BroadcastPetalReport(std::string_view done, ERarity rarity, std::string_view petal_name,
                                   std::string_view doer)
{
    if (done.empty() || petal_name.empty() || doer.empty() || !IsKnownRarity(rarity)) return false;

    INetworkModule* network = GetNetworkModule();
    if (!network) return false;

    std::string rarity_name(GetRarityName(rarity));
    std::string message;
    message.reserve(rarity_name.size() * 2 + petal_name.size() + doer.size() + done.size() + 32);
    message += "<";
    message += rarity_name;
    message += ">(";
    message += RarityReportArticle(rarity);
    message += " ";
    message += rarity_name;
    message += " ";
    message.append(petal_name);
    message += " has been ";
    message.append(done);
    message += " by ";
    message.append(doer);
    message += ")";

    const SChatEntry* chat = SubmitChat(nullptr, { 0.f, 0.f }, EChatFlag::Server, 0, "Server", message);
    if (!chat) return false;
    network->BroadcastChat(*chat);
    return true;
}

bool CServer::BroadcastMobReport(std::string_view action, ERarity rarity, std::string_view mob_name)
{
    if (action.empty() || mob_name.empty() || !IsKnownRarity(rarity)) return false;

    INetworkModule* network = GetNetworkModule();
    if (!network) return false;

    std::string rarity_name(GetRarityName(rarity));
    std::string message;
    message.reserve(rarity_name.size() * 2 + mob_name.size() + action.size() + 24);
    message += "<";
    message += rarity_name;
    message += ">(";
    message += RarityReportArticle(rarity);
    message += " ";
    message += rarity_name;
    message += " ";
    message.append(mob_name);
    message += " ";
    message.append(action);
    message += ")";

    const SChatEntry* chat = SubmitChat(nullptr, { 0.f, 0.f }, EChatFlag::Server, 0, "Server", message);
    if (!chat) return false;
    network->BroadcastChat(*chat);
    return true;
}

bool CServer::BroadcastMobSpawnReport(CGameWorld& source_world, std::string_view action, ERarity rarity,
                                      std::string_view mob_name)
{
    if (action.empty() || mob_name.empty() || !IsKnownRarity(rarity) || !m_p_world_module) return false;

    INetworkModule* network = GetNetworkModule();
    if (!network) return false;

    std::string rarity_name(GetRarityName(rarity));
    std::string message;
    message.reserve(rarity_name.size() * 2 + mob_name.size() + action.size() + 24);
    message += "<";
    message += rarity_name;
    message += ">(";
    message += RarityReportArticle(rarity);
    message += " ";
    message += rarity_name;
    message += " ";
    message.append(mob_name);
    message += " ";
    message.append(action);
    message += ")";

    bool sent = false;
    for (const auto& world : m_p_world_module->GetWorlds())
    {
        if (!world) continue;

        std::string world_message = message;
        if (world.get() != &source_world)
        {
            world_message += " <";
            world_message += rarity_name;
            world_message += ">(somewhere)";
        }

        const SChatEntry* chat = SubmitChat(world.get(), { 0.f, 0.f }, EChatFlag::Server, 0, "Server", world_message);
        if (!chat) continue;
        network->BroadcastChat(*chat);
        sent = true;
    }
    return sent;
}

void CServer::Run()
{
    const float dt = game_config::server_fixed_dt;
    if (!std::isfinite(dt) || dt <= 0.f)
    {
        LOG_FATAL("server", "server_fixed_dt must be finite and greater than zero");
        m_running = false;
        return;
    }

    const auto tick_interval =
        std::max(server_clock::duration{ 1 },
                 std::chrono::duration_cast<server_clock::duration>(std::chrono::duration<double>(dt)));
    const double target_tick_ms = std::chrono::duration<double, std::milli>(tick_interval).count();
    const double slow_tick_ms = std::max(0.0, static_cast<double>(game_config::slow_tick_profile_ms));
    auto next_tick = server_clock::now();
    SPerformanceWindow window;
    window.Reset(next_tick);
    std::uint64_t total_overruns = 0;
    std::uint64_t total_catch_up_ticks = 0;
    std::uint64_t total_dropped_ticks = 0;

    m_performance_telemetry = {};
    m_performance_telemetry.target_tps = 1.0 / static_cast<double>(dt);

    auto publish_telemetry = [this, &window, &total_overruns, &total_catch_up_ticks,
                              &total_dropped_ticks](server_clock::time_point now, bool write_log) {
        if (window.ticks == 0) return;

        const double sample_seconds = std::chrono::duration<double>(now - window.start).count();
        if (sample_seconds <= 0.0) return;

        m_performance_telemetry.actual_tps = static_cast<double>(window.ticks) / sample_seconds;
        m_performance_telemetry.sample_seconds = sample_seconds;
        m_performance_telemetry.average_tick_ms = window.tick_ms_sum / static_cast<double>(window.ticks);
        m_performance_telemetry.max_tick_ms = window.max_tick_ms;
        m_performance_telemetry.max_schedule_lag_ms = window.max_schedule_lag_ms;
        m_performance_telemetry.ticks_in_sample = window.ticks;
        m_performance_telemetry.overruns_in_sample = window.overruns;
        m_performance_telemetry.slow_ticks_in_sample = window.slow_ticks;
        m_performance_telemetry.catch_up_ticks_in_sample = window.catch_up_ticks;
        m_performance_telemetry.dropped_ticks_in_sample = window.dropped_ticks;
        m_performance_telemetry.total_overruns = total_overruns;
        m_performance_telemetry.total_catch_up_ticks = total_catch_up_ticks;
        m_performance_telemetry.total_dropped_ticks = total_dropped_ticks;
        m_performance_telemetry.world_count = 0;
        m_performance_telemetry.player_count = 0;
        m_performance_telemetry.entity_count = 0;
        m_performance_telemetry.active_entity_count = 0;

        if (m_p_world_module)
        {
            for (const auto& world : m_p_world_module->GetWorlds())
            {
                if (!world) continue;
                ++m_performance_telemetry.world_count;
                m_performance_telemetry.player_count += world->GetPlayerCount();
                m_performance_telemetry.entity_count += world->GetEntityCount();
                m_performance_telemetry.active_entity_count += world->GetLastActiveEntityCount();
            }
        }

        if (write_log)
        {
            std::ostringstream message;
            message << std::fixed << std::setprecision(2) << "tps=" << m_performance_telemetry.actual_tps << "/"
                    << m_performance_telemetry.target_tps << " tick_ms(last/avg/max)="
                    << m_performance_telemetry.last_tick_ms << "/" << m_performance_telemetry.average_tick_ms << "/"
                    << m_performance_telemetry.max_tick_ms << " lag_max_ms="
                    << m_performance_telemetry.max_schedule_lag_ms << " overruns=" << window.overruns
                    << " slow_ticks=" << window.slow_ticks << " catch_up=" << window.catch_up_ticks
                    << " dropped=" << window.dropped_ticks << " worlds=" << m_performance_telemetry.world_count
                    << " players=" << m_performance_telemetry.player_count
                    << " entities(active/total)=" << m_performance_telemetry.active_entity_count << "/"
                    << m_performance_telemetry.entity_count;
            LOG_INFO("performance", message.str());
        }

        window.Reset(now);
    };

    while (m_running)
    {
        auto now = server_clock::now();
        if (now < next_tick)
        {
            std::this_thread::sleep_until(next_tick);
            continue;
        }

        std::uint64_t ticks_this_batch = 0;
        do
        {
            const auto tick_start = server_clock::now();
            const double schedule_lag_ms =
                std::chrono::duration<double, std::milli>(tick_start - next_tick).count();
            window.max_schedule_lag_ms = std::max(window.max_schedule_lag_ms, std::max(0.0, schedule_lag_ms));
            if (ticks_this_batch > 0)
            {
                ++window.catch_up_ticks;
                ++total_catch_up_ticks;
            }

            bool tick_completed = false;
            try
            {
                report::ProcessAsyncResults();
                TickTitanForgeCooldowns(dt);
                for (auto& module : m_modules)
                {
                    module->Tick(dt);
                }
                ++m_tick;
                ProcessHotReloadRequest();
                tick_completed = true;
            } catch (const std::exception& e)
            {
                LOG_FATAL("server", std::string("Unhandled exception in server tick: ") + e.what());
                m_running = false;
            } catch (...)
            {
                LOG_FATAL("server", "Unhandled unknown exception in server tick");
                m_running = false;
            }

            const auto tick_end = server_clock::now();
            const double tick_ms = std::chrono::duration<double, std::milli>(tick_end - tick_start).count();
            m_performance_telemetry.last_tick_ms = tick_ms;
            if (tick_completed)
            {
                ++window.ticks;
                window.tick_ms_sum += tick_ms;
                window.max_tick_ms = std::max(window.max_tick_ms, tick_ms);
                if (tick_ms > target_tick_ms)
                {
                    ++window.overruns;
                    ++total_overruns;
                }
                if (slow_tick_ms > 0.0 && tick_ms >= slow_tick_ms) ++window.slow_ticks;
            }

            ++ticks_this_batch;
            next_tick += tick_interval;
            now = tick_end;
        } while (m_running && now >= next_tick && ticks_this_batch < max_ticks_per_batch);

        if (m_running && now >= next_tick)
        {
            const auto missed_deadlines = (now - next_tick) / tick_interval + 1;
            const auto dropped_ticks = static_cast<std::uint64_t>(missed_deadlines);
            window.dropped_ticks += dropped_ticks;
            total_dropped_ticks += dropped_ticks;
            next_tick += tick_interval * missed_deadlines;
        }

        now = server_clock::now();
        if (now - window.start >= performance_sample_interval) publish_telemetry(now, true);
    }

    publish_telemetry(server_clock::now(), false);
}

void CServer::ShutDown()
{
    if (m_shutdown) return;
    m_shutdown = true;
    for (auto& module : m_modules)
    {
        module->ShutDown();
    }
    if (m_p_data_store)
    {
        std::string persistence_error;
        if (!m_p_data_store->ShutDown(persistence_error)) LOG_ERROR("persistence", persistence_error);
    }
    m_running = false;
}
