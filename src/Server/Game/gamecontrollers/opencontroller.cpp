#include "opencontroller.h"
#include "../entities/drop.h"
#include "../player.h"
#include "../gameworld.h"
#include "../gamecontext.h"
#include "../entities/flower.h"
#include "../zone_mob_tools.h"
#include "../../Module/network_module.h"
#include "../../../Engine/map_tools.h"
#include "../../../Shared/game_config.h"
#include "../../../Shared/drop_rate.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <random>
#include <unordered_map>

namespace
{
constexpr float spawn_query_radius = game_config::WorldUnits(512.f);
constexpr float min_spawn_distance = game_config::WorldUnits(128.f);
constexpr size_t max_spawn_num = 12;
constexpr int max_spawn_per_zone_tick = 8;
constexpr int max_zone_spawn_target = 512;
constexpr int spawn_position_attempts = 32;
constexpr float rarity_difficulty_scale = 10.f;
constexpr float rarity_gaussian_sigma = 0.36f;
constexpr float drop_spread_radius = game_config::WorldUnits(32.f);
constexpr const char* new_player_checkpoint_name = "new_players";

struct lootable_player
{
    CPlayer* player = nullptr;
    float damage = 0.f;
};

bool IsAboveUltra(ERarity rarity)
{
    return GetLevel(rarity) > GetLevel(ERarity::Ultra);
}

bool IsSuperOrHigher(ERarity rarity)
{
    return GetLevel(rarity) >= GetLevel(ERarity::Super);
}

int ExpForDropRarity(ERarity rarity)
{
    int level = std::clamp(GetLevel(rarity), 0, 30);
    return 1 << level;
}

sf::Vector2f DropSpreadOffset(size_t index, size_t count)
{
    if (count <= 1) return {0.f, 0.f};
    float angle = 2.f * game_config::pi * static_cast<float>(index) / static_cast<float>(count);
    return {std::cos(angle) * drop_spread_radius, std::sin(angle) * drop_spread_radius};
}

std::mt19937& SpawnRng()
{
    static std::mt19937 rng{std::random_device{}()};
    return rng;
}

ERarity PickRarityForDifficulty(float difficulty)
{
    struct rarity_weight
    {
        ERarity rarity = ERarity::Null;
        float weight = 0.f;
    };

    constexpr ERarity naturally_spawned_rarities[] = {
        ERarity::Common,
        ERarity::Unusual,
        ERarity::Rare,
        ERarity::Epic,
        ERarity::Legendary,
        ERarity::Mythic,
        ERarity::Ultra,
        ERarity::Super,
        ERarity::Eternal,
        ERarity::Primordial,
    };

    if (difficulty <= 0) return ERarity::Common;

    const float center_level = std::clamp(difficulty / rarity_difficulty_scale,
                                          static_cast<float>(GetLevel(ERarity::Common)),
                                          static_cast<float>(GetLevel(ERarity::Primordial)));
    const float two_sigma_sq = 2.f * rarity_gaussian_sigma * rarity_gaussian_sigma;

    std::vector<rarity_weight> weights;
    weights.reserve(std::size(naturally_spawned_rarities));

    float total_weight = 0.f;
    for (ERarity rarity : naturally_spawned_rarities)
    {
        const float delta = static_cast<float>(GetLevel(rarity)) - center_level;
        const float weight = std::exp(-(delta * delta) / two_sigma_sq);
        if (weight <= 0.f) continue;

        weights.push_back({rarity, weight});
        total_weight += weight;
    }

    if (weights.empty() || total_weight <= 0.f) return ERarity::Common;

    std::uniform_real_distribution<float> dist(0.f, total_weight);
    float roll = dist(SpawnRng());
    for (const rarity_weight& entry : weights)
    {
        if (roll <= entry.weight) return entry.rarity;
        roll -= entry.weight;
    }

    return weights.back().rarity;
}

bool RandomPointInZone(const FlorrBtMap::Zone& zone, sf::Vector2f& out_pos)
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
    for (int attempt = 0; attempt < spawn_position_attempts; ++attempt)
    {
        sf::Vector2f candidate = {random_x(SpawnRng()), random_y(SpawnRng())};
        if (!IsPointInZone(zone, candidate)) continue;

        out_pos = candidate;
        return true;
    }

    return false;
}

bool RandomPointInCheckpoint(const FlorrBtMap::Checkpoint& checkpoint, sf::Vector2f& out_pos)
{
    if (checkpoint.w <= 0.f || checkpoint.h <= 0.f) return false;
    std::uniform_real_distribution<float> random_x(checkpoint.x, checkpoint.x + checkpoint.w);
    std::uniform_real_distribution<float> random_y(checkpoint.y, checkpoint.y + checkpoint.h);
    out_pos = {random_x(SpawnRng()), random_y(SpawnRng())};
    return true;
}

sf::Vector2f PickNewPlayerSpawnPosition(CGameWorld& world)
{
    const FlorrBtMap* map = world.GetMap();
    if (map)
    {
        std::vector<const FlorrBtMap::Checkpoint*> candidates;
        for (const FlorrBtMap::Checkpoint& checkpoint : map->checkpoints)
        {
            if (checkpoint.name != new_player_checkpoint_name) continue;
            if (checkpoint.w <= 0.f || checkpoint.h <= 0.f) continue;
            candidates.push_back(&checkpoint);
        }

        if (!candidates.empty())
        {
            std::uniform_int_distribution<size_t> dist(0, candidates.size() - 1);
            sf::Vector2f pos;
            if (RandomPointInCheckpoint(*candidates[dist(SpawnRng())], pos)) return pos;
        }
    }

    return {game_config::player_respawn_x, game_config::player_respawn_y};
}

sf::Vector2f PickReturningPlayerSpawnPosition(CGameWorld& world)
{
    const FlorrBtMap* map = world.GetMap();
    if (map)
    {
        std::vector<const FlorrBtMap::Checkpoint*> candidates;
        for (const FlorrBtMap::Checkpoint& checkpoint : map->checkpoints)
        {
            if (checkpoint.name == new_player_checkpoint_name) continue;
            if (checkpoint.w <= 0.f || checkpoint.h <= 0.f) continue;
            candidates.push_back(&checkpoint);
        }

        if (!candidates.empty())
        {
            std::uniform_int_distribution<size_t> dist(0, candidates.size() - 1);
            sf::Vector2f pos;
            if (RandomPointInCheckpoint(*candidates[dist(SpawnRng())], pos)) return pos;
        }

        std::vector<const FlorrBtMap::Zone*> zones;
        for (const FlorrBtMap::Zone& zone : map->zones)
        {
            if (zone.vertices.size() >= 3) zones.push_back(&zone);
        }

        if (!zones.empty())
        {
            std::uniform_int_distribution<size_t> dist(0, zones.size() - 1);
            for (size_t attempt = 0; attempt < zones.size(); ++attempt)
            {
                sf::Vector2f pos;
                if (RandomPointInZone(*zones[dist(SpawnRng())], pos)) return pos;
            }
        }
    }

    return {game_config::player_respawn_x, game_config::player_respawn_y};
}

float ZoneArea(const FlorrBtMap::Zone& zone)
{
    const std::vector<FlorrBtMap::Point>& vertices = zone.vertices;
    if (vertices.size() < 3) return 0.f;

    double area = 0.0;
    for (size_t i = 0, j = vertices.size() - 1; i < vertices.size(); j = i++)
        area += static_cast<double>(vertices[j].x) * static_cast<double>(vertices[i].y) -
                static_cast<double>(vertices[i].x) * static_cast<double>(vertices[j].y);
    area = std::abs(area) * 0.5;
    if (!std::isfinite(area) || area <= 0.0) return 0.f;
    return static_cast<float>(std::min(area, static_cast<double>(std::numeric_limits<float>::max())));
}

int ZoneTargetMobCount(const FlorrBtMap::Zone& zone)
{
    if (zone.density <= 0.f) return 0;

    const double desired = static_cast<double>(ZoneArea(zone)) * static_cast<double>(zone.density);
    if (!std::isfinite(desired) || desired <= 0.0) return 0;

    const double capped_desired = std::min(desired, static_cast<double>(max_zone_spawn_target));
    int target = static_cast<int>(std::floor(capped_desired));
    const double fractional = capped_desired - std::floor(capped_desired);
    if (target < max_zone_spawn_target && fractional > 0.0)
    {
        std::uniform_real_distribution<double> dist(0.0, 1.0);
        if (dist(SpawnRng()) < fractional) ++target;
    }
    return std::clamp(target, 0, max_zone_spawn_target);
}

bool CountsForZoneDensity(const CMobBase* mob)
{
    if (!mob || mob->m_is_marked_for_des || mob->IsDead()) return false;
    if (mob->m_mob_type == EMobType::PlayerFlower) return false;
    if (mob->m_mob_type == EMobType::SummonedBeetle || mob->m_mob_type == EMobType::SummonedSoldierAnt) return false;
    return true;
}

int CountMobsInZone(const FlorrBtMap::Zone& zone, const std::vector<CEntity*>& entities)
{
    int count = 0;
    for (const CEntity* entity : entities)
    {
        const auto* mob = dynamic_cast<const CMobBase*>(entity);
        if (!CountsForZoneDensity(mob)) continue;
        if (!IsPointInZone(zone, mob->m_pos)) continue;
        ++count;
    }
    return count;
}

bool IsFarFromPendingSpawns(const sf::Vector2f& pos, const std::vector<sf::Vector2f>& pending_spawns)
{
    for (const sf::Vector2f& pending_pos : pending_spawns)
        if (Length(pending_pos - pos) <= min_spawn_distance)
            return false;
    return true;
}

bool CanSpawnAt(CGameWorld& world, const sf::Vector2f& pos)
{
    int nearby_mobs = 0;
    CEntity* closest_mob = world.FindClosestEntity(pos, spawn_query_radius, [&nearby_mobs](const CEntity* entity)
    {
        const auto* mob = dynamic_cast<const CMobBase*>(entity);
        if (!mob || mob->m_is_marked_for_des || mob->IsDead()) return false;
        if (mob->m_mob_type == EMobType::PlayerFlower) return false;
        ++nearby_mobs;
        return true;
    });

    if (nearby_mobs >= static_cast<int>(max_spawn_num)) return false;
    if (!closest_mob) return true;
    return Length(closest_mob->m_pos - pos) > min_spawn_distance;
}

bool HasNearbySuperOrHigherMob(CGameWorld& world, const sf::Vector2f& pos, float radius)
{
    if (radius <= 0.f) return false;
    return world.FindClosestEntity(pos, radius, [](const CEntity* entity)
    {
        const auto* mob = dynamic_cast<const CMobBase*>(entity);
        if (!mob || mob->m_is_marked_for_des || mob->IsDead()) return false;
        if (mob->m_mob_type == EMobType::PlayerFlower) return false;
        return IsSuperOrHigher(mob->GetRarity());
    }) != nullptr;
}

bool ShouldSkipSuperOrHigherSpawn(CGameWorld& world, const sf::Vector2f& pos, ERarity rarity)
{
    if (!IsSuperOrHigher(rarity)) return false;

    if (HasNearbySuperOrHigherMob(world, pos, game_config::open_super_plus_block_radius))
        return true;

    if (!HasNearbySuperOrHigherMob(world, pos, game_config::open_super_plus_suppress_radius))
        return false;

    float multiplier = std::clamp(game_config::open_super_plus_suppress_multiplier, 0.f, 1.f);
    if (multiplier >= 1.f) return false;
    if (multiplier <= 0.f) return true;

    std::uniform_real_distribution<float> dist(0.f, 1.f);
    return dist(SpawnRng()) >= multiplier;
}

std::vector<lootable_player> FindLootablePlayers(const CMobBase& mob)
{
    const bool above_ultra = IsAboveUltra(mob.GetRarity());
    const size_t max_lootable =
        above_ultra ? game_config::max_lootable_player_above_ultra : game_config::max_lootable_players;
    const float min_damage_rate = above_ultra ? 0.01f : 0.05f;

    float total_damage = 0.f;
    std::unordered_map<CPlayer*, float> damage_by_player;
    for (const CDamageData& damage_data : mob.GetDamageData())
    {
        if (!damage_data.m_player || damage_data.m_total_dmg <= 0.f) continue;
        total_damage += damage_data.m_total_dmg;
        damage_by_player[damage_data.m_player] += damage_data.m_total_dmg;
    }

    const SMobStats* stats = mob.GetFinalStats();
    const float max_health = stats ? stats->max_health : 0.f;
    const float min_damage = std::min(total_damage, max_health) * min_damage_rate;

    std::vector<lootable_player> candidates;
    candidates.reserve(damage_by_player.size());
    for (const auto& [player, damage] : damage_by_player)
    {
        if (damage >= min_damage) candidates.push_back({player, damage});
    }

    std::sort(candidates.begin(), candidates.end(), [](const lootable_player& lhs, const lootable_player& rhs)
    {
        return lhs.damage > rhs.damage;
    });
    if (candidates.size() > max_lootable) candidates.resize(max_lootable);
    return candidates;
}
}

void COpenController::OnTick(CGameWorld& world, float dt)
{
    if (m_count <= 0)
    {
        SpawnMobs(world);
        m_count = game_config::open_spawn_interval;
    } else {
        m_count -= dt;
    }
}

void COpenController::SpawnMobs(CGameWorld& world)
{
    const FlorrBtMap* map = world.GetMap();
    if (!map)
    {
        LOG_WARN("opencontroller", "Spawn skipped: world has no map");
        return;
    }

    int spawned = 0;
    int skipped_density = 0;
    int skipped_position = 0;
    int skipped_spacing = 0;
    int skipped_pool = 0;
    int skipped_create = 0;
    const std::vector<CEntity*> entities = world.GetAllEntities();
    for (const FlorrBtMap::Zone& zone : map->zones)
    {
        const int target_mobs = ZoneTargetMobCount(zone);
        if (target_mobs <= 0)
        {
            ++skipped_density;
            continue;
        }

        int current_mobs = CountMobsInZone(zone, entities);
        if (current_mobs >= target_mobs)
        {
            continue;
        }

        std::vector<SZoneMobEntry> mob_entries = ParseZoneMobEntries(zone.mobs);
        if (mob_entries.empty())
        {
            ++skipped_pool;
            LOG_WARN("opencontroller", "Spawn zone skipped: no mob type for pool '" + zone.mobs + "'");
            continue;
        }

        std::vector<sf::Vector2f> pending_spawns;
        const int spawn_budget = std::min({target_mobs - current_mobs, max_spawn_per_zone_tick});
        for (int i = 0; i < spawn_budget; ++i)
        {
            sf::Vector2f pos;
            if (!RandomPointInZone(zone, pos))
            {
                ++skipped_position;
                continue;
            }
            if (!CanSpawnAt(world, pos) || !IsFarFromPendingSpawns(pos, pending_spawns))
            {
                ++skipped_spacing;
                continue;
            }

            EMobType mob_type = PickZoneMobType(mob_entries);
            if (mob_type == EMobType::None)
            {
                ++skipped_pool;
                continue;
            }

            ERarity rarity = PickRarityForDifficulty(zone.difficulty);
            if (ShouldSkipSuperOrHigherSpawn(world, pos, rarity))
                continue;

            auto mob = CreateMob(mob_type, &world, pos, rarity);
            if (mob)
            {
                world.InsertEntity(std::move(mob));
                pending_spawns.push_back(pos);
                ++current_mobs;
                ++spawned;
            } else {
                ++skipped_create;
            }
        }
    }
}

void COpenController::OnPlayerConnect(CGameWorld& world, CPlayer* player)
{
    if (!player) return;

    sf::Vector2f spawn_pos = player->ConsumeUseNewPlayerSpawn()
                                 ? PickNewPlayerSpawnPosition(world)
                                 : PickReturningPlayerSpawnPosition(world);
    auto entity = CreateMob(EMobType::PlayerFlower, &world, spawn_pos, ERarity::Common);
    CEntity* raw_entity = world.InsertEntity(std::move(entity));
    if (raw_entity) OnPlayerSpawn(world, player, raw_entity);
}

void COpenController::OnPlayerSpawn(CGameWorld& world, CPlayer* player, CEntity* entity)
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
}

void COpenController::OnEntityDie(CGameWorld& world, CEntity* entity)
{
    auto* mob = dynamic_cast<CMobBase*>(entity);
    if (!mob) return;

    std::vector<SDropRate> drops = RollDrops(mob->m_mob_type, mob->GetRarity());
    std::vector<lootable_player> lootable_players = FindLootablePlayers(*mob);
    const size_t total_drop_count = drops.size() * lootable_players.size();
    size_t drop_index = 0;
    for (const lootable_player& lootable : lootable_players)
    {
        if (!lootable.player) continue;
        auto* player_flower = dynamic_cast<CPlayerFlower*>(lootable.player->GetEntity());
        for (const SDropRate& drop : drops)
        {
            sf::Vector2f drop_pos = mob->m_pos + DropSpreadOffset(drop_index++, total_drop_count);
            auto drop_entity = std::make_unique<CDrop>(&world, drop_pos, drop.type, drop.rarity, lootable.player->GetId());
            world.InsertEntity(std::move(drop_entity));
            if (player_flower) player_flower->TakeExp(ExpForDropRarity(drop.rarity));
        }
        if (player_flower)
        {
            if (CGameContext* context = world.GameContext()) context->Network().QueueOwnerStateUpdate(*lootable.player);
        }
    }
}
