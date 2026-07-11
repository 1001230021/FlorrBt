#include "../Engine/commands_registry.h"
#include "../Engine/console.h"
#include "../Engine/logger.h"
#include "server.h"
#include "Game/gamecontext.h"
#include "Module/network_module.h"
#include "../Shared/game_config.h"
#include "../Shared/rarity.h"
#include "Game/entities/flower.h"
#include "Game/entities/mob.h"
#include "Game/entities/petals/petal.h"
#include "Game/gameworld.h"
#include "Game/player.h"
#include "Module/network_module.h"
#include <algorithm>
#include <charconv>
#include <cctype>
#include <cmath>
#include <optional>
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

std::optional<ERarity> ParseRarity(std::string_view text)
{
    if (auto value = ParseInt(text))
    {
        if (*value > static_cast<int>(ERarity::Null) && *value <= static_cast<int>(ERarity::Primordial))
            return static_cast<ERarity>(*value);
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
    for (const auto& [type, proto] : g_petal_registry)
    {
        if (!proto) continue;
        if (MatchPetalTypeAlias(target, type) || ToLower(proto->m_name) == target) return proto.get();
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
    for (const auto& [type, proto] : g_mob_registry)
    {
        if (!proto) continue;
        if (MatchMobTypeAlias(target, type) || ToLower(proto->m_name) == target || ToLower(GetMobTypeName(type)) == target)
            return proto.get();
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
        if (ToLower(player->GetName()) == target || ToLower(player->GetAccountName()) == target)
            return player.get();
    }
    return nullptr;
}

CPlayer* FindPlayerByIdOrName(CGameContext* context, std::string_view text)
{
    if (!context) return nullptr;
    auto* network = &context->Network();
    if (auto id = ParseInt(text))
        return *id >= 0 ? network->FindPlayerById(static_cast<uint32_t>(*id)) : nullptr;
    return FindPlayerByName(context, text);
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
}

REGISTER_CONSOLE_COMMAND(quit, {
    if (auto* server = CServer::GetInstance())
        server->RequestStop();
    else
        LOG_FATAL("server", "Can't find instance of the server");
})

REGISTER_CONSOLE_COMMAND(echo, {
    if (args.empty()) return;
    LOG_INFO("console", args[0]);
})

REGISTER_CONSOLE_COMMAND(who, {
    if (args.empty())
    {
        LOG_INFO("console", "Usage: who [player name]");
        return;
    }

    auto* server = CServer::GetInstance();
    CGameContext* context = server ? server->GameContext() : nullptr;
    CPlayer* player = FindPlayerByName(context, args[0]);
    if (!player)
    {
        LOG_WARN("console", "Player not found: " + args[0]);
        return;
    }

    CEntity* entity = player->GetEntity();
    LOG_INFO("console", "name=" + player->GetName() + " account=" + player->GetAccountName() +
                            " player_id=" + std::to_string(player->GetId()) + " entity_id=" +
                            std::to_string(entity ? entity->m_id : -1));
})

REGISTER_CONSOLE_COMMAND(set, {
    if (args.size() < 2) return;

    const std::string& value = args[1];
    if (game_config::SetConfig(args[0], value))
        LOG_INFO("console", "Set " + args[0] + " to " + value);
    else
        LOG_WARN("console", "Unknown config: " + args[0]);
})

REGISTER_CONSOLE_COMMAND(get, {
    if (args.empty()) return;

    std::string result = game_config::GetConfig(args[0]);
    LOG_INFO("console", "Value of the " + args[0] + ": " + result);
})

REGISTER_CONSOLE_COMMAND(mute, {
    if (args.empty())
    {
        LOG_INFO("console", "Usage: mute [player name/id] [seconds=60]");
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

    float seconds = 60.f;
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
    LOG_INFO("console", "Muted " + player->GetName() + " for " +
                            std::to_string(static_cast<int>(std::round(seconds))) + "s.");
})

REGISTER_CONSOLE_COMMAND(unmute, {
    if (args.empty())
    {
        LOG_INFO("console", "Usage: unmute [player name/id]");
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

REGISTER_CONSOLE_COMMAND(kick, {
    if (args.empty())
    {
        LOG_INFO("console", "Usage: kick [player name/id]");
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
    if (network->KickPlayer(id, "kicked by console"))
        LOG_INFO("console", "Kicked " + name + ".");
})

REGISTER_CONSOLE_COMMAND(ban_ip, {
    if (args.size() < 2)
    {
        LOG_INFO("console", "Usage: ban_ip [player name/id] [seconds|-1]");
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

REGISTER_CONSOLE_COMMAND(ban_name, {
    if (args.size() < 2)
    {
        LOG_INFO("console", "Usage: ban_name [player name/id] [seconds|-1]");
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

REGISTER_CONSOLE_COMMAND(control, {
    if (args.size() < 2)
    {
        LOG_INFO("console", "Usage: control [player id] [entity id]");
        return;
    }

    auto player_id = ParseInt(args[0]);
    auto entity_id = ParseInt(args[1]);
    if (!player_id || *player_id < 0)
    {
        LOG_WARN("console", "Invalid player id: " + args[0]);
        return;
    }
    if (!entity_id || *entity_id < 0)
    {
        LOG_WARN("console", "Invalid entity id: " + args[1]);
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

    if (!network->AssignPlayerEntity(static_cast<uint32_t>(*player_id), *entity_id))
    {
        LOG_WARN("console", "Failed to let player " + std::to_string(*player_id) + " control entity " +
                                std::to_string(*entity_id) + ".");
        return;
    }

    LOG_INFO("console", "Player " + std::to_string(*player_id) + " is now controlling entity " +
                            std::to_string(*entity_id) + ".");
})

REGISTER_CONSOLE_COMMAND(equip, {
    if (args.size() < 3)
    {
        LOG_INFO("console", "Usage: equip [player id] [rarity] [petal] or equip [player id] [slot] [rarity] [petal]");
        return;
    }

    size_t rarity_index = 1;
    size_t petal_index = 2;
    std::optional<int> forced_slot;
    if (args.size() >= 4)
    {
        forced_slot = ParseInt(args[1]);
        if (!forced_slot)
        {
            LOG_WARN("console", "Invalid slot: " + args[1]);
            return;
        }
        rarity_index = 2;
        petal_index = 3;
    }

    auto rarity = ParseRarity(args[rarity_index]);
    if (!rarity)
    {
        LOG_WARN("console", "Invalid rarity: " + args[rarity_index]);
        return;
    }

    const CPetalPrototype* proto = FindPetalPrototypeByText(args[petal_index]);
    if (!proto)
    {
        LOG_WARN("console", "Unknown petal: " + args[petal_index]);
        return;
    }

    auto* server = CServer::GetInstance();
    CGameContext* context = server ? server->GameContext() : nullptr;
    CGameWorld* world = context ? &context->World() : nullptr;
    if (!world)
    {
        LOG_WARN("console", "No active world for equip command.");
        return;
    }

    CPlayer* target_player = FindPlayerByIdOrName(context, args[0]);
    auto id = ParseInt(args[0]);
    int target_id = id.value_or(target_player ? static_cast<int>(target_player->GetId()) : -1);
    auto* network = context ? &context->Network() : nullptr;
    CEntity* target_entity = nullptr;
    if (target_player)
    {
        target_entity = target_player->GetEntity();
    } else if (network && id && *id >= 0)
    {
        if (CPlayer* player = network->FindPlayerById(static_cast<uint32_t>(*id)))
            target_entity = player->GetEntity();
    }
    if (!target_entity && id) target_entity = world->GetEntity(*id);

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
                            std::to_string(target_entity->m_id) + " slot " + std::to_string(slot) + ".");
})

REGISTER_CONSOLE_COMMAND(say, {
    if (args.size() < 2)
    {
        LOG_INFO("console", "Usage: say [server|global|local] ...");
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
        chat = server->SubmitChat(nullptr, {0.f, 0.f}, EChatFlag::Server, 0, teller, message, target_player_id);
    } else if (flag == "global") {
        if (args.size() < 3)
        {
            LOG_INFO("console", "Usage: say global [teller] [msg]");
            return;
        }
        chat = server->SubmitChat(context ? &context->World() : nullptr, {0.f, 0.f}, EChatFlag::Global, 0,
                                  args[1], JoinArgs(args, 2));
    } else if (flag == "local") {
        if (args.size() < 5)
        {
            LOG_INFO("console", "Usage: say local [teller] [x,y] [msg] or say local [teller] [x] [y] [msg]");
            return;
        }

        size_t message_index = 3;
        auto pos = ParsePosition(args, 2);
        if (!pos)
        {
            LOG_WARN("console", "Invalid local chat position.");
            return;
        }
        if (args[2].find(',') == std::string::npos) message_index = 4;
        chat = server->SubmitChat(context ? &context->World() : nullptr, *pos, EChatFlag::Local, 0,
                                  args[1], JoinArgs(args, message_index));
    } else if (flag == "whisper") {
        if (args.size() < 4)
        {
            LOG_INFO("console", "Usage: say whisper [target name] [teller] [msg]");
            return;
        }
        CPlayer* target = FindPlayerByName(context, args[1]);
        if (!target)
        {
            LOG_WARN("console", "Whisper target not found: " + args[1]);
            return;
        }
        chat = server->SubmitChat(context ? &context->World() : nullptr, {0.f, 0.f}, EChatFlag::Whisper, 0,
                                  args[2], JoinArgs(args, 3), static_cast<int>(target->GetId()));
    } else {
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

REGISTER_CONSOLE_COMMAND(spawn, {
    if (args.size() < 3)
    {
        LOG_INFO("console", "Usage: spawn [mobtype] [rarity] [x,y] or spawn [mobtype] [rarity] [x] [y]");
        return;
    }

    const CMobPrototype* proto = FindMobPrototypeByText(args[0]);
    if (!proto)
    {
        LOG_WARN("console", "Unknown mob type: " + args[0]);
        return;
    }

    auto rarity = ParseRarity(args[1]);
    if (!rarity)
    {
        LOG_WARN("console", "Invalid rarity: " + args[1]);
        return;
    }

    auto pos = ParsePosition(args, 2);
    if (!pos)
    {
        LOG_WARN("console", "Invalid position. Use x,y or x y.");
        return;
    }

    auto* server = CServer::GetInstance();
    CGameContext* context = server ? server->GameContext() : nullptr;
    CGameWorld* world = context ? &context->World() : nullptr;
    if (!world)
    {
        LOG_WARN("console", "No active world for spawn command.");
        return;
    }

    auto mob = CreateMob(proto->m_type, world, *pos, *rarity);
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

    LOG_INFO("console", "Spawned " + RarityName(*rarity) + " " + std::string(GetMobTypeName(proto->m_type)) +
                            " id " + std::to_string(entity->m_id) + " at " + std::to_string(pos->x) + ", " +
                            std::to_string(pos->y) + ".");
})
