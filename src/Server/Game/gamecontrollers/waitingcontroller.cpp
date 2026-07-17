#include "waitingcontroller.h"
#include "../entities/flower.h"
#include "../entities/mob.h"
#include "../gamecontext.h"
#include "../gameworld.h"
#include "../player.h"
#include "../../../Shared/tools.h"
#include <utility>

namespace
{
sf::Vector2f PickWaitingSpawnPosition(CGameWorld& world)
{
    const FlorrBtMap* map = world.GetMap();
    if (!map)
        return {0.f, 0.f};

    for (const FlorrBtMap::Checkpoint& checkpoint : map->checkpoints)
    {
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
}

CWaitingController::CWaitingController(release_predicate predicate, CGameWorld* destination_world)
    : m_release_predicate(std::move(predicate)), m_p_destination_world(destination_world)
{
}

CWaitingController::CWaitingController(std::function<bool(size_t)> predicate, CGameWorld* destination_world)
    : CWaitingController(
          [predicate = std::move(predicate)](CGameWorld&, size_t player_count)
          {
              return predicate ? predicate(player_count) : false;
          },
          destination_world)
{
}

void CWaitingController::OnTick(CGameWorld& world, float dt)
{
    (void)dt;
    MaintainPetalBans(world);

    const size_t player_count = CountWaitingPlayers(world);
    if (!m_p_destination_world || !m_release_predicate || !m_release_predicate(world, player_count)) return;

    ReleasePlayers(world);
}

void CWaitingController::OnPlayerConnect(CGameWorld& world, CPlayer* player)
{
    if (!player) return;

    auto entity = CreateMob(EMobType::PlayerFlower, &world, PickWaitingSpawnPosition(world), ERarity::Common);
    CEntity* raw_entity = world.InsertEntity(std::move(entity));
    if (raw_entity) OnPlayerSpawn(world, player, raw_entity);
}

void CWaitingController::OnPlayerSpawn(CGameWorld& world, CPlayer* player, CEntity* entity)
{
    (void)world;
    if (!player) return;

    auto* flower = dynamic_cast<CPlayerFlower*>(entity);
    if (!flower) return;

    flower->m_name = player->GetName();
    player->SetOwnedEntity(flower);
    player->ApplySavedProgress();
    player->ApplySavedTalents();
    player->ApplySavedSlots();
    SetPlayerPetalsBanned(player, true);
}

void CWaitingController::OnEntityDie(CGameWorld& world, CEntity* entity)
{
    (void)world;
    (void)entity;
}

size_t CWaitingController::CountWaitingPlayers(CGameWorld& world) const
{
    return CollectWaitingPlayers(world).size();
}

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

void CWaitingController::ReleasePlayers(CGameWorld& world)
{
    if (!m_p_destination_world) return;

    std::vector<CPlayer*> players = CollectWaitingPlayers(world);
    for (CPlayer* player : players)
    {
        if (!player) continue;
        SetPlayerPetalsBanned(player, false);
        world.TransferPlayerEntityToWorld(*player, *m_p_destination_world);
    }
}
