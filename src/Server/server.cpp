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
#include "../Shared/drop_rate.h"
#include "../Shared/game_config.h"
#include <algorithm>
#include <chrono>
#include <cctype>
#include <fstream>
#include <stdexcept>
#include <SFML/System/Clock.hpp>
#include <SFML/System/Sleep.hpp>

CServer* CServer::s_p_instance = nullptr;

CServer::CServer()
{
    s_p_instance = this;
    m_modules.emplace_back(std::make_unique<IConsoleModule>(m_console));
    m_modules.emplace_back(std::make_unique<IServerGuiModule>(m_console));

    auto world_mod = std::make_unique<IWorldModule>();
    auto& worlds = world_mod->GetWorlds();

    CGameWorld& world = *worlds[0];
    m_p_main_world = &world;
    m_modules.emplace_back(std::move(world_mod));
    auto network_mod = std::make_unique<INetworkModule>(world);
    m_p_network_module = network_mod.get();
    m_modules.emplace_back(std::move(network_mod));
    m_p_game_context = std::make_unique<CGameContext>(world, *m_p_network_module);
    world.SetGameContext(m_p_game_context.get());
}

CServer::~CServer()
{
    s_p_instance = nullptr;
}

void CServer::Init()
{
    RegisterPetals();
    RegisterMobs();
    RegisterDropRates();
    m_console.InstallCommands();
    ExecuteStartupCommands();

    std::string account_error;
    if (!CAccountDataStore::Load(game_config::account_data_path, &account_error))
    {
        LOG_FATAL("account", "Failed to load account data: " + account_error);
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

        report::ProcessAsyncResults();
        for (auto& module : m_modules)
            module->Tick(dt);

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
