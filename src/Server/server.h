#pragma once
#include "../Engine/console.h"
#include "../Shared/network_msg.h"
#include "../Shared/petal_type.h"
#include "../Shared/rarity.h"
#include "Game/game_services.h"
#include "Module/module.h"
#include <SFML/System/Vector2.hpp>
#include <array>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

class CGameWorld;
class CGameContext;
class INetworkModule;
class IWorldModule;
class IServerDataStore;
class IUniquePetalRegistry;

class CServer : public IGameEventSink, public IGameWorldResolver
{
  public:
    using TTitanForgeCooldowns = std::array<float, petal_type_names.size()>;

    static constexpr int hot_reload_exit_code = 42;
    static constexpr int restore_failed_exit_code = 43;

    struct SPerformanceTelemetry
    {
        double target_tps = 0.0;
        double actual_tps = 0.0;
        double sample_seconds = 0.0;
        double last_tick_ms = 0.0;
        double average_tick_ms = 0.0;
        double max_tick_ms = 0.0;
        double max_schedule_lag_ms = 0.0;
        std::uint64_t ticks_in_sample = 0;
        std::uint64_t overruns_in_sample = 0;
        std::uint64_t slow_ticks_in_sample = 0;
        std::uint64_t catch_up_ticks_in_sample = 0;
        std::uint64_t dropped_ticks_in_sample = 0;
        std::uint64_t total_overruns = 0;
        std::uint64_t total_catch_up_ticks = 0;
        std::uint64_t total_dropped_ticks = 0;
        std::size_t world_count = 0;
        std::size_t player_count = 0;
        std::size_t entity_count = 0;
        std::size_t active_entity_count = 0;
    };

    struct SChatEntry
    {
        CGameWorld* world = nullptr;
        sf::Vector2f pos = { 0.f, 0.f };
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
    void RequestHotReload(std::string candidate = {});
    void SetRestoreSnapshotPath(std::filesystem::path path) { m_restore_snapshot_path = std::move(path); }
    void SetReadyFilePath(std::filesystem::path path) { m_ready_file_path = std::move(path); }
    bool IsRunning() const { return m_running; }
    int GetExitCode() const { return m_exit_code; }
    std::uint64_t GetTick() const { return m_tick; }
    const SPerformanceTelemetry& GetPerformanceTelemetry() const { return m_performance_telemetry; }
    void SetTickForRestore(std::uint64_t tick) { m_tick = tick; }
    CConsole& GetConsole() { return m_console; }
    CGameWorld* GetMainWorld() const { return m_p_main_world; }
    CGameWorld* FindWorldById(std::uint32_t world_id) const override;
    std::vector<CGameWorld*> GetWorlds() const;
    std::vector<CGameWorld*> FindWorldsByMapName(const std::string& map_name) const;
    CGameWorld* FindRandomWorldByMapName(const std::string& map_name) const;
    INetworkModule* GetNetworkModule() const { return m_p_network_module; }
    IServerDataStore* GetDataStore() const { return m_p_data_store.get(); }
    IUniquePetalRegistry* GetUniquePetalRegistry() const;
    bool CanTitanForgePetal(EPetalType type) const;
    void StartTitanForgeCooldown(EPetalType type);
    const TTitanForgeCooldowns& GetTitanForgeCooldowns() const { return m_titan_forge_cooldowns; }
    void RestoreTitanForgeCooldowns(TTitanForgeCooldowns cooldowns);
    void MergeLegacyTitanForgeCooldownForRestore(float cooldown);
    CGameContext* GameContext() const { return m_p_game_context.get(); }
    const std::vector<SChatEntry>& GetChats() const { return m_chats; }
    const SChatEntry* SubmitChat(CGameWorld* world, sf::Vector2f pos, EChatFlag flag, uint32_t player_id,
                                 const std::string& player_name, const std::string& message, int target_player_id = -1);
    const SChatEntry* SubmitServerChat(const std::string& message);
    static bool MeetsPetalReportRarity(ERarity rarity, int min_rarity);
    bool ShouldReportRarity(ERarity rarity, int minimum_rarity) const override;
    bool ReportMob(std::string_view action, ERarity rarity, std::string_view mob_name) override;
    bool ReportMobSpawn(CGameWorld& source_world, std::string_view action, ERarity rarity,
                        std::string_view mob_name) override;
    bool BroadcastPetalReport(std::string_view done, ERarity rarity, std::string_view petal_name,
                              std::string_view doer);
    bool BroadcastMobReport(std::string_view action, ERarity rarity, std::string_view mob_name);
    bool BroadcastMobSpawnReport(CGameWorld& source_world, std::string_view action, ERarity rarity,
                                 std::string_view mob_name);

  private:
    void ExecuteStartupCommands();
    void InstallLogSink();
    bool RestoreConfiguredSnapshot();
    bool PublishReadyFile();
    void ProcessHotReloadRequest();
    void TickTitanForgeCooldowns(float dt);
    static CServer* s_p_instance;
    CConsole m_console;
    CGameWorld* m_p_main_world = nullptr;
    IWorldModule* m_p_world_module = nullptr;
    INetworkModule* m_p_network_module = nullptr;
    std::unique_ptr<IServerDataStore> m_p_data_store;
    std::unique_ptr<CGameContext> m_p_game_context;
    std::vector<SChatEntry> m_chats;
    std::vector<std::unique_ptr<IModule>> m_modules;
    size_t m_log_sink_id = 0;
    std::shared_ptr<std::ofstream> m_log_file;
    std::shared_ptr<std::mutex> m_log_mutex;
    SPerformanceTelemetry m_performance_telemetry;
    bool m_running = true;
    bool m_shutdown = false;
    bool m_hot_reload_requested = false;
    int m_exit_code = 0;
    std::uint64_t m_tick = 0;
    TTitanForgeCooldowns m_titan_forge_cooldowns{};
    std::string m_hot_reload_candidate;
    std::filesystem::path m_restore_snapshot_path;
    std::filesystem::path m_ready_file_path;
};
