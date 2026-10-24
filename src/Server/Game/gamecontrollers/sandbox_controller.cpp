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
    const size_t needed = comma == std::string::npos ? 2 : 1;
    if (args.size() != begin + needed) return false;
    const std::string x_text = comma == std::string::npos ? token : token.substr(0, comma);
    const std::string y_text = comma == std::string::npos ? (begin + 1 < args.size() ? args[begin + 1] : "") : token.substr(comma + 1);
    if (x_text.empty() || y_text.empty()) return false;
    float x = 0.f;
    float y = 0.f;
    if (!ParseFiniteFloatText(x_text, x) || !ParseFiniteFloatText(y_text, y)) return false;
    out = { x, y };
    return true;
}

std::optional<ERarity> SandboxParseRarity(std::string_view text)
{
    int value = 0;
    if (ParseStrictIntText(text, value) && value >= static_cast<int>(ERarity::Common) &&
        value <= static_cast<int>(ERarity::Primordial))
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
    SChatEntry entry;
    entry.flag = EChatFlag::Server;
    entry.player_id = 0;
    entry.target_player_id = static_cast<int>(player.GetId());
    entry.player_name = "Server";
    entry.message = message;
    context->Extensions().Globals().Find<IChatService>()->SendTo(player, entry);
}

CPlayer* CSandboxController::FindSandboxPlayer(CGameWorld& world, std::string_view name) const
{
    CGameContext* context = world.GameContext();
    if (!context) return nullptr;
    const std::string lower = SandboxLower(name);
    for (const auto& player : context->Players())
    {
        if (!player || !player->IsConnected() || !IsMember(world, *player)) continue;
        if (player->GetName() == name || SandboxLower(player->GetName()) == lower) return player.get();
    }
    return nullptr;
}

bool CSandboxController::IsMember(const CGameWorld& world, const CPlayer& player) const
{
    const auto* entity = player.GetEntity();
    return player.IsAuthenticated() && entity && entity->GameWorld() == &world &&
        player.HasIsolatedProgress() && player.ProgressWorld() ==
            SWorldHandle{world.GetId(), world.GetInstanceGeneration()};
}

bool CSandboxController::IsAdmin(const CPlayer& player) const
{
    const auto found = m_admin_sessions.find(player.GetId());
    return found != m_admin_sessions.end() && found->second == player.SessionGeneration();
}

void CSandboxController::PruneAdmins(CGameWorld& world)
{
    const auto* context = world.GameContext();
    std::erase_if(m_admin_sessions, [&](const auto& entry) {
        CPlayer* member = context ? context->Network().FindPlayerById(entry.first) : nullptr;
        return !member || member->SessionGeneration() != entry.second || !IsMember(world, *member);
    });
}

void CSandboxController::OnPlayerArrivalCommitted(CGameWorld& world, CPlayer& player)
{
    if (!IsMember(world, player)) return;
    PruneAdmins(world);
    if (m_enable_permission && m_admin_sessions.empty())
        m_admin_sessions[player.GetId()] = player.SessionGeneration();
}

void CSandboxController::OnPlayerDepartureCommitted(CGameWorld&, CPlayer& player, CEntity*)
{
    if (IsAdmin(player)) m_admin_sessions.erase(player.GetId());
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
    PruneAdmins(world);
    if (!m_enable_spawn) return;
    COpenController::OnTick(world, dt);
}

void CSandboxController::CaptureSnapshot(CSnapshotWriter& writer) const
{
    COpenController::CaptureSnapshot(writer);
    writer.Field("enable_permission", m_enable_permission);
    writer.Field("enable_spawn", m_enable_spawn);
    writer.Field("enable_loot", m_enable_loot);
    writer.Field("map", m_map_path);
    auto admins = MakeJsonArray();
    std::vector<std::uint32_t> ids;
    for (const auto& [id, generation] : m_admin_sessions) ids.push_back(id);
    std::sort(ids.begin(), ids.end());
    for (auto id : ids)
    {
        auto value = MakeJsonObject(); CSnapshotWriter entry(value.get());
        entry.Field("id", id); entry.UInt64("session_generation", m_admin_sessions.at(id));
        AppendJson(admins.get(), value.release());
    }
    writer.Node("admins", admins.release());
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
    m_map_path = reader.String("map");
    std::unordered_map<std::uint32_t, std::uint64_t> admins;
    if (version >= 2)
    {
        const auto* values = reader.Node("admins");
        if (!json_is_array(values) || json_array_size(values) > 4096)
        { error = "Invalid sandbox permissions"; return false; }
        for (size_t i = 0; i < json_array_size(values); ++i)
        {
            CSnapshotReader entry(json_array_get(values, i));
            const auto id = entry.UInt32("id"); const auto generation = entry.UInt64("session_generation");
            if (!id || !generation || generation == UINT64_MAX || !admins.emplace(id, generation).second)
            { error = "Invalid sandbox permission identity"; return false; }
        }
    }
    m_admin_sessions = std::move(admins);
    m_restore_legacy_permissions = version < 2;
    return COpenController::RestoreSnapshot(reader, version, error);
}

bool CSandboxController::ResolveReferences(CGameWorld& world, const IGameWorldResolver&, std::string& error)
{
    PruneAdmins(world);
    if (m_restore_legacy_permissions && m_enable_permission && m_admin_sessions.empty() && world.GameContext())
    {
        for (const auto& player : world.GameContext()->Players())
            if (player && IsMember(world, *player))
            { error = "Legacy sandbox permissions cannot be reconstructed; leave the sandbox before upgrading"; return false; }
    }
    m_restore_legacy_permissions = false;
    return true;
}

bool CSandboxController::HandlePlayerCommand(CGameWorld& world, CPlayer& player,
                                             const std::vector<std::string>& args)
{
    if (args.empty()) return false;
    if (!player.IsConnected() || !IsMember(world, player)) return true;
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
    PruneAdmins(world);
    if (!IsAdmin(player))
    { SendSystem(world, player, "no permission to manage sandbox permissions"); return true; }
    if (args.size() != 2)
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
        if (m_admin_sessions.emplace(target_id, target->SessionGeneration()).second)
            SendSystem(world, player, target->GetName() + " granted sandbox permissions");
        else SendSystem(world, player, target->GetName() + " already has sandbox permissions");
    } else
    {
        if (IsAdmin(*target) && m_admin_sessions.size() == 1)
        { SendSystem(world, player, "cannot revoke the last sandbox administrator"); return true; }
        const bool was_owner = m_admin_sessions.erase(target_id) != 0;
        SendSystem(world, player, was_owner ? target->GetName() + " sandbox permissions revoked"
                                            : target->GetName() + " had no sandbox permissions");
    }
    return true;
}

bool CSandboxController::HandleSandboxCommand(CGameWorld& world, CPlayer& player,
                                              const std::vector<std::string>& args)
{
    const std::string name = SandboxLower(args[0]);
    if (m_enable_permission && !IsAdmin(player))
    {
        SendSystem(world, player, "no sandbox permissions (owner can grant with /perm <player>)");
        return true;
    }

    CGameContext* context = world.GameContext();

    if (name == "add")
    {
        // /add [player] <petal> <rarity> [count]  (player defaults to self)
        CPlayer* target = &player;
        size_t petal_index = 1;
        if (args.size() >= 4 && !SandboxFindPetalByText(args[1]))
        {
            target = FindSandboxPlayer(world, args[1]);
            if (!target) { SendSystem(world, player, "player not found in this sandbox: " + args[1]); return true; }
            petal_index = 2;
        }
        if (args.size() < petal_index + 2 || args.size() > petal_index + 3)
        { SendSystem(world, player, "Usage: /add [player] <petal> <rarity> [count]"); return true; }
        const CPetalPrototype* proto = SandboxFindPetalByText(args[petal_index]);
        const auto rarity = SandboxParseRarity(args[petal_index + 1]);
        if (!proto)
        {
            SendSystem(world, player, "unknown petal: " + args[petal_index]);
            return true;
        }
        if (!rarity)
        {
            SendSystem(world, player, "invalid rarity");
            return true;
        }
        uint32_t count = 1;
        if (args.size() == petal_index + 3)
        {
            int parsed = 0;
            if (!ParseStrictIntText(args[petal_index + 2], parsed) || parsed <= 0 || parsed > 9999)
            { SendSystem(world, player, "count must be in [1, 9999]"); return true; }
            count = static_cast<uint32_t>(parsed);
        }
        CAccountDataStore::AddItem(target->GetProgressAccountName(), static_cast<uint8_t>(proto->m_type),
                                   static_cast<uint8_t>(*rarity), count);
        if (context) context->Network().QueueInventoryUpdate(*target);
        return true;
    }

    if (name == "tele")
    {
        if (args.size() < 2 || args.size() > 4)
        { SendSystem(world, player, "Usage: /tele [player] <x,y>"); return true; }
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
        if (!world.TransferPlayerEntityToWorld(*target, world, position))
        { SendSystem(world, player, "teleport rejected"); return true; }
        SendSystem(world, player, target->GetName() + " teleported to " + std::to_string(position.x) + "," +
                                       std::to_string(position.y));
        return true;
    }

    if (name == "kill")
    {
        if (args.size() > 2) { SendSystem(world, player, "Usage: /kill [player]"); return true; }
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
