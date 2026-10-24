#include "pvp_controller.h"
#include "../../HotReload/snapshot_archive.h"
#include "../../../Engine/logger.h"
#include "../../../Shared/game_config.h"
#include "../entities/flower.h"
#include "../entities/petals/petal.h"
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
          "team for over-cap joins (>= 1)", &ValidateSpectatorTeam },
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
    m_start_team = m_next_team;

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
    // strip the loadout that LoadSavedSlots just loaded.
    flower.Team() = AssignTeam(player);
    StripPlayerPetals(player);
}

void CAntHelPvpController::OnPlayerArrivalCommitted(CGameWorld& world, CPlayer& player)
{
    (void)world;
    if (CEntity* entity = player.GetEntity()) entity->Team() = AssignTeam(player);
    StripPlayerPetals(player);
}

void CAntHelPvpController::OnPlayerDepartureCommitted(CGameWorld& world, CPlayer& player, CEntity* departing)
{
    (void)world;
    auto* departing_flower = dynamic_cast<CPlayerFlower*>(departing);
    if (LocksPetalLoadout() && departing_flower) player.RestoreSavedLoadout(departing_flower);
    // Match teams are local to this controller. Returning to a cooperative
    // world/waiting room must not retain an enemy PvP team. A destination PvP
    // controller will assign its own team in OnPlayerArrivalCommitted.
    if (departing_flower) departing_flower->Team() = game_config::mob_player_flower_team;
    // Rejoining a started game counts as a mid-game join (spectator).
    m_player_teams.erase(player.GetId());
}

int CAntHelPvpController::AssignTeam(CPlayer& player)
{
    const std::uint32_t player_id = player.GetId();
    const auto existing = m_player_teams.find(player_id);
    if (existing != m_player_teams.end()) return existing->second;

    int team = m_spectator_team;
    if (m_next_team < m_start_team + kMaxPlayers)
    {
        // Arrivals up to the fixed 2-player cap each get their own team
        // (start_team, start_team + 1); everyone after that spectates.
        m_started = true;
        team = m_next_team++;
    }
    m_player_teams[player_id] = team;
    LOG_INFO("pvp", "Player " + std::to_string(player_id) + " (" + player.GetName() +
                        ") assigned team " + std::to_string(team));
    return team;
}

int CAntHelPvpController::PlayerTeam(std::uint32_t player_id) const
{
    const auto it = m_player_teams.find(player_id);
    return it == m_player_teams.end() ? 0 : it->second;
}

bool CAntHelPvpController::HasMatchTeam(std::uint32_t player_id) const
{
    return PlayerTeam(player_id) >= m_start_team;
}

void CAntHelPvpController::StripPlayerPetals(CPlayer& player)
{
    if (!LocksPetalLoadout()) return;

    auto* flower = dynamic_cast<CPlayerFlower*>(player.GetEntity());
    if (!flower) return;

    auto& slots = flower->GetSlots();
    for (size_t i = 0; i < slots.size(); ++i)
    {
        if (slots[i].m_p_proto && PreservesControllerPetal(slots[i].m_p_proto->m_type)) continue;
        if (slots[i].m_p_proto) flower->ForceUnequipPetal(static_cast<int>(i));
        flower->SetBanned(true, static_cast<int>(i));
    }
    // ForceUnequipPetal already clears that slot's entities. A blanket
    // destruction here also destroys the controller-owned petals we kept.
}

void CAntHelPvpController::MaintainStrip(CGameWorld& world) const
{
    if (!LocksPetalLoadout()) return;

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
            if (slots[i].m_p_proto && PreservesControllerPetal(slots[i].m_p_proto->m_type)) continue;
            if (slots[i].m_p_proto) flower->ForceUnequipPetal(static_cast<int>(i));
            flower->SetBanned(true, static_cast<int>(i));
        }
    }
}

void CAntHelPvpController::CaptureSnapshot(CSnapshotWriter& writer) const
{
    writer.Field("started", m_started);
    writer.Field("start_team", m_start_team);
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
    m_start_team = std::max(2, reader.Int("start_team", 2));
    m_next_team = std::max(2, reader.Int("next_team", m_start_team));
    m_spectator_team = std::max(1, reader.Int("spectator_team", kSpectatorTeam));
    m_lock_loadout = reader.Bool("lock_loadout", true);
    m_player_teams.clear();
    constexpr std::uint32_t max_restore_player_teams = 4096;
    const std::uint32_t count = std::min(reader.UInt32("player_teams_size", 0), max_restore_player_teams);
    for (std::uint32_t i = 0; i < count; ++i)
    {
        const std::uint32_t player_id = reader.UInt32("player_team_id_" + std::to_string(i), 0);
        const int team = reader.Int("player_team_" + std::to_string(player_id), 0);
        if (player_id != 0 && team >= 1) m_player_teams[player_id] = team;
    }
    return true;
}
