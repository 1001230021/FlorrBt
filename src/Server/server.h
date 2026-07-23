#pragma once
#include "../Engine/console.h"
#include "../Shared/network_msg.h"
#include "../Shared/rarity.h"
#include "Module/module.h"
#include <SFML/System/Vector2.hpp>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

class CGameWorld;
class CGameContext;
class INetworkModule;
class IWorldModule;

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
    std::vector<CGameWorld*> FindWorldsByMapName(const std::string& map_name) const;
    CGameWorld* FindRandomWorldByMapName(const std::string& map_name) const;
    INetworkModule* GetNetworkModule() const { return m_p_network_module; }
    CGameContext* GameContext() const { return m_p_game_context.get(); }
    const std::vector<SChatEntry>& GetChats() const { return m_chats; }
    const SChatEntry* SubmitChat(CGameWorld* world, sf::Vector2f pos, EChatFlag flag, uint32_t player_id,
                                 const std::string& player_name, const std::string& message,
                                 int target_player_id = -1);
    const SChatEntry* SubmitServerChat(const std::string& message);
    static bool MeetsPetalReportRarity(ERarity rarity, int min_rarity);
    bool BroadcastPetalReport(std::string_view done, ERarity rarity, std::string_view petal_name,
                              std::string_view doer);

  private:
    void ExecuteStartupCommands();
    void InstallLogSink();
    static CServer* s_p_instance;
    CConsole m_console;
    CGameWorld* m_p_main_world = nullptr;
    IWorldModule* m_p_world_module = nullptr;
    INetworkModule* m_p_network_module = nullptr;
    std::unique_ptr<CGameContext> m_p_game_context;
    std::vector<SChatEntry> m_chats;
    std::vector<std::unique_ptr<IModule>> m_modules;
    size_t m_log_sink_id = 0;
    std::shared_ptr<std::ofstream> m_log_file;
    std::shared_ptr<std::mutex> m_log_mutex;
    bool m_running = true;
};
