#include "match_rules.h"
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
#include <stdexcept>

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
}

bool CMatchRules::ValidateStartTeam(const std::string& value)
{
    int team = 0;
    return ParseIntStrict(value, team) && team >= 2 && team <= 254;
}

bool CMatchRules::ValidateSpectatorTeam(const std::string& value)
{
    int team = 0;
    return ParseIntStrict(value, team) && team >= 1 && team <= 255;
}

bool CMatchRules::ValidateLockLoadout(const std::string& value)
{
    return ControllerParamValueLegal(EControllerParamType::Bool, value);
}

std::vector<SControllerParamInfo> CMatchRules::ParamSchemaStatic()
{
    return {
        { "start_team", EControllerParamType::Int, "pvp_start_team",
          "首个参赛阵营（2–254）", &ValidateStartTeam },
        { "spectator_team", EControllerParamType::Int, "pvp_spectator_team",
          "观战阵营（1–255，不能与参赛阵营重叠）", &ValidateSpectatorTeam },
        { "lock_loadout", EControllerParamType::Bool, "pvp_lock_loadout",
          "是否移除并锁定本世界的花瓣装备", &ValidateLockLoadout },
    };
}

CMatchRules::CMatchRules(CControllerParams params, EPetalType preserved)
    : m_preserved(preserved)
{
    if (params.Contains("start_team"))
        m_next_team = params.GetInt("start_team", -1);
    else if (int default_team = 0; ParseIntStrict(ResolveControllerParamDefault("pvp_start_team"), default_team))
        m_next_team = default_team;
    m_start_team = m_next_team;

    if (params.Contains("spectator_team"))
        m_spectator_team = params.GetInt("spectator_team", -1);
    else if (int default_team = 0; ParseIntStrict(ResolveControllerParamDefault("pvp_spectator_team"), default_team))
        m_spectator_team = default_team;

    if (m_start_team < 2 || m_start_team > 254 || m_spectator_team < 1 || m_spectator_team > 255 ||
        (m_spectator_team >= m_start_team && m_spectator_team < m_start_team + kMaxPlayers))
        throw std::invalid_argument("PvP 队号超出协议范围，或观战阵营与参赛阵营重叠");

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

void CMatchRules::OnTick(CGameWorld& world, float dt)
{
    (void)dt;
    MaintainStrip(world);
}

void CMatchRules::OnPlayerEntityReady(CGameWorld& world, CPlayer& player, CPlayerFlower& flower,
                                               EPlayerSpawnReason reason)
{
    (void)world;
    (void)reason;
    // 重生保持原角色，并重新应用模式装备限制。
    flower.SetTeam(AssignTeam(player));
    StripPlayerPetals(player);
}

void CMatchRules::OnPlayerArrivalCommitted(CGameWorld& world, CPlayer& player)
{
    (void)world;
    if (CEntity* entity = player.GetEntity()) entity->SetTeam(AssignTeam(player));
    StripPlayerPetals(player);
}

void CMatchRules::OnPlayerDepartureCommitted(CGameWorld& world, CPlayer& player, CEntity* departing)
{
    (void)world;
    auto* departing_flower = dynamic_cast<CPlayerFlower*>(departing);
    if (LocksPetalLoadout() && departing_flower) player.RestoreSavedLoadout(departing_flower);
    // 模式阵营不带出房间，目标模式在进入时重新分配。
    if (departing_flower) departing_flower->SetTeam(game_config::mob_player_flower_team);
    m_participants.erase(player.GetId());
}

int CMatchRules::AssignTeam(CPlayer& player)
{
    const std::uint32_t player_id = player.GetId();
    const auto existing = m_participants.find(player_id);
    if (existing != m_participants.end() && existing->second.session_generation == player.SessionGeneration())
        return existing->second.team;

    int team = m_spectator_team;
    ERole role = ERole::Spectator;
    if (m_next_team < m_start_team + kMaxPlayers)
    {
        // 保留原有规则：两个初始名额用完后，新进入的会话只能观战。
        m_started = true;
        team = m_next_team++;
        role = ERole::Participant;
    }
    m_participants[player_id] = {team, role, player.SessionGeneration()};
    LOG_INFO("pvp", "Player " + std::to_string(player_id) + " (" + player.GetName() +
                        ") assigned team " + std::to_string(team));
    return team;
}

int CMatchRules::PlayerTeam(std::uint32_t player_id) const
{
    const auto it = m_participants.find(player_id);
    return it == m_participants.end() ? 0 : it->second.team;
}

bool CMatchRules::HasMatchTeam(const CPlayer& player) const
{
    const auto it = m_participants.find(player.GetId());
    return it != m_participants.end() && it->second.role == ERole::Participant &&
        (!it->second.session_generation || it->second.session_generation == player.SessionGeneration());
}

bool CMatchRules::ResolveReferences(CGameWorld& world, const IGameWorldResolver&, std::string&)
{
    const auto players = world.Players();
    for (auto it = m_participants.begin(); it != m_participants.end();)
    {
        const auto player = std::find_if(players.begin(), players.end(), [&](const auto* candidate) {
            return candidate && candidate->GetId() == it->first &&
                (!it->second.session_generation || it->second.session_generation == candidate->SessionGeneration());
        });
        if (player == players.end()) it = m_participants.erase(it);
        else { it->second.session_generation = (*player)->SessionGeneration(); ++it; }
    }
    return true;
}

void CMatchRules::StripPlayerPetals(CPlayer& player)
{
    if (!LocksPetalLoadout()) return;

    auto* flower = dynamic_cast<CPlayerFlower*>(player.GetEntity());
    if (!flower) return;

    auto& slots = flower->GetSlots();
    for (size_t i = 0; i < slots.size(); ++i)
    {
        if (slots[i].m_p_proto && Preserves(slots[i].m_p_proto->m_type)) continue;
        if (slots[i].m_p_proto) flower->ForceUnequipPetal(static_cast<int>(i));
        flower->SetBanned(true, static_cast<int>(i));
    }
    // 仅清理改动槽位，保留模式提供的技能花瓣。
}

void CMatchRules::MaintainStrip(CGameWorld& world) const
{
    if (!LocksPetalLoadout()) return;

    CGameContext* context = world.GameContext();
    if (!context) return;

    for (auto* player : world.Players())
    {
        if (!player) continue;
        CEntity* entity = player->GetEntity();
        if (!entity || entity->GameWorld() != &world || entity->IsMarkedForDes()) continue;
        auto* flower = dynamic_cast<CPlayerFlower*>(entity);
        if (!flower) continue;

        auto& slots = flower->GetSlots();
        for (size_t i = 0; i < slots.size(); ++i)
        {
            if (slots[i].m_p_proto && Preserves(slots[i].m_p_proto->m_type)) continue;
            if (slots[i].m_p_proto) flower->ForceUnequipPetal(static_cast<int>(i));
            flower->SetBanned(true, static_cast<int>(i));
        }
    }
}

void CMatchRules::CaptureSnapshot(CSnapshotWriter& writer) const
{
    writer.Field("started", m_started);
    writer.Field("start_team", m_start_team);
    writer.Field("next_team", m_next_team);
    writer.Field("spectator_team", m_spectator_team);
    writer.Field("lock_loadout", m_lock_loadout);
    writer.Field("participant_schema", 1);
    writer.Field("player_teams_size", static_cast<std::uint32_t>(m_participants.size()));
    std::uint32_t index = 0;
    for (const auto& [player_id, participant] : m_participants)
    {
        writer.Field("player_team_id_" + std::to_string(index), player_id);
        writer.Field("player_team_" + std::to_string(player_id), participant.team);
        writer.Field("player_role_" + std::to_string(player_id), static_cast<int>(participant.role));
        writer.UInt64("player_session_" + std::to_string(player_id), participant.session_generation);
        ++index;
    }
}

bool CMatchRules::RestoreSnapshot(const CSnapshotReader& reader, std::uint32_t version, std::string& error)
{
    if (version > 2)
    {
        error = "Unsupported pvp controller snapshot version " + std::to_string(version);
        return false;
    }

    m_started = reader.Bool("started", false);
    m_start_team = reader.Int("start_team", 2);
    m_next_team = reader.Int("next_team", m_start_team);
    m_spectator_team = reader.Int("spectator_team", kSpectatorTeam);
    if (m_start_team < 2 || m_start_team > 254 || m_spectator_team < 1 || m_spectator_team > 255 ||
        (m_spectator_team >= m_start_team && m_spectator_team < m_start_team + kMaxPlayers) ||
        m_next_team < m_start_team || m_next_team > m_start_team + kMaxPlayers)
    { error = "PvP 快照阵营配置无效"; return false; }
    m_lock_loadout = reader.Bool("lock_loadout", true);
    m_participants.clear();
    constexpr std::uint32_t max_restore_player_teams = 4096;
    const auto count = reader.UInt32("player_teams_size", 0);
    if (count > max_restore_player_teams || reader.Int("participant_schema", 0) > 1)
    { error = "PvP 快照角色记录超出限制"; return false; }
    for (std::uint32_t i = 0; i < count; ++i)
    {
        const std::uint32_t player_id = reader.UInt32("player_team_id_" + std::to_string(i), 0);
        const int team = reader.Int("player_team_" + std::to_string(player_id), 0);
        const bool participant = team >= m_start_team && team < m_start_team + kMaxPlayers;
        const int role = reader.Int("player_role_" + std::to_string(player_id), participant ? 1 : 0);
        const auto generation = reader.UInt64("player_session_" + std::to_string(player_id), 0);
        if (!player_id || m_participants.contains(player_id) || (team != m_spectator_team && !participant) ||
            (role != 0 && role != 1) || (role == 1) != participant ||
            (reader.Has("participant_schema") && !generation))
        { error = "PvP 快照角色身份无效"; return false; }
        m_participants[player_id] = {team, static_cast<ERole>(role), generation};
    }
    return true;
}
