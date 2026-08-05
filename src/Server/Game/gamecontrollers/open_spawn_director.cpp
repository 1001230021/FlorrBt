#include "open_spawn_director.h"
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
#include <algorithm>
#include <cmath>
#include <limits>
#include <random>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
constexpr std::string_view new_player_checkpoint_name = "new_players";

struct zone_bounds
{
    sf::Vector2f center = { 0.f, 0.f };
    float radius = 0.f;
    float min_x = 0.f;
    float min_y = 0.f;
    float max_x = 0.f;
    float max_y = 0.f;
};

struct zone_cache
{
    zone_bounds bounds;
    float area = 0.f;
    std::vector<SZoneMobEntry> mob_entries;
};

struct single_spawn_zone_mob
{
    int id = -1;
    std::uint64_t generation = 0;
};

struct pending_spawn
{
    sf::Vector2f pos = { 0.f, 0.f };
    float radius = 0.f;
};

struct spawn_zone_work
{
    size_t zone_index = 0;
    const FlorrBtMap::Zone* zone = nullptr;
    zone_bounds bounds;
    int target_mobs = 0;
    int current_mobs = 0;
    bool single_spawn = false;
};

struct spawn_scratch
{
    std::vector<sf::Vector2f> player_positions;
    std::vector<size_t> active_zone_indices;
    std::vector<spawn_zone_work> zone_work;
    std::vector<pending_spawn> pending_spawns;
};

zone_bounds BuildZoneBounds(const FlorrBtMap::Zone& zone)
{
    zone_bounds bounds;
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

bool IsSingleSpawnZone(const FlorrBtMap::Zone& zone) { return zone.density == 0.f && !zone.mobs.empty(); }

bool CountsForZoneDensity(const CMobBase* mob)
{
    if (!mob || mob->m_is_marked_for_des || mob->IsDead()) return false;
    if (mob->GetMobType() == EMobType::PlayerFlower) return false;
    if (mob->GetMobType() == EMobType::SummonedBeetle || mob->GetMobType() == EMobType::SummonedSoldierAnt) return false;
    if (dynamic_cast<const CSummonedMeleeController*>(mob->GetController())) return false;
    return true;
}

bool PointInZoneBounds(const zone_bounds& bounds, const sf::Vector2f& point)
{
    return point.x >= bounds.min_x && point.x <= bounds.max_x && point.y >= bounds.min_y && point.y <= bounds.max_y;
}

void CountMobsInSpawnZones(CGameWorld& world, std::vector<spawn_zone_work>& zones)
{
    if (zones.empty()) return;

    for (spawn_zone_work& work : zones)
    {
        if (work.single_spawn || work.current_mobs >= work.target_mobs || !work.zone) continue;
        world.ForEachEntityInEdgeRange(work.bounds.center, work.bounds.radius, [&work](const CEntity* entity) {
            const auto* mob = dynamic_cast<const CMobBase*>(entity);
            if (!CountsForZoneDensity(mob)) return;
            if (!PointInZoneBounds(work.bounds, mob->m_pos)) return;
            if (!IsPointInZone(*work.zone, mob->m_pos)) return;
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

bool IsFarFromPendingSpawns(const sf::Vector2f& pos, float spawn_radius,
                            const std::vector<pending_spawn>& pending_spawns)
{
    for (const pending_spawn& pending : pending_spawns)
    {
        const float min_distance = SpawnSpacingDistance(spawn_radius, pending.radius);
        if (DistanceSq(pending.pos, pos) <= min_distance * min_distance) return false;
    }
    return true;
}

bool CanSpawnAt(CGameWorld& world, const sf::Vector2f& pos, float spawn_radius)
{
    if (world.CircleBlockedByWall(pos, spawn_radius)) return false;

    int nearby_mobs = 0;
    bool too_close_to_mob = false;
    const float spawn_query_radius = std::max(0.f, game_config::open_spawn_query_radius);
    const float spawn_query_radius_sq = spawn_query_radius * spawn_query_radius;

    world.GetSpatialGrid().ForEachInRange(pos, spawn_query_radius, [&](const CEntity* entity) {
        const auto* mob = dynamic_cast<const CMobBase*>(entity);
        if (!mob || mob->m_is_marked_for_des || mob->IsDead()) return;

        const float dist_sq = DistanceSq(mob->m_pos, pos);
        if (dist_sq <= spawn_query_radius_sq && CountsForZoneDensity(mob)) ++nearby_mobs;

        if (!mob->CanCollide()) return;
        const float min_distance = SpawnSpacingDistance(spawn_radius, mob->m_radius);
        if (dist_sq <= min_distance * min_distance) too_close_to_mob = true;
    });

    if (nearby_mobs >= static_cast<int>(game_config::open_max_spawn_num)) return false;
    return !too_close_to_mob;
}

void CollectPlayerFlowerPositions(CGameWorld& world, std::vector<sf::Vector2f>& positions)
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
        if (!mob || mob->m_is_marked_for_des || mob->IsDead()) continue;
        if (mob->GetMobType() != EMobType::PlayerFlower) continue;
        if (entity->GameWorld() != &world) continue;
        positions.push_back(mob->m_pos);
    }
}
} // namespace

struct COpenSpawnDirector::implementation
{
    implementation() : rng(std::random_device{}()) {}

    void EnsureZoneCache(CGameWorld& world, const FlorrBtMap& map)
    {
        const size_t zone_count = map.zones.size();
        if (has_cache_identity && cached_world_id == world.GetId() && cached_map_path == world.GetMapPath() &&
            cached_zone_count == zone_count)
            return;

        zones.clear();
        zones.reserve(zone_count);
        for (const FlorrBtMap::Zone& zone : map.zones)
            zones.push_back({ BuildZoneBounds(zone), ComputeZoneArea(zone), ParseZoneMobEntries(zone.mobs) });

        cached_world_id = world.GetId();
        cached_map_path = world.GetMapPath();
        cached_zone_count = zone_count;
        has_cache_identity = true;
    }

    ERarity PickRarityForDifficulty(float difficulty)
    {
        struct rarity_weight
        {
            ERarity rarity = ERarity::Null;
            float probability = 0.f;
        };

        constexpr ERarity naturally_spawned_rarities[] = {
            ERarity::Common, ERarity::Unusual, ERarity::Rare,  ERarity::Epic,    ERarity::Legendary,
            ERarity::Mythic, ERarity::Ultra,   ERarity::Super, ERarity::Eternal, ERarity::Primordial,
        };

        if (difficulty <= 0) return ERarity::Common;

        const float difficulty_scale =
            std::max(game_config::entity_collision_epsilon, game_config::open_rarity_difficulty_scale);
        const float sigma = std::max(game_config::entity_collision_epsilon, game_config::open_rarity_gaussian_sigma);
        const float center_level =
            std::clamp(difficulty / difficulty_scale, static_cast<float>(GetLevel(ERarity::Common)),
                       static_cast<float>(GetLevel(ERarity::Primordial)));
        const float two_sigma_sq = 2.f * sigma * sigma;

        std::vector<rarity_weight> weights;
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

        for (rarity_weight& entry : weights)
            entry.probability /= total_weight;

        std::uniform_real_distribution<float> dist(0.f, 1.f);
        float roll = dist(rng);
        for (const rarity_weight& entry : weights)
        {
            if (roll <= entry.probability) return entry.rarity;
            roll -= entry.probability;
        }

        return weights.back().rarity;
    }

    bool RandomPointInZone(const FlorrBtMap::Zone& zone, const zone_bounds& bounds, sf::Vector2f& out_pos)
    {
        if (zone.vertices.empty()) return false;
        if (bounds.min_x >= bounds.max_x || bounds.min_y >= bounds.max_y) return false;

        std::uniform_real_distribution<float> random_x(bounds.min_x, bounds.max_x);
        std::uniform_real_distribution<float> random_y(bounds.min_y, bounds.max_y);
        sf::Vector2f candidate = { random_x(rng), random_y(rng) };
        if (!IsPointInZone(zone, candidate)) return false;
        out_pos = candidate;
        return true;
    }

    bool RandomPointInCheckpoint(const CGameWorld& world, const FlorrBtMap::Checkpoint& checkpoint,
                                 sf::Vector2f& out_pos)
    {
        if (checkpoint.w <= 0.f || checkpoint.h <= 0.f) return false;
        std::uniform_real_distribution<float> random_x(0.f, checkpoint.w);
        std::uniform_real_distribution<float> random_y(0.f, checkpoint.h);
        const int attempts = std::max(1, game_config::open_spawn_position_attempts);
        for (int attempt = 0; attempt < attempts; ++attempt)
        {
            const FlorrBtMap::Point point = CheckpointLocalToWorldPoint(checkpoint, random_x(rng), random_y(rng));
            sf::Vector2f candidate = { point.x, point.y };
            if (world.CircleBlockedByWall(candidate, game_config::mob_player_flower_radius)) continue;
            out_pos = candidate;
            return true;
        }
        return false;
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
                if (RandomPointInCheckpoint(world, *candidates[dist(rng)], pos)) return pos;
            }
        }

        return { game_config::player_respawn_x, game_config::player_respawn_y };
    }

    sf::Vector2f PickReturningPlayerSpawnPosition(CGameWorld& world)
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
                sf::Vector2f pos;
                if (RandomPointInCheckpoint(world, *candidates[dist(rng)], pos)) return pos;
            }
        }

        return { game_config::player_respawn_x, game_config::player_respawn_y };
    }

    int ZoneTargetMobCount(const FlorrBtMap::Zone& zone, float area)
    {
        if (zone.density < 0.f) return 0;
        if (zone.density == 0.f) return zone.mobs.empty() ? 0 : 1;

        const double density_area = std::max(1.0, static_cast<double>(game_config::open_spawn_density_area));
        const double desired = static_cast<double>(area) / density_area * static_cast<double>(zone.density);
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

    bool RandomPointNearActivePlayerInZone(const FlorrBtMap::Zone& zone, const zone_bounds& bounds,
                                           const std::vector<sf::Vector2f>& player_positions, sf::Vector2f& out_pos)
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

        const sf::Vector2f& player_pos = player_positions[player_index(rng)];
        if (DistanceSq(player_pos, bounds.center) > interest_radius_sq) return false;

        const float distance = std::sqrt(unit(rng)) * spawn_radius;
        const float direction = angle(rng);
        const sf::Vector2f candidate = player_pos + sf::Vector2f{ std::cos(direction), std::sin(direction) } * distance;
        if (!IsPointInZone(zone, candidate)) return false;

        out_pos = candidate;
        return true;
    }

    enum class spawn_position_status
    {
        Found,
        NoPosition,
        Blocked,
    };

    spawn_position_status PickSpawnPosition(CGameWorld& world, const FlorrBtMap::Zone& zone, const zone_bounds& bounds,
                                            float spawn_radius, const std::vector<pending_spawn>& pending_spawns,
                                            const std::vector<sf::Vector2f>& player_positions, sf::Vector2f& out_pos)
    {
        bool found_zone_position = false;
        const int near_player_attempts =
            player_positions.empty() ? 0 : std::max(0, game_config::open_active_spawn_near_player_attempts);
        for (int attempt = 0; attempt < near_player_attempts; ++attempt)
        {
            sf::Vector2f candidate;
            if (!RandomPointNearActivePlayerInZone(zone, bounds, player_positions, candidate)) continue;
            found_zone_position = true;

            if (!CanSpawnAt(world, candidate, spawn_radius)) continue;
            if (!IsFarFromPendingSpawns(candidate, spawn_radius, pending_spawns)) continue;

            out_pos = candidate;
            return spawn_position_status::Found;
        }

        const int attempts = std::max(1, game_config::open_spawn_position_attempts);
        for (int attempt = 0; attempt < attempts; ++attempt)
        {
            sf::Vector2f candidate;
            if (!RandomPointInZone(zone, bounds, candidate)) continue;
            found_zone_position = true;

            if (!CanSpawnAt(world, candidate, spawn_radius)) continue;
            if (!IsFarFromPendingSpawns(candidate, spawn_radius, pending_spawns)) continue;

            out_pos = candidate;
            return spawn_position_status::Found;
        }

        return found_zone_position ? spawn_position_status::Blocked : spawn_position_status::NoPosition;
    }

    bool ZoneIsNearPlayer(const zone_bounds& bounds, const std::vector<sf::Vector2f>& player_positions) const
    {
        if (player_positions.empty()) return false;

        const float interest_radius =
            bounds.radius + std::max(game_config::simulation_active_view_radius_cap, game_config::default_horizon);
        const float interest_radius_sq = interest_radius * interest_radius;
        for (const sf::Vector2f& player_pos : player_positions)
        {
            if (DistanceSq(player_pos, bounds.center) <= interest_radius_sq) return true;
        }
        return false;
    }

    bool SingleSpawnZoneMobAlive(CGameWorld& world, size_t zone_index) const
    {
        if (zone_index >= single_spawn_zone_mobs.size()) return false;
        const single_spawn_zone_mob& tracked = single_spawn_zone_mobs[zone_index];
        if (tracked.id < 0 || tracked.generation == 0) return false;

        CEntity* entity = world.GetEntity(tracked.id, tracked.generation);
        return entity && !entity->m_is_marked_for_des && !entity->IsDead();
    }

    void RememberSingleSpawnZoneMob(size_t zone_index, const CEntity* entity)
    {
        if (!entity || zone_index >= single_spawn_zone_mobs.size()) return;
        single_spawn_zone_mobs[zone_index] = { entity->m_id, entity->m_generation };
    }

    void ForgetSingleSpawnZoneMob(const CEntity* entity)
    {
        if (!entity || entity->m_id < 0 || entity->m_generation == 0) return;
        for (single_spawn_zone_mob& tracked : single_spawn_zone_mobs)
        {
            if (tracked.id != entity->m_id || tracked.generation != entity->m_generation) continue;
            tracked = {};
        }
    }

    void SpawnMobs(CGameWorld& world, COpenSpawnDirector::spawn_callback on_spawn)
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
        auto& zone_work = scratch.zone_work;
        auto& pending_spawns = scratch.pending_spawns;
        CollectPlayerFlowerPositions(world, player_positions);
        active_zone_indices.clear();
        zone_work.clear();
        pending_spawns.clear();

        const bool has_players = !player_positions.empty();
        const size_t zone_count = map->zones.size();
        if (zone_count == 0) return;
        if (single_spawn_zone_mobs.size() != zone_count)
            single_spawn_zone_mobs.assign(zone_count, single_spawn_zone_mob{});

        // Ambient mobs can skip ticks, so an empty world cannot naturally consume or retire an idle population.
        // Filling map-wide density targets here would therefore create permanent entities without serving a player.
        if (!has_players) return;

        const size_t idle_zones_per_tick =
            static_cast<size_t>(std::max(1, game_config::open_idle_spawn_zones_per_tick));
        if (has_players)
        {
            active_zone_indices.reserve(zone_count);
            for (size_t zone_index = 0; zone_index < zone_count; ++zone_index)
            {
                if (ZoneIsNearPlayer(zones[zone_index].bounds, player_positions))
                    active_zone_indices.push_back(zone_index);
            }
        }

        const size_t zones_to_visit = has_players
                                          ? std::min(active_zone_indices.size(),
                                                     std::max<size_t>(1, game_config::open_active_spawn_zones_per_tick))
                                          : std::min(zone_count, idle_zones_per_tick);

        zone_work.reserve(zones_to_visit);
        for (size_t visit_index = 0; visit_index < zones_to_visit; ++visit_index)
        {
            const size_t zone_index =
                has_players ? active_zone_indices[(spawn_zone_cursor + visit_index) % active_zone_indices.size()]
                            : (idle_spawn_zone_cursor + visit_index) % zone_count;
            const FlorrBtMap::Zone& zone = map->zones[zone_index];
            const zone_cache& cached_zone = zones[zone_index];
            const bool single_spawn_zone = IsSingleSpawnZone(zone);
            if (single_spawn_zone && SingleSpawnZoneMobAlive(world, zone_index)) continue;

            const int target_mobs = ZoneTargetMobCount(zone, cached_zone.area);
            if (target_mobs <= 0) continue;
            zone_work.push_back({ zone_index, &zone, cached_zone.bounds, target_mobs, 0, single_spawn_zone });
        }
        CountMobsInSpawnZones(world, zone_work);

        const int active_spawn_limit = std::max(1, game_config::open_max_spawn_per_zone_tick);
        pending_spawns.reserve(zone_work.size() * static_cast<size_t>(active_spawn_limit));
        for (spawn_zone_work& work : zone_work)
        {
            const size_t zone_index = work.zone_index;
            const FlorrBtMap::Zone& zone = *work.zone;
            const bool single_spawn_zone = work.single_spawn;
            const int target_mobs = work.target_mobs;
            int& current_mobs = work.current_mobs;
            if (current_mobs >= target_mobs) continue;

            const std::vector<SZoneMobEntry>& mob_entries = zones[zone_index].mob_entries;
            if (mob_entries.empty())
            {
                LOG_WARN("opencontroller", "Spawn zone skipped: no mob type for pool '" + zone.mobs + "'");
                continue;
            }

            const int max_spawn_for_zone =
                has_players ? active_spawn_limit : std::max(1, game_config::open_idle_spawn_per_zone_tick);
            const int spawn_budget = std::min(target_mobs - current_mobs, max_spawn_for_zone);
            for (int i = 0; i < spawn_budget; ++i)
            {
                EMobType mob_type = PickZoneMobType(mob_entries, rng);
                if (mob_type == EMobType::None) continue;

                ERarity rarity = PickRarityForDifficulty(zone.difficulty);
                const CMobPrototype* proto = FindMobPrototype(mob_type);
                if (!proto) continue;

                const float spawn_radius = SpawnRadiusFor(*proto, rarity);
                sf::Vector2f pos;
                switch (PickSpawnPosition(world, zone, zones[zone_index].bounds, spawn_radius, pending_spawns,
                                          player_positions, pos))
                {
                case spawn_position_status::Found:
                    break;
                case spawn_position_status::NoPosition:
                    continue;
                case spawn_position_status::Blocked:
                    continue;
                }

                auto mob = CreateMob(mob_type, &world, pos, rarity);
                if (!mob) continue;

                CMobBase* raw_mob = dynamic_cast<CMobBase*>(world.InsertEntity(std::move(mob)));
                if (!raw_mob) continue;

                if (single_spawn_zone) RememberSingleSpawnZoneMob(zone_index, raw_mob);
                if (on_spawn) on_spawn(world, *raw_mob);
                pending_spawns.push_back({ pos, spawn_radius });
                for (spawn_zone_work& counted_work : zone_work)
                {
                    if (counted_work.single_spawn || !counted_work.zone) continue;
                    if (!PointInZoneBounds(counted_work.bounds, pos)) continue;
                    if (IsPointInZone(*counted_work.zone, pos)) ++counted_work.current_mobs;
                }
                if (single_spawn_zone) ++current_mobs;
            }
        }

        if (has_players && !active_zone_indices.empty())
            spawn_zone_cursor = (spawn_zone_cursor + zones_to_visit) % active_zone_indices.size();
        else if (!has_players) idle_spawn_zone_cursor = (idle_spawn_zone_cursor + zones_to_visit) % zone_count;
    }

    float count = game_config::open_initial_spawn_delay;
    size_t spawn_zone_cursor = 0;
    size_t idle_spawn_zone_cursor = 0;
    std::mt19937 rng;
    std::vector<single_spawn_zone_mob> single_spawn_zone_mobs;
    std::vector<zone_cache> zones;
    spawn_scratch scratch;
    std::uint32_t cached_world_id = 0;
    std::string cached_map_path;
    size_t cached_zone_count = 0;
    bool has_cache_identity = false;
};

COpenSpawnDirector::COpenSpawnDirector() : m_impl(std::make_unique<implementation>()) {}

COpenSpawnDirector::~COpenSpawnDirector() = default;

void COpenSpawnDirector::Tick(CGameWorld& world, float dt, spawn_callback on_spawn)
{
    if (m_impl->count <= 0)
    {
        m_impl->SpawnMobs(world, on_spawn);
        m_impl->count = game_config::open_spawn_interval;
    } else
    {
        m_impl->count -= dt;
    }
}

void COpenSpawnDirector::SpawnMobs(CGameWorld& world, spawn_callback on_spawn) { m_impl->SpawnMobs(world, on_spawn); }

std::optional<sf::Vector2f> COpenSpawnDirector::SelectPlayerSpawn(CGameWorld& world, bool use_new_player_spawn,
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
    writer.UInt64("idle_spawn_zone_cursor", static_cast<std::uint64_t>(m_impl->idle_spawn_zone_cursor));
    std::ostringstream rng_state;
    rng_state << m_impl->rng;
    writer.Field("spawn_rng", rng_state.str());

    CJsonOwner single_spawn_mobs = MakeJsonArray();
    for (const single_spawn_zone_mob& tracked : m_impl->single_spawn_zone_mobs)
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
    m_impl->idle_spawn_zone_cursor = static_cast<size_t>(reader.UInt64("idle_spawn_zone_cursor"));
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
