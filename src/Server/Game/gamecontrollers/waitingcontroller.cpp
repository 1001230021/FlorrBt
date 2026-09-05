#include "waitingcontroller.h"
#include "../../HotReload/snapshot_archive.h"
#include "../../Module/network_module.h"
#include "../../Module/world_module.h"
#include "../../../Shared/tools.h"
#include "../../server.h"
#include "../entities/flower.h"
#include "../gamecontext.h"
#include "../gameworld.h"
#include "../player.h"
#include "world_controller_factory.h"
#include <algorithm>
#include <cctype>
#include <chrono>
#include <filesystem>

namespace
{
florrbt::Vector2f PickWaitingSpawnPosition(CGameWorld& world)
{
    const FlorrBtMap* map = world.GetMap();
    if (!map) return { 0.f, 0.f };

    for (const FlorrBtMap::Checkpoint& checkpoint : map->checkpoints)
    {
        if (!checkpoint.is_respawn_area) continue;
        if (checkpoint.w <= 0.f || checkpoint.h <= 0.f) continue;
        return {
            checkpoint.x + GetLimitedRng(0.f, checkpoint.w),
            checkpoint.y + GetLimitedRng(0.f, checkpoint.h),
        };
    }

    return {
        static_cast<float>(map->width * map->tile_width) * 0.5f,
        static_cast<float>(map->height * map->tile_height) * 0.5f,
    };
}

std::string_view WaitingReleasePolicyKey(EWaitingReleasePolicy policy)
{
    switch (policy)
    {
    case EWaitingReleasePolicy::Disabled:
        return "disabled";
    case EWaitingReleasePolicy::MinimumPlayers:
        return "minimum_players";
    }
    return "disabled";
}

std::string ToLowerCopy(std::string text)
{
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return text;
}

// Resolves a /setting map value to a map path: "garden" -> "data/maps/garden.tmj".
std::string ResolveGameMapPath(const std::string& value)
{
    const bool looks_like_path = value.find('/') != std::string::npos ||
                                 value.find('\\') != std::string::npos ||
                                 (value.size() >= 4 && value.substr(value.size() - 4) == ".tmj");
    const std::string candidate = looks_like_path ? value : "data/maps/" + value + ".tmj";
    std::error_code ec;
    if (std::filesystem::exists(candidate, ec)) return candidate;
    return {};
}
} // namespace

CWaitingController::CWaitingController(SWaitingControllerConfig config) : m_config(config)
{
    m_config.minimum_player_count = std::max(1u, m_config.minimum_player_count);
}

void CWaitingController::OnActivate(CGameWorld& world) { MaintainPetalBans(world); }

void CWaitingController::OnDeactivate(CGameWorld& world)
{
    // Teardown hands any players still waiting over to the game world; without
    // a game world they are just un-banned (petal access restored).
    CGameWorld* destination_world = ResolveDestinationWorld(world);
    if (destination_world && destination_world != &world)
        ReleasePlayers(world, *destination_world);
    else
    {
        for (CPlayer* player : CollectWaitingPlayers(world))
            SetPlayerPetalsBanned(player, false);
    }
}

void CWaitingController::OnTick(CGameWorld& world, float dt)
{
    (void)dt;
    MaintainPetalBans(world);

    const size_t player_count = CountWaitingPlayers(world);
    if (!ShouldRelease(player_count)) return;

    CGameWorld* destination_world = ResolveOrCreateDestinationWorld(world);
    if (!destination_world) return;

    m_start_requested = false;
    ReleasePlayers(world, *destination_world);
}

void CWaitingController::CaptureSnapshot(CSnapshotWriter& writer) const
{
    writer.Field("release_policy", WaitingReleasePolicyKey(m_config.release_policy));
    writer.Field("minimum_player_count", m_config.minimum_player_count);
    writer.Field("destination_world_id", m_config.destination_world_id);
    writer.Field("game_map_path", m_config.game_map_path);
    writer.Field("game_controller_key", m_config.game_controller_key);
    writer.Field("room_code", m_config.room_code);
    writer.Field("start_requested", m_start_requested);
    m_config.game_controller_params.CaptureSnapshot(writer);
}

bool CWaitingController::RestoreSnapshot(const CSnapshotReader& reader, std::uint32_t version, std::string& error)
{
    if (version > SnapshotVersion())
    {
        error = "Unsupported waiting controller snapshot version " + std::to_string(version);
        return false;
    }

    const std::string policy = reader.String("release_policy", "disabled");
    if (policy == "disabled") m_config.release_policy = EWaitingReleasePolicy::Disabled;
    else if (policy == "minimum_players") m_config.release_policy = EWaitingReleasePolicy::MinimumPlayers;
    else
    {
        error = "Unknown waiting controller release policy " + policy;
        return false;
    }

    m_config.minimum_player_count = reader.UInt32("minimum_player_count", 1);
    m_config.destination_world_id =
        reader.UInt32("destination_world_id", SWaitingControllerConfig::invalid_world_id);
    m_config.game_map_path = reader.String("game_map_path", "");
    m_config.game_controller_key = reader.String("game_controller_key", "");
    m_config.room_code = reader.Int("room_code", -1);
    m_start_requested = reader.Bool("start_requested", false);
    m_config.game_controller_params.RestoreSnapshot(reader);
    if (m_config.minimum_player_count == 0)
    {
        error = "Waiting controller minimum player count must be positive";
        return false;
    }
    return true;
}

bool CWaitingController::ResolveReferences(CGameWorld& world, const IGameWorldResolver& worlds, std::string& error)
{
    MaintainPetalBans(world);
    if (m_config.release_policy == EWaitingReleasePolicy::Disabled) return true;

    if (m_config.destination_world_id != SWaitingControllerConfig::invalid_world_id)
    {
        CGameWorld* destination_world = worlds.FindWorldById(m_config.destination_world_id);
        if (!destination_world)
        {
            error = "Waiting controller destination world " + std::to_string(m_config.destination_world_id) +
                    " is unavailable";
            return false;
        }
        if (destination_world == &world)
        {
            error = "Waiting controller destination cannot be its own world";
            return false;
        }
        return true;
    }

    if (m_config.room_code < 0 || m_config.game_map_path.empty() || m_config.game_controller_key.empty())
    {
        error = "Waiting controller has no destination world or room creation blueprint";
        return false;
    }
    return true;
}

std::optional<florrbt::Vector2f> CWaitingController::SelectPlayerSpawn(CGameWorld& world, CPlayer& player,
                                                                  EPlayerSpawnReason reason)
{
    (void)player;
    if (reason != EPlayerSpawnReason::Login) return std::nullopt;
    return PickWaitingSpawnPosition(world);
}

void CWaitingController::OnPlayerEntityReady(CGameWorld& world, CPlayer& player, CPlayerFlower& flower,
                                              EPlayerSpawnReason reason)
{
    (void)world;
    (void)flower;
    (void)reason;
    SetPlayerPetalsBanned(&player, true);
}

void CWaitingController::OnPlayerEnteredWorld(CGameWorld& world, CPlayer& player)
{
    (void)world;
    SetPlayerPetalsBanned(&player, true);
}

void CWaitingController::OnPlayerLeftWorld(CGameWorld& world, CPlayer& player)
{
    (void)world;
    SetPlayerPetalsBanned(&player, false);
}

bool CWaitingController::HandlePlayerCommand(CGameWorld& world, CPlayer& player,
                                             const std::vector<std::string>& args)
{
    if (args.empty()) return false;

    std::string name = ToLowerCopy(args[0]);
    if (!name.empty() && name.front() == '/') name.erase(name.begin());

    if (name == "setting" || name == "set") return HandleSettingCommand(world, player, args);

    if (name != "start") return false;

    if (m_config.destination_world_id == SWaitingControllerConfig::invalid_world_id &&
        (m_config.game_map_path.empty() || m_config.game_controller_key.empty()))
    {
        SendSystemMessage(world, player, "this waiting room has no game world");
        return true;
    }

    CGameWorld* destination_world = ResolveOrCreateDestinationWorld(world);
    if (!destination_world)
    {
        SendSystemMessage(world, player, "failed to create game world");
        return true;
    }

    RequestStart();
    SendSystemMessage(world, player, "start requested: releasing to game world");
    return true;
}

bool CWaitingController::HandleSettingCommand(CGameWorld& world, CPlayer& player,
                                              const std::vector<std::string>& args)
{
    if (args.size() < 3)
    {
        SendSystemMessage(world, player,
                          "Usage: /setting [map|controller|policy|min_players|<controller param>] [value]");
        return true;
    }

    const std::string key = ToLowerCopy(args[1]);
    const std::string value = args[2];
    const bool started = m_config.destination_world_id != SWaitingControllerConfig::invalid_world_id;

    if (key == "map")
    {
        if (started)
        {
            SendSystemMessage(world, player, "game world already created; map setting ignored");
            return true;
        }
        const std::string map_path = ResolveGameMapPath(value);
        if (map_path.empty())
        {
            SendSystemMessage(world, player, "unknown map: " + value);
            return true;
        }
        m_config.game_map_path = map_path;
        // Derive the default controller from the map unless one was chosen.
        if (m_config.game_controller_key.empty())
            m_config.game_controller_key = std::string(map_declarations::ControllerKeyForMap(map_path));
        SendSystemMessage(world, player, "map set to " + map_path +
                                             (m_config.game_controller_key.empty()
                                                  ? ""
                                                  : " (controller " + m_config.game_controller_key + ")"));
        return true;
    }

    if (key == "controller" || key == "ctrl")
    {
        if (started)
        {
            SendSystemMessage(world, player, "game world already created; controller setting ignored");
            return true;
        }
        const std::string_view controller_key = map_declarations::ControllerKeyForName(ToLowerCopy(value));
        if (controller_key.empty() || controller_key == world_controller_keys::waiting)
        {
            SendSystemMessage(world, player, "unknown controller: " + value);
            return true;
        }
        m_config.game_controller_key = std::string(controller_key);
        SendSystemMessage(world, player, "controller set to " + std::string(controller_key));
        return true;
    }

    if (key == "policy" || key == "release")
    {
        const std::string policy = ToLowerCopy(value);
        if (policy == "disabled" || policy == "manual")
            m_config.release_policy = EWaitingReleasePolicy::Disabled;
        else if (policy == "minimum" || policy == "min_players" || policy == "auto")
            m_config.release_policy = EWaitingReleasePolicy::MinimumPlayers;
        else
        {
            SendSystemMessage(world, player, "unknown policy: " + value + " (disabled|minimum)");
            return true;
        }
        SendSystemMessage(world, player,
                          "release policy set to " + std::string(WaitingReleasePolicyKey(m_config.release_policy)));
        return true;
    }

    if (key == "min_players" || key == "minplayers" || key == "minimum")
    {
        std::uint32_t count = 0;
        try
        {
            count = static_cast<std::uint32_t>(std::stoul(value));
        } catch (...)
        {
            SendSystemMessage(world, player, "invalid player count: " + value);
            return true;
        }
        m_config.minimum_player_count = std::max(1u, count);
        SendSystemMessage(world, player,
                          "minimum player count set to " + std::to_string(m_config.minimum_player_count));
        return true;
    }

    // Generic controller parameters: any key declared by the destination
    // controller's ParamSchema can be set here. Values pass the 审查函数
    // first, then a type-level legality check, and are stored as overrides.
    const std::vector<SControllerParamInfo> schema =
        ControllerParamSchemaForKey(m_config.game_controller_key);
    for (const SControllerParamInfo& info : schema)
    {
        if (info.key != key) continue;

        if (started)
        {
            SendSystemMessage(world, player, "game world already created; " + key + " ignored");
            return true;
        }
        if (info.validate && !info.validate(value))
        {
            SendSystemMessage(world, player, "invalid value for " + key + ": " + value);
            return true;
        }
        if (!info.validate && !ControllerParamValueLegal(info.type, value))
        {
            SendSystemMessage(world, player, "invalid " + std::string(ControllerParamTypeName(info.type)) +
                                                 " value for " + key + ": " + value);
            return true;
        }
        m_config.game_controller_params.Set(key, value);
        SendSystemMessage(world, player, key + " set to " + value);
        return true;
    }

    SendSystemMessage(world, player, "unknown setting: " + key);
    return true;
}

bool CWaitingController::ShouldRelease(size_t player_count) const
{
    if (m_config.release_policy == EWaitingReleasePolicy::MinimumPlayers)
        return player_count >= static_cast<size_t>(m_config.minimum_player_count);
    // A manual start request releases whenever at least one player waits.
    if (m_start_requested) return player_count > 0;
    return false;
}

CGameWorld* CWaitingController::ResolveDestinationWorld(CGameWorld& world) const
{
    if (m_config.destination_world_id == SWaitingControllerConfig::invalid_world_id) return nullptr;
    CGameContext* context = world.GameContext();
    return context ? context->Worlds().FindWorldById(m_config.destination_world_id) : nullptr;
}

CGameWorld* CWaitingController::ResolveOrCreateDestinationWorld(CGameWorld& world)
{
    CGameWorld* destination_world = ResolveDestinationWorld(world);
    if (destination_world && destination_world != &world) return destination_world;

    if (m_config.room_code < 0) return nullptr;
    CServer* server = CServer::GetInstance();
    IWorldModule* world_module = server ? server->GetWorldModule() : nullptr;
    if (!world_module) return nullptr;

    CGameWorld* created_world = world_module->CreateRoomGameWorld(m_config.room_code);
    if (!created_world) return nullptr;
    m_config.destination_world_id = created_world->GetId();
    return created_world;
}

size_t CWaitingController::CountWaitingPlayers(CGameWorld& world) const { return CollectWaitingPlayers(world).size(); }

std::vector<CPlayer*> CWaitingController::CollectWaitingPlayers(CGameWorld& world) const
{
    std::vector<CPlayer*> result;
    CGameContext* context = world.GameContext();
    if (!context) return result;

    for (const auto& player : context->Players())
    {
        if (!player) continue;
        CEntity* entity = player->GetEntity();
        if (!entity || entity->GameWorld() != &world || entity->IsMarkedForDes() || entity->IsDead()) continue;
        if (!dynamic_cast<CPlayerFlower*>(entity)) continue;
        result.push_back(player.get());
    }
    return result;
}

void CWaitingController::SetPlayerPetalsBanned(CPlayer* player, bool banned) const
{
    if (!player) return;

    auto* flower = dynamic_cast<CPlayerFlower*>(player->GetEntity());
    if (!flower) return;

    auto& slots = flower->GetSlots();
    for (size_t i = 0; i < slots.size(); ++i)
    {
        flower->SetBanned(banned, static_cast<int>(i));
    }
    if (banned) flower->DestroyPetalEntities();
}

void CWaitingController::MaintainPetalBans(CGameWorld& world) const
{
    for (CPlayer* player : CollectWaitingPlayers(world))
    {
        SetPlayerPetalsBanned(player, true);
    }
}

void CWaitingController::ReleasePlayers(CGameWorld& world, CGameWorld& destination_world)
{
    std::vector<CPlayer*> players = CollectWaitingPlayers(world);
    for (CPlayer* player : players)
    {
        if (!player) continue;
        world.TransferPlayerEntityToWorld(*player, destination_world);
    }
}

void CWaitingController::SendSystemMessage(CGameWorld& world, CPlayer& player, const std::string& message) const
{
    CGameContext* context = world.GameContext();
    if (!context) return;

    CServer::SChatEntry entry;
    entry.flag = EChatFlag::Server;
    entry.player_id = 0;
    entry.target_player_id = static_cast<int>(player.GetId());
    entry.player_name = "Server";
    entry.message = message;
    entry.system_time = static_cast<uint32_t>(
        std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch())
            .count());
    context->Network().SendChatToPlayer(player, entry);
}
