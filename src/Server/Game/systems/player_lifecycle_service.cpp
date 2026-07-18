#include "player_lifecycle_service.h"
#include "../entities/flower.h"
#include "../entities/mob.h"
#include "../gameworld.h"
#include "../player.h"
#include "../../../Engine/logger.h"
#include "../../../Shared/game_config.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <random>
#include <utility>

namespace
{
constexpr int checkpoint_spawn_attempts = 48;
constexpr uint8_t checkpoint_check_interval_ticks = 4;

std::mt19937& RespawnRng()
{
    static std::mt19937 rng{std::random_device{}()};
    return rng;
}

bool RandomPointInCheckpoint(const CGameWorld& world, const FlorrBtMap::Checkpoint& checkpoint, sf::Vector2f& out_pos)
{
    if (checkpoint.w <= 0.f || checkpoint.h <= 0.f) return false;

    std::uniform_real_distribution<float> random_x(checkpoint.x, checkpoint.x + checkpoint.w);
    std::uniform_real_distribution<float> random_y(checkpoint.y, checkpoint.y + checkpoint.h);
    for (int attempt = 0; attempt < checkpoint_spawn_attempts; ++attempt)
    {
        sf::Vector2f candidate = {random_x(RespawnRng()), random_y(RespawnRng())};
        if (world.CircleBlockedByWall(candidate, game_config::mob_player_flower_radius)) continue;
        out_pos = candidate;
        return true;
    }
    return false;
}

bool CheckpointContains(const FlorrBtMap::Checkpoint& checkpoint, sf::Vector2f pos)
{
    return checkpoint.w > 0.f && checkpoint.h > 0.f &&
           pos.x >= checkpoint.x && pos.x <= checkpoint.x + checkpoint.w &&
           pos.y >= checkpoint.y && pos.y <= checkpoint.y + checkpoint.h;
}

sf::Vector2f CheckpointCenter(const FlorrBtMap::Checkpoint& checkpoint)
{
    return {checkpoint.x + checkpoint.w * 0.5f, checkpoint.y + checkpoint.h * 0.5f};
}

bool HasVisitedCheckpoint(const CPlayer& player, const FlorrBtMap::Checkpoint& checkpoint)
{
    for (const SPlayerCheckpointVisit& visit : player.m_cp_history)
    {
        if (visit.level == checkpoint.level && CheckpointContains(checkpoint, visit.pos)) return true;
    }
    return false;
}

int HighestVisitedCheckpointLevel(const CPlayer& player)
{
    int level = 0;
    for (const SPlayerCheckpointVisit& visit : player.m_cp_history)
        level = std::max(level, visit.level);
    return level;
}

void UpdatePlayerCheckpoint(CPlayer& player)
{
    if (!player.IsAuthenticated()) return;

    CEntity* entity = player.GetEntity();
    if (!entity || entity->m_is_marked_for_des || entity->IsDead()) return;

    CGameWorld* world = entity->GameWorld();
    const FlorrBtMap* map = world ? world->GetMap() : nullptr;
    if (!map) return;

    for (const FlorrBtMap::Checkpoint& checkpoint : map->checkpoints)
    {
        if (!checkpoint.is_save || checkpoint.level <= 0) continue;
        if (!CheckpointContains(checkpoint, entity->m_pos)) continue;
        if (HasVisitedCheckpoint(player, checkpoint)) continue;

        player.m_cp_history.push_back({CheckpointCenter(checkpoint), checkpoint.level});
        return;
    }
}

bool ShouldUpdatePlayerCheckpoint(CPlayer& player)
{
    player.m_cp_check_phase = static_cast<uint8_t>((player.m_cp_check_phase + 1) % checkpoint_check_interval_ticks);
    return player.m_cp_check_phase == 0;
}

bool PickVisitedCheckpointRespawnPosition(const CPlayer& player, const CGameWorld& world, const FlorrBtMap& map,
                                          sf::Vector2f death_pos, sf::Vector2f& out_pos)
{
    const int target_level = HighestVisitedCheckpointLevel(player) - 1;
    if (target_level <= 0) return false;

    const SPlayerCheckpointVisit* best_visit = nullptr;
    float best_distance_sq = std::numeric_limits<float>::max();
    for (const SPlayerCheckpointVisit& visit : player.m_cp_history)
    {
        if (visit.level != target_level) continue;
        const sf::Vector2f delta = {visit.pos.x - death_pos.x, visit.pos.y - death_pos.y};
        const float distance_sq = delta.x * delta.x + delta.y * delta.y;
        if (distance_sq >= best_distance_sq) continue;
        best_distance_sq = distance_sq;
        best_visit = &visit;
    }
    if (!best_visit) return false;

    for (const FlorrBtMap::Checkpoint& checkpoint : map.checkpoints)
    {
        if (!checkpoint.is_save) continue;
        if (checkpoint.level != target_level) continue;
        if (checkpoint.w <= 0.f || checkpoint.h <= 0.f) continue;
        if (!CheckpointContains(checkpoint, best_visit->pos)) continue;
        return RandomPointInCheckpoint(world, checkpoint, out_pos);
    }
    return false;
}

sf::Vector2f PickRespawnPosition(CPlayer& player, CGameWorld& world, sf::Vector2f death_pos)
{
    const FlorrBtMap* map = world.GetMap();
    if (map)
    {
        sf::Vector2f saved_pos;
        if (PickVisitedCheckpointRespawnPosition(player, world, *map, death_pos, saved_pos)) return saved_pos;

        std::vector<size_t> checkpoints;
        for (size_t i = 0; i < map->checkpoints.size(); ++i)
        {
            if (!map->checkpoints[i].is_respawn_area) continue;
            if (map->checkpoints[i].w > 0.f && map->checkpoints[i].h > 0.f) checkpoints.push_back(i);
        }
        if (!checkpoints.empty())
        {
            std::uniform_int_distribution<size_t> dist(0, checkpoints.size() - 1);
            sf::Vector2f pos;
            if (RandomPointInCheckpoint(world, map->checkpoints[checkpoints[dist(RespawnRng())]], pos)) return pos;
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
        if (ShouldUpdatePlayerCheckpoint(*player)) UpdatePlayerCheckpoint(*player);
        if (player->TickDropPickup()) notifier.QueueInventory(*player);
    }
}

CEntity* CPlayerLifecycleService::Respawn(CPlayer& player, CGameWorld& world) const
{
    sf::Vector2f death_pos = {game_config::player_respawn_x, game_config::player_respawn_y};
    if (auto* old_flower = dynamic_cast<CPlayerFlower*>(player.GetEntity()))
    {
        death_pos = old_flower->m_pos;
        old_flower->PrepareRespawnDestroy();
        player.SetOwnedEntity(nullptr);
    }

    auto entity = CreateMob(EMobType::PlayerFlower, &world, PickRespawnPosition(player, world, death_pos), ERarity::Common);
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
