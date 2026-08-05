#include "waitingcontroller.h"
#include "../../HotReload/snapshot_archive.h"
#include "../../../Shared/tools.h"
#include "../entities/flower.h"
#include "../gamecontext.h"
#include "../gameworld.h"
#include "../player.h"
#include <algorithm>

namespace
{
sf::Vector2f PickWaitingSpawnPosition(CGameWorld& world)
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
} // namespace

CWaitingController::CWaitingController(SWaitingControllerConfig config) : m_config(config)
{
    m_config.minimum_player_count = std::max(1u, m_config.minimum_player_count);
}

void CWaitingController::OnActivate(CGameWorld& world) { MaintainPetalBans(world); }

void CWaitingController::OnDeactivate(CGameWorld& world)
{
    for (CPlayer* player : CollectWaitingPlayers(world))
        SetPlayerPetalsBanned(player, false);
}

void CWaitingController::OnTick(CGameWorld& world, float dt)
{
    (void)dt;
    MaintainPetalBans(world);

    const size_t player_count = CountWaitingPlayers(world);
    if (!ShouldRelease(player_count)) return;

    CGameWorld* destination_world = ResolveDestinationWorld(world);
    if (!destination_world || destination_world == &world) return;

    ReleasePlayers(world, *destination_world);
}

void CWaitingController::CaptureSnapshot(CSnapshotWriter& writer) const
{
    writer.Field("release_policy", WaitingReleasePolicyKey(m_config.release_policy));
    writer.Field("minimum_player_count", m_config.minimum_player_count);
    writer.Field("destination_world_id", m_config.destination_world_id);
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
    if (m_config.destination_world_id == SWaitingControllerConfig::invalid_world_id)
    {
        error = "Waiting controller has no destination world";
        return false;
    }

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

std::optional<sf::Vector2f> CWaitingController::SelectPlayerSpawn(CGameWorld& world, CPlayer& player,
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

bool CWaitingController::ShouldRelease(size_t player_count) const
{
    switch (m_config.release_policy)
    {
    case EWaitingReleasePolicy::Disabled:
        return false;
    case EWaitingReleasePolicy::MinimumPlayers:
        return player_count >= static_cast<size_t>(m_config.minimum_player_count);
    }
    return false;
}

CGameWorld* CWaitingController::ResolveDestinationWorld(CGameWorld& world) const
{
    if (m_config.destination_world_id == SWaitingControllerConfig::invalid_world_id) return nullptr;
    CGameContext* context = world.GameContext();
    return context ? context->Worlds().FindWorldById(m_config.destination_world_id) : nullptr;
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
        if (!entity || entity->GameWorld() != &world || entity->m_is_marked_for_des || entity->IsDead()) continue;
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
