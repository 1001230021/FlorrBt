#include "server.h"
#include "../Engine/account_data.h"
#include "Game/gamecontext.h"
#include "Game/entities/mob.h"
#include "Game/entities/petals/petals_behavior.h"
#include "Module/console_module.h"
#include "Module/network_module.h"
#include "Module/server_gui_module.h"
#include "Module/world_module.h"
#include "report.h"
#include "../Engine/logger.h"
#include "../Shared/drop_rate.h"
#include "../Shared/game_config.h"
#include <algorithm>
#include <chrono>
#include <cctype>
#include <exception>
#include <filesystem>
#include <fstream>
#include <random>
#include <stdexcept>
#include <SFML/System/Clock.hpp>
#include <SFML/System/Sleep.hpp>

namespace
{
std::string GenerateRconPassword()
{
    static const std::string upper = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    static const std::string lower = "abcdefghijklmnopqrstuvwxyz";
    static const std::string digits = "0123456789";
    static const std::string all = upper + lower + digits;

    std::random_device random_device;
    std::mt19937 rng(random_device());
    auto pick = [&rng](const std::string& pool)
    {
        std::uniform_int_distribution<size_t> dist(0, pool.size() - 1);
        return pool[dist(rng)];
    };

    std::string password;
    password.reserve(6);
    password.push_back(pick(upper));
    password.push_back(pick(lower));
    password.push_back(pick(digits));
    while (password.size() < 6)
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

}

CServer* CServer::s_p_instance = nullptr;

CServer::CServer()
{
    s_p_instance = this;
    m_modules.emplace_back(std::make_unique<IConsoleModule>(m_console));
    m_modules.emplace_back(std::make_unique<IServerGuiModule>(m_console));

    auto world_mod = std::make_unique<IWorldModule>();
    m_p_world_module = world_mod.get();
    auto& worlds = world_mod->GetWorlds();

    CGameWorld& world = *worlds[0];
    m_p_main_world = &world;
    m_modules.emplace_back(std::move(world_mod));
    auto network_mod = std::make_unique<INetworkModule>(world);
    m_p_network_module = network_mod.get();
    m_modules.emplace_back(std::move(network_mod));
    m_p_game_context = std::make_unique<CGameContext>(world, *m_p_network_module);
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
    RegisterPetals();
    RegisterMobs();
    RegisterDropRates();
    m_console.InstallCommands();
    ExecuteStartupCommands();
    InstallLogSink();
    EnsureRconPasswordInitialized();

    std::string account_error;
    if (!CAccountDataStore::Load(game_config::account_data_path, &account_error))
    {
        LOG_FATAL("account", "Failed to load account data: " + account_error);
        m_running = false;
        return;
    }
    if (!account_error.empty()) LOG_WARN("account", account_error);

    for (auto& module : m_modules)
    {
        if (!module->Init())
        {
            m_running = false;
            return;
        }
    }
}

std::vector<CGameWorld*> CServer::FindWorldsByMapName(const std::string& map_name) const
{
    return m_p_world_module ? m_p_world_module->FindWorldsByMapName(map_name) : std::vector<CGameWorld*>{};
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
            LOG_WARN("server", "Failed to create log directory: " + log_path.parent_path().string() + " (" + ec.message() + ")");
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
    m_log_sink_id = CLogger::AddSink([file, mutex](const std::string& sender, ELogPriority priority, const std::string& msg)
    {
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
        while (begin < line.size() && std::isspace(static_cast<unsigned char>(line[begin]))) ++begin;
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
    constexpr size_t max_saved_chats = 256;
    if (m_chats.size() > max_saved_chats)
        m_chats.erase(m_chats.begin(), m_chats.begin() + static_cast<std::ptrdiff_t>(m_chats.size() - max_saved_chats));

    return &m_chats.back();
}

const CServer::SChatEntry* CServer::SubmitServerChat(const std::string& message)
{
    return SubmitChat(nullptr, {0.f, 0.f}, EChatFlag::Server, 0, "Server", message);
}

void CServer::Run()
{
    const float dt = game_config::server_fixed_dt;
    const sf::Time targetFrameTime = sf::seconds(dt);

    while (m_running)
    {
        sf::Clock frameClock;

        try
        {
            report::ProcessAsyncResults();
            for (auto& module : m_modules)
            {
                module->Tick(dt);
            }
        }
        catch (const std::exception& e)
        {
            LOG_FATAL("server", std::string("Unhandled exception in server tick: ") + e.what());
            m_running = false;
        }
        catch (...)
        {
            LOG_FATAL("server", "Unhandled unknown exception in server tick");
            m_running = false;
        }

        sf::Time elapsed = frameClock.getElapsedTime();
        if (elapsed < targetFrameTime) sf::sleep(targetFrameTime - elapsed);
    }
}

void CServer::ShutDown()
{
    CAccountDataStore::Save();
    for (auto& module : m_modules)
    {
        module->ShutDown();
    }
    m_running = false;
}
