#include "opencontroller.h"
#include "../controllers/melee_controller.h"
#include "../entities/drop.h"
#include "../player.h"
#include "../gameworld.h"
#include "../gamecontext.h"
#include "../entities/flower.h"
#include "../zone_mob_tools.h"
#include "../../server.h"
#include "../../Module/network_module.h"
#include "../../../Engine/map_tools.h"
#include "../../../Shared/game_config.h"
#include "../../../Shared/drop_rate.h"
#include <algorithm>
#include <cmath>
#include <sstream>
#include <limits>
#include <random>
#include <string_view>
#include <unordered_map>

namespace
{
constexpr float spawn_query_radius = game_config::WorldUnits(512.f);
constexpr float min_spawn_distance = game_config::WorldUnits(128.f);
constexpr size_t max_spawn_num = 12;
constexpr int max_spawn_per_zone_tick = 8;
constexpr size_t active_spawn_zones_per_tick = 8;
constexpr int max_zone_spawn_target = 512;
constexpr int spawn_position_attempts = 32;
constexpr float rarity_difficulty_scale = 10.f;
constexpr float rarity_gaussian_sigma = 0.36f;
constexpr float drop_spread_radius = game_config::WorldUnits(32.f);
struct lootable_player
{
    CPlayer* player = nullptr;
    float damage = 0.f;
};

struct zone_bounds
{
    sf::Vector2f center = {0.f, 0.f};
    float radius = 0.f;
    float min_x = 0.f;
    float min_y = 0.f;
    float max_x = 0.f;
    float max_y = 0.f;
};

struct pending_spawn
{
    sf::Vector2f pos = {0.f, 0.f};
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

thread_local spawn_scratch g_spawn_scratch;

bool IsActivePlayer(const CGameContext* context, const CPlayer* player)
{
    if (!player) return false;
    if (!context) return true;

    for (const auto& existing : context->Players())
    {
        if (existing.get() == player) return true;
    }
    return false;
}

float TotalDamageRecorded(const CMobBase& mob)
{
    float total = 0.f;
    for (const CDamageData& damage_data : mob.GetDamageData())
    {
        if (damage_data.m_total_dmg > 0.f) total += damage_data.m_total_dmg;
    }
    return total;
}

std::string DamageSummary(const CMobBase& mob)
{
    const CGameContext* context = const_cast<CMobBase&>(mob).GameContext();
    std::vector<const CDamageData*> records;
    records.reserve(mob.GetDamageData().size());
    for (const CDamageData& damage_data : mob.GetDamageData())
    {
        if (damage_data.m_total_dmg > 0.f) records.push_back(&damage_data);
    }

    std::sort(records.begin(), records.end(), [](const CDamageData* lhs, const CDamageData* rhs)
    {
        return lhs->m_total_dmg > rhs->m_total_dmg;
    });

    std::ostringstream oss;
    for (size_t i = 0; i < records.size() && i < 5; ++i)
    {
        const CDamageData* record = records[i];
        if (i > 0) oss << ", ";
        const bool active_player = IsActivePlayer(context, record->m_player);
        oss << (active_player ? record->m_player->GetName() : "<stale>")
            << "#" << (active_player ? record->m_player->GetId() : 0)
            << "=" << record->m_total_dmg;
    }
    if (records.size() > 5) oss << ", ...";
    std::string text = oss.str();
    return text.empty() ? "<none>" : text;
}

std::string LootableSummary(const std::vector<lootable_player>& lootable_players)
{
    std::ostringstream oss;
    for (size_t i = 0; i < lootable_players.size(); ++i)
    {
        const lootable_player& lootable = lootable_players[i];
        if (i > 0) oss << ", ";
        oss << (lootable.player ? lootable.player->GetName() : "<null>")
            << "#" << (lootable.player ? lootable.player->GetId() : 0)
            << "=" << lootable.damage;
    }
    std::string text = oss.str();
    return text.empty() ? "<none>" : text;
}

std::string DropSummary(const std::vector<SDropRate>& drops)
{
    std::ostringstream oss;
    for (size_t i = 0; i < drops.size(); ++i)
    {
        const SDropRate& drop = drops[i];
        if (i > 0) oss << ", ";
        oss << std::string(GetRarityName(drop.rarity)) << " " << std::string(GetPetalTypeName(drop.type));
    }
    std::string text = oss.str();
    return text.empty() ? "<none>" : text;
}

void LogSuperLootIssue(const CMobBase& mob, const std::string& reason)
{
    const SMobStats* stats = mob.GetFinalStats();
    const float max_health = stats ? stats->max_health : 0.f;
    LOG_WARN("loot", reason + " mob=" + std::string(GetRarityName(mob.GetRarity())) + " " +
                        std::string(GetMobTypeName(mob.m_mob_type)) +
                        " id=" + std::to_string(mob.m_id) +
                        " max_health=" + std::to_string(max_health) +
                        " recorded_damage=" + std::to_string(TotalDamageRecorded(mob)) +
                        " damage_sources=" + DamageSummary(mob));
}

bool IsPlayerOwnedSummon(CGameWorld& world, const CMobBase& mob)
{
    auto* controller = dynamic_cast<const CSummonedMeleeController*>(mob.GetController());
    if (!controller) return false;

    CGameContext* context = world.GameContext();
    CEntity* owner = controller->GetOwner(&world);
    return context && owner && context->FindPlayerFromEntity(owner) != nullptr;
}

void BroadcastMobSpawnReport(CGameWorld& world, const CMobBase& mob, std::string_view action)
{
    if (IsPlayerOwnedSummon(world, mob)) return;
    if (!CServer::MeetsPetalReportRarity(mob.GetRarity(), game_config::min_mob_spawn_report_rarity)) return;

    const CMobPrototype* proto = FindMobPrototype(mob.m_mob_type);
    const std::string mob_name = proto && !proto->m_name.empty()
                                     ? proto->m_name
                                     : std::string(GetMobTypeName(mob.m_mob_type));
    if (CServer* server = CServer::GetInstance())
        server->BroadcastMobReport(action, mob.GetRarity(), mob_name);
}

CPlayer* FindTopDamagePlayer(const CMobBase& mob)
{
    const CGameContext* context = const_cast<CMobBase&>(mob).GameContext();
    std::unordered_map<CPlayer*, float> damage_by_player;
    for (const CDamageData& damage_data : mob.GetDamageData())
    {
        if (!IsActivePlayer(context, damage_data.m_player) || damage_data.m_total_dmg <= 0.f) continue;
        damage_by_player[damage_data.m_player] += damage_data.m_total_dmg;
    }

    CPlayer* best_player = nullptr;
    float best_damage = 0.f;
    for (const auto& [player, damage] : damage_by_player)
    {
        if (damage > best_damage)
        {
            best_player = player;
            best_damage = damage;
        }
    }
    return best_player;
}

void BroadcastMobDefeatReport(CGameWorld& world, const CMobBase& mob)
{
    if (IsPlayerOwnedSummon(world, mob)) return;
    if (!CServer::MeetsPetalReportRarity(mob.GetRarity(), game_config::min_mob_spawn_report_rarity)) return;

    const CMobPrototype* proto = FindMobPrototype(mob.m_mob_type);
    const std::string mob_name = proto && !proto->m_name.empty()
                                     ? proto->m_name
                                     : std::string(GetMobTypeName(mob.m_mob_type));
    std::string action = "has been defeated";
    if (CPlayer* top_player = FindTopDamagePlayer(mob))
    {
        action += " by ";
        action += top_player->GetName();
    }

    if (CServer* server = CServer::GetInstance())
        server->BroadcastMobReport(action, mob.GetRarity(), mob_name);
}

const std::vector<SZoneMobEntry>& CachedZoneMobEntries(const std::string& mobs)
{
    static const std::vector<SZoneMobEntry> empty;
    static std::unordered_map<std::string, std::vector<SZoneMobEntry>> cache;

    auto [it, inserted] = cache.try_emplace(mobs);
    if (inserted) it->second = ParseZoneMobEntries(mobs);
    return it->second.empty() ? empty : it->second;
}

zone_bounds ZoneBounds(const FlorrBtMap::Zone& zone)
{
    static std::unordered_map<const FlorrBtMap::Zone*, zone_bounds> cache;
    if (auto it = cache.find(&zone); it != cache.end()) return it->second;

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

    bounds.center = {(min_x + max_x) * 0.5f, (min_y + max_y) * 0.5f};
    bounds.radius = Distance(bounds.center, {max_x, max_y});
    bounds.min_x = min_x;
    bounds.min_y = min_y;
    bounds.max_x = max_x;
    bounds.max_y = max_y;
    cache.emplace(&zone, bounds);
    return bounds;
}

bool IsAboveUltra(ERarity rarity)
{
    return IsAboveRarity(rarity, ERarity::Ultra);
}

bool IsSuperOrHigher(ERarity rarity)
{
    return IsAtLeastRarity(rarity, ERarity::Super);
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
        const float delta = GetRarityValueLevel(rarity) - center_level;
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
    const zone_bounds bounds = ZoneBounds(zone);
    if (bounds.min_x >= bounds.max_x || bounds.min_y >= bounds.max_y) return false;

    std::uniform_real_distribution<float> random_x(bounds.min_x, bounds.max_x);
    std::uniform_real_distribution<float> random_y(bounds.min_y, bounds.max_y);
    sf::Vector2f candidate = {random_x(SpawnRng()), random_y(SpawnRng())};
    if (!IsPointInZone(zone, candidate)) return false;
    out_pos = candidate;
    return true;
}

bool RandomPointInCheckpoint(const CGameWorld& world, const FlorrBtMap::Checkpoint& checkpoint, sf::Vector2f& out_pos)
{
    if (checkpoint.w <= 0.f || checkpoint.h <= 0.f) return false;
    std::uniform_real_distribution<float> random_x(0.f, checkpoint.w);
    std::uniform_real_distribution<float> random_y(0.f, checkpoint.h);
    for (int attempt = 0; attempt < spawn_position_attempts; ++attempt)
    {
        const FlorrBtMap::Point point = CheckpointLocalToWorldPoint(checkpoint, random_x(SpawnRng()), random_y(SpawnRng()));
        sf::Vector2f candidate = {point.x, point.y};
        if (world.CircleBlockedByWall(candidate, game_config::mob_player_flower_radius)) continue;
        out_pos = candidate;
        return true;
    }
    return false;
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
            if (RandomPointInCheckpoint(world, *candidates[dist(SpawnRng())], pos)) return pos;
        }
    }

    return {game_config::player_respawn_x, game_config::player_respawn_y};
}

float ZoneArea(const FlorrBtMap::Zone& zone)
{
    static std::unordered_map<const FlorrBtMap::Zone*, float> cache;
    if (auto it = cache.find(&zone); it != cache.end()) return it->second;

    const std::vector<FlorrBtMap::Point>& vertices = zone.vertices;
    if (vertices.size() < 3)
    {
        cache[&zone] = 0.f;
        return 0.f;
    }

    double area = 0.0;
    for (size_t i = 0, j = vertices.size() - 1; i < vertices.size(); j = i++)
        area += static_cast<double>(vertices[j].x) * static_cast<double>(vertices[i].y) -
                static_cast<double>(vertices[i].x) * static_cast<double>(vertices[j].y);
    area = std::abs(area) * 0.5;
    float result = (!std::isfinite(area) || area <= 0.0) ? 0.f :
                   static_cast<float>(std::min(area, static_cast<double>(std::numeric_limits<float>::max())));
    cache[&zone] = result;
    return result;
}

int ZoneTargetMobCount(const FlorrBtMap::Zone& zone)
{
    if (zone.density < 0.f) return 0;
    if (zone.density == 0.f) return zone.mobs.empty() ? 0 : 1;

    const double density_area = std::max(1.0, static_cast<double>(game_config::open_spawn_density_area));
    const double desired = static_cast<double>(ZoneArea(zone)) / density_area * static_cast<double>(zone.density);
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

bool IsSingleSpawnZone(const FlorrBtMap::Zone& zone)
{
    return zone.density == 0.f && !zone.mobs.empty();
}

bool CountsForZoneDensity(const CMobBase* mob)
{
    if (!mob || mob->m_is_marked_for_des || mob->IsDead()) return false;
    if (mob->m_mob_type == EMobType::PlayerFlower) return false;
    if (mob->m_mob_type == EMobType::SummonedBeetle || mob->m_mob_type == EMobType::SummonedSoldierAnt) return false;
    if (dynamic_cast<const CSummonedMeleeController*>(mob->GetController())) return false;
    return true;
}

bool PointInZoneBounds(const zone_bounds& bounds, const sf::Vector2f& point)
{
    return point.x >= bounds.min_x && point.x <= bounds.max_x &&
           point.y >= bounds.min_y && point.y <= bounds.max_y;
}

void CountMobsInSpawnZones(CGameWorld& world, std::vector<spawn_zone_work>& zones)
{
    if (zones.empty()) return;

    world.ForEachEntity([&zones](const CEntity* entity)
    {
        const auto* mob = dynamic_cast<const CMobBase*>(entity);
        if (!CountsForZoneDensity(mob)) return;

        for (spawn_zone_work& work : zones)
        {
            if (work.single_spawn || work.current_mobs >= work.target_mobs || !work.zone) continue;
            if (!PointInZoneBounds(work.bounds, mob->m_pos)) continue;
            if (!IsPointInZone(*work.zone, mob->m_pos)) continue;
            ++work.current_mobs;
        }
    });
}

float SpawnSpacingDistance(float lhs_radius, float rhs_radius)
{
    return std::max(0.f, lhs_radius) + std::max(0.f, rhs_radius) + min_spawn_distance;
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
        if (DistanceSq(pending.pos, pos) <= min_distance * min_distance)
            return false;
    }
    return true;
}

bool CanSpawnAt(CGameWorld& world, const sf::Vector2f& pos, float spawn_radius)
{
    if (world.CircleBlockedByWall(pos, spawn_radius)) return false;

    int nearby_mobs = 0;
    bool too_close_to_mob = false;
    const float spawn_query_radius_sq = spawn_query_radius * spawn_query_radius;

    world.GetSpatialGrid().ForEachInRange(pos, spawn_query_radius, [&](const CEntity* entity)
    {
        const auto* mob = dynamic_cast<const CMobBase*>(entity);
        if (!mob || mob->m_is_marked_for_des || mob->IsDead()) return;

        const float dist_sq = DistanceSq(mob->m_pos, pos);
        if (dist_sq <= spawn_query_radius_sq && CountsForZoneDensity(mob))
            ++nearby_mobs;

        if (!mob->CanCollide()) return;
        const float min_distance = SpawnSpacingDistance(spawn_radius, mob->m_radius);
        if (dist_sq <= min_distance * min_distance)
            too_close_to_mob = true;
    });

    if (nearby_mobs >= static_cast<int>(max_spawn_num)) return false;
    return !too_close_to_mob;
}

enum class spawn_position_status
{
    Found,
    NoPosition,
    Blocked,
};

spawn_position_status PickSpawnPosition(CGameWorld& world, const FlorrBtMap::Zone& zone, float spawn_radius,
                                        const std::vector<pending_spawn>& pending_spawns, sf::Vector2f& out_pos)
{
    bool found_zone_position = false;
    for (int attempt = 0; attempt < spawn_position_attempts; ++attempt)
    {
        sf::Vector2f candidate;
        if (!RandomPointInZone(zone, candidate)) continue;
        found_zone_position = true;

        if (!CanSpawnAt(world, candidate, spawn_radius)) continue;
        if (!IsFarFromPendingSpawns(candidate, spawn_radius, pending_spawns)) continue;

        out_pos = candidate;
        return spawn_position_status::Found;
    }

    return found_zone_position ? spawn_position_status::Blocked : spawn_position_status::NoPosition;
}

void CollectPlayerFlowerPositions(CGameWorld& world, std::vector<sf::Vector2f>& positions)
{
    positions.clear();
    world.ForEachEntity([&positions](const CEntity* entity)
    {
        const auto* mob = dynamic_cast<const CMobBase*>(entity);
        if (!mob || mob->m_is_marked_for_des || mob->IsDead()) return;
        if (mob->m_mob_type != EMobType::PlayerFlower) return;
        positions.push_back(mob->m_pos);
    });
}

bool ZoneIsNearPlayer(const FlorrBtMap::Zone& zone, const std::vector<sf::Vector2f>& player_positions)
{
    if (player_positions.empty()) return false;

    const zone_bounds bounds = ZoneBounds(zone);
    const float interest_radius = bounds.radius + std::max(game_config::simulation_active_view_radius_cap,
                                                           game_config::default_horizon);
    const float interest_radius_sq = interest_radius * interest_radius;
    for (const sf::Vector2f& player_pos : player_positions)
    {
        if (DistanceSq(player_pos, bounds.center) <= interest_radius_sq) return true;
    }
    return false;
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
    const CGameContext* context = const_cast<CMobBase&>(mob).GameContext();
    const bool above_ultra = IsAboveUltra(mob.GetRarity());
    const size_t max_lootable =
        above_ultra ? game_config::max_lootable_player_above_ultra : game_config::max_lootable_players;
    const float min_damage_rate = above_ultra ? 0.01f : 0.05f;

    float total_damage = 0.f;
    std::unordered_map<CPlayer*, float> damage_by_player;
    for (const CDamageData& damage_data : mob.GetDamageData())
    {
        if (!IsActivePlayer(context, damage_data.m_player) || damage_data.m_total_dmg <= 0.f) continue;
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

    auto& player_positions = g_spawn_scratch.player_positions;
    auto& active_zone_indices = g_spawn_scratch.active_zone_indices;
    auto& zone_work = g_spawn_scratch.zone_work;
    auto& pending_spawns = g_spawn_scratch.pending_spawns;
    CollectPlayerFlowerPositions(world, player_positions);
    active_zone_indices.clear();
    zone_work.clear();
    pending_spawns.clear();

    const bool has_players = !player_positions.empty();
    const size_t zone_count = map->zones.size();
    if (zone_count == 0) return;
    if (m_single_spawn_zone_mobs.size() != zone_count)
        m_single_spawn_zone_mobs.assign(zone_count, single_spawn_zone_mob{});

    const size_t idle_zones_per_tick = static_cast<size_t>(
        std::max(1, game_config::open_idle_spawn_zones_per_tick));
    if (has_players)
    {
        active_zone_indices.reserve(zone_count);
        for (size_t zone_index = 0; zone_index < zone_count; ++zone_index)
        {
            if (ZoneIsNearPlayer(map->zones[zone_index], player_positions))
                active_zone_indices.push_back(zone_index);
        }
    }

    const size_t zones_to_visit = has_players
                                      ? std::min(active_zone_indices.size(), active_spawn_zones_per_tick)
                                      : std::min(zone_count, idle_zones_per_tick);

    zone_work.reserve(zones_to_visit);
    for (size_t visit_index = 0; visit_index < zones_to_visit; ++visit_index)
    {
        const size_t zone_index = has_players
                                      ? active_zone_indices[(m_spawn_zone_cursor + visit_index) % active_zone_indices.size()]
                                      : (m_idle_spawn_zone_cursor + visit_index) % zone_count;
        const FlorrBtMap::Zone& zone = map->zones[zone_index];
        const bool single_spawn_zone = IsSingleSpawnZone(zone);
        if (single_spawn_zone && SingleSpawnZoneMobAlive(world, zone_index)) continue;

        const int target_mobs = ZoneTargetMobCount(zone);
        if (target_mobs <= 0) continue;
        zone_work.push_back({zone_index, &zone, ZoneBounds(zone), target_mobs, 0, single_spawn_zone});
    }
    CountMobsInSpawnZones(world, zone_work);

    pending_spawns.reserve(zone_work.size() * static_cast<size_t>(max_spawn_per_zone_tick));
    for (spawn_zone_work& work : zone_work)
    {
        const size_t zone_index = work.zone_index;
        const FlorrBtMap::Zone& zone = *work.zone;
        const bool single_spawn_zone = work.single_spawn;
        const int target_mobs = work.target_mobs;
        int& current_mobs = work.current_mobs;
        if (current_mobs >= target_mobs)
        {
            continue;
        }

        const std::vector<SZoneMobEntry>& mob_entries = CachedZoneMobEntries(zone.mobs);
        if (mob_entries.empty())
        {
            LOG_WARN("opencontroller", "Spawn zone skipped: no mob type for pool '" + zone.mobs + "'");
            continue;
        }

        const int max_spawn_for_zone = has_players ? max_spawn_per_zone_tick :
                                      std::max(1, game_config::open_idle_spawn_per_zone_tick);
        const int spawn_budget = std::min(target_mobs - current_mobs, max_spawn_for_zone);
        for (int i = 0; i < spawn_budget; ++i)
        {
            EMobType mob_type = PickZoneMobType(mob_entries);
            if (mob_type == EMobType::None)
            {
                continue;
            }

            ERarity rarity = PickRarityForDifficulty(zone.difficulty);
            const CMobPrototype* proto = FindMobPrototype(mob_type);
            if (!proto)
            {
                continue;
            }

            const float spawn_radius = SpawnRadiusFor(*proto, rarity);
            sf::Vector2f pos;
            switch (PickSpawnPosition(world, zone, spawn_radius, pending_spawns, pos))
            {
            case spawn_position_status::Found:
                break;
            case spawn_position_status::NoPosition:
                continue;
            case spawn_position_status::Blocked:
                continue;
            }

            if (ShouldSkipSuperOrHigherSpawn(world, pos, rarity))
                continue;

            auto mob = CreateMob(mob_type, &world, pos, rarity);
            if (mob)
            {
                CMobBase* raw_mob = dynamic_cast<CMobBase*>(world.InsertEntity(std::move(mob)));
                if (raw_mob)
                {
                    if (single_spawn_zone) RememberSingleSpawnZoneMob(zone_index, raw_mob);
                    BroadcastMobSpawnReport(world, *raw_mob, "has spawned");
                }
                pending_spawns.push_back({pos, spawn_radius});
                for (spawn_zone_work& counted_work : zone_work)
                {
                    if (counted_work.single_spawn || !counted_work.zone) continue;
                    if (!PointInZoneBounds(counted_work.bounds, pos)) continue;
                    if (IsPointInZone(*counted_work.zone, pos)) ++counted_work.current_mobs;
                }
                if (single_spawn_zone) ++current_mobs;
            }
        }
    }

    if (has_players && !active_zone_indices.empty())
        m_spawn_zone_cursor = (m_spawn_zone_cursor + zones_to_visit) % active_zone_indices.size();
    else if (!has_players)
        m_idle_spawn_zone_cursor = (m_idle_spawn_zone_cursor + zones_to_visit) % zone_count;
}

bool COpenController::SingleSpawnZoneMobAlive(CGameWorld& world, size_t zone_index) const
{
    if (zone_index >= m_single_spawn_zone_mobs.size()) return false;
    const single_spawn_zone_mob& tracked = m_single_spawn_zone_mobs[zone_index];
    if (tracked.id < 0 || tracked.generation == 0) return false;

    CEntity* entity = world.GetEntity(tracked.id, tracked.generation);
    return entity && !entity->m_is_marked_for_des && !entity->IsDead();
}

void COpenController::RememberSingleSpawnZoneMob(size_t zone_index, const CEntity* entity)
{
    if (!entity || zone_index >= m_single_spawn_zone_mobs.size()) return;
    m_single_spawn_zone_mobs[zone_index] = {entity->m_id, entity->m_generation};
}

void COpenController::ForgetSingleSpawnZoneMob(const CEntity* entity)
{
    if (!entity || entity->m_id < 0 || entity->m_generation == 0) return;
    for (single_spawn_zone_mob& tracked : m_single_spawn_zone_mobs)
    {
        if (tracked.id != entity->m_id || tracked.generation != entity->m_generation) continue;
        tracked = {};
    }
}

void COpenController::OnPlayerConnect(CGameWorld& world, CPlayer* player)
{
    if (!player) return;

    player->ConsumeUseNewPlayerSpawn();
    sf::Vector2f spawn_pos = PickReturningPlayerSpawnPosition(world);
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

void COpenController::ModifyTalentContext(CGameWorld&, CPlayer*, ETalentEvent, STalentContext&)
{
}

void COpenController::OnEntityDie(CGameWorld& world, CEntity* entity)
{
    ForgetSingleSpawnZoneMob(entity);

    auto* mob = dynamic_cast<CMobBase*>(entity);
    if (!mob) return;
    if (mob->m_mob_type == EMobType::SummonedBeetle || mob->m_mob_type == EMobType::SummonedSoldierAnt) return;
    if (IsPlayerOwnedSummon(world, *mob)) return;

    BroadcastMobDefeatReport(world, *mob);

    std::vector<lootable_player> lootable_players = FindLootablePlayers(*mob);
    if (lootable_players.empty() && IsSuperOrHigher(mob->GetRarity()))
    {
        LogSuperLootIssue(*mob, "No lootable players");
        return;
    }

    const bool log_loot_issues = IsSuperOrHigher(mob->GetRarity());
    for (const lootable_player& lootable : lootable_players)
    {
        if (!lootable.player)
        {
            if (log_loot_issues)
                LogSuperLootIssue(*mob, "Skipped lootable null player");
            continue;
        }

        const std::vector<SDropRate>& rates = QueryDropRates(mob->m_mob_type, mob->GetRarity());
        if (rates.empty())
        {
            if (log_loot_issues)
                LogSuperLootIssue(*mob, "No drop table");
            continue;
        }

        std::vector<SDropRate> drops = RollDrops(mob->m_mob_type, mob->GetRarity());
        if (drops.empty())
        {
            if (log_loot_issues)
                LOG_WARN("loot", "Rolled zero drops mob=" + std::string(GetRarityName(mob->GetRarity())) + " " +
                                    std::string(GetMobTypeName(mob->m_mob_type)) +
                                    " id=" + std::to_string(mob->m_id) +
                                    " player=" + lootable.player->GetName() +
                                    "#" + std::to_string(lootable.player->GetId()) +
                                    " lootables=" + LootableSummary(lootable_players) +
                                    " damage_sources=" + DamageSummary(*mob));
            continue;
        }

        size_t drop_index = 0;
        const size_t player_drop_count = drops.size();
        for (const SDropRate& drop : drops)
        {
            sf::Vector2f drop_pos = mob->m_pos + DropSpreadOffset(drop_index++, player_drop_count);
            auto drop_entity = std::make_unique<CDrop>(&world, drop_pos, drop.type, drop.rarity, lootable.player->GetId());
            world.InsertEntity(std::move(drop_entity));
        }
        if (log_loot_issues && mob->m_mob_type == EMobType::AntHole)
        {
            LOG_INFO("loot", "Generated AntHole drops mob=" + std::string(GetRarityName(mob->GetRarity())) +
                              " id=" + std::to_string(mob->m_id) +
                              " player=" + lootable.player->GetName() +
                              "#" + std::to_string(lootable.player->GetId()) +
                              " drops=" + DropSummary(drops));
        }
    }
}
