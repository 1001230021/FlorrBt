#include "sandbox_controller.h"
#include "../../../Engine/logger.h"
#include "../../../Shared/game_config.h"
#include "../../../Shared/petal_type.h"
#include "../../../Shared/rarity.h"
#include "../../HotReload/snapshot_archive.h"
#include "../../Module/network_module.h"
#include "../../Persistence/account_store.h"
#include "../controller_params.h"
#include "../entities/flower.h"
#include "../entities/mob.h"
#include "../entities/petals/petal.h"
#include "../gamecontext.h"
#include "../gameworld.h"
#include "../player.h"
#include "../zone_mob_tools.h"
#include "../../server.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <filesystem>

namespace
{
std::string SandboxLower(std::string_view text)
{
    std::string result;
    result.reserve(text.size());
    for (char ch : text) result.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
    return result;
}

bool SandboxMapPathExists(std::string_view value)
{
    if (value.empty()) return false;
    std::string path(value);
    std::filesystem::path resolved_path(path);
    if (!std::filesystem::exists(resolved_path))
    {
        const std::filesystem::path candidates[] = {
            std::filesystem::path("..") / path,
            std::filesystem::path("..") / ".." / path,
        };
        for (const auto& candidate : candidates)
            if (std::filesystem::exists(candidate)) resolved_path = candidate;
    }
    return std::filesystem::exists(resolved_path);
}

bool SandboxParsePosition(const std::vector<std::string>& args, size_t begin, florrbt::Vector2f& out)
{
    if (begin >= args.size()) return false;
    const std::string& token = args[begin];
    const size_t comma = token.find(',');
    const std::string x_text = comma == std::string::npos ? token : token.substr(0, comma);
    const std::string y_text = comma == std::string::npos ? (begin + 1 < args.size() ? args[begin + 1] : "") : token.substr(comma + 1);
    if (x_text.empty() || y_text.empty()) return false;
    char* x_end = nullptr;
    char* y_end = nullptr;
    const float x = std::strtof(x_text.c_str(), &x_end);
    const float y = std::strtof(y_text.c_str(), &y_end);
    if (!x_end || *x_end != '\0' || !y_end || *y_end != '\0') return false;
    out = { x, y };
    return true;
}

std::optional<ERarity> SandboxParseRarity(std::string_view text)
{
    const int value = std::atoi(std::string(text).c_str());
    if (value >= static_cast<int>(ERarity::Common) && value <= static_cast<int>(ERarity::Primordial))
        return static_cast<ERarity>(value);
    const std::string lower = SandboxLower(text);
    for (int rarity = static_cast<int>(ERarity::Common); rarity <= static_cast<int>(ERarity::Primordial); ++rarity)
    {
        std::string name = SandboxLower(GetRarityName(static_cast<ERarity>(rarity)));
        if (name == lower) return static_cast<ERarity>(rarity);
    }
    return std::nullopt;
}

const CPetalPrototype* SandboxFindPetalByText(std::string_view text)
{
    const std::string lower = SandboxLower(text);
    for (int i = 1; i < kPetalIdCapacity; ++i)
    {
        if (SandboxLower(GetPetalTypeName(static_cast<EPetalType>(i))) == lower)
        {
            if (const CPetalPrototype* proto = FindPetalPrototype(static_cast<EPetalType>(i))) return proto;
        }
    }
    return nullptr;
}
} // namespace

CSandboxController::CSandboxController(CControllerParams params)
{
    if (params.Contains("enable_permission"))
        m_enable_permission = params.GetBool("enable_permission", false);
    if (params.Contains("enable_spawn"))
        m_enable_spawn = params.GetBool("enable_spawn", true);
    if (params.Contains("enable_loot"))
        m_enable_loot = params.GetBool("enable_loot", true);
    if (params.Contains("map"))
        m_map_path = params.Get("map", "");
}

std::vector<SControllerParamInfo> CSandboxController::ParamSchema() const
{
    return {
        { "enable_permission", EControllerParamType::Bool, "sandbox_enable_permission",
          "gate the sandbox commands behind a permission list (room owner auto-promoted)", &CSandboxController::ValidateBool },
        { "enable_spawn", EControllerParamType::Bool, "sandbox_enable_spawn",
          "allow natural mob spawning", &CSandboxController::ValidateBool },
        { "enable_loot", EControllerParamType::Bool, "sandbox_enable_loot",
          "allow mob drops", &CSandboxController::ValidateBool },
        { "map", EControllerParamType::String, "sandbox_map",
          "initial map path for this sandbox room (change it in the waiting room)", &CSandboxController::ValidateMapPath },
    };
}

bool CSandboxController::ValidateBool(const std::string& value)
{
    return ControllerParamValueLegal(EControllerParamType::Bool, value);
}

bool CSandboxController::ValidateMapPath(const std::string& value)
{
    return SandboxMapPathExists(value);
}

void CSandboxController::SendSystem(CGameWorld& world, CPlayer& player, const std::string& message) const
{
    CGameContext* context = world.GameContext();
    if (!context) return;
    CServer::SChatEntry entry;
    entry.flag = EChatFlag::Server;
    entry.player_id = 0;
    entry.target_player_id = static_cast<int>(player.GetId());
    entry.player_name = "Server";
    entry.message = message;
    context->Network().SendChatToPlayer(player, entry);
}

CPlayer* CSandboxController::FindSandboxPlayer(CGameWorld& world, std::string_view name) const
{
    CGameContext* context = world.GameContext();
    if (!context) return nullptr;
    const std::string lower = SandboxLower(name);
    for (const auto& player : context->Players())
    {
        if (!player) continue;
        if (player->GetName() == name || SandboxLower(player->GetName()) == lower) return player.get();
    }
    return nullptr;
}

void CSandboxController::OnPlayerEntityReady(CGameWorld& world, CPlayer& player, CPlayerFlower&,
                                             EPlayerSpawnReason)
{
    const uint32_t player_id = player.GetId();

    // First player to enter is the room owner when permissions are enabled.
    if (m_enable_permission && m_admin_ids.empty()) m_admin_ids.insert(player_id);

    // Detach the inventory/loadout from the persistent account: clone the
    // current account state into an ephemeral sandbox key the player uses
    // for the rest of their time in this world.
    const std::string& account = player.GetAccountName();
    if (!account.empty() && !m_original_accounts.contains(player_id))
    {
        m_original_accounts[player_id] = account;
        const std::string sandbox_key = "@sandbox:" + std::to_string(world.GetId()) + ":" + account;
        CAccountDataStore::SandboxCloneAccount(sandbox_key, account);
        player.SetSandboxAccountName(sandbox_key);
        LOG_INFO("sandbox", "player " + std::to_string(player_id) + " switched to sandbox account " + sandbox_key);
    }
}

void CSandboxController::OnPlayerLeftWorld(CGameWorld&, CPlayer& player)
{
    const uint32_t player_id = player.GetId();
    auto it = m_original_accounts.find(player_id);
    if (it != m_original_accounts.end())
    {
        player.SetSandboxAccountName(it->second);
        m_original_accounts.erase(it);
        LOG_INFO("sandbox", "player " + std::to_string(player_id) + " restored to persistent account");
    }
    m_admin_ids.erase(player_id);
}

void CSandboxController::OnMobDefeated(CGameWorld& world, CMobBase& mob)
{
    if (!m_enable_loot)
    {
        // Keep the spawn-director bookkeeping but skip reward generation.
        SpawnDirector().OnEntityRemoved(mob);
        return;
    }
    COpenController::OnMobDefeated(world, mob);
}

void CSandboxController::OnTick(CGameWorld& world, float dt)
{
    if (!m_enable_spawn) return;
    COpenController::OnTick(world, dt);
}

void CSandboxController::CaptureSnapshot(CSnapshotWriter& writer) const
{
    COpenController::CaptureSnapshot(writer);
    writer.Field("enable_permission", m_enable_permission);
    writer.Field("enable_spawn", m_enable_spawn);
    writer.Field("enable_loot", m_enable_loot);
}

bool CSandboxController::RestoreSnapshot(const CSnapshotReader& reader, std::uint32_t version,
                                         std::string& error)
{
    if (version > SnapshotVersion())
    {
        error = "Unsupported sandbox controller snapshot version " + std::to_string(version);
        return false;
    }
    m_enable_permission = reader.Bool("enable_permission", m_enable_permission);
    m_enable_spawn = reader.Bool("enable_spawn", m_enable_spawn);
    m_enable_loot = reader.Bool("enable_loot", m_enable_loot);
    return COpenController::RestoreSnapshot(reader, version, error);
}

bool CSandboxController::HandlePlayerCommand(CGameWorld& world, CPlayer& player,
                                             const std::vector<std::string>& args)
{
    if (args.empty()) return false;
    const std::string name = SandboxLower(args[0]);

    if (name == "perm" || name == "grant" || name == "unperm" || name == "revoke")
        return HandlePermCommand(world, player, args);

    if (name == "add" || name == "tele" || name == "kill" || name == "spawn")
        return HandleSandboxCommand(world, player, args);

    return false;
}

bool CSandboxController::HandlePermCommand(CGameWorld& world, CPlayer& player,
                                           const std::vector<std::string>& args)
{
    if (!m_enable_permission)
    {
        SendSystem(world, player, "permission system is disabled in this room");
        return true;
    }
    if (args.size() < 2)
    {
        SendSystem(world, player, "Usage: /perm <player>   /unperm <player>");
        return true;
    }

    const bool granting = args[0] != "unperm" && args[0] != "revoke";
    CPlayer* target = FindSandboxPlayer(world, args[1]);
    if (!target)
    {
        SendSystem(world, player, "player not found: " + args[1]);
        return true;
    }

    const uint32_t target_id = target->GetId();
    if (granting)
    {
        if (m_admin_ids.insert(target_id).second)
            SendSystem(world, player, target->GetName() + " granted sandbox permissions");
        else SendSystem(world, player, target->GetName() + " already has sandbox permissions");
    } else
    {
        bool was_owner = false;
        if (m_admin_ids.erase(target_id)) was_owner = true;
        if (was_owner && target_id != player.GetId()) m_admin_ids.insert(player.GetId());
        SendSystem(world, player, was_owner ? target->GetName() + " sandbox permissions revoked"
                                            : target->GetName() + " had no sandbox permissions");
    }
    return true;
}

bool CSandboxController::HandleSandboxCommand(CGameWorld& world, CPlayer& player,
                                              const std::vector<std::string>& args)
{
    const std::string name = SandboxLower(args[0]);
    if (m_enable_permission && !IsAdmin(player.GetId()))
    {
        SendSystem(world, player, "no sandbox permissions (owner can grant with /perm <player>)");
        return true;
    }

    CGameContext* context = world.GameContext();

    if (name == "add")
    {
        // /add [player] <petal> <rarity> [count]  (player defaults to self)
        size_t target_index = 0;
        size_t petal_index = 1;
        size_t rarity_index = 2;
        const CPetalPrototype* proto = SandboxFindPetalByText(args.size() > petal_index ? args[petal_index] : "");
        auto rarity = args.size() > rarity_index ? SandboxParseRarity(args[rarity_index]) : std::nullopt;
        if (!proto || !rarity)
        {
            target_index = 2;
            petal_index = 0;
            rarity_index = 1;
            proto = SandboxFindPetalByText(args.size() > petal_index ? args[petal_index] : "");
            rarity = args.size() > rarity_index ? SandboxParseRarity(args[rarity_index]) : std::nullopt;
        }
        if (!proto)
        {
            SendSystem(world, player, "unknown petal: " + std::string(target_index == 0 ? (args.size() > 1 ? args[1] : "") : ""));
            return true;
        }
        if (!rarity)
        {
            SendSystem(world, player, "invalid rarity");
            return true;
        }
        CPlayer* target = &player;
        if (target_index == 2)
        {
            target = FindSandboxPlayer(world, args[1]);
            if (!target)
            {
                SendSystem(world, player, "player not found: " + args[1]);
                return true;
            }
        }
        uint32_t count = 1;
        if (args.size() >= 4)
        {
            const int parsed = std::atoi(args[3].c_str());
            if (parsed > 0) count = static_cast<uint32_t>(std::min(parsed, 9999));
        }
        CAccountDataStore::AddItem(target->GetAccountName(), static_cast<uint8_t>(proto->m_type),
                                   static_cast<uint8_t>(*rarity), count);
        if (context) context->Network().QueueInventoryUpdate(*target);
        return true;
    }

    if (name == "tele")
    {
        // /tele [player] <x,y>
        CPlayer* target = &player;
        florrbt::Vector2f position{};
        if (!SandboxParsePosition(args, 1, position))
        {
            target = FindSandboxPlayer(world, args[1]);
            if (!target || !SandboxParsePosition(args, 2, position))
            {
                SendSystem(world, player, "Usage: /tele [player] <x,y>");
                return true;
            }
        }
        CEntity* entity = target->GetEntity();
        if (!entity)
        {
            SendSystem(world, player, "target has no entity");
            return true;
        }
        entity->Pos() = position;
        entity->PrevPos() = position;
        SendSystem(world, player, target->GetName() + " teleported to " + std::to_string(position.x) + "," +
                                       std::to_string(position.y));
        return true;
    }

    if (name == "kill")
    {
        // /kill [player] (defaults to the sender)
        CPlayer* target = &player;
        if (args.size() >= 2)
        {
            target = FindSandboxPlayer(world, args[1]);
            if (!target)
            {
                SendSystem(world, player, "player not found: " + args[1]);
                return true;
            }
        }
        CEntity* entity = target->GetEntity();
        if (entity)
        {
            entity->Health() = 0.f;
            entity->MarkForDestroy(EEntityRemovalReason::Defeated);
        }
        SendSystem(world, player, target->GetName() + " removed");
        return true;
    }

    if (name == "spawn")
    {
        // /spawn <mob> <rarity> <x,y>
        if (args.size() < 4)
        {
            SendSystem(world, player, "Usage: /spawn <mob> <rarity> <x,y>");
            return true;
        }
        EMobType mob_type = EMobType::None;
        if (!TryParseZoneMobType(args[1], mob_type))
        {
            SendSystem(world, player, "unknown mob: " + args[1]);
            return true;
        }
        auto rarity = SandboxParseRarity(args[2]);
        if (!rarity)
        {
            SendSystem(world, player, "invalid rarity");
            return true;
        }
        florrbt::Vector2f position{};
        if (!SandboxParsePosition(args, 3, position))
        {
            SendSystem(world, player, "Usage: /spawn <mob> <rarity> <x,y>");
            return true;
        }
        auto mob = CreateMob(mob_type, &world, position, *rarity, false);
        if (!mob)
        {
            SendSystem(world, player, "failed to create mob");
            return true;
        }
        if (!world.InsertEntity(std::move(mob)))
        {
            SendSystem(world, player, "failed to insert mob");
            return true;
        }
        SendSystem(world, player, "spawned " + args[1] + " at " + std::to_string(position.x) + "," +
                                       std::to_string(position.y));
        return true;
    }

    return false;
}
