#include "player_lifecycle_service.h"
#include "../entities/flower.h"
#include "../entities/mob.h"
#include "../gameworld.h"
#include "../player.h"
#include "../zone_mob_tools.h"
#include "../../../Engine/logger.h"
#include "../../../Shared/game_config.h"
#include <algorithm>
#include <limits>
#include <random>
#include <utility>

namespace
{
constexpr const char* new_player_checkpoint_name = "new_players";

std::mt19937& RespawnRng()
{
    static std::mt19937 rng{std::random_device{}()};
    return rng;
}

bool RandomPointInCheckpoint(const FlorrBtMap::Checkpoint& checkpoint, sf::Vector2f& out_pos)
{
    if (checkpoint.w <= 0.f || checkpoint.h <= 0.f) return false;

    std::uniform_real_distribution<float> random_x(checkpoint.x, checkpoint.x + checkpoint.w);
    std::uniform_real_distribution<float> random_y(checkpoint.y, checkpoint.y + checkpoint.h);
    out_pos = {random_x(RespawnRng()), random_y(RespawnRng())};
    return true;
}

bool RandomPointInSpawnZone(const FlorrBtMap::Zone& zone, sf::Vector2f& out_pos)
{
    if (zone.vertices.empty()) return false;

    float min_x = std::numeric_limits<float>::max();
    float min_y = std::numeric_limits<float>::max();
    float max_x = std::numeric_limits<float>::lowest();
    float max_y = std::numeric_limits<float>::lowest();
    for (const auto& vertex : zone.vertices)
    {
        min_x = std::min(min_x, vertex.x);
        min_y = std::min(min_y, vertex.y);
        max_x = std::max(max_x, vertex.x);
        max_y = std::max(max_y, vertex.y);
    }
    if (min_x >= max_x || min_y >= max_y) return false;

    std::uniform_real_distribution<float> random_x(min_x, max_x);
    std::uniform_real_distribution<float> random_y(min_y, max_y);
    for (int attempt = 0; attempt < 32; ++attempt)
    {
        sf::Vector2f candidate = {random_x(RespawnRng()), random_y(RespawnRng())};
        if (!IsPointInZone(zone, candidate)) continue;
        out_pos = candidate;
        return true;
    }
    return false;
}

sf::Vector2f PickRespawnPosition(CGameWorld& world)
{
    const FlorrBtMap* map = world.GetMap();
    if (map)
    {
        std::vector<size_t> checkpoints;
        for (size_t i = 0; i < map->checkpoints.size(); ++i)
        {
            if (map->checkpoints[i].name == new_player_checkpoint_name) continue;
            if (map->checkpoints[i].w > 0.f && map->checkpoints[i].h > 0.f) checkpoints.push_back(i);
        }
        if (!checkpoints.empty())
        {
            std::uniform_int_distribution<size_t> dist(0, checkpoints.size() - 1);
            sf::Vector2f pos;
            if (RandomPointInCheckpoint(map->checkpoints[checkpoints[dist(RespawnRng())]], pos)) return pos;
        }

        std::vector<size_t> zones;
        for (size_t i = 0; i < map->zones.size(); ++i)
        {
            if (map->zones[i].vertices.size() >= 3) zones.push_back(i);
        }
        if (!zones.empty())
        {
            std::uniform_int_distribution<size_t> dist(0, zones.size() - 1);
            for (size_t attempt = 0; attempt < zones.size(); ++attempt)
            {
                sf::Vector2f pos;
                if (RandomPointInSpawnZone(map->zones[zones[dist(RespawnRng())]], pos)) return pos;
            }
        }
    }

    return {game_config::player_respawn_x, game_config::player_respawn_y};
}
}

void CPlayerLifecycleService::ProcessDropPickups(const std::vector<std::unique_ptr<CPlayer>>& players,
                                                 IPlayerLifecycleNotifier& notifier) const
{
    for (const auto& player : players)
    {
        if (!player || !player->IsConnected() || !player->IsAuthenticated()) continue;
        if (player->TickDropPickup()) notifier.QueueInventory(*player);
    }
}

CEntity* CPlayerLifecycleService::Respawn(CPlayer& player, CGameWorld& world) const
{
    if (auto* old_flower = dynamic_cast<CPlayerFlower*>(player.GetEntity()))
    {
        old_flower->PrepareRespawnDestroy();
        player.SetOwnedEntity(nullptr);
    }

    auto entity = CreateMob(EMobType::PlayerFlower, &world, PickRespawnPosition(world), ERarity::Common);
    auto* raw_flower = dynamic_cast<CPlayerFlower*>(entity.get());
    if (!raw_flower) return nullptr;

    raw_flower->m_name = player.GetName();
    raw_flower = dynamic_cast<CPlayerFlower*>(world.InsertEntity(std::move(entity)));
    if (!raw_flower) return nullptr;

    if (auto* controller = world.GetController())
        controller->OnPlayerSpawn(world, &player, raw_flower);
    else
    {
        player.SetOwnedEntity(raw_flower);
        player.ApplySavedProgress();
        player.ApplySavedTalents();
        player.ApplySavedSlots();
    }

    player.m_logged_missing_entity = false;
    LOG_INFO("network", "Player " + std::to_string(player.GetId()) + " respawned");
    return raw_flower;
}

void CPlayerLifecycleService::RespawnDeadControlledEntities(const std::vector<std::unique_ptr<CPlayer>>& players,
                                                            CGameWorld& respawn_world,
                                                            IPlayerLifecycleNotifier& notifier) const
{
    for (const auto& player : players)
    {
        if (!player || !player->IsAuthenticated()) continue;

        CEntity* entity = player->GetEntity();
        if (!entity)
        {
            if (!player->HasOwnedEntity()) continue;
            player->SetOwnedEntity(nullptr);
            if (Respawn(*player, respawn_world) && player->IsConnected())
                NotifyPlayerWorldChanged(*player, notifier);
            continue;
        }
        if (dynamic_cast<CPlayerFlower*>(entity)) continue;
        if (!entity->IsDead()) continue;

        player->SetOwnedEntity(nullptr);
        if (Respawn(*player, respawn_world) && player->IsConnected())
            NotifyPlayerWorldChanged(*player, notifier);
    }
}

void CPlayerLifecycleService::NotifyPlayerLogin(CPlayer& player, IPlayerLifecycleNotifier& notifier) const
{
    notifier.QueueWelcome(player);
    notifier.QueueInventory(player);
    notifier.QueueOwnerStateUpdate(player);
}

void CPlayerLifecycleService::NotifyPlayerWorldChanged(CPlayer& player, IPlayerLifecycleNotifier& notifier) const
{
    notifier.QueueWelcome(player);
    notifier.QueueOwnerStateUpdate(player);
}
