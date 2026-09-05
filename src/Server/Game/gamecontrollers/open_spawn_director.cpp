#include "open_spawn_director.h"
#include "spawn_rules.h"
#include "../../../Engine/map_tools.h"
#include "../../../Shared/game_config.h"
#include "../../HotReload/snapshot_archive.h"
#include "../controllers/melee_controller.h"
#include "../entities/mob.h"
#include "../gamecontext.h"
#include "../gamecontroller.h"
#include "../gameworld.h"
#include "../player.h"
#include "../zone_mob_tools.h"
#include "../../server.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <random>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace
{
constexpr std::string_view new_player_checkpoint_name = "new_players";

struct SZoneBounds
{
    florrbt::Vector2f center = { 0.f, 0.f };
    float radius = 0.f;
    float min_x = 0.f;
    float min_y = 0.f;
    float max_x = 0.f;
    float max_y = 0.f;
};

struct SZoneCache
{
    SZoneBounds bounds;
    float area = 0.f;
    std::vector<SZoneMobEntry> mob_entries;
    std::string spawn_profile;
};

struct SSingleSpawnZoneMob
{
    int id = -1;
    std::uint64_t generation = 0;
};

struct SPendingSpawn
{
    florrbt::Vector2f pos = { 0.f, 0.f };
    float radius = 0.f;
};

struct SSpawnZoneWork
{
    size_t zone_index = 0;
    const FlorrBtMap::Zone* zone = nullptr;
    SZoneBounds bounds;
    int target_mobs = 0;
    int nearby_mob_limit = 0;
    int current_mobs = 0;
    int population_limit = 0;
    bool single_spawn = false;
    bool rare_only = false;
    const std::vector<SZoneMobEntry>* mob_entries = nullptr;
    float difficulty = 0.f;
};

struct SSpawnScratch
{
    std::vector<florrbt::Vector2f> player_positions;
    std::vector<size_t> active_zone_indices;
    std::vector<size_t> rare_zone_indices;
    std::vector<SSpawnZoneWork> zone_work;
    std::vector<SSpawnZoneWork> rare_zone_work;
    std::vector<SPendingSpawn> pending_spawns;
    std::unordered_set<int> natural_rare_types;
};

// All open-world directors run on the same server thread. Cache the expensive
// cross-world occupancy scan for one server tick, then extend the cache as a
// director inserts a natural rare mob. Without this, N open-world instances
// each scan all N worlds on the same tick.
struct SNaturalRareTypeCache
{
    CServer* server = nullptr;
    std::uint64_t tick = 0;
    bool valid = false;
    std::unordered_set<int> types;
};

SNaturalRareTypeCache g_natural_rare_type_cache;

SZoneBounds BuildZoneBounds(const FlorrBtMap::Zone& zone)
{
    SZoneBounds bounds;
    if (zone.vertices.empty()) return bounds;

    float min_x = zone.vertices.front().x;
    float max_x = zone.vertices.front().x;
    float min_y = zone.vertices.front().y;
    float max_y = zone.vertices.front().y;
    for (const auto& vertex : zone.vertices)
    {
        min_x = std::min(min_x, vertex.x);
        max_x = std::max(max_x, vertex.x);
        min_y = std::min(min_y, vertex.y);
        max_y = std::max(max_y, vertex.y);
    }

    bounds.center = { (min_x + max_x) * 0.5f, (min_y + max_y) * 0.5f };
    bounds.radius = Distance(bounds.center, { max_x, max_y });
    bounds.min_x = min_x;
    bounds.min_y = min_y;
    bounds.max_x = max_x;
    bounds.max_y = max_y;
    return bounds;
}

float ComputeZoneArea(const FlorrBtMap::Zone& zone)
{
    const std::vector<FlorrBtMap::Point>& vertices = zone.vertices;
    if (vertices.size() < 3) return 0.f;

    double area = 0.0;
    for (size_t i = 0, j = vertices.size() - 1; i < vertices.size(); j = i++)
        area += static_cast<double>(vertices[j].x) * static_cast<double>(vertices[i].y) -
                static_cast<double>(vertices[i].x) * static_cast<double>(vertices[j].y);
    area = std::abs(area) * 0.5;
    return (!std::isfinite(area) || area <= 0.0)
               ? 0.f
               : static_cast<float>(std::min(area, static_cast<double>(std::numeric_limits<float>::max())));
}

bool IsSingleSpawnZone(const FlorrBtMap::Zone& zone)
{
    return zone.density == 0.f && (!zone.mobs.empty() || !zone.spawn_profile.empty());
}

bool CountsForZoneDensity(const CMobBase* mob)
{
    if (!mob || mob->IsMarkedForDes() || mob->IsDead()) return false;
    if (mob->GetMobType() == EMobType::PlayerFlower) return false;
    if (mob->GetMobType() == EMobType::SummonedBeetle || mob->GetMobType() == EMobType::SummonedSoldierAnt) return false;
    if (dynamic_cast<const CSummonedMeleeController*>(mob->GetController())) return false;
    return true;
}

bool IsNaturalRareRarity(ERarity rarity)
{
    return rarity == ERarity::Eternal || rarity == ERarity::Unique;
}

// Collect the server-wide natural ET/Unique occupancy once per spawn pass.
// This avoids scanning every world for every rarity roll and keeps the
// uniqueness rule separate from console/summoned/precise spawn paths.
void CollectNaturalRareMobTypes(CGameWorld& source_world, std::unordered_set<int>& result)
{
    result.clear();
    CServer* server = CServer::GetInstance();
    if (server)
    {
        const std::uint64_t tick = server->GetTick();
        if (g_natural_rare_type_cache.valid && g_natural_rare_type_cache.server == server &&
            g_natural_rare_type_cache.tick == tick)
        {
            result = g_natural_rare_type_cache.types;
            return;
        }

        g_natural_rare_type_cache.server = server;
        g_natural_rare_type_cache.tick = tick;
        g_natural_rare_type_cache.valid = true;
        g_natural_rare_type_cache.types.clear();
    }

    auto collect_world = [&result](CGameWorld* world) {
        if (!world) return;
        world->ForEachEntity([&](CEntity* entity) {
            if (!entity || entity->IsMarkedForDes()) return;
            const auto* mob = dynamic_cast<const CMobBase*>(entity);
            if (!mob || mob->IsDead() || !mob->IsNaturallySpawned()) return;
            if (IsNaturalRareRarity(mob->GetRarity()))
                result.insert(static_cast<int>(mob->GetMobType()));
        });
    };

    if (server)
    {
        for (CGameWorld* world : server->GetWorlds())
            collect_world(world);
    }
    else
    {
        collect_world(&source_world);
    }

    if (server) g_natural_rare_type_cache.types = result;
}

void RememberNaturalRareMobType(const CMobBase& mob, std::unordered_set<int>& result)
{
    if (!IsNaturalRareRarity(mob.GetRarity())) return;

    const int type = static_cast<int>(mob.GetMobType());
    result.insert(type);
    CServer* server = CServer::GetInstance();
    if (server && g_natural_rare_type_cache.valid && g_natural_rare_type_cache.server == server &&
        g_natural_rare_type_cache.tick == server->GetTick())
        g_natural_rare_type_cache.types.insert(type);
}

bool PointInZoneBounds(const SZoneBounds& bounds, const florrbt::Vector2f& point)
{
    return point.x >= bounds.min_x && point.x <= bounds.max_x && point.y >= bounds.min_y && point.y <= bounds.max_y;
}

void CountMobsInSpawnZones(CGameWorld& world, std::vector<SSpawnZoneWork>& spawn_zones)
{
    for (SSpawnZoneWork& work : spawn_zones)
    {
        if (!work.zone || work.single_spawn || work.population_limit <= 0) continue;
        world.ForEachEntityInEdgeRange(work.bounds.center, work.bounds.radius, [&work](const CEntity* entity) {
            const auto* mob = dynamic_cast<const CMobBase*>(entity);
            if (!CountsForZoneDensity(mob) || !PointInZoneBounds(work.bounds, mob->Pos()) ||
                !IsPointInZone(*work.zone, mob->Pos()))
                return;
            ++work.current_mobs;
        });
    }
}

float SpawnSpacingDistance(float lhs_radius, float rhs_radius)
{
    return std::max(0.f, lhs_radius) + std::max(0.f, rhs_radius) + std::max(0.f, game_config::open_min_spawn_distance);
}

float SpawnRadiusFor(const CMobPrototype& proto, ERarity rarity)
{
    return std::max(0.f, proto.BuildFlowerStats(rarity).radius);
}

bool IsFarFromPendingSpawns(const florrbt::Vector2f& pos, float spawn_radius,
                            const std::vector<SPendingSpawn>& pending_spawns)
{
    for (const SPendingSpawn& pending : pending_spawns)
    {
        const float min_distance = SpawnSpacingDistance(spawn_radius, pending.radius);
        if (DistanceSq(pending.pos, pos) <= min_distance * min_distance) return false;
    }
    return true;
}

bool CanSpawnAt(CGameWorld& world, const florrbt::Vector2f& pos, float spawn_radius, int nearby_mob_limit)
{
    if (nearby_mob_limit <= 0 || world.CircleBlockedByWall(pos, spawn_radius)) return false;

    int nearby_mobs = 0;
    bool too_close_to_mob = false;
    const float spawn_query_radius = std::max(0.f, game_config::open_spawn_query_radius);
    const float density_query_radius_sq = spawn_query_radius * spawn_query_radius;
    // The spacing requirement is spawn_radius + mob_radius + min_distance, so the
    // query must cover at least that for the largest possible mob; otherwise a
    // large mob just outside the query circle could violate true minimum spacing.
    const float largest_mob_radius = std::max(
        0.f, game_config::open_spawn_query_radius * 0.5f + std::max(0.f, game_config::open_min_spawn_distance));
    const float query_radius = std::max(spawn_query_radius, spawn_radius + largest_mob_radius);
    const float query_radius_sq = query_radius * query_radius;

    world.GetSpatialGrid().ForEachInRange(pos, query_radius, [&](const CEntity* entity) {
        const auto* mob = dynamic_cast<const CMobBase*>(entity);
        if (!mob || mob->IsMarkedForDes() || mob->IsDead()) return;

        const float dist_sq = DistanceSq(mob->Pos(), pos);
        if (dist_sq <= density_query_radius_sq && CountsForZoneDensity(mob)) ++nearby_mobs;

        if (!mob->CanCollide()) return;
        const float min_distance = SpawnSpacingDistance(spawn_radius, mob->Radius());
        if (dist_sq <= min_distance * min_distance) too_close_to_mob = true;
    });

    if (nearby_mobs >= nearby_mob_limit) return false;
    return !too_close_to_mob;
}

void CollectPlayerFlowerPositions(CGameWorld& world, std::vector<florrbt::Vector2f>& positions)
{
    positions.clear();
    const CGameContext* context = world.GameContext();
    if (!context) return;

    positions.reserve(context->Players().size());
    for (const auto& player : context->Players())
    {
        if (!player || !player->IsConnected() || !player->IsAuthenticated()) continue;
        const CEntity* entity = player->GetEntity();
        const auto* mob = dynamic_cast<const CMobBase*>(entity);
        if (!mob || mob->IsMarkedForDes() || mob->IsDead()) continue;
        if (mob->GetMobType() != EMobType::PlayerFlower) continue;
        if (entity->GameWorld() != &world) continue;
        positions.push_back(mob->Pos());
    }
}
} // namespace

struct COpenSpawnDirector::CImplementation
{
    CImplementation() : rng(std::random_device{}()) {}

    void EnsureZoneCache(CGameWorld& world, const FlorrBtMap& map)
    {
        const size_t zone_count = map.zones.size();
        if (has_cache_identity && cached_world_id == world.GetId() && cached_map_path == world.GetMapPath() &&
            cached_zone_count == zone_count)
            return;

        spawn_rules.Clear();
        if (!map.spawn_rules_path.empty())
        {
            std::string error;
            if (!spawn_rules.Load(map.spawn_rules_path, error))
                LOG_WARN("opencontroller", "Failed to load spawn rules '" + map.spawn_rules_path + "': " + error);
        }

        zones.clear();
        zones.reserve(zone_count);
        for (const FlorrBtMap::Zone& zone : map.zones)
        {
            if (!zone.spawn_profile.empty() && !spawn_rules.HasProfile(zone.spawn_profile))
                LOG_WARN("opencontroller", "Unknown spawn profile '" + zone.spawn_profile + "' in " +
                                               world.GetMapPath() + "; using legacy mobs pool");
            zones.push_back({ BuildZoneBounds(zone), ComputeZoneArea(zone), ParseZoneMobEntries(zone.mobs),
                              zone.spawn_profile });
        }

        cached_world_id = world.GetId();
        cached_map_path = world.GetMapPath();
        cached_zone_count = zone_count;
        has_cache_identity = true;
    }

    ERarity PickRarityForDifficulty(float difficulty)
    {
        struct SRarityWeight
        {
            ERarity rarity = ERarity::Null;
            float probability = 0.f;
        };

        constexpr ERarity naturally_spawned_rarities[] = {
            ERarity::Common, ERarity::Unusual, ERarity::Rare,  ERarity::Epic,    ERarity::Legendary,
            ERarity::Mythic, ERarity::Ultra,   ERarity::Super, ERarity::Eternal, ERarity::Unique,
            ERarity::Primordial,
        };

        if (difficulty <= 0) return ERarity::Common;

        const float difficulty_scale =
            std::max(game_config::entity_collision_epsilon, game_config::open_rarity_difficulty_scale);
        const float sigma = std::max(game_config::entity_collision_epsilon, game_config::open_rarity_gaussian_sigma);
        const float center_level =
            std::clamp(difficulty / difficulty_scale, static_cast<float>(GetLevel(ERarity::Common)),
                       static_cast<float>(GetLevel(ERarity::Primordial)));
        const float two_sigma_sq = 2.f * sigma * sigma;

        std::vector<SRarityWeight> weights;
        weights.reserve(std::size(naturally_spawned_rarities));

        float total_weight = 0.f;
        for (ERarity rarity : naturally_spawned_rarities)
        {
            const float delta = GetRarityValueLevel(rarity) - center_level;
            const float weight = std::exp(-(delta * delta) / two_sigma_sq);
            if (weight <= 0.f) continue;

            weights.push_back({ rarity, weight });
            total_weight += weight;
        }

        if (weights.empty() || total_weight <= 0.f) return ERarity::Common;

        for (SRarityWeight& entry : weights)
            entry.probability /= total_weight;

        std::uniform_real_distribution<float> dist(0.f, 1.f);
        float roll = dist(rng);
        for (const SRarityWeight& entry : weights)
        {
            if (roll <= entry.probability) return entry.rarity;
            roll -= entry.probability;
        }

        return weights.back().rarity;
    }

    bool RandomPointInZone(const FlorrBtMap::Zone& zone, const SZoneBounds& bounds, florrbt::Vector2f& out_pos)
    {
        if (zone.vertices.empty()) return false;
        if (bounds.min_x >= bounds.max_x || bounds.min_y >= bounds.max_y) return false;

        std::uniform_real_distribution<float> random_x(bounds.min_x, bounds.max_x);
        std::uniform_real_distribution<float> random_y(bounds.min_y, bounds.max_y);
        florrbt::Vector2f candidate = { random_x(rng), random_y(rng) };
        if (!IsPointInZone(zone, candidate)) return false;
        out_pos = candidate;
        return true;
    }

    bool RandomPointInCheckpoint(const CGameWorld& world, const FlorrBtMap::Checkpoint& checkpoint,
                                 florrbt::Vector2f& out_pos)
    {
        if (checkpoint.w <= 0.f || checkpoint.h <= 0.f) return false;
        std::uniform_real_distribution<float> random_x(0.f, checkpoint.w);
        std::uniform_real_distribution<float> random_y(0.f, checkpoint.h);
        const int attempts = std::max(1, game_config::open_spawn_position_attempts);
        for (int attempt = 0; attempt < attempts; ++attempt)
        {
            const FlorrBtMap::Point point = CheckpointLocalToWorldPoint(checkpoint, random_x(rng), random_y(rng));
            florrbt::Vector2f candidate = { point.x, point.y };
            if (world.CircleBlockedByWall(candidate, game_config::mob_player_flower_radius)) continue;
            out_pos = candidate;
            return true;
        }
        return false;
    }

    florrbt::Vector2f PickNewPlayerSpawnPosition(CGameWorld& world)
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
                florrbt::Vector2f pos;
                if (RandomPointInCheckpoint(world, *candidates[dist(rng)], pos)) return pos;
            }
        }

        return { game_config::player_respawn_x, game_config::player_respawn_y };
    }

    florrbt::Vector2f PickReturningPlayerSpawnPosition(CGameWorld& world)
    {
        const FlorrBtMap* map = world.GetMap();
        if (map)
        {
            std::vector<const FlorrBtMap::Checkpoint*> candidates;
            for (const FlorrBtMap::Checkpoint& checkpoint : map->checkpoints)
            {
                if (!checkpoint.is_respawn_area) continue;
                if (checkpoint.w <= 0.f || checkpoint.h <= 0.f) continue;
                candidates.push_back(&checkpoint);
            }

            if (!candidates.empty())
            {
                std::uniform_int_distribution<size_t> dist(0, candidates.size() - 1);
                florrbt::Vector2f pos;
                if (RandomPointInCheckpoint(world, *candidates[dist(rng)], pos)) return pos;
            }
        }

        return { game_config::player_respawn_x, game_config::player_respawn_y };
    }

    int ZoneTargetMobCount(const FlorrBtMap::Zone& zone, float area, float density_multiplier)
    {
        if (zone.density < 0.f) return 0;
        // density == 0 with a mob pool is a map-authored single-spawn sentinel,
        // not a population density. Keep it independent from the ambient wave.
        if (zone.density == 0.f) return IsSingleSpawnZone(zone) ? 1 : 0;

        const double density_area = std::max(1.0, static_cast<double>(game_config::open_spawn_density_area));
        const float wave = std::clamp(density_multiplier, 0.f, 1.f);
        const double desired = static_cast<double>(area) / density_area * static_cast<double>(zone.density) *
                               static_cast<double>(wave);
        if (!std::isfinite(desired) || desired <= 0.0) return 0;

        const int max_target = std::max(0, game_config::open_max_zone_spawn_target);
        const double capped_desired = std::min(desired, static_cast<double>(max_target));
        int target = static_cast<int>(std::floor(capped_desired));
        const double fractional = capped_desired - std::floor(capped_desired);
        if (target < max_target && fractional > 0.0)
        {
            std::uniform_real_distribution<double> dist(0.0, 1.0);
            if (dist(rng) < fractional) ++target;
        }
        return std::clamp(target, 0, max_target);
    }

    int NearbyMobLimit(const FlorrBtMap::Zone& zone, float density_multiplier)
    {
        const size_t configured_limit =
            std::min(game_config::open_max_spawn_num, static_cast<size_t>(std::numeric_limits<int>::max()));
        const int hard_limit = static_cast<int>(configured_limit);
        if (hard_limit <= 0) return 0;
        if (IsSingleSpawnZone(zone)) return hard_limit;

        const double query_radius = std::max(0.0, static_cast<double>(game_config::open_spawn_query_radius));
        const double query_area = static_cast<double>(game_config::pi) * query_radius * query_radius;
        const float local_area = static_cast<float>(
            std::min(query_area, static_cast<double>(std::numeric_limits<float>::max())));
        return std::min(hard_limit, ZoneTargetMobCount(zone, local_area, density_multiplier));
    }

    int ZonePopulationLimit(const FlorrBtMap::Zone& zone) const
    {
        if (IsSingleSpawnZone(zone)) return 1;
        return std::max(0, game_config::open_max_zone_population);
    }

    bool RandomPointNearActivePlayerInZone(const FlorrBtMap::Zone& zone, const SZoneBounds& bounds,
                                           const std::vector<florrbt::Vector2f>& player_positions, florrbt::Vector2f& out_pos)
    {
        if (player_positions.empty()) return false;

        const float spawn_radius = std::max(0.f, game_config::open_active_spawn_near_player_radius);
        if (spawn_radius <= 0.f) return false;

        const float interest_radius =
            bounds.radius + std::max(game_config::simulation_active_view_radius_cap, game_config::default_horizon);
        const float interest_radius_sq = interest_radius * interest_radius;
        std::uniform_int_distribution<size_t> player_index(0, player_positions.size() - 1);
        std::uniform_real_distribution<float> angle(0.f, 2.f * game_config::pi);
        std::uniform_real_distribution<float> unit(0.f, 1.f);

        const florrbt::Vector2f& player_pos = player_positions[player_index(rng)];
        if (DistanceSq(player_pos, bounds.center) > interest_radius_sq) return false;

        const float distance = std::sqrt(unit(rng)) * spawn_radius;
        const float direction = angle(rng);
        const florrbt::Vector2f candidate = player_pos + florrbt::Vector2f{ std::cos(direction), std::sin(direction) } * distance;
        if (!IsPointInZone(zone, candidate)) return false;

        out_pos = candidate;
        return true;
    }

    enum class ESpawnPositionStatus
    {
        Found,
        NoPosition,
        Blocked,
    };

    ESpawnPositionStatus PickSpawnPosition(CGameWorld& world, const FlorrBtMap::Zone& zone, const SZoneBounds& bounds,
                                            float spawn_radius, int nearby_mob_limit,
                                            const std::vector<SPendingSpawn>& pending_spawns,
                                            const std::vector<florrbt::Vector2f>& player_positions, florrbt::Vector2f& out_pos)
    {
        bool found_zone_position = false;
        const int near_player_attempts =
            player_positions.empty() ? 0 : std::max(0, game_config::open_active_spawn_near_player_attempts);
        for (int attempt = 0; attempt < near_player_attempts; ++attempt)
        {
            florrbt::Vector2f candidate;
            if (!RandomPointNearActivePlayerInZone(zone, bounds, player_positions, candidate)) continue;
            found_zone_position = true;

            if (!CanSpawnAt(world, candidate, spawn_radius, nearby_mob_limit)) continue;
            if (!IsFarFromPendingSpawns(candidate, spawn_radius, pending_spawns)) continue;

            out_pos = candidate;
            return ESpawnPositionStatus::Found;
        }

        const int attempts = std::max(1, game_config::open_spawn_position_attempts);
        for (int attempt = 0; attempt < attempts; ++attempt)
        {
            florrbt::Vector2f candidate;
            if (!RandomPointInZone(zone, bounds, candidate)) continue;
            found_zone_position = true;

            if (!CanSpawnAt(world, candidate, spawn_radius, nearby_mob_limit)) continue;
            if (!IsFarFromPendingSpawns(candidate, spawn_radius, pending_spawns)) continue;

            out_pos = candidate;
            return ESpawnPositionStatus::Found;
        }

        return found_zone_position ? ESpawnPositionStatus::Blocked : ESpawnPositionStatus::NoPosition;
    }

    bool ZoneIsNearPlayer(const SZoneBounds& bounds, const std::vector<florrbt::Vector2f>& player_positions) const
    {
        if (player_positions.empty()) return false;

        const float interest_radius =
            bounds.radius + std::max(game_config::simulation_active_view_radius_cap, game_config::default_horizon);
        const float interest_radius_sq = interest_radius * interest_radius;
        for (const florrbt::Vector2f& player_pos : player_positions)
        {
            if (DistanceSq(player_pos, bounds.center) <= interest_radius_sq) return true;
        }
        return false;
    }

    bool SingleSpawnZoneMobAlive(CGameWorld& world, size_t zone_index) const
    {
        if (zone_index >= single_spawn_zone_mobs.size()) return false;
        const SSingleSpawnZoneMob& tracked = single_spawn_zone_mobs[zone_index];
        if (tracked.id < 0 || tracked.generation == 0) return false;

        CEntity* entity = world.GetEntity(tracked.id, tracked.generation);
        return entity && !entity->IsMarkedForDes() && !entity->IsDead();
    }

    void RememberSingleSpawnZoneMob(size_t zone_index, const CEntity* entity)
    {
        if (!entity || zone_index >= single_spawn_zone_mobs.size()) return;
        single_spawn_zone_mobs[zone_index] = { entity->Id(), entity->Generation() };
    }

    void ForgetSingleSpawnZoneMob(const CEntity* entity)
    {
        if (!entity || entity->Id() < 0 || entity->Generation() == 0) return;
        for (SSingleSpawnZoneMob& tracked : single_spawn_zone_mobs)
        {
            if (tracked.id != entity->Id() || tracked.generation != entity->Generation()) continue;
            tracked = {};
        }
    }

    void SpawnMobs(CGameWorld& world, float density_multiplier, COpenSpawnDirector::spawn_callback on_spawn,
                   const SWeatherDiurnalSnapshot* environment)
    {
        const FlorrBtMap* map = world.GetMap();
        if (!map)
        {
            LOG_WARN("opencontroller", "Spawn skipped: world has no map");
            return;
        }

        EnsureZoneCache(world, *map);
        auto& player_positions = scratch.player_positions;
        auto& active_zone_indices = scratch.active_zone_indices;
        auto& rare_zone_indices = scratch.rare_zone_indices;
        auto& zone_work = scratch.zone_work;
        auto& rare_zone_work = scratch.rare_zone_work;
        auto& pending_spawns = scratch.pending_spawns;
        CollectPlayerFlowerPositions(world, player_positions);
        active_zone_indices.clear();
        rare_zone_indices.clear();
        zone_work.clear();
        rare_zone_work.clear();
        pending_spawns.clear();
        CollectNaturalRareMobTypes(world, scratch.natural_rare_types);

        const bool has_players = !player_positions.empty();
        const size_t zone_count = map->zones.size();
        if (zone_count == 0) return;
        if (single_spawn_zone_mobs.size() != zone_count)
            single_spawn_zone_mobs.assign(zone_count, SSingleSpawnZoneMob{});

        // Ambient mobs can skip ticks, so an empty world cannot naturally consume or retire an idle population.
        // Filling map-wide density targets here would therefore create permanent entities without serving a player.
        if (!has_players) return;

        active_zone_indices.reserve(zone_count);
        rare_zone_indices.reserve(zone_count);
        for (size_t zone_index = 0; zone_index < zone_count; ++zone_index)
        {
            if (ZoneIsNearPlayer(zones[zone_index].bounds, player_positions))
                active_zone_indices.push_back(zone_index);
            else if (has_players && map->zones[zone_index].difficulty >= game_config::open_rare_spawn_min_difficulty)
                rare_zone_indices.push_back(zone_index);
        }

        const size_t zones_to_visit = has_players
                                          ? std::min(active_zone_indices.size(),
                                                     std::max<size_t>(1, game_config::open_active_spawn_zones_per_tick))
                                          : 0;

        zone_work.reserve(zones_to_visit);
        for (size_t visit_index = 0; visit_index < zones_to_visit; ++visit_index)
        {
            const size_t zone_index = active_zone_indices[(spawn_zone_cursor + visit_index) % active_zone_indices.size()];
            const FlorrBtMap::Zone& zone = map->zones[zone_index];
            const SZoneCache& cached_zone = zones[zone_index];
            const bool single_spawn_zone = IsSingleSpawnZone(zone);
            if (single_spawn_zone && SingleSpawnZoneMobAlive(world, zone_index)) continue;

            const SResolvedSpawnProfile* resolved =
                cached_zone.spawn_profile.empty() ? nullptr : spawn_rules.Resolve(cached_zone.spawn_profile, environment);
            const float effective_density_multiplier =
                density_multiplier * (resolved ? resolved->density_multiplier : 1.f);
            const std::vector<SZoneMobEntry>* mob_entries =
                resolved ? &resolved->mob_entries : &cached_zone.mob_entries;
            const float difficulty = zone.difficulty + (resolved ? resolved->difficulty_offset : 0.f);

            const int target_mobs = ZoneTargetMobCount(zone, cached_zone.area, effective_density_multiplier);
            if (target_mobs <= 0) continue;
            const int nearby_mob_limit = NearbyMobLimit(zone, effective_density_multiplier);
            if (nearby_mob_limit <= 0) continue;
            const int population_limit = ZonePopulationLimit(zone);
            if (population_limit <= 0) continue;
            zone_work.push_back({ zone_index, &zone, cached_zone.bounds, target_mobs, nearby_mob_limit, 0,
                                  population_limit, single_spawn_zone, false, mob_entries, difficulty });
        }
        CountMobsInSpawnZones(world, zone_work);

        const int active_spawn_limit = std::max(1, game_config::open_max_spawn_per_zone_tick);
        pending_spawns.reserve(zone_work.size() * static_cast<size_t>(active_spawn_limit));
        for (SSpawnZoneWork& work : zone_work)
        {
            const size_t zone_index = work.zone_index;
            const FlorrBtMap::Zone& zone = *work.zone;
            const bool single_spawn_zone = work.single_spawn;
            const int target_mobs = work.target_mobs;
            if (work.current_mobs >= work.population_limit) continue;

            const std::vector<SZoneMobEntry>& mob_entries = *work.mob_entries;
            if (mob_entries.empty())
            {
                const std::string pool = zone.spawn_profile.empty() ? zone.mobs : zone.spawn_profile;
                LOG_WARN("opencontroller", "Spawn zone skipped: no mob type for pool '" + pool + "'");
                continue;
            }

            const int max_spawn_for_zone =
                has_players ? active_spawn_limit : std::max(1, game_config::open_idle_spawn_per_zone_tick);
            const int remaining_population = work.population_limit - work.current_mobs;
            const int spawn_budget = std::min({ target_mobs, max_spawn_for_zone, remaining_population });
            for (int i = 0; i < spawn_budget; ++i)
            {
                EMobType mob_type = PickZoneMobType(mob_entries, rng);
                if (mob_type == EMobType::None) continue;

                ERarity rarity = PickRarityForDifficulty(work.difficulty);
                if (IsNaturalRareRarity(rarity) &&
                    scratch.natural_rare_types.contains(static_cast<int>(mob_type)))
                    continue;
                const CMobPrototype* proto = FindMobPrototype(mob_type);
                if (!proto) continue;

                const float spawn_radius = SpawnRadiusFor(*proto, rarity);
                florrbt::Vector2f pos;
                switch (PickSpawnPosition(world, zone, zones[zone_index].bounds, spawn_radius,
                                          work.nearby_mob_limit, pending_spawns, player_positions, pos))
                {
                case ESpawnPositionStatus::Found:
                    break;
                case ESpawnPositionStatus::NoPosition:
                    continue;
                case ESpawnPositionStatus::Blocked:
                    continue;
                }

                auto mob = CreateMob(mob_type, &world, pos, rarity, true, true);
                if (!mob) continue;

                // A prototype rarity resolver may alter the final tier.
                if (IsNaturalRareRarity(mob->GetRarity()) &&
                    scratch.natural_rare_types.contains(static_cast<int>(mob->GetMobType())))
                    continue;

                CMobBase* raw_mob = dynamic_cast<CMobBase*>(world.InsertEntity(std::move(mob)));
                if (!raw_mob) continue;

                if (single_spawn_zone) RememberSingleSpawnZoneMob(zone_index, raw_mob);
                // Reserve the type before the callback can perform any
                // secondary work, so one natural ET/Unique cannot open a
                // duplicate window within this tick.
                RememberNaturalRareMobType(*raw_mob, scratch.natural_rare_types);
                if (on_spawn) on_spawn(world, *raw_mob);
                pending_spawns.push_back({ pos, spawn_radius });
                for (SSpawnZoneWork& counted_work : zone_work)
                {
                    if (!counted_work.zone || counted_work.single_spawn) continue;
                    if (!PointInZoneBounds(counted_work.bounds, pos)) continue;
                    if (IsPointInZone(*counted_work.zone, pos)) ++counted_work.current_mobs;
                }
                if (single_spawn_zone) ++work.current_mobs;
                if (work.current_mobs >= work.population_limit) break;
            }
        }

        // Only the rare path is allowed to sample an inactive advanced zone.
        // It is intentionally bounded: this removes the proximity gate for ET
        // and Unique without waking/populating every idle zone on the server.
        const size_t rare_zones_to_visit =
            std::min(rare_zone_indices.size(), std::max<size_t>(1, game_config::open_rare_spawn_zones_per_tick));
        rare_zone_work.reserve(rare_zones_to_visit);
        for (size_t visit_index = 0; visit_index < rare_zones_to_visit; ++visit_index)
        {
            const size_t zone_index = rare_zone_indices[(rare_spawn_zone_cursor + visit_index) % rare_zone_indices.size()];
            const FlorrBtMap::Zone& zone = map->zones[zone_index];
            if (IsSingleSpawnZone(zone)) continue;
            const SZoneCache& cached_zone = zones[zone_index];
            const SResolvedSpawnProfile* resolved =
                cached_zone.spawn_profile.empty() ? nullptr : spawn_rules.Resolve(cached_zone.spawn_profile, environment);
            const float effective_density_multiplier =
                density_multiplier * (resolved ? resolved->density_multiplier : 1.f);
            const std::vector<SZoneMobEntry>* mob_entries =
                resolved ? &resolved->mob_entries : &cached_zone.mob_entries;
            const int nearby_mob_limit = NearbyMobLimit(zone, effective_density_multiplier);
            const int population_limit = ZonePopulationLimit(zone);
            if (mob_entries->empty() || nearby_mob_limit <= 0 || population_limit <= 0) continue;

            const float difficulty = zone.difficulty + (resolved ? resolved->difficulty_offset : 0.f);
            rare_zone_work.push_back({ zone_index, &zone, cached_zone.bounds, 1, nearby_mob_limit, 0,
                                       population_limit, false, true, mob_entries, difficulty });
        }
        CountMobsInSpawnZones(world, rare_zone_work);
        for (SSpawnZoneWork& work : rare_zone_work)
        {
            if (work.current_mobs >= work.population_limit) continue;
            const size_t zone_index = work.zone_index;
            const FlorrBtMap::Zone& zone = *work.zone;
            const std::vector<SZoneMobEntry>& mob_entries = *work.mob_entries;
            const int rolls = std::max(1, game_config::open_rare_spawn_rolls_per_zone_tick);
            for (int roll = 0; roll < rolls; ++roll)
            {
                const EMobType mob_type = PickZoneMobType(mob_entries, rng);
                if (mob_type == EMobType::None) continue;
                const ERarity rarity = PickRarityForDifficulty(work.difficulty);
                if (!IsNaturalRareRarity(rarity) ||
                    scratch.natural_rare_types.contains(static_cast<int>(mob_type)))
                    continue;

                const CMobPrototype* proto = FindMobPrototype(mob_type);
                if (!proto) continue;
                const float spawn_radius = SpawnRadiusFor(*proto, rarity);
                florrbt::Vector2f pos;
                if (PickSpawnPosition(world, zone, work.bounds, spawn_radius, work.nearby_mob_limit, pending_spawns,
                                      player_positions, pos) != ESpawnPositionStatus::Found)
                    continue;

                auto mob = CreateMob(mob_type, &world, pos, rarity, true, true);
                if (!mob || !IsNaturalRareRarity(mob->GetRarity()) ||
                    scratch.natural_rare_types.contains(static_cast<int>(mob->GetMobType())))
                    continue;

                CMobBase* raw_mob = dynamic_cast<CMobBase*>(world.InsertEntity(std::move(mob)));
                if (!raw_mob) continue;
                RememberNaturalRareMobType(*raw_mob, scratch.natural_rare_types);
                if (on_spawn) on_spawn(world, *raw_mob);
                pending_spawns.push_back({ pos, spawn_radius });
                ++work.current_mobs;
                break;
            }
        }

        if (!active_zone_indices.empty())
            spawn_zone_cursor = (spawn_zone_cursor + zones_to_visit) % active_zone_indices.size();
        if (!rare_zone_indices.empty())
            rare_spawn_zone_cursor = (rare_spawn_zone_cursor + rare_zones_to_visit) % rare_zone_indices.size();
    }

    float count = game_config::open_initial_spawn_delay;
    size_t spawn_zone_cursor = 0;
    size_t rare_spawn_zone_cursor = 0;
    std::mt19937 rng;
    std::vector<SSingleSpawnZoneMob> single_spawn_zone_mobs;
    std::vector<SZoneCache> zones;
    CSpawnRuleSet spawn_rules;
    SSpawnScratch scratch;
    std::uint32_t cached_world_id = 0;
    std::string cached_map_path;
    size_t cached_zone_count = 0;
    bool has_cache_identity = false;
};

COpenSpawnDirector::COpenSpawnDirector() : m_impl(std::make_unique<CImplementation>()) {}

COpenSpawnDirector::~COpenSpawnDirector() = default;

void COpenSpawnDirector::Tick(CGameWorld& world, float dt, float density_multiplier, spawn_callback on_spawn,
                              const SWeatherDiurnalSnapshot* environment)
{
    if (m_impl->count <= 0)
    {
        m_impl->SpawnMobs(world, density_multiplier, on_spawn, environment);
        m_impl->count = game_config::open_spawn_interval;
    } else
    {
        m_impl->count -= dt;
    }
}

void COpenSpawnDirector::SpawnMobs(CGameWorld& world, float density_multiplier, spawn_callback on_spawn,
                                   const SWeatherDiurnalSnapshot* environment)
{
    m_impl->SpawnMobs(world, density_multiplier, on_spawn, environment);
}

std::optional<florrbt::Vector2f> COpenSpawnDirector::SelectPlayerSpawn(CGameWorld& world, bool use_new_player_spawn,
                                                                  EPlayerSpawnReason reason)
{
    if (reason != EPlayerSpawnReason::Login) return std::nullopt;
    return use_new_player_spawn ? m_impl->PickNewPlayerSpawnPosition(world)
                                : m_impl->PickReturningPlayerSpawnPosition(world);
}

void COpenSpawnDirector::OnEntityRemoved(const CEntity& entity) { m_impl->ForgetSingleSpawnZoneMob(&entity); }

void COpenSpawnDirector::CaptureSnapshot(CSnapshotWriter& writer) const
{
    writer.Field("spawn_countdown", m_impl->count);
    writer.UInt64("spawn_zone_cursor", static_cast<std::uint64_t>(m_impl->spawn_zone_cursor));
    writer.UInt64("rare_spawn_zone_cursor", static_cast<std::uint64_t>(m_impl->rare_spawn_zone_cursor));
    std::ostringstream rng_state;
    rng_state << m_impl->rng;
    writer.Field("spawn_rng", rng_state.str());

    CJsonOwner single_spawn_mobs = MakeJsonArray();
    for (const SSingleSpawnZoneMob& tracked : m_impl->single_spawn_zone_mobs)
    {
        CJsonOwner entry = MakeJsonObject();
        CSnapshotWriter entry_writer(entry.get());
        entry_writer.Field("id", tracked.id);
        entry_writer.UInt64("generation", tracked.generation);
        AppendJson(single_spawn_mobs.get(), entry.release());
    }
    writer.Node("single_spawn_zone_mobs", single_spawn_mobs.release());
}

bool COpenSpawnDirector::RestoreSnapshot(const CSnapshotReader& reader, std::string& error)
{
    m_impl->count = reader.Float("spawn_countdown", game_config::open_initial_spawn_delay);
    m_impl->spawn_zone_cursor = static_cast<size_t>(reader.UInt64("spawn_zone_cursor"));
    // Accept snapshots written before the inactive rare-zone cursor was
    // introduced; the old cursor has no semantic state to preserve.
    m_impl->rare_spawn_zone_cursor = static_cast<size_t>(reader.UInt64(
        "rare_spawn_zone_cursor", reader.UInt64("idle_spawn_zone_cursor")));
    if (const std::string rng_state = reader.String("spawn_rng"); !rng_state.empty())
    {
        std::istringstream input(rng_state);
        input >> m_impl->rng;
        if (!input)
        {
            error = "Invalid open controller RNG state";
            return false;
        }
    }

    m_impl->single_spawn_zone_mobs.clear();
    if (const json_t* entries = reader.Node("single_spawn_zone_mobs"); json_is_array(entries))
    {
        const size_t count = json_array_size(entries);
        m_impl->single_spawn_zone_mobs.reserve(count);
        for (size_t i = 0; i < count; ++i)
        {
            CSnapshotReader entry(json_array_get(entries, i));
            m_impl->single_spawn_zone_mobs.push_back({ entry.Int("id", -1), entry.UInt64("generation") });
        }
    }
    return true;
}
