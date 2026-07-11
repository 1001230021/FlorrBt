#pragma once
#include "../Engine/console.h"
#include "../Shared/network_msg.h"
#include "Module/module.h"
#include <SFML/System/Vector2.hpp>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

class CGameWorld;
class CGameContext;
class INetworkModule;

class CServer
{
  public:
    struct SChatEntry
    {
        CGameWorld* world = nullptr;
        sf::Vector2f pos = {0.f, 0.f};
        EChatFlag flag = EChatFlag::Global;
        uint32_t player_id = 0;
        int target_player_id = -1;
        std::string player_name;
        uint32_t system_time = 0;
        std::string message;
    };

    static CServer* GetInstance() { return s_p_instance; }
    CServer();
    ~CServer();

    void Init();
    void Run();
    void ShutDown();
    void RequestStop() { m_running = false; }
    bool IsRunning() const { return m_running; }
    CConsole& GetConsole() { return m_console; }
    CGameWorld* GetMainWorld() const { return m_p_main_world; }
    INetworkModule* GetNetworkModule() const { return m_p_network_module; }
    CGameContext* GameContext() const { return m_p_game_context.get(); }
    const std::vector<SChatEntry>& GetChats() const { return m_chats; }
    const SChatEntry* SubmitChat(CGameWorld* world, sf::Vector2f pos, EChatFlag flag, uint32_t player_id,
                                 const std::string& player_name, const std::string& message,
                                 int target_player_id = -1);
    const SChatEntry* SubmitServerChat(const std::string& message);

  private:
    void ExecuteStartupCommands();
    static CServer* s_p_instance;
    CConsole m_console;
    CGameWorld* m_p_main_world = nullptr;
    INetworkModule* m_p_network_module = nullptr;
    std::unique_ptr<CGameContext> m_p_game_context;
    std::vector<SChatEntry> m_chats;
    std::vector<std::unique_ptr<IModule>> m_modules;
    bool m_running = true;
};
