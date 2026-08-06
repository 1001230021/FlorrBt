#include "../Engine/commands_registry.h"
#include "../Engine/console.h"
#include "../Engine/logger.h"
#include "../Shared/damage_type.h"
#include "../Shared/game_config.h"
#include "../Shared/rarity.h"
#include "../Shared/version.h"
#include "Game/entities/flower.h"
#include "Game/entities/mob.h"
#include "Game/entities/petals/petal.h"
#include "Game/gamecontext.h"
#include "Game/gameworld.h"
#include "Game/player.h"
#include "Module/network_module.h"
#include "Persistence/account_store.h"
#include "server.h"
#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <optional>
#include <sstream>
#include <string_view>

namespace
{
std::string ToLower(std::string_view text)
{
    std::string result;
    result.reserve(text.size());
    for (char ch : text)
    {
        result.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
    }
    return result;
}

std::optional<int> ParseInt(std::string_view text)
{
    int value = 0;
    const char* begin = text.data();
    const char* end = begin + text.size();
    auto [ptr, error] = std::from_chars(begin, end, value);
    if (error != std::errc{} || ptr != end) return std::nullopt;
    return value;
}

std::optional<float> ParseFloat(std::string_view text)
{
    float value = 0.f;
    const char* begin = text.data();
    const char* end = begin + text.size();
    auto [ptr, error] = std::from_chars(begin, end, value);
    if (error != std::errc{} || ptr != end) return std::nullopt;
    return value;
}

std::optional<std::uint32_t> ParseWorldId(std::string_view text)
{
    auto value = ParseInt(text);
    if (!value || *value < 0) return std::nullopt;
    return static_cast<std::uint32_t>(*value);
}

struct SWorldEntityRef
{
    std::uint32_t world_id = 0;
    int entity_id = -1;
};

std::optional<SWorldEntityRef> ParseWorldEntityRef(std::string_view text)
{
    const size_t separator = text.find(':');
    if (separator == std::string_view::npos) return std::nullopt;

    auto world_id = ParseWorldId(text.substr(0, separator));
    auto entity_id = ParseInt(text.substr(separator + 1));
    if (!world_id || !entity_id || *entity_id < 0) return std::nullopt;
    return SWorldEntityRef{ *world_id, *entity_id };
}

std::optional<ERarity> ParseRarity(std::string_view text)
{
    if (auto value = ParseInt(text))
    {
        ERarity parsed = static_cast<ERarity>(*value);
        if (IsKnownRarity(parsed)) return parsed;
        return std::nullopt;
    }

    const std::string rarity = ToLower(text);
    if (rarity == "common") return ERarity::Common;
    if (rarity == "unusual") return ERarity::Unusual;
    if (rarity == "rare") return ERarity::Rare;
    if (rarity == "epic") return ERarity::Epic;
    if (rarity == "legendary") return ERarity::Legendary;
    if (rarity == "mythic") return ERarity::Mythic;
    if (rarity == "ultra") return ERarity::Ultra;
    if (rarity == "super") return ERarity::Super;
    if (rarity == "eternal") return ERarity::Eternal;
    if (rarity == "unique") return ERarity::Unique;
    if (rarity == "primordial") return ERarity::Primordial;
    if (rarity == "ex" || rarity == "exotic") return ERarity::Exotic;
    return std::nullopt;
}

std::string RarityName(ERarity rarity)
{
    switch (rarity)
    {
    case ERarity::Common:
        return "Common";
    case ERarity::Unusual:
        return "Unusual";
    case ERarity::Rare:
        return "Rare";
    case ERarity::Epic:
        return "Epic";
    case ERarity::Legendary:
        return "Legendary";
    case ERarity::Mythic:
        return "Mythic";
    case ERarity::Ultra:
        return "Ultra";
    case ERarity::Super:
        return "Super";
    case ERarity::Eternal:
        return "Eternal";
    case ERarity::Unique:
        return "Unique";
    case ERarity::Primordial:
        return "Primordial";
    case ERarity::Exotic:
        return "Exotic";
    default:
        return "Null";
    }
}

const CPetalPrototype* FindPetalPrototypeByText(std::string_view text)
{
    if (auto value = ParseInt(text))
    {
        return FindPetalPrototype(static_cast<EPetalType>(*value));
    }

    const std::string target = ToLower(text);
    for (const auto& [type, proto] : PetalRegistry())
    {
        if (!proto) continue;
        if (MatchPetalTypeAlias(target, type) || ToLower(proto->m_name) == target) return proto;
    }
    return nullptr;
}

const CMobPrototype* FindMobPrototypeByText(std::string_view text)
{
    if (auto value = ParseInt(text))
    {
        return FindMobPrototype(static_cast<EMobType>(*value));
    }

    const std::string target = ToLower(text);
    for (const auto& [type, proto] : MobRegistry())
    {
        if (!proto) continue;
        if (MatchMobTypeAlias(target, type) || ToLower(proto->m_name) == target ||
            ToLower(GetMobTypeName(type)) == target)
            return proto;
    }
    return nullptr;
}

std::optional<sf::Vector2f> ParsePosition(const std::vector<std::string>& args, size_t begin)
{
    if (begin >= args.size()) return std::nullopt;

    const std::string& first = args[begin];
    size_t comma = first.find(',');
    if (comma != std::string::npos)
    {
        auto x = ParseFloat(std::string_view(first).substr(0, comma));
        auto y = ParseFloat(std::string_view(first).substr(comma + 1));
        if (x && y) return sf::Vector2f(*x, *y);
        return std::nullopt;
    }

    if (begin + 1 >= args.size()) return std::nullopt;

    auto x = ParseFloat(args[begin]);
    auto y = ParseFloat(args[begin + 1]);
    if (!x || !y) return std::nullopt;
    return sf::Vector2f(*x, *y);
}

int FindFirstEquipSlot(CFlower& flower)
{
    auto& slots = flower.GetSlots();
    for (size_t i = 0; i < slots.size(); ++i)
    {
        if (slots[i].m_available && !slots[i].m_banned && !slots[i].m_p_proto) return static_cast<int>(i);
    }
    return -1;
}

CPlayer* FindPlayerByName(CGameContext* context, std::string_view name)
{
    if (!context) return nullptr;
    const std::string target = ToLower(name);
    for (const auto& player : context->Players())
    {
        if (!player) continue;
        if (ToLower(player->GetName()) == target || ToLower(player->GetAccountName()) == target) return player.get();
    }
    return nullptr;
}

CPlayer* FindPlayerByIdOrName(CGameContext* context, std::string_view text)
{
    if (!context) return nullptr;
    auto* network = &context->Network();
    if (auto id = ParseInt(text)) return *id >= 0 ? network->FindPlayerById(static_cast<uint32_t>(*id)) : nullptr;
    return FindPlayerByName(context, text);
}

CEntity* FindEntityByCommandTarget(CServer* server, CGameContext* context, std::string_view text)
{
    if (!server || !context) return nullptr;
    if (auto ref = ParseWorldEntityRef(text))
    {
        CGameWorld* world = server->FindWorldById(ref->world_id);
        return world ? world->GetEntity(ref->entity_id) : nullptr;
    }
    if (text.find(':') != std::string_view::npos) return nullptr;
    if (CPlayer* player = FindPlayerByIdOrName(context, text)) return player->GetEntity();
    auto entity_id = ParseInt(text);
    return entity_id && *entity_id >= 0 ? context->World().GetEntity(*entity_id) : nullptr;
}

std::string JoinArgs(const std::vector<std::string>& args, size_t begin)
{
    std::string result;
    for (size_t i = begin; i < args.size(); ++i)
    {
        if (!result.empty()) result += " ";
        result += args[i];
    }
    return result;
}

std::string EntityCommandRef(const CEntity* entity)
{
    if (!entity) return "none";
    const CGameWorld* world = entity->GameWorld();
    return std::to_string(world ? world->GetId() : 0) + ":" + std::to_string(entity->m_id);
}

void LogWorldInfo(const CGameWorld& world)
{
    LOG_INFO("console", "id: " + std::to_string(world.GetId()) + ", map: " + world.GetMapName() +
                            ", players: " + std::to_string(world.GetPlayerCount()));
}

int TalentPointGainForCommandLevel(int level)
{
    if (level <= 1) return 0;

    int gain = game_config::player_talent_point_base_gain;
    const int minor_interval = game_config::player_talent_point_minor_level_interval;
    const int major_interval = game_config::player_talent_point_major_level_interval;
    if (minor_interval > 0 && level % minor_interval == 0) gain += game_config::player_talent_point_minor_bonus;
    if (major_interval > 0 && level % major_interval == 0) gain += game_config::player_talent_point_major_bonus;
    return gain;
}
} // namespace

REGISTER_CONSOLE_COMMAND_USAGE(quit, "quit", {}, {
    if (auto* server = CServer::GetInstance()) server->RequestStop();
    else LOG_FATAL("server", "Can't find instance of the server");
})

REGISTER_CONSOLE_COMMAND_USAGE(version, "version", {}, { LOG_INFO("console", std::string(florrbt::version_label)); })

REGISTER_CONSOLE_COMMAND_USAGE(perf, "perf", {}, {
    const auto* server = CServer::GetInstance();
    if (!server)
    {
        LOG_ERROR("performance", "Can't find instance of the server");
        return;
    }

    const CServer::SPerformanceTelemetry& telemetry = server->GetPerformanceTelemetry();
    const INetworkModule* network = server->GetNetworkModule();
    std::ostringstream message;
    message << std::fixed << std::setprecision(2) << "tps=" << telemetry.actual_tps << "/"
            << telemetry.target_tps << " tick_ms(last/avg/max)=" << telemetry.last_tick_ms << "/"
            << telemetry.average_tick_ms << "/" << telemetry.max_tick_ms << " lag_max_ms="
            << telemetry.max_schedule_lag_ms << " overruns=" << telemetry.overruns_in_sample
            << " slow_ticks=" << telemetry.slow_ticks_in_sample << " catch_up="
            << telemetry.catch_up_ticks_in_sample << " dropped=" << telemetry.dropped_ticks_in_sample
            << " worlds=" << telemetry.world_count << " players=" << telemetry.player_count
            << " entities(active/total)=" << telemetry.active_entity_count << "/" << telemetry.entity_count;
    if (network)
    {
        const INetworkModule::SSnapshotTelemetry& snapshot = network->GetSnapshotTelemetry();
        const double saved_percent =
            snapshot.full_snapshot_bytes > 0
                ? 100.0 * (1.0 - static_cast<double>(snapshot.actual_snapshot_bytes) /
                                     static_cast<double>(snapshot.full_snapshot_bytes))
                : 0.0;
        message << " snapshots(delta/full)=" << snapshot.delta_snapshots_queued << "/"
                << snapshot.full_snapshots_queued << " snapshot_bytes(full/actual/saved)="
                << snapshot.full_snapshot_bytes << "/" << snapshot.actual_snapshot_bytes << "/"
                << std::clamp(saved_percent, 0.0, 100.0) << "% snapshot_entities(changed/removed)="
                << snapshot.changed_entities << "/" << snapshot.removed_entities
                << " snapshot_build_ms(last/avg/max)=" << snapshot.last_build_ms << "/"
                << snapshot.average_build_ms << "/" << snapshot.max_build_ms
                << " snapshot_builds=" << snapshot.build_count;
    }
    if (game_config::world_tick_phase_profile_enabled)
    {
        constexpr std::array<const char*, CGameWorld::tick_phase_count> phase_names = {
            "active", "entities", "controller", "walls", "collisions", "cleanup"
        };
        std::array<double, CGameWorld::tick_phase_count> last_sum{};
        std::array<double, CGameWorld::tick_phase_count> average_sum{};
        std::array<double, CGameWorld::tick_phase_count> max_world{};
        std::uint64_t sample_count = 0;
        std::size_t profiled_worlds = 0;
        for (const CGameWorld* world : server->GetWorlds())
        {
            if (!world) continue;
            const CGameWorld::STickPhaseTelemetry& phases = world->GetTickPhaseTelemetry();
            if (phases.samples == 0) continue;
            ++profiled_worlds;
            sample_count = sample_count == 0 ? phases.samples : std::min(sample_count, phases.samples);
            for (std::size_t i = 0; i < CGameWorld::tick_phase_count; ++i)
            {
                last_sum[i] += phases.last_ms[i];
                average_sum[i] += phases.total_ms[i] / static_cast<double>(phases.samples);
                max_world[i] = std::max(max_world[i], phases.max_ms[i]);
            }
        }
        if (profiled_worlds > 0)
        {
            message << " world_phase_ms(last_sum/avg_sum/max_world)=";
            for (std::size_t i = 0; i < CGameWorld::tick_phase_count; ++i)
            {
                if (i > 0) message << ",";
                message << phase_names[i] << ":" << last_sum[i] << "/" << average_sum[i] << "/" << max_world[i];
            }
            message << " world_phase_samples=" << sample_count;
        }
    }
    LOG_INFO("performance", message.str());
})

REGISTER_CONSOLE_COMMAND_USAGE(hot_reload, "hot_reload [executable]", {}, {
    if (auto* server = CServer::GetInstance())
    {
        server->RequestHotReload(JoinArgs(args, 0));
    } else
    {
        LOG_ERROR("hot_reload", "Can't find instance of the server");
    }
})

REGISTER_CONSOLE_COMMAND_USAGE(echo, "echo <message...>", {}, {
    if (args.empty()) return;
    LOG_INFO("console", JoinArgs(args, 0));
})

REGISTER_CONSOLE_COMMAND_USAGE(repeat, "repeat <count> <command...>", {}, {
    if (args.size() < 2)
    {
        LOG_INFO("console", "Usage: repeat <count> <command...>");
        return;
    }

    auto times = ParseInt(args[0]);
    if (!times || *times <= 0)
    {
        LOG_WARN("console", "Invalid repeat count: " + args[0]);
        return;
    }
    const int max_repeat_count = std::max(1, game_config::console_repeat_max_count);
    if (*times > max_repeat_count)
    {
        LOG_WARN("console", "Repeat count is capped at " + std::to_string(max_repeat_count) + ".");
        return;
    }

    std::string command = JoinArgs(args, 1);
    if (ToLower(args[1]) == "repeat")
    {
        LOG_WARN("console", "Nested repeat is blocked.");
        return;
    }

    auto* server = CServer::GetInstance();
    if (!server) return;
    for (int i = 0; i < *times; ++i)
        server->GetConsole().ExecuteLine(command);
})

REGISTER_CONSOLE_COMMAND_USAGE(who, "who <player>", "player", {
    if (args.empty())
    {
        LOG_INFO("console", "Usage: who <player>");
        return;
    }

    auto* server = CServer::GetInstance();
    CGameContext* context = server ? server->GameContext() : nullptr;
    CPlayer* player = FindPlayerByIdOrName(context, args[0]);
    if (!player)
    {
        LOG_WARN("console", "Player not found: " + args[0]);
        return;
    }

    CEntity* entity = player->GetEntity();
    LOG_INFO("console", "name=" + player->GetName() + " account=" + player->GetAccountName() +
                            " player_id=" + std::to_string(player->GetId()) + " entity=" + EntityCommandRef(entity));
})

REGISTER_CONSOLE_COMMAND_USAGE(level_up, "level_up <player> <count>", "player", {
    if (args.size() < 2)
    {
        LOG_INFO("console", "Usage: level_up <player> <count>");
        return;
    }

    auto num = ParseInt(args[1]);
    if (!num || *num <= 0)
    {
        LOG_WARN("console", "Invalid level count: " + args[1]);
        return;
    }

    auto* server = CServer::GetInstance();
    CGameContext* context = server ? server->GameContext() : nullptr;
    CPlayer* player = FindPlayerByIdOrName(context, args[0]);
    if (!player)
    {
        LOG_WARN("console", "Player not found: " + args[0]);
        return;
    }

    auto* flower = dynamic_cast<CPlayerFlower*>(player->GetEntity());
    if (!flower)
    {
        LOG_WARN("console", "Player has no player flower: " + args[0]);
        return;
    }

    int gained_talent_points = 0;
    int old_level = flower->m_level;
    const int max_level = std::max(1, game_config::player_level_max);
    for (int i = 0; i < *num && flower->m_level < max_level; ++i)
    {
        ++flower->m_level;
        flower->m_exp = 0;
        gained_talent_points += TalentPointGainForCommandLevel(flower->m_level);
    }

    flower->RebuildFinalStats();
    if (gained_talent_points > 0) player->AddTalentPoints(gained_talent_points);
    if (player->IsAuthenticated())
        CAccountDataStore::SetProgress(player->GetAccountName(), flower->m_level, flower->m_exp);
    if (context) context->Network().QueueOwnerStateUpdate(*player);

    LOG_INFO("console", "Level " + player->GetName() + " " + std::to_string(old_level) + " -> " +
                            std::to_string(flower->m_level) + ", +" + std::to_string(gained_talent_points) + " TP.");
})

REGISTER_CONSOLE_COMMAND_USAGE(query_player_id, "query_player_id <player>", "player", {
    if (args.empty())
    {
        LOG_INFO("console", "Usage: query_player_id <player>");
        return;
    }

    auto* server = CServer::GetInstance();
    CGameContext* context = server ? server->GameContext() : nullptr;
    CPlayer* player = FindPlayerByIdOrName(context, args[0]);
    if (!player)
    {
        LOG_WARN("console", "Player not found: " + args[0]);
        return;
    }

    LOG_INFO("console", "name=" + player->GetName() + " account=" + player->GetAccountName() +
                            " player_id=" + std::to_string(player->GetId()));
})

REGISTER_CONSOLE_COMMAND_USAGE(query_entity_id, "query_entity_id <player>", "player", {
    if (args.empty())
    {
        LOG_INFO("console", "Usage: query_entity_id <player>");
        return;
    }

    auto* server = CServer::GetInstance();
    CGameContext* context = server ? server->GameContext() : nullptr;
    CPlayer* player = FindPlayerByIdOrName(context, args[0]);
    if (!player)
    {
        LOG_WARN("console", "Player not found: " + args[0]);
        return;
    }

    CEntity* entity = player->GetEntity();
    LOG_INFO("console", "player_id=" + std::to_string(player->GetId()) + " name=" + player->GetName() +
                            " entity=" + EntityCommandRef(entity));
})

REGISTER_CONSOLE_COMMAND_USAGE(kill, "kill <player>", "player", {
    if (args.empty())
    {
        LOG_INFO("console", "Usage: kill <player>");
        return;
    }

    auto* server = CServer::GetInstance();
    CGameContext* context = server ? server->GameContext() : nullptr;
    CPlayer* player = FindPlayerByIdOrName(context, args[0]);
    if (!player)
    {
        LOG_WARN("console", "Player not found: " + args[0]);
        return;
    }

    CEntity* entity = player->GetEntity();
    if (!entity)
    {
        LOG_WARN("console", "Player has no controlled entity: " + args[0]);
        return;
    }

    entity->TakeDamage(std::numeric_limits<float>::max(), nullptr, EDamageType::Poison);
    LOG_INFO("console", "Killed player_id=" + std::to_string(player->GetId()) + " name=" + player->GetName());
})

REGISTER_CONSOLE_COMMAND_USAGE(query_world, "query_world <world_id|map>", "world_or_map", {
    if (args.empty())
    {
        LOG_INFO("console", "Usage: query_world <world_id|map>");
        return;
    }

    auto* server = CServer::GetInstance();
    if (!server)
    {
        LOG_WARN("console", "No active server.");
        return;
    }

    if (auto world_id = ParseWorldId(args[0]))
    {
        CGameWorld* world = server->FindWorldById(*world_id);
        if (!world)
        {
            LOG_WARN("console", "World not found: " + args[0]);
            return;
        }
        LogWorldInfo(*world);
        return;
    }

    const std::vector<CGameWorld*> worlds = server->FindWorldsByMapName(args[0]);
    if (worlds.empty())
    {
        LOG_WARN("console", "World not found: " + args[0]);
        return;
    }
    for (CGameWorld* world : worlds)
        if (world) LogWorldInfo(*world);
})

REGISTER_CONSOLE_COMMAND_USAGE(tele, "tele <player> <world_id> <x,y>", "player world", {
    if (args.size() < 3)
    {
        LOG_INFO("console", "Usage: tele <player> <world_id> <x,y>");
        return;
    }

    std::optional<sf::Vector2f> pos = ParsePosition(args, 2);
    if (!pos)
    {
        LOG_WARN("console", "Invalid position. Usage: tele <player> <world_id> <x,y>");
        return;
    }

    auto* server = CServer::GetInstance();
    CGameContext* context = server ? server->GameContext() : nullptr;
    auto world_id = ParseWorldId(args[1]);
    CGameWorld* target_world = world_id && server ? server->FindWorldById(*world_id) : nullptr;
    if (!target_world)
    {
        LOG_WARN("console", "World not found: " + args[1]);
        return;
    }

    CPlayer* player = FindPlayerByIdOrName(context, args[0]);
    if (!player)
    {
        LOG_WARN("console", "Player not found: " + args[0]);
        return;
    }

    CEntity* entity = player->GetEntity();
    if (!entity)
    {
        LOG_WARN("console", "Player has no controlled entity: " + args[0]);
        return;
    }

    CGameWorld* source_world = entity->GameWorld();
    sf::Vector2f old_pos = entity->m_pos;
    const std::uint32_t old_world_id = source_world ? source_world->GetId() : 0;
    if (!source_world || !source_world->TransferPlayerEntityToWorld(*player, *target_world, *pos))
    {
        LOG_WARN("console", "Failed to teleport player: " + player->GetName());
        return;
    }

    LOG_INFO("console", "Teleported " + player->GetName() + " from world " + std::to_string(old_world_id) + " at " +
                            std::to_string(old_pos.x) + "," + std::to_string(old_pos.y) + " to world " +
                            std::to_string(target_world->GetId()) + " at " + std::to_string(pos->x) + "," +
                            std::to_string(pos->y));
})

REGISTER_CONSOLE_COMMAND_USAGE(set_team, "set_team <world_id>:<entity_id> <team>", "entity", {
    if (args.size() < 2)
    {
        LOG_INFO("console", "Usage: set_team <world_id>:<entity_id> <team>");
        return;
    }

    auto team = ParseInt(args[1]);
    if (!team)
    {
        LOG_WARN("console", "Invalid team: " + args[1]);
        return;
    }

    auto* server = CServer::GetInstance();
    CGameContext* context = server ? server->GameContext() : nullptr;
    CEntity* entity = FindEntityByCommandTarget(server, context, args[0]);
    if (!entity)
    {
        LOG_WARN("console", "Entity not found: " + args[0]);
        return;
    }

    int old_team = entity->m_team;
    entity->m_team = *team;
    CGameWorld* world = entity->GameWorld();
    LOG_INFO("console", "Entity " + std::to_string(world ? world->GetId() : 0) + ":" + std::to_string(entity->m_id) +
                            " team " + std::to_string(old_team) + " -> " + std::to_string(entity->m_team));
})

REGISTER_CONSOLE_COMMAND_USAGE(set, "set <config> <value>", "config", {
    if (args.size() < 2)
    {
        LOG_INFO("console", "Usage: set <config> <value>");
        return;
    }

    const std::string& value = args[1];
    if (game_config::SetConfig(args[0], value))
        LOG_INFO("console", "Set " + args[0] + " to " +
                                (game_config::IsSensitiveConfig(args[0]) ? "<redacted>" : value));
    else LOG_WARN("console", "Unknown config: " + args[0]);
})

REGISTER_CONSOLE_COMMAND_USAGE(get, "get <config>", "config", {
    if (args.empty())
    {
        LOG_INFO("console", "Usage: get <config>");
        return;
    }

    std::string result = game_config::GetConfig(args[0]);
    LOG_INFO("console", "Value of the " + args[0] + ": " + result);
})

REGISTER_CONSOLE_COMMAND_USAGE(gui_console, "gui_console <0|1>", "bool", {
    if (args.empty())
    {
        LOG_INFO("console", "Usage: gui_console <0|1>");
        return;
    }

    std::string value = ToLower(args[0]);
    if (value == "1" || value == "true" || value == "on")
    {
        game_config::gui_console_enabled = true;
        LOG_INFO("console", "GUI console enabled.");
        return;
    }
    if (value == "0" || value == "false" || value == "off")
    {
        game_config::gui_console_enabled = false;
        LOG_INFO("console", "GUI console disabled.");
        return;
    }

    LOG_WARN("console", "Usage: gui_console <0|1>");
})

REGISTER_CONSOLE_COMMAND_USAGE(rcon_keyboard, "rcon_keyboard [password...]", {}, {
    if (args.empty())
    {
        game_config::rcon_password.clear();
        LOG_INFO("console", "RCON password cleared. Remote console disabled.");
        return;
    }

    game_config::rcon_password = JoinArgs(args, 0);
    LOG_INFO("console", "RCON password updated.");
})

REGISTER_CONSOLE_COMMAND_USAGE(mute, "mute <player> [seconds]", "player", {
    if (args.empty())
    {
        LOG_INFO("console", "Usage: mute <player> [seconds]");
        return;
    }

    auto* server = CServer::GetInstance();
    CGameContext* context = server ? server->GameContext() : nullptr;
    CPlayer* player = FindPlayerByIdOrName(context, args[0]);
    if (!player)
    {
        LOG_WARN("console", "Player not found: " + args[0]);
        return;
    }

    float seconds = std::max(0.f, game_config::console_mute_default_seconds);
    if (args.size() >= 2)
    {
        auto parsed = ParseFloat(args[1]);
        if (!parsed || *parsed <= 0.f)
        {
            LOG_WARN("console", "Invalid mute seconds: " + args[1]);
            return;
        }
        seconds = *parsed;
    }

    player->MuteFor(seconds);
    LOG_INFO("console",
             "Muted " + player->GetName() + " for " + std::to_string(static_cast<int>(std::round(seconds))) + "s.");
})

REGISTER_CONSOLE_COMMAND_USAGE(unmute, "unmute <player>", "player", {
    if (args.empty())
    {
        LOG_INFO("console", "Usage: unmute <player>");
        return;
    }

    auto* server = CServer::GetInstance();
    CGameContext* context = server ? server->GameContext() : nullptr;
    CPlayer* player = FindPlayerByIdOrName(context, args[0]);
    if (!player)
    {
        LOG_WARN("console", "Player not found: " + args[0]);
        return;
    }

    player->Unmute();
    LOG_INFO("console", "Unmuted " + player->GetName() + ".");
})

REGISTER_CONSOLE_COMMAND_USAGE(kick, "kick <player>", "player", {
    if (args.empty())
    {
        LOG_INFO("console", "Usage: kick <player>");
        return;
    }

    auto* server = CServer::GetInstance();
    CGameContext* context = server ? server->GameContext() : nullptr;
    auto* network = context ? &context->Network() : nullptr;
    CPlayer* player = FindPlayerByIdOrName(context, args[0]);
    if (!network || !player)
    {
        LOG_WARN("console", "Player not found: " + args[0]);
        return;
    }

    uint32_t id = player->GetId();
    std::string name = player->GetName();
    if (network->KickPlayer(id, "kicked by console")) LOG_INFO("console", "Kicked " + name + ".");
})

REGISTER_CONSOLE_COMMAND_USAGE(ban_ip, "ban_ip <player> <seconds|-1>", "player", {
    if (args.size() < 2)
    {
        LOG_INFO("console", "Usage: ban_ip <player> <seconds|-1>");
        return;
    }

    auto seconds = ParseFloat(args[1]);
    if (!seconds)
    {
        LOG_WARN("console", "Invalid ban time: " + args[1]);
        return;
    }

    auto* server = CServer::GetInstance();
    CGameContext* context = server ? server->GameContext() : nullptr;
    auto* network = context ? &context->Network() : nullptr;
    CPlayer* player = FindPlayerByIdOrName(context, args[0]);
    if (!network || !player)
    {
        LOG_WARN("console", "Player not found: " + args[0]);
        return;
    }

    uint32_t id = player->GetId();
    std::string name = player->GetName();
    std::string ip = player->GetRemoteAddress();
    network->BanPlayerIp(id, *seconds);
    LOG_INFO("console", "Banned IP " + ip + " from " + name + " for " + args[1] + "s.");
})

REGISTER_CONSOLE_COMMAND_USAGE(ban_name, "ban_name <player|account> <seconds|-1>", "player", {
    if (args.size() < 2)
    {
        LOG_INFO("console", "Usage: ban_name <player|account> <seconds|-1>");
        return;
    }

    auto seconds = ParseFloat(args[1]);
    if (!seconds)
    {
        LOG_WARN("console", "Invalid ban time: " + args[1]);
        return;
    }

    auto* server = CServer::GetInstance();
    CGameContext* context = server ? server->GameContext() : nullptr;
    auto* network = context ? &context->Network() : nullptr;
    CPlayer* player = FindPlayerByIdOrName(context, args[0]);
    std::string name = player ? player->GetAccountName() : args[0];
    if (!network)
    {
        LOG_WARN("console", "No active network.");
        return;
    }

    network->BanName(name, *seconds);
    LOG_INFO("console", "Banned name " + name + " for " + args[1] + "s.");
})

REGISTER_CONSOLE_COMMAND_USAGE(control, "control <player> <world_id>:<entity_id>", "player entity", {
    if (args.size() < 2)
    {
        LOG_INFO("console", "Usage: control <player> <world_id>:<entity_id>");
        return;
    }

    auto* server = CServer::GetInstance();
    CGameContext* context = server ? server->GameContext() : nullptr;
    auto* network = context ? &context->Network() : nullptr;
    if (!network)
    {
        LOG_WARN("console", "No active network module for control command.");
        return;
    }

    CPlayer* player = FindPlayerByIdOrName(context, args[0]);
    if (!player)
    {
        LOG_WARN("console", "Player not found: " + args[0]);
        return;
    }

    std::optional<SWorldEntityRef> entity_ref = ParseWorldEntityRef(args[1]);
    if (!entity_ref && args.size() >= 3)
    {
        auto world_id = ParseWorldId(args[1]);
        auto entity_id = ParseInt(args[2]);
        if (world_id && entity_id && *entity_id >= 0) entity_ref = SWorldEntityRef{ *world_id, *entity_id };
    }
    if (!entity_ref)
    {
        LOG_WARN("console", "Invalid world/entity reference: " + args[1]);
        return;
    }

    CGameWorld* world = server->FindWorldById(entity_ref->world_id);
    if (!world)
    {
        LOG_WARN("console", "World not found: " + std::to_string(entity_ref->world_id));
        return;
    }

    if (!network->AssignPlayerEntity(player->GetId(), *world, entity_ref->entity_id))
    {
        LOG_WARN("console", "Failed to let player " + std::to_string(player->GetId()) + " control entity " +
                                std::to_string(entity_ref->world_id) + ":" + std::to_string(entity_ref->entity_id) +
                                ".");
        return;
    }

    LOG_INFO("console", "Player " + std::to_string(player->GetId()) + " is now controlling entity " +
                            std::to_string(entity_ref->world_id) + ":" + std::to_string(entity_ref->entity_id) + ".");
})

REGISTER_CONSOLE_COMMAND_USAGE(add, "add <player|account> <petal> <rarity> [count]", "player petal rarity", {
    if (args.size() < 3)
    {
        LOG_INFO("console", "Usage: add <player|account> <petal> <rarity> [count]");
        return;
    }

    size_t target_index = 0;
    size_t petal_index = 1;
    size_t rarity_index = 2;
    const CPetalPrototype* proto = FindPetalPrototypeByText(args[petal_index]);
    auto rarity = ParseRarity(args[rarity_index]);
    if (!proto || !rarity)
    {
        target_index = 2;
        petal_index = 0;
        rarity_index = 1;
        proto = FindPetalPrototypeByText(args[petal_index]);
        rarity = ParseRarity(args[rarity_index]);
    }
    if (!proto)
    {
        LOG_WARN("console", "Unknown petal: " + args[petal_index]);
        return;
    }

    if (!rarity)
    {
        LOG_WARN("console", "Invalid rarity: " + args[rarity_index]);
        return;
    }

    uint32_t count = 1;
    if (args.size() >= 4)
    {
        auto parsed_count = ParseInt(args[3]);
        if (!parsed_count || *parsed_count <= 0)
        {
            LOG_WARN("console", "Invalid item count: " + args[3]);
            return;
        }
        count = static_cast<uint32_t>(std::min(*parsed_count, static_cast<int>(max_inventory_item_count)));
    }

    auto* server = CServer::GetInstance();
    CGameContext* context = server ? server->GameContext() : nullptr;
    CPlayer* player = FindPlayerByIdOrName(context, args[target_index]);
    std::string account_name = player ? player->GetAccountName() : args[target_index];
    if (account_name.empty())
    {
        LOG_WARN("console", "Player/account has no account name: " + args[target_index]);
        return;
    }

    const uint8_t petal_type = static_cast<uint8_t>(proto->m_type);
    const uint8_t rarity_id = static_cast<uint8_t>(*rarity);
    CAccountDataStore::AddItem(account_name, petal_type, rarity_id, count);

    if (player && player->IsAuthenticated() && context) context->Network().QueueInventoryUpdate(*player);

    LOG_INFO("console", "Added " + std::to_string(count) + " " + RarityName(*rarity) + " " + proto->m_name + " to " +
                            account_name + ".");
})

REGISTER_CONSOLE_COMMAND_USAGE(
    equip, "equip <player|world_id:entity_id> [slot] <petal> <rarity>", "target petal rarity", {
        if (args.size() < 3)
        {
            LOG_INFO("console", "Usage: equip <player|world_id:entity_id> [slot] <petal> <rarity>");
            return;
        }

        size_t petal_index = 1;
        size_t rarity_index = 2;
        std::optional<int> forced_slot;
        if (args.size() >= 4)
        {
            forced_slot = ParseInt(args[1]);
            if (!forced_slot)
            {
                LOG_WARN("console", "Invalid slot: " + args[1]);
                return;
            }
            petal_index = 2;
            rarity_index = 3;
        }

        const CPetalPrototype* proto = FindPetalPrototypeByText(args[petal_index]);
        auto rarity = ParseRarity(args[rarity_index]);
        if (!proto || !rarity)
        {
            std::swap(petal_index, rarity_index);
            proto = FindPetalPrototypeByText(args[petal_index]);
            rarity = ParseRarity(args[rarity_index]);
        }
        if (!proto)
        {
            LOG_WARN("console", "Unknown petal: " + args[petal_index]);
            return;
        }

        if (!rarity)
        {
            LOG_WARN("console", "Invalid rarity: " + args[rarity_index]);
            return;
        }

        auto* server = CServer::GetInstance();
        CGameContext* context = server ? server->GameContext() : nullptr;
        CGameWorld* world = context ? &context->World() : nullptr;
        if (!server || !context || !world)
        {
            LOG_WARN("console", "No active server/world for equip command.");
            return;
        }

        CEntity* target_entity = FindEntityByCommandTarget(server, context, args[0]);

        auto* flower = dynamic_cast<CFlower*>(target_entity);
        if (!flower)
        {
            LOG_WARN("console", "Player/entity " + args[0] + " is not a flower.");
            return;
        }

        int slot = forced_slot.value_or(FindFirstEquipSlot(*flower));
        if (slot < 0)
        {
            LOG_WARN("console", "Flower " + args[0] + " has no available slot.");
            return;
        }
        if (slot >= static_cast<int>(flower->GetSlots().size()))
        {
            LOG_WARN("console", "Slot " + std::to_string(slot) + " is out of range.");
            return;
        }

        flower->EquipPetal(slot, proto, *rarity);
        LOG_INFO("console", "Equipped " + RarityName(*rarity) + " " + proto->m_name + " to entity " +
                                EntityCommandRef(target_entity) + " slot " + std::to_string(slot) + ".");
    })

REGISTER_CONSOLE_COMMAND_USAGE(
    say,
    "say server [player_id] [teller] <message...> | say global <teller> <message...> | say local <world_id> "
    "<x,y> <teller> <message...> | say whisper <player> <teller> <message...>",
    "channel", {
        if (args.size() < 2)
        {
            LOG_INFO("console", "Usage: say <server|global|local|whisper> <arguments...>");
            return;
        }

        auto* server = CServer::GetInstance();
        CGameContext* context = server ? server->GameContext() : nullptr;
        auto* network = context ? &context->Network() : nullptr;
        if (!server || !network)
        {
            LOG_WARN("console", "No active server/network for say command.");
            return;
        }

        std::string flag = ToLower(args[0]);
        const CServer::SChatEntry* chat = nullptr;
        if (flag == "server")
        {
            std::string teller = "Server";
            int target_player_id = -1;
            size_t message_index = 1;
            if (auto target = ParseInt(args[1]))
            {
                target_player_id = *target;
                message_index = 2;
            }
            if (args.size() > message_index + 1)
            {
                teller = args[message_index];
                ++message_index;
            }
            std::string message = JoinArgs(args, message_index);
            chat = server->SubmitChat(nullptr, { 0.f, 0.f }, EChatFlag::Server, 0, teller, message, target_player_id);
        } else if (flag == "global")
        {
            if (args.size() < 3)
            {
                LOG_INFO("console", "Usage: say global <teller> <message...>");
                return;
            }
            chat = server->SubmitChat(context ? &context->World() : nullptr, { 0.f, 0.f }, EChatFlag::Global, 0,
                                      args[1], JoinArgs(args, 2));
        } else if (flag == "local")
        {
            const bool canonical = ParseWorldId(args[1]).has_value();
            const size_t world_index = canonical ? 1 : 2;
            const size_t position_index = canonical ? 2 : 3;
            if (args.size() <= position_index)
            {
                LOG_INFO("console", "Usage: say local <world_id> <x,y> <teller> <message...>");
                return;
            }

            auto world_id = ParseWorldId(args[world_index]);
            CGameWorld* world = world_id && server ? server->FindWorldById(*world_id) : nullptr;
            if (!world)
            {
                LOG_WARN("console", "World not found: " + args[world_index]);
                return;
            }

            auto pos = ParsePosition(args, position_index);
            if (!pos)
            {
                LOG_WARN("console", "Invalid local chat position.");
                return;
            }
            const size_t position_arg_count = args[position_index].find(',') == std::string::npos ? 2 : 1;
            const size_t teller_index = canonical ? position_index + position_arg_count : 1;
            const size_t message_index = canonical ? teller_index + 1 : position_index + position_arg_count;
            if (args.size() <= message_index)
            {
                LOG_INFO("console", "Usage: say local <world_id> <x,y> <teller> <message...>");
                return;
            }
            chat =
                server->SubmitChat(world, *pos, EChatFlag::Local, 0, args[teller_index], JoinArgs(args, message_index));
        } else if (flag == "whisper")
        {
            if (args.size() < 4)
            {
                LOG_INFO("console", "Usage: say whisper <player> <teller> <message...>");
                return;
            }
            CPlayer* target = FindPlayerByIdOrName(context, args[1]);
            if (!target)
            {
                LOG_WARN("console", "Whisper target not found: " + args[1]);
                return;
            }
            chat = server->SubmitChat(context ? &context->World() : nullptr, { 0.f, 0.f }, EChatFlag::Whisper, 0,
                                      args[2], JoinArgs(args, 3), static_cast<int>(target->GetId()));
        } else
        {
            LOG_WARN("console", "Unknown say flag: " + args[0]);
            return;
        }

        if (!chat)
        {
            LOG_WARN("console", "Empty chat message.");
            return;
        }
        network->BroadcastChat(*chat);
    })

REGISTER_CONSOLE_COMMAND_USAGE(spawn, "spawn <world_id> <mob> <rarity> <x,y>", "world mob rarity", {
    if (args.size() < 4)
    {
        LOG_INFO("console", "Usage: spawn <world_id> <mob> <rarity> <x,y>");
        return;
    }

    size_t world_index = 0;
    size_t mob_index = 1;
    size_t rarity_index = 2;
    auto world_id = ParseWorldId(args[world_index]);
    const CMobPrototype* proto = FindMobPrototypeByText(args[mob_index]);
    auto rarity = ParseRarity(args[rarity_index]);
    if (!world_id || !proto || !rarity)
    {
        world_index = 2;
        mob_index = 0;
        rarity_index = 1;
        world_id = ParseWorldId(args[world_index]);
        proto = FindMobPrototypeByText(args[mob_index]);
        rarity = ParseRarity(args[rarity_index]);
    }
    if (!proto)
    {
        LOG_WARN("console", "Unknown mob: " + args[mob_index]);
        return;
    }

    if (!rarity)
    {
        LOG_WARN("console", "Invalid rarity: " + args[rarity_index]);
        return;
    }

    auto pos = ParsePosition(args, 3);
    if (!pos)
    {
        LOG_WARN("console", "Invalid position. Use x,y or x y.");
        return;
    }

    auto* server = CServer::GetInstance();
    CGameWorld* world = world_id && server ? server->FindWorldById(*world_id) : nullptr;
    if (!world)
    {
        LOG_WARN("console", "World not found: " + args[world_index]);
        return;
    }

    auto mob = CreateMob(proto->m_type, world, *pos, *rarity, false);
    if (!mob)
    {
        LOG_WARN("console", "Failed to create mob: " + std::string(GetMobTypeName(proto->m_type)));
        return;
    }

    CEntity* entity = world->InsertEntity(std::move(mob));
    if (!entity)
    {
        LOG_WARN("console", "Failed to insert mob: " + std::string(GetMobTypeName(proto->m_type)));
        return;
    }

    const auto* spawned_mob = dynamic_cast<const CMobBase*>(entity);
    const ERarity spawned_rarity = spawned_mob ? spawned_mob->GetRarity() : *rarity;
    LOG_INFO("console", "Spawned " + RarityName(spawned_rarity) + " " + std::string(GetMobTypeName(proto->m_type)) +
                            " entity " + EntityCommandRef(entity) + " at " + std::to_string(pos->x) + ", " +
                            std::to_string(pos->y) + ".");

    if (server && CServer::MeetsPetalReportRarity(spawned_rarity, game_config::min_mob_spawn_report_rarity))
    {
        const std::string mob_name =
            !proto->m_name.empty() ? proto->m_name : std::string(GetMobTypeName(proto->m_type));
        server->BroadcastMobSpawnReport(*world, "has spawned", spawned_rarity, mob_name);
    }
})
