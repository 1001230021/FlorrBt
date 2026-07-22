#include "player_lifecycle_service.h"
#include "../entities/flower.h"
#include "../entities/mob.h"
#include "../gameworld.h"
#include "../player.h"
#include "../../../Engine/logger.h"
#include "../../../Shared/game_config.h"
#include "../../../Shared/tools.h"
#include <cmath>
#include <random>
#include <sstream>
#include <string>

namespace
{
constexpr int checkpoint_spawn_attempts = 48;
constexpr int respawn_safety_rings = 24;
constexpr int respawn_safety_angles = 16;
constexpr float respawn_mob_separation_skin = 4.f;
constexpr uint8_t checkpoint_check_interval_ticks = 4;
constexpr uint8_t checkpoint_death_protection_max = 3;

std::mt19937& RespawnRng()
{
    static std::mt19937 rng{std::random_device{}()};
    return rng;
}

bool RespawnPointBlockedByMob(const CGameWorld& world, sf::Vector2f pos, float radius)
{
    bool blocked = false;
    world.ForEachEntity([&](const CEntity* entity)
    {
        if (blocked) return;

        const auto* mob = dynamic_cast<const CMobBase*>(entity);
        if (!mob || mob->m_mob_type == EMobType::PlayerFlower) return;
        if (mob->m_is_marked_for_des || mob->IsDead() || !mob->CanCollide()) return;

        const float min_distance = radius + std::max(0.f, mob->m_radius) + respawn_mob_separation_skin;
        if (DistanceSq(pos, mob->m_pos) <= min_distance * min_distance) blocked = true;
    });
    return blocked;
}

bool RespawnPointSafe(const CGameWorld& world, const FlorrBtMap::Checkpoint& checkpoint, sf::Vector2f pos,
                      float radius)
{
    if (!CheckpointContainsPoint(checkpoint, pos.x, pos.y)) return false;
    if (world.CircleBlockedByWall(pos, radius)) return false;
    return !RespawnPointBlockedByMob(world, pos, radius);
}

bool MoveRespawnPointToSafeSpot(const CGameWorld& world, const FlorrBtMap::Checkpoint& checkpoint,
                                sf::Vector2f& pos, float radius)
{
    if (RespawnPointSafe(world, checkpoint, pos, radius)) return true;

    const float step = std::max(radius * 0.5f, 16.f);
    for (int ring = 1; ring <= respawn_safety_rings; ++ring)
    {
        const float distance = step * static_cast<float>(ring);
        for (int angle_index = 0; angle_index < respawn_safety_angles; ++angle_index)
        {
            const float angle = game_config::pi * 2.f * static_cast<float>(angle_index) /
                                static_cast<float>(respawn_safety_angles);
            sf::Vector2f candidate = {pos.x + std::cos(angle) * distance, pos.y + std::sin(angle) * distance};
            if (!RespawnPointSafe(world, checkpoint, candidate, radius)) continue;
            pos = candidate;
            return true;
        }
    }

    return false;
}

bool RandomPointInCheckpoint(const CGameWorld& world, const FlorrBtMap::Checkpoint& checkpoint, sf::Vector2f& out_pos)
{
    if (checkpoint.w <= 0.f || checkpoint.h <= 0.f) return false;

    std::uniform_real_distribution<float> random_x(0.f, checkpoint.w);
    std::uniform_real_distribution<float> random_y(0.f, checkpoint.h);
    for (int attempt = 0; attempt < checkpoint_spawn_attempts; ++attempt)
    {
        const FlorrBtMap::Point point =
            CheckpointLocalToWorldPoint(checkpoint, random_x(RespawnRng()), random_y(RespawnRng()));
        sf::Vector2f candidate = {point.x, point.y};
        if (!MoveRespawnPointToSafeSpot(world, checkpoint, candidate, game_config::mob_player_flower_radius)) continue;
        out_pos = candidate;
        return true;
    }
    return false;
}

bool CheckpointContains(const FlorrBtMap::Checkpoint& checkpoint, sf::Vector2f pos)
{
    return CheckpointContainsPoint(checkpoint, pos.x, pos.y);
}

sf::Vector2f CheckpointCenter(const FlorrBtMap::Checkpoint& checkpoint)
{
    const FlorrBtMap::Point point = CheckpointCenterPoint(checkpoint);
    return {point.x, point.y};
}

std::string Vec2String(sf::Vector2f pos)
{
    std::ostringstream oss;
    oss << pos.x << "," << pos.y;
    return oss.str();
}

std::string CheckpointLabel(const FlorrBtMap::Checkpoint& checkpoint)
{
    std::ostringstream oss;
    const sf::Vector2f center = CheckpointCenter(checkpoint);
    oss << "id=" << checkpoint.id << " level=" << checkpoint.level
        << " center=" << Vec2String(center) << " size=" << checkpoint.w << "x" << checkpoint.h
        << " rotation=" << checkpoint.rotation;
    return oss.str();
}

std::string CheckpointEntryLabel(const SPlayerCheckpointEntry& entry)
{
    std::ostringstream oss;
    oss << "id=" << entry.checkpoint_id << " level=" << entry.level
        << " count=" << static_cast<int>(entry.count);
    return oss.str();
}

std::string CheckpointStackLabel(const CPlayer& player)
{
    std::ostringstream oss;
    oss << "[";
    for (size_t i = 0; i < player.m_cp_stack.size(); ++i)
    {
        if (i > 0) oss << ", ";
        oss << CheckpointEntryLabel(player.m_cp_stack[i]);
    }
    oss << "]";
    return oss.str();
}

void LogCheckpoint(const CPlayer& player, const std::string& msg)
{
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

        if (checkpoint.id == 0)
        {
            LogCheckpoint(player, "hit checkpoint with missing id at pos=" + Vec2String(entity->m_pos) +
                                      " stack=" + CheckpointStackLabel(player));
            return;
        }

        if (!player.m_cp_stack.empty() && player.m_cp_stack.back().checkpoint_id == checkpoint.id) return;

        const std::string before_stack = CheckpointStackLabel(player);

        while (!player.m_cp_stack.empty() && player.m_cp_stack.back().level > checkpoint.level)
        {
            LogCheckpoint(player, "pop higher checkpoint " + CheckpointEntryLabel(player.m_cp_stack.back()) +
                                      " before hit " + CheckpointLabel(checkpoint));
            player.m_cp_stack.pop_back();
        }

        uint8_t count = checkpoint_death_protection_max;
        if (!player.m_cp_stack.empty() && player.m_cp_stack.back().level == checkpoint.level)
        {
            count = player.m_cp_stack.back().count;
            LogCheckpoint(player, "replace same-level checkpoint " + CheckpointEntryLabel(player.m_cp_stack.back()) +
                                      " with " + CheckpointLabel(checkpoint));
            player.m_cp_stack.pop_back();
        }

        player.m_cp_stack.push_back({checkpoint.id, checkpoint.level, count});
        LogCheckpoint(player, "hit " + CheckpointLabel(checkpoint) + " pos=" + Vec2String(entity->m_pos) +
                                  " stack " + before_stack + " -> " + CheckpointStackLabel(player));
        return;
    }
}

bool ShouldUpdatePlayerCheckpoint(CPlayer& player)
{
    player.m_cp_check_phase = static_cast<uint8_t>((player.m_cp_check_phase + 1) % checkpoint_check_interval_ticks);
    return player.m_cp_check_phase == 0;
}

bool PickCheckpointRespawnPosition(const CPlayer& player, const CGameWorld& world, const FlorrBtMap& map,
                                   const SPlayerCheckpointEntry& entry, sf::Vector2f& out_pos)
{
    if (entry.checkpoint_id == 0)
    {
        LogCheckpoint(player, "skip respawn checkpoint with missing id entry=" + CheckpointEntryLabel(entry));
        return false;
    }

    for (const FlorrBtMap::Checkpoint& checkpoint : map.checkpoints)
    {
        if (!checkpoint.is_save) continue;
        if (checkpoint.id != entry.checkpoint_id) continue;

        if (checkpoint.level != entry.level)
            LogCheckpoint(player, "checkpoint level changed for " + CheckpointEntryLabel(entry) +
                                      " map_level=" + std::to_string(checkpoint.level));

        if (checkpoint.w > 0.f && checkpoint.h > 0.f && RandomPointInCheckpoint(world, checkpoint, out_pos))
        {
            LogCheckpoint(player, "respawn selected " + CheckpointLabel(checkpoint) + " pos=" + Vec2String(out_pos));
            return true;
        }

        LogCheckpoint(player, "respawn point failed for " + CheckpointLabel(checkpoint) +
                                  " entry=" + CheckpointEntryLabel(entry));
        return false;
    }

    LogCheckpoint(player, "respawn checkpoint id not found entry=" + CheckpointEntryLabel(entry));
    return false;
}

bool PickStackCheckpointRespawnPosition(CPlayer& player, const CGameWorld& world, const FlorrBtMap& map,
                                        sf::Vector2f& out_pos)
{
    if (player.m_cp_stack.empty())
    {
        LogCheckpoint(player, "respawn has empty checkpoint stack");
        return false;
    }

    if (player.m_cp_stack.back().count > 0)
    {
        const std::string before_stack = CheckpointStackLabel(player);
        --player.m_cp_stack.back().count;
        const SPlayerCheckpointEntry current_entry = player.m_cp_stack.back();
        if (PickCheckpointRespawnPosition(player, world, map, current_entry, out_pos))
        {
            LogCheckpoint(player, "respawn keep current checkpoint " + CheckpointEntryLabel(current_entry) +
                                      " stack " + before_stack + " -> " + CheckpointStackLabel(player));
            return true;
        }

        LogCheckpoint(player, "respawn pop unresolved protected checkpoint " + CheckpointEntryLabel(current_entry));
        player.m_cp_stack.pop_back();
    }
    else
    {
        const std::string before_stack = CheckpointStackLabel(player);
        const SPlayerCheckpointEntry current_entry = player.m_cp_stack.back();
        player.m_cp_stack.pop_back();
        LogCheckpoint(player, "respawn discard depleted checkpoint " + CheckpointEntryLabel(current_entry) +
                                  " stack " + before_stack + " -> " + CheckpointStackLabel(player));
    }

    while (!player.m_cp_stack.empty())
    {
        const SPlayerCheckpointEntry entry = player.m_cp_stack.back();
        if (PickCheckpointRespawnPosition(player, world, map, entry, out_pos)) return true;
        LogCheckpoint(player, "respawn pop unresolved checkpoint " + CheckpointEntryLabel(entry));
        player.m_cp_stack.pop_back();
    }
    LogCheckpoint(player, "respawn exhausted checkpoint stack");
    return false;
}

sf::Vector2f PickRespawnPosition(CPlayer& player, CGameWorld& world)
{
    const FlorrBtMap* map = world.GetMap();
    if (map)
    {
        sf::Vector2f saved_pos;
        if (PickStackCheckpointRespawnPosition(player, world, *map, saved_pos)) return saved_pos;

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
            const FlorrBtMap::Checkpoint& checkpoint = map->checkpoints[checkpoints[dist(RespawnRng())]];
            if (RandomPointInCheckpoint(world, checkpoint, pos))
            {
                LogCheckpoint(player, "respawn selected respawn_area " + CheckpointLabel(checkpoint) +
                                          " pos=" + Vec2String(pos));
                return pos;
            }
        }
    }

    LogCheckpoint(player, "respawn selected default pos=" +
                              Vec2String({game_config::player_respawn_x, game_config::player_respawn_y}));
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
        if (player->TickDropPickup())
        {
            notifier.QueueInventory(*player);
            notifier.QueueOwnerStateUpdate(*player);
        }
    }
}

CEntity* CPlayerLifecycleService::Respawn(CPlayer& player, CGameWorld& world) const
{
    if (auto* old_flower = dynamic_cast<CPlayerFlower*>(player.GetEntity()))
    {
        old_flower->PrepareRespawnDestroy();
        player.SetOwnedEntity(nullptr);
    }

    auto entity = CreateMob(EMobType::PlayerFlower, &world, PickRespawnPosition(player, world), ERarity::Common);
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
