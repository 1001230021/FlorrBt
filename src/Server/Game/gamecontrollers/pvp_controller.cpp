#include "pvp_controller.h"
#include "../../HotReload/snapshot_archive.h"
#include "../../../Engine/logger.h"
#include "../../../Shared/game_config.h"
#include "../entities/flower.h"
#include "../gamecontext.h"
#include "../gameworld.h"
#include "../player.h"
#include <sstream>
#include <string>

namespace
{
bool ParseIntStrict(const std::string& value, int& out)
{
    std::stringstream stream(value);
    int parsed = 0;
    stream >> parsed;
    if (stream.fail() || !stream.eof()) return false;
    out = parsed;
    return true;
}
} // namespace

bool CAntHelPvpController::ValidateStartTeam(const std::string& value)
{
    int team = 0;
    return ParseIntStrict(value, team) && team >= 2;
}

bool CAntHelPvpController::ValidateSpectatorTeam(const std::string& value)
{
    int team = 0;
    return ParseIntStrict(value, team) && team >= 1;
}

bool CAntHelPvpController::ValidateLockLoadout(const std::string& value)
{
    return ControllerParamValueLegal(EControllerParamType::Bool, value);
}

std::vector<SControllerParamInfo> CAntHelPvpController::ParamSchemaStatic()
{
    return {
        { "start_team", EControllerParamType::Int, "pvp_start_team",
          "team of the first released player (>= 2)", &ValidateStartTeam },
        { "spectator_team", EControllerParamType::Int, "pvp_spectator_team",
          "team for mid-game joins (>= 1)", &ValidateSpectatorTeam },
        { "lock_loadout", EControllerParamType::Bool, "pvp_lock_loadout",
          "strip and lock petal loadouts in this world", &ValidateLockLoadout },
    };
}

CAntHelPvpController::CAntHelPvpController(CControllerParams params)
{
    if (params.Contains("start_team"))
        m_next_team = std::max(2, params.GetInt("start_team", 2));
    else if (int default_team = 0; ParseIntStrict(ResolveControllerParamDefault("pvp_start_team"), default_team))
        m_next_team = std::max(2, default_team);

    if (params.Contains("spectator_team"))
        m_spectator_team = std::max(1, params.GetInt("spectator_team", kSpectatorTeam));
    else if (int default_team = 0; ParseIntStrict(ResolveControllerParamDefault("pvp_spectator_team"), default_team))
        m_spectator_team = std::max(1, default_team);

    if (params.Contains("lock_loadout"))
        m_lock_loadout = params.GetBool("lock_loadout", m_lock_loadout);
    else
    {
        const std::string default_lock = ResolveControllerParamDefault("pvp_lock_loadout");
        if (!default_lock.empty())
            m_lock_loadout = default_lock == "1" || default_lock == "true" || default_lock == "yes" ||
                             default_lock == "on";
    }
}

void CAntHelPvpController::OnTick(CGameWorld& world, float dt)
{
    (void)dt;
    MaintainStrip(world);
}

void CAntHelPvpController::OnPlayerEntityReady(CGameWorld& world, CPlayer& player, CPlayerFlower& flower,
                                               EPlayerSpawnReason reason)
{
    (void)world;
    (void)reason;
    // Respawns create a fresh flower: restore the player's assigned team and
    // strip the loadout that ApplySavedSlots just loaded.
    flower.Team() = AssignTeam(player);
    StripPlayerPetals(player);
}

void CAntHelPvpController::OnPlayerEnteredWorld(CGameWorld& world, CPlayer& player)
{
    (void)world;
    if (CEntity* entity = player.GetEntity()) entity->Team() = AssignTeam(player);
    StripPlayerPetals(player);
}

void CAntHelPvpController::OnPlayerLeftWorld(CGameWorld& world, CPlayer& player)
{
    (void)world;
    // Rejoining a started game counts as a mid-game join (spectator).
    m_player_teams.erase(player.GetId());
}

int CAntHelPvpController::AssignTeam(CPlayer& player)
{
    const std::uint32_t player_id = player.GetId();
    const auto existing = m_player_teams.find(player_id);
    if (existing != m_player_teams.end()) return existing->second;

    int team = m_spectator_team;
    if (!m_started)
    {
        // First arrivals are the waiting room release batch: one player per
        // team, numbering from start_team.
        m_started = true;
        team = m_next_team++;
    }
    m_player_teams[player_id] = team;
    LOG_INFO("pvp", "Player " + std::to_string(player_id) + " (" + player.GetName() +
                        ") assigned team " + std::to_string(team));
    return team;
}

void CAntHelPvpController::StripPlayerPetals(CPlayer& player)
{
    auto* flower = dynamic_cast<CPlayerFlower*>(player.GetEntity());
    if (!flower) return;

    auto& slots = flower->GetSlots();
    for (size_t i = 0; i < slots.size(); ++i)
    {
        if (slots[i].m_p_proto) flower->ForceUnequipPetal(static_cast<int>(i));
        flower->SetBanned(true, static_cast<int>(i));
    }
    flower->DestroyPetalEntities();
    flower->MarkFinalStatsDirty();
}

void CAntHelPvpController::MaintainStrip(CGameWorld& world) const
{
    CGameContext* context = world.GameContext();
    if (!context) return;

    for (const auto& player : context->Players())
    {
        if (!player) continue;
        CEntity* entity = player->GetEntity();
        if (!entity || entity->GameWorld() != &world || entity->IsMarkedForDes()) continue;
        auto* flower = dynamic_cast<CPlayerFlower*>(entity);
        if (!flower) continue;

        auto& slots = flower->GetSlots();
        for (size_t i = 0; i < slots.size(); ++i)
        {
            if (slots[i].m_p_proto) flower->ForceUnequipPetal(static_cast<int>(i));
            flower->SetBanned(true, static_cast<int>(i));
        }
        flower->DestroyPetalEntities();
    }
}

void CAntHelPvpController::CaptureSnapshot(CSnapshotWriter& writer) const
{
    writer.Field("started", m_started);
    writer.Field("next_team", m_next_team);
    writer.Field("spectator_team", m_spectator_team);
    writer.Field("lock_loadout", m_lock_loadout);
    writer.Field("player_teams_size", static_cast<std::uint32_t>(m_player_teams.size()));
    std::uint32_t index = 0;
    for (const auto& [player_id, team] : m_player_teams)
    {
        writer.Field("player_team_id_" + std::to_string(index), player_id);
        writer.Field("player_team_" + std::to_string(player_id), team);
        ++index;
    }
}

bool CAntHelPvpController::RestoreSnapshot(const CSnapshotReader& reader, std::uint32_t version, std::string& error)
{
    if (version > SnapshotVersion())
    {
        error = "Unsupported pvp controller snapshot version " + std::to_string(version);
        return false;
    }

    m_started = reader.Bool("started", false);
    m_next_team = std::max(2, reader.Int("next_team", 2));
    m_spectator_team = std::max(1, reader.Int("spectator_team", kSpectatorTeam));
    m_lock_loadout = reader.Bool("lock_loadout", true);
    m_player_teams.clear();
    const std::uint32_t count = reader.UInt32("player_teams_size", 0);
    for (std::uint32_t i = 0; i < count; ++i)
    {
        const std::uint32_t player_id = reader.UInt32("player_team_id_" + std::to_string(i), 0);
        const int team = reader.Int("player_team_" + std::to_string(player_id), 0);
        if (player_id != 0 && team >= 1) m_player_teams[player_id] = team;
    }
    return true;
}