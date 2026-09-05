#include "world_module.h"
#include "../../Engine/logger.h"
#include "../../Shared/game_config.h"
#include "../../Shared/tools.h"
#include "../Game/gamecontext.h"
#include "../Game/player.h"
#include "../Game/gamecontrollers/world_controller_factory.h"
#include "../Game/gamecontrollers/waitingcontroller.h"
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <random>

namespace
{

std::string NormalizeMapName(std::string name)
{
    std::replace(name.begin(), name.end(), '\\', '/');
    std::filesystem::path path(name);
    if (path.has_extension()) name = path.stem().generic_string();
    else if (path.has_parent_path()) name = path.filename().generic_string();

    std::transform(name.begin(), name.end(), name.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return name;
}

} // namespace

IWorldModule::IWorldModule()
{
    m_worlds.reserve(map_declarations::kMaps.size() * 64);
    for (const map_declarations::SMapDeclaration& declaration : map_declarations::kMaps)
    {
        const size_t instance_count = std::clamp<size_t>(OpenWorldInstancesForMap(declaration.map_path), 1, 64);
        for (size_t instance = 0; instance < instance_count; ++instance)
        {
            auto world = std::make_unique<CGameWorld>(std::string(declaration.map_path),
                                                      static_cast<std::uint32_t>(m_worlds.size()));
            if (!world->GetMap())
            {
                LOG_ERROR("world", "Startup world skipped (map not loaded): " + std::string(declaration.map_path));
                continue;
            }
            m_worlds.emplace_back(std::move(world));
        }
    }
}

size_t IWorldModule::OpenWorldInstancesForMap(std::string_view map_path) const
{
    const std::string name = NormalizeMapName(std::string(map_path));
    if (name == "garden") return game_config::garden_world_instances;
    if (name == "ant_hel") return game_config::ant_hel_world_instances;
    return game_config::open_world_instances_per_map;
}

bool IWorldModule::Init() { return true; }

void IWorldModule::Tick(float dt)
{
    // Index-based: room controllers may create new worlds (game world on
    // /start) while worlds are ticking.
    for (size_t i = 0; i < m_worlds.size(); ++i)
    {
        if (m_worlds[i]) m_worlds[i]->Tick(dt);
    }
    UpdateRoomLifecycle(dt);
}

void IWorldModule::ShutDown() {}

CGameWorld* IWorldModule::FindWorldById(std::uint32_t world_id) const
{
    for (const auto& world : m_worlds)
    {
        if (world && world->GetId() == world_id) return world.get();
    }
    return nullptr;
}

std::vector<CGameWorld*> IWorldModule::FindWorldsByMapName(const std::string& map_name) const
{
    std::vector<CGameWorld*> result;
    const std::string target = NormalizeMapName(map_name);
    if (target.empty()) return result;

    for (const auto& world : m_worlds)
    {
        if (!world) continue;
        if (NormalizeMapName(world->GetMapName()) == target || NormalizeMapName(world->GetMapPath()) == target)
            result.push_back(world.get());
    }
    return result;
}

CGameWorld* IWorldModule::FindRandomWorldByMapName(const std::string& map_name) const
{
    std::vector<CGameWorld*> worlds = FindWorldsByMapName(map_name);
    if (worlds.empty()) return nullptr;

    std::uniform_int_distribution<size_t> dist(0, worlds.size() - 1);
    return worlds[dist(GetRng())];
}

CGameWorld* IWorldModule::CreateWorld(const std::string& map_path, std::string_view controller_key,
                                      const CControllerParams& controller_params)
{
    CGameContext* context = nullptr;
    for (const auto& world : m_worlds)
    {
        if (world && world->GameContext())
        {
            context = world->GameContext();
            break;
        }
    }
    if (!context) return nullptr;

    std::uint32_t next_id = 0;
    for (const auto& world : m_worlds)
        if (world) next_id = std::max(next_id, world->GetId() + 1);

    auto world = std::make_unique<CGameWorld>(map_path, next_id);
    if (!world->GetMap())
    {
        LOG_ERROR("world", "CreateWorld failed: cannot load map " + map_path);
        return nullptr;
    }
    world->SetGameContext(context);
    if (!controller_key.empty())
        world->SetController(CreateWorldController(controller_key, controller_params));

    CGameWorld* raw = world.get();
    m_worlds.emplace_back(std::move(world));
    return raw;
}

CGameWorld* IWorldModule::CreateRoomWorld(const std::string& map_path, std::string_view controller_key, int room_code)
{
    auto existing = m_rooms.find(room_code);
    if (existing != m_rooms.end())
    {
        LOG_WARN("world", "CreateRoomWorld: room code " + std::to_string(room_code) + " already in use");
        return nullptr;
    }

    // The room starts as a bare waiting world; the game world (map + named
    // controller) is created lazily when /start releases the players.
    CGameWorld* waiting_world = CreateWorld(game_config::room_waiting_map_path, world_controller_keys::waiting);
    if (!waiting_world) return nullptr;

    SWaitingControllerConfig config;
    config.release_policy = EWaitingReleasePolicy::Disabled; // start only via /start
    config.minimum_player_count = 1;
    config.destination_world_id = SWaitingControllerConfig::invalid_world_id;
    config.game_map_path = map_path;
    config.game_controller_key = std::string(controller_key);
    config.room_code = room_code;
    waiting_world->SetController(std::make_unique<CWaitingController>(config));

    m_rooms[room_code] = waiting_world;
    m_room_waiting_worlds[room_code] = waiting_world;
    LOG_INFO("world", "Room " + std::to_string(room_code) + " created (waiting world " +
                          std::to_string(waiting_world->GetId()) + " on " +
                          game_config::room_waiting_map_path + ", game world " + map_path + " / " +
                          std::string(controller_key) + " created on start)");
    return waiting_world;
}

CGameWorld* IWorldModule::CreateRoomGameWorld(int room_code)
{
    auto room_it = m_rooms.find(room_code);
    if (room_it == m_rooms.end()) return nullptr;

    CGameWorld* waiting_world = room_it->second;
    auto* waiting = waiting_world ? dynamic_cast<CWaitingController*>(waiting_world->GetController()) : nullptr;
    if (!waiting) return nullptr;

    const SWaitingControllerConfig& config = waiting->GetConfig();
    if (config.game_map_path.empty() || config.game_controller_key.empty())
    {
        LOG_ERROR("world", "Room " + std::to_string(room_code) + " has no game world blueprint");
        return nullptr;
    }

    CGameWorld* game_world = CreateWorld(config.game_map_path, config.game_controller_key,
                                         config.game_controller_params);
    if (!game_world)
    {
        LOG_ERROR("world", "Room " + std::to_string(room_code) + " failed to create game world on " +
                               config.game_map_path);
        return nullptr;
    }

    m_rooms[room_code] = game_world;
    LOG_INFO("world", "Room " + std::to_string(room_code) + " started: game world " +
                          std::to_string(game_world->GetId()) + " (" + config.game_map_path + " / " +
                          config.game_controller_key + ")");
    return game_world;
}

CGameWorld* IWorldModule::FindRoomWorld(int room_code) const
{
    auto it = m_rooms.find(room_code);
    return it == m_rooms.end() ? nullptr : it->second;
}

bool IWorldModule::DestroyWorldById(std::uint32_t world_id)
{
    for (size_t i = 0; i < m_worlds.size(); ++i)
    {
        CGameWorld* world = m_worlds[i].get();
        if (!world || world->GetId() != world_id) continue;

        // Release the world's players first: drop their entities so the
        // player lifecycle keeps running (they return to the lobby flow).
        CGameContext* context = world->GameContext();
        if (context)
        {
            for (const auto& player : context->Players())
            {
                if (!player) continue;
                CEntity* entity = player->GetEntity();
                if (entity && entity->GameWorld() == world)
                    entity->MarkForDestroy(EEntityRemovalReason::OwnerRemoved);
            }
        }

        // Unregister rooms pointing at this world, then drop the world.
        for (auto it = m_rooms.begin(); it != m_rooms.end();)
        {
            if (it->second == world) it = m_rooms.erase(it);
            else ++it;
        }
        for (auto it = m_room_waiting_worlds.begin(); it != m_room_waiting_worlds.end();)
        {
            if (it->second == world) it = m_room_waiting_worlds.erase(it);
            else ++it;
        }
        m_worlds.erase(m_worlds.begin() + static_cast<std::ptrdiff_t>(i));
        LOG_INFO("world", "Destroyed world id=" + std::to_string(world_id) + " (" + world->GetMapPath() + ")");
        return true;
    }
    LOG_WARN("world", "DestroyWorldById: world " + std::to_string(world_id) + " not found");
    return false;
}

int IWorldModule::NextRoomCode() const
{
    for (int attempt = 0; attempt < 64; ++attempt)
    {
        const int code = 1000 + static_cast<int>(GetLimitedRng(0.f, 9000.f));
        if (m_rooms.find(code) == m_rooms.end()) return code;
    }
    return -1;
}

void IWorldModule::UpdateRoomLifecycle(float dt)
{
    if (m_rooms.empty()) return;

    std::vector<int> idle_room_codes;
    std::vector<int> retired_waiting_worlds;
    for (const auto& [room_code, active_world] : m_rooms)
    {
        auto waiting_it = m_room_waiting_worlds.find(room_code);
        CGameWorld* waiting_world = waiting_it != m_room_waiting_worlds.end() ? waiting_it->second : nullptr;

        size_t player_count = active_world ? active_world->GetPlayerCount() : 0;
        if (waiting_world && waiting_world != active_world) player_count += waiting_world->GetPlayerCount();

        if (player_count > 0)
        {
            m_room_empty_seconds[room_code] = 0.f;
            continue;
        }

        // Started room: the waiting world has served its purpose, retire it as
        // soon as it empties (players were released into the game world).
        if (waiting_world && waiting_world != active_world)
        {
            retired_waiting_worlds.push_back(room_code);
            continue;
        }

        float& empty_seconds = m_room_empty_seconds[room_code];
        empty_seconds += dt;
        if (empty_seconds >= game_config::room_idle_destroy_seconds) idle_room_codes.push_back(room_code);
    }

    for (int room_code : retired_waiting_worlds)
        DestroyRoomWaitingWorld(room_code);
    for (int room_code : idle_room_codes)
        DestroyRoom(room_code);
}

void IWorldModule::DestroyRoomWaitingWorld(int room_code)
{
    auto waiting_it = m_room_waiting_worlds.find(room_code);
    if (waiting_it == m_room_waiting_worlds.end()) return;

    CGameWorld* waiting_world = waiting_it->second;
    m_room_waiting_worlds.erase(waiting_it);
    if (!waiting_world) return;

    // Hand any players still waiting over to the game world before teardown.
    if (IGameController* controller = waiting_world->GetController()) controller->OnDeactivate(*waiting_world);

    auto it = std::find_if(m_worlds.begin(), m_worlds.end(), [waiting_world](const std::unique_ptr<CGameWorld>& candidate) {
        return candidate.get() == waiting_world;
    });
    if (it != m_worlds.end()) m_worlds.erase(it);
    LOG_INFO("world", "Room " + std::to_string(room_code) + " waiting world " +
                          std::to_string(waiting_world->GetId()) + " retired");
}

void IWorldModule::DestroyRoom(int room_code)
{
    auto room_it = m_rooms.find(room_code);
    if (room_it == m_rooms.end()) return;
    CGameWorld* active_world = room_it->second;
    m_rooms.erase(room_it);

    auto waiting_it = m_room_waiting_worlds.find(room_code);
    CGameWorld* waiting_world = waiting_it != m_room_waiting_worlds.end() ? waiting_it->second : nullptr;
    if (waiting_it != m_room_waiting_worlds.end()) m_room_waiting_worlds.erase(waiting_it);
    m_room_empty_seconds.erase(room_code);

    std::vector<CGameWorld*> doomed_worlds;
    if (waiting_world) doomed_worlds.push_back(waiting_world);
    if (active_world && active_world != waiting_world) doomed_worlds.push_back(active_world);

    for (CGameWorld* world : doomed_worlds)
    {
        if (!world) continue;
        if (IGameController* controller = world->GetController()) controller->OnDeactivate(*world);
        auto it = std::find_if(m_worlds.begin(), m_worlds.end(),
                               [world](const std::unique_ptr<CGameWorld>& candidate) { return candidate.get() == world; });
        if (it != m_worlds.end()) m_worlds.erase(it);
    }
    LOG_INFO("world", "Room " + std::to_string(room_code) + " destroyed (empty for " +
                          std::to_string(game_config::room_idle_destroy_seconds) + "s)");
}
