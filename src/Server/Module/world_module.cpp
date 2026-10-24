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
#include <unordered_set>

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
    if (!m_worlds.empty() && m_worlds.front()) m_main_world_ref = MakeWorldRef(m_worlds.front().get());
}

IWorldModule::SWorldRef IWorldModule::MakeWorldRef(const CGameWorld* world) const
{
    if (!world) return {};
    return { world->GetId(), world->GetInstanceGeneration() };
}

CGameWorld* IWorldModule::ResolveWorld(const SWorldRef& reference) const
{
    if (!reference.IsValid()) return nullptr;
    for (const auto& world : m_worlds)
    {
        if (!world || world->GetId() != reference.id) continue;
        if (world->GetInstanceGeneration() == reference.instance_generation) return world.get();
    }
    return nullptr;
}

CGameWorld* IWorldModule::GetMainWorld() const { return ResolveWorld(m_main_world_ref); }

bool IWorldModule::SetMainWorld(std::uint32_t world_id)
{
    if (IsSimulationActive()) return false;
    for (const auto& candidate : m_worlds) if (candidate && candidate->IsTransitionActive()) return false;
    CGameWorld* world = FindWorldById(world_id);
    if (!world) return false;
    const SWorldRef ref = MakeWorldRef(world);
    for (const auto& [code, active] : m_rooms) if (active == ref) return false;
    for (const auto& [code, waiting] : m_room_waiting_worlds) if (waiting == ref) return false;
    m_main_world_ref = ref;
    return true;
}

bool IWorldModule::IsMainWorld(const CGameWorld& world) const
{
    return MakeWorldRef(&world) == m_main_world_ref;
}

size_t IWorldModule::OpenWorldInstancesForMap(std::string_view map_path) const
{
    const std::string name = NormalizeMapName(std::string(map_path));
    if (name == "garden") return game_config::garden_world_instances;
    if (name == "ant_hel") return game_config::ant_hel_world_instances;
    return game_config::open_world_instances_per_map;
}

bool IWorldModule::Init() { return true; }

IWorldModule::STopologySnapshot IWorldModule::CaptureTopology() const
{
    STopologySnapshot result;
    result.main_world = m_main_world_ref.id;
    for (const auto& world : m_worlds)
        if (world) result.worlds.push_back({world->GetId(), world->GetInstanceGeneration(), world->GetMapPath()});
    for (const auto& [code, active] : m_rooms)
    {
        if (!ResolveWorld(active)) continue;
        SRoomBlueprint room;
        room.code = code;
        room.active_world = active.id;
        const auto waiting = m_room_waiting_worlds.find(code);
        if (waiting != m_room_waiting_worlds.end() && ResolveWorld(waiting->second)) room.waiting_world = waiting->second.id;
        const auto timer = m_room_empty_seconds.find(code);
        if (timer != m_room_empty_seconds.end()) room.empty_seconds = timer->second;
        result.rooms.push_back(room);
    }
    std::sort(result.rooms.begin(), result.rooms.end(), [](const auto& a, const auto& b) { return a.code < b.code; });
    return result;
}

bool IWorldModule::ValidateTopology(const STopologySnapshot& snapshot, std::string& error) const
{
    if (IsSimulationActive()) { error = "Cannot replace topology during world simulation"; return false; }
    for (const auto& world : m_worlds)
        if (world && world->IsTransitionActive()) { error = "Cannot replace topology during a transition"; return false; }
    if (snapshot.worlds.empty() || snapshot.worlds.size() > 1024 || snapshot.rooms.size() > 1024)
    { error = "Invalid world topology size"; return false; }
    std::unordered_set<std::uint32_t> ids;
    std::unordered_set<std::uint64_t> generations;
    for (const auto& world : snapshot.worlds)
    {
        if (world.id == SWorldRef::invalid_id || world.generation == 0 ||
            world.generation == std::numeric_limits<std::uint64_t>::max() || world.map_path.empty() ||
            !ids.insert(world.id).second || !generations.insert(world.generation).second)
        { error = "Invalid or duplicate world identity in topology"; return false; }
    }
    if (!ids.contains(snapshot.main_world)) { error = "Main world missing from topology"; return false; }
    std::unordered_set<int> codes;
    std::unordered_set<std::uint32_t> room_worlds;
    for (const auto& room : snapshot.rooms)
    {
        if (room.code <= 0 || !codes.insert(room.code).second || !ids.contains(room.active_world) ||
            (room.waiting_world != SWorldRef::invalid_id && !ids.contains(room.waiting_world)) ||
            !std::isfinite(room.empty_seconds) || room.empty_seconds < 0.f)
        { error = "Invalid room registry in topology"; return false; }
        // Room teardown owns both worlds. Aliasing the main world or another
        // room would allow idle cleanup to delete a still-referenced world.
        if (room.active_world == snapshot.main_world || room.waiting_world == snapshot.main_world ||
            !room_worlds.insert(room.active_world).second ||
            (room.waiting_world != SWorldRef::invalid_id && room.waiting_world != room.active_world &&
             !room_worlds.insert(room.waiting_world).second))
        { error = "Room worlds must be exclusive and cannot own the main world"; return false; }
    }
    return true;
}

bool IWorldModule::RestoreTopology(const STopologySnapshot& snapshot, std::string& error)
{
    if (!ValidateTopology(snapshot, error)) return false;
    CGameContext* context = GetMainWorld() ? GetMainWorld()->GameContext() : nullptr;
    // Load every missing map before discarding existing worlds. Consumers
    // resolve the current main-world handle; no object address is pinned.
    std::unordered_map<std::uint32_t, std::unique_ptr<CGameWorld>> staged;
    for (const auto& blueprint : snapshot.worlds)
    {
        CGameWorld* existing = FindWorldById(blueprint.id);
        if (existing && existing->GetMapPath() == blueprint.map_path) continue;
        auto world = std::make_unique<CGameWorld>(blueprint.map_path, blueprint.id);
        if (!world->GetMap()) { error = "Cannot restore world map " + blueprint.map_path; return false; }
        staged.emplace(blueprint.id, std::move(world));
    }

    std::vector<std::unique_ptr<CGameWorld>> restored;
    restored.reserve(snapshot.worlds.size());
    for (const auto& blueprint : snapshot.worlds)
    {
        std::unique_ptr<CGameWorld> world;
        if (auto fresh = staged.find(blueprint.id); fresh != staged.end()) world = std::move(fresh->second);
        else
            for (auto& existing : m_worlds)
                if (existing && existing->GetId() == blueprint.id) { world = std::move(existing); break; }
        world->ClearEntitiesForRestore();
        world->SetGameContext(nullptr);
        if (!world->RestoreInstanceGeneration(blueprint.generation))
        { error = "Failed to restore validated world identity"; return false; }
        restored.push_back(std::move(world));
    }
    m_worlds = std::move(restored);
    for (const auto& world : m_worlds) world->SetGameContext(context);
    m_main_world_ref = MakeWorldRef(FindWorldById(snapshot.main_world));
    m_rooms.clear();
    m_room_waiting_worlds.clear();
    m_room_empty_seconds.clear();
    for (const auto& room : snapshot.rooms)
    {
        m_rooms.emplace(room.code, MakeWorldRef(FindWorldById(room.active_world)));
        if (room.waiting_world != SWorldRef::invalid_id)
            m_room_waiting_worlds.emplace(room.code, MakeWorldRef(FindWorldById(room.waiting_world)));
        m_room_empty_seconds.emplace(room.code, room.empty_seconds);
    }
    return true;
}

void IWorldModule::Tick(float dt)
{
    // Index-based: room controllers may create new worlds (game world on
    // /start) while worlds are ticking.
    {
        struct CGuard { bool& value; CGuard(bool& v) : value(v) { value = true; } ~CGuard() { value = false; } } guard(m_simulating_worlds);
        for (size_t i = 0; i < m_worlds.size(); ++i)
            if (m_worlds[i]) m_worlds[i]->Tick(dt);
    }
    // Every entity has completed its source-world simulation before any
    // queued transfer becomes visible to a destination-world tick.
    auto requests = std::move(m_deferred_transfers);
    m_deferred_transfers.clear();
    for (const auto& request : requests)
    {
        CGameWorld* source = FindWorldById(request.source.id);
        CGameWorld* destination = FindWorldById(request.destination.id);
        if (!source || !destination || source->GetInstanceGeneration() != request.source.generation ||
            destination->GetInstanceGeneration() != request.destination.generation || !source->GameContext()) continue;
        CPlayer* player = nullptr;
        for (const auto& candidate : source->GameContext()->Players())
            if (candidate && candidate->MatchesSession(request.player)) { player = candidate.get(); break; }
        if (!player || !player->GetEntity() || player->GetEntity()->GetHandle() != request.entity) continue;
        const auto result = CWorldTransfer::Move(*player, *source, *destination, request.position);
        if (!result) LOG_WARN("world", "Deferred world transfer rejected: " + result.error);
    }
    UpdateRoomLifecycle(dt);
}

bool IWorldModule::DeferWorldTransfer(const SDeferredWorldTransfer& request)
{
    if (!m_simulating_worlds) return false;
    // First portal/policy request wins for this session in this frame.
    if (std::none_of(m_deferred_transfers.begin(), m_deferred_transfers.end(), [&](const auto& pending) {
        return pending.player == request.player;
    })) m_deferred_transfers.push_back(request);
    return true;
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
    if (!controller_key.empty())
    {
        auto config = world->ExtensionConfig();
        config.squads = controller_key != world_controller_keys::waiting;
        config.weather_spawns = controller_key == "world_controller.weather_diurnal_open";
        world->ConfigureExtensions(config);
        world->SetController(CreateWorldController(controller_key, controller_params));
    }
    world->SetGameContext(context);

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

    m_rooms[room_code] = MakeWorldRef(waiting_world);
    m_room_waiting_worlds[room_code] = MakeWorldRef(waiting_world);
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

    CGameWorld* waiting_world = ResolveWorld(room_it->second);
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

    m_rooms[room_code] = MakeWorldRef(game_world);
    LOG_INFO("world", "Room " + std::to_string(room_code) + " started: game world " +
                          std::to_string(game_world->GetId()) + " (" + config.game_map_path + " / " +
                          config.game_controller_key + ")");
    return game_world;
}

CGameWorld* IWorldModule::FindRoomWorld(int room_code) const
{
    auto it = m_rooms.find(room_code);
    return it == m_rooms.end() ? nullptr : ResolveWorld(it->second);
}

bool IWorldModule::DestroyWorldById(std::uint32_t world_id)
{
    if (IsSimulationActive()) return false;
    for (size_t i = 0; i < m_worlds.size(); ++i)
    {
        CGameWorld* world = m_worlds[i].get();
        if (!world || world->GetId() != world_id) continue;
        if (world->IsTransitionActive()) return false;
        if (IsMainWorld(*world))
        {
            LOG_WARN("world", "Refusing to destroy the main world");
            return false;
        }
        const std::string map_path = world->GetMapPath();

        // Migrate while both controllers and the source handles are alive.
        // If an entity is already being removed, world teardown explicitly
        // schedules its player for respawn instead of retaining a raw pointer.
        CGameContext* context = world->GameContext();
        CGameWorld* destination = GetMainWorld();
        if (context)
        {
            for (const auto& player : context->Players())
            {
                if (!player) continue;
                CEntity* entity = player->GetEntity();
                if (!entity || entity->GameWorld() != world) continue;
                if (destination && world->TransferPlayerEntityToWorld(*player, *destination)) continue;
                // Forced world teardown is a separate committed departure.
                // A rejected transfer has not run any membership effects.
                if (IGameController* controller = world->GetController())
                    controller->OnPlayerDepartureCommitted(*world, *player, entity);
                entity = player->GetEntity();
                if (entity && entity->GameWorld() == world) entity->MarkForDestroy(EEntityRemovalReason::OwnerRemoved);
            }
        }

        // Unregister rooms pointing at this world, then drop the world.
        const SWorldRef destroyed_ref = MakeWorldRef(world);
        std::vector<SWorldRef> companion_worlds;
        for (auto it = m_rooms.begin(); it != m_rooms.end();)
        {
            if (it->second == destroyed_ref)
            {
                const int code = it->first;
                const auto waiting = m_room_waiting_worlds.find(code);
                if (waiting != m_room_waiting_worlds.end())
                {
                    if (!(waiting->second == destroyed_ref)) companion_worlds.push_back(waiting->second);
                    m_room_waiting_worlds.erase(waiting);
                }
                m_room_empty_seconds.erase(code);
                it = m_rooms.erase(it);
            }
            else ++it;
        }
        for (auto it = m_room_waiting_worlds.begin(); it != m_room_waiting_worlds.end();)
        {
            if (it->second == destroyed_ref) it = m_room_waiting_worlds.erase(it);
            else ++it;
        }
        m_worlds.erase(m_worlds.begin() + static_cast<std::ptrdiff_t>(i));
        LOG_INFO("world", "Destroyed world id=" + std::to_string(world_id) + " (" + map_path + ")");
        // Close the room's retained waiting world through the same player
        // migration path. Resolve the generation again after leave hooks.
        for (const SWorldRef& companion : companion_worlds)
            if (CGameWorld* remaining = ResolveWorld(companion)) DestroyWorldById(remaining->GetId());
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
    for (const auto& [room_code, active_ref] : m_rooms)
    {
        CGameWorld* active_world = ResolveWorld(active_ref);
        auto waiting_it = m_room_waiting_worlds.find(room_code);
        CGameWorld* waiting_world = waiting_it != m_room_waiting_worlds.end() ? ResolveWorld(waiting_it->second) : nullptr;

        const bool has_separate_waiting = waiting_world && waiting_world != active_world;
        const size_t waiting_players = has_separate_waiting ? waiting_world->GetPlayerCount() : 0;
        const size_t player_count = (active_world ? active_world->GetPlayerCount() : 0) + waiting_players;

        // Retiring an empty waiting room does not depend on whether its game
        // is still occupied. Never tear down a waiting room with players in it.
        if (has_separate_waiting && waiting_players == 0) retired_waiting_worlds.push_back(room_code);

        if (player_count > 0)
        {
            m_room_empty_seconds[room_code] = 0.f;
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

    CGameWorld* waiting_world = ResolveWorld(waiting_it->second);
    if (waiting_world && IsMainWorld(*waiting_world))
    {
        LOG_ERROR("world", "Refusing to retire the main world through a room reference");
        return;
    }
    m_room_waiting_worlds.erase(waiting_it);
    if (!waiting_world) return;
    const std::uint32_t waiting_world_id = waiting_world->GetId();

    auto it = std::find_if(m_worlds.begin(), m_worlds.end(), [waiting_world](const std::unique_ptr<CGameWorld>& candidate) {
        return candidate.get() == waiting_world;
    });
    if (it != m_worlds.end()) m_worlds.erase(it);
    LOG_INFO("world", "Room " + std::to_string(room_code) + " waiting world " +
                          std::to_string(waiting_world_id) + " retired");
}

void IWorldModule::DestroyRoom(int room_code)
{
    auto room_it = m_rooms.find(room_code);
    if (room_it == m_rooms.end()) return;
    CGameWorld* active_world = ResolveWorld(room_it->second);
    m_rooms.erase(room_it);

    auto waiting_it = m_room_waiting_worlds.find(room_code);
    CGameWorld* waiting_world =
        waiting_it != m_room_waiting_worlds.end() ? ResolveWorld(waiting_it->second) : nullptr;
    if (waiting_it != m_room_waiting_worlds.end()) m_room_waiting_worlds.erase(waiting_it);
    m_room_empty_seconds.erase(room_code);

    std::vector<CGameWorld*> doomed_worlds;
    if (waiting_world) doomed_worlds.push_back(waiting_world);
    if (active_world && active_world != waiting_world) doomed_worlds.push_back(active_world);

    for (CGameWorld* world : doomed_worlds)
    {
        if (!world) continue;
        if (IsMainWorld(*world))
        {
            LOG_ERROR("world", "Refusing to destroy the main world through a room reference");
            continue;
        }
        auto it = std::find_if(m_worlds.begin(), m_worlds.end(),
                               [world](const std::unique_ptr<CGameWorld>& candidate) { return candidate.get() == world; });
        if (it != m_worlds.end()) m_worlds.erase(it);
    }
    LOG_INFO("world", "Room " + std::to_string(room_code) + " destroyed (empty for " +
                          std::to_string(game_config::room_idle_destroy_seconds) + "s)");
}
