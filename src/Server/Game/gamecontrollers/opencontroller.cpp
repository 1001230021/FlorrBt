#include "opencontroller.h"
#include "../../../Engine/map_tools.h"
#include "../../../Shared/drop_rate.h"
#include "../../../Shared/game_config.h"
#include "../../Module/network_module.h"
#include "../../server.h"
#include "../controllers/melee_controller.h"
#include "../entities/drop.h"
#include "../entities/flower.h"
#include "../gamecontext.h"
#include "../gameworld.h"
#include "../player.h"
#include "../zone_mob_tools.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <random>
#include <sstream>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

namespace
{
struct lootable_player
{
    CPlayer* player = nullptr;
    float damage = 0.f;
};

struct zone_bounds
{
    sf::Vector2f center = { 0.f, 0.f };
    float radius = 0.f;
    float min_x = 0.f;
    float min_y = 0.f;
    float max_x = 0.f;
    float max_y = 0.f;
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

    std::sort(records.begin(), records.end(),
              [](const CDamageData* lhs, const CDamageData* rhs) { return lhs->m_total_dmg > rhs->m_total_dmg; });

    std::ostringstream oss;
    for (size_t i = 0; i < records.size() && i < 5; ++i)
    {
        const CDamageData* record = records[i];
        if (i > 0) oss << ", ";
        const bool active_player = IsActivePlayer(context, record->m_player);
        oss << (active_player ? record->m_player->GetName() : "<stale>") << "#"
            << (active_player ? record->m_player->GetId() : 0) << "=" << record->m_total_dmg;
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
        oss << (lootable.player ? lootable.player->GetName() : "<null>") << "#"
            << (lootable.player ? lootable.player->GetId() : 0) << "=" << lootable.damage;
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
                         std::string(GetMobTypeName(mob.m_mob_type)) + " id=" + std::to_string(mob.m_id) +
                         " max_health=" + std::to_string(max_health) + " recorded_damage=" +
                         std::to_string(TotalDamageRecorded(mob)) + " damage_sources=" + DamageSummary(mob));
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
    const std::string mob_name =
        proto && !proto->m_name.empty() ? proto->m_name : std::string(GetMobTypeName(mob.m_mob_type));
    if (CServer* server = CServer::GetInstance())
        server->BroadcastMobSpawnReport(world, action, mob.GetRarity(), mob_name);
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
    const std::string mob_name =
        proto && !proto->m_name.empty() ? proto->m_name : std::string(GetMobTypeName(mob.m_mob_type));
    std::string action = "has been defeated";
    if (CPlayer* top_player = FindTopDamagePlayer(mob))
    {
        action += " by ";
        action += top_player->GetName();
    }

    if (CServer* server = CServer::GetInstance()) server->BroadcastMobReport(action, mob.GetRarity(), mob_name);
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

    bounds.center = { (min_x + max_x) * 0.5f, (min_y + max_y) * 0.5f };
    bounds.radius = Distance(bounds.center, { max_x, max_y });
    bounds.min_x = min_x;
    bounds.min_y = min_y;
    bounds.max_x = max_x;
    bounds.max_y = max_y;
    cache.emplace(&zone, bounds);
    return bounds;
}

bool IsAboveUltra(ERarity rarity) { return IsAboveRarity(rarity, ERarity::Ultra); }

bool IsSuperOrHigher(ERarity rarity) { return IsAtLeastRarity(rarity, ERarity::Super); }

sf::Vector2f DropSpreadOffset(size_t index, size_t count)
{
    if (count <= 1) return { 0.f, 0.f };
    float angle = 2.f * game_config::pi * static_cast<float>(index) / static_cast<float>(count);
    const float radius = std::max(0.f, game_config::open_drop_spread_radius);
    return { std::cos(angle) * radius, std::sin(angle) * radius };
}

std::mt19937& SpawnRng()
{
    static std::mt19937 rng{ std::random_device{}() };
    return rng;
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
    const float center_level = std::clamp(difficulty / difficulty_scale, static_cast<float>(GetLevel(ERarity::Common)),
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
    float roll = dist(SpawnRng());
    for (const rarity_weight& entry : weights)
    {
        if (roll <= entry.probability) return entry.rarity;
        roll -= entry.probability;
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
    sf::Vector2f candidate = { random_x(SpawnRng()), random_y(SpawnRng()) };
    if (!IsPointInZone(zone, candidate)) return false;
    out_pos = candidate;
    return true;
}

bool RandomPointInCheckpoint(const CGameWorld& world, const FlorrBtMap::Checkpoint& checkpoint, sf::Vector2f& out_pos)
{
    if (checkpoint.w <= 0.f || checkpoint.h <= 0.f) return false;
    std::uniform_real_distribution<float> random_x(0.f, checkpoint.w);
    std::uniform_real_distribution<float> random_y(0.f, checkpoint.h);
    const int attempts = std::max(1, game_config::open_spawn_position_attempts);
    for (int attempt = 0; attempt < attempts; ++attempt)
    {
        const FlorrBtMap::Point point =
            CheckpointLocalToWorldPoint(checkpoint, random_x(SpawnRng()), random_y(SpawnRng()));
        sf::Vector2f candidate = { point.x, point.y };
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

    return { game_config::player_respawn_x, game_config::player_respawn_y };
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
    float result = (!std::isfinite(area) || area <= 0.0)
                       ? 0.f
                       : static_cast<float>(std::min(area, static_cast<double>(std::numeric_limits<float>::max())));
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

    const int max_target = std::max(0, game_config::open_max_zone_spawn_target);
    const double capped_desired = std::min(desired, static_cast<double>(max_target));
    int target = static_cast<int>(std::floor(capped_desired));
    const double fractional = capped_desired - std::floor(capped_desired);
    if (target < max_target && fractional > 0.0)
    {
        std::uniform_real_distribution<double> dist(0.0, 1.0);
        if (dist(SpawnRng()) < fractional) ++target;
    }
    return std::clamp(target, 0, max_target);
}

bool IsSingleSpawnZone(const FlorrBtMap::Zone& zone) { return zone.density == 0.f && !zone.mobs.empty(); }

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

bool RandomPointNearActivePlayerInZone(const FlorrBtMap::Zone& zone, const std::vector<sf::Vector2f>& player_positions,
                                       sf::Vector2f& out_pos)
{
    if (player_positions.empty()) return false;

    const float spawn_radius = std::max(0.f, game_config::open_active_spawn_near_player_radius);
    if (spawn_radius <= 0.f) return false;

    const zone_bounds bounds = ZoneBounds(zone);
    const float interest_radius =
        bounds.radius + std::max(game_config::simulation_active_view_radius_cap, game_config::default_horizon);
    const float interest_radius_sq = interest_radius * interest_radius;
    std::uniform_int_distribution<size_t> player_index(0, player_positions.size() - 1);
    std::uniform_real_distribution<float> angle(0.f, 2.f * game_config::pi);
    std::uniform_real_distribution<float> unit(0.f, 1.f);

    const sf::Vector2f& player_pos = player_positions[player_index(SpawnRng())];
    if (DistanceSq(player_pos, bounds.center) > interest_radius_sq) return false;

    const float distance = std::sqrt(unit(SpawnRng())) * spawn_radius;
    const float direction = angle(SpawnRng());
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

spawn_position_status PickSpawnPosition(CGameWorld& world, const FlorrBtMap::Zone& zone, float spawn_radius,
                                        const std::vector<pending_spawn>& pending_spawns,
                                        const std::vector<sf::Vector2f>& player_positions, sf::Vector2f& out_pos)
{
    bool found_zone_position = false;
    const int near_player_attempts =
        player_positions.empty() ? 0 : std::max(0, game_config::open_active_spawn_near_player_attempts);
    for (int attempt = 0; attempt < near_player_attempts; ++attempt)
    {
        sf::Vector2f candidate;
        if (!RandomPointNearActivePlayerInZone(zone, player_positions, candidate)) continue;
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
    const CGameContext* context = world.GameContext();
    if (!context) return;

    positions.reserve(context->Players().size());
    for (const auto& player : context->Players())
    {
        if (!player || !player->IsConnected() || !player->IsAuthenticated()) continue;
        const CEntity* entity = player->GetEntity();
        const auto* mob = dynamic_cast<const CMobBase*>(entity);
        if (!mob || mob->m_is_marked_for_des || mob->IsDead()) continue;
        if (mob->m_mob_type != EMobType::PlayerFlower) continue;
        if (entity->GameWorld() != &world) continue;
        positions.push_back(mob->m_pos);
    }
}

bool ZoneIsNearPlayer(const FlorrBtMap::Zone& zone, const std::vector<sf::Vector2f>& player_positions)
{
    if (player_positions.empty()) return false;

    const zone_bounds bounds = ZoneBounds(zone);
    const float interest_radius =
        bounds.radius + std::max(game_config::simulation_active_view_radius_cap, game_config::default_horizon);
    const float interest_radius_sq = interest_radius * interest_radius;
    for (const sf::Vector2f& player_pos : player_positions)
    {
        if (DistanceSq(player_pos, bounds.center) <= interest_radius_sq) return true;
    }
    return false;
}

std::vector<lootable_player> FindLootablePlayers(COpenController& controller, CGameWorld& world, const CMobBase& mob)
{
    const CGameContext* context = const_cast<CMobBase&>(mob).GameContext();
    const bool above_ultra = IsAboveUltra(mob.GetRarity());
    const size_t max_lootable =
        above_ultra ? game_config::max_lootable_player_above_ultra : game_config::max_lootable_players;
    const float min_damage_rate = above_ultra ? game_config::open_loot_min_damage_rate_above_ultra
                                              : game_config::open_loot_min_damage_rate_normal;

    float total_damage = 0.f;
    std::unordered_map<CPlayer*, float> damage_by_player;
    std::vector<CPlayer*> damage_contributors;
    for (const CDamageData& damage_data : mob.GetDamageData())
    {
        if (!IsActivePlayer(context, damage_data.m_player) || damage_data.m_total_dmg <= 0.f) continue;
        total_damage += damage_data.m_total_dmg;
        if (!damage_by_player.contains(damage_data.m_player)) damage_contributors.push_back(damage_data.m_player);
        damage_by_player[damage_data.m_player] += damage_data.m_total_dmg;
    }

    for (CPlayer* contributor : damage_contributors)
    {
        if (!contributor) continue;
        for (CPlayer* squad_member : controller.GetSquadPlayerList(world, *contributor))
        {
            if (IsActivePlayer(context, squad_member)) damage_by_player.try_emplace(squad_member, 0.f);
        }
    }

    const SMobStats* stats = mob.GetFinalStats();
    const float max_health = stats ? stats->max_health : 0.f;
    const float min_damage = std::min(total_damage, max_health) * min_damage_rate;

    std::vector<lootable_player> candidates;
    candidates.reserve(damage_by_player.size());
    for (const auto& [player, damage] : damage_by_player)
    {
        candidates.push_back({ player, damage });
    }

    std::unordered_map<std::uint32_t, std::vector<lootable_player*>> squad_damage;
    for (lootable_player& candidate : candidates)
    {
        if (!candidate.player) continue;
        squad_damage[controller.GetSquadRootId(world, *candidate.player)].push_back(&candidate);
    }

    const float squad_damage_range = std::max(0.f, game_config::open_squad_loot_damage_range);
    for (auto& [_, members] : squad_damage)
    {
        if (members.size() < 2) continue;

        float min_member_damage = members.front()->damage;
        float max_member_damage = min_member_damage;
        float total_member_damage = 0.f;
        for (const lootable_player* member : members)
        {
            min_member_damage = std::min(min_member_damage, member->damage);
            max_member_damage = std::max(max_member_damage, member->damage);
            total_member_damage += member->damage;
        }

        const float average_member_damage = total_member_damage / static_cast<float>(members.size());
        const float member_damage_span = max_member_damage - min_member_damage;
        for (lootable_player* member : members)
        {
            if (member_damage_span <= std::numeric_limits<float>::epsilon())
            {
                member->damage = average_member_damage;
                continue;
            }

            const float progress = (member->damage - min_member_damage) / member_damage_span;
            member->damage = average_member_damage - squad_damage_range + progress * squad_damage_range * 2.f;
        }
    }

    candidates.erase(std::remove_if(candidates.begin(), candidates.end(),
                                    [min_damage](const lootable_player& player) { return player.damage < min_damage; }),
                     candidates.end());

    std::sort(candidates.begin(), candidates.end(), [](const lootable_player& lhs, const lootable_player& rhs) {
        if (lhs.damage != rhs.damage) return lhs.damage > rhs.damage;
        const std::uint32_t lhs_id = lhs.player ? lhs.player->GetId() : 0;
        const std::uint32_t rhs_id = rhs.player ? rhs.player->GetId() : 0;
        return lhs_id < rhs_id;
    });
    if (candidates.size() > max_lootable) candidates.resize(max_lootable);
    return candidates;
}
} // namespace

void COpenController::OnTick(CGameWorld& world, float dt)
{
    PruneSquads(world);
    if (m_count <= 0)
    {
        SpawnMobs(world);
        m_count = game_config::open_spawn_interval;
    } else
    {
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

    const size_t idle_zones_per_tick = static_cast<size_t>(std::max(1, game_config::open_idle_spawn_zones_per_tick));
    if (has_players)
    {
        active_zone_indices.reserve(zone_count);
        for (size_t zone_index = 0; zone_index < zone_count; ++zone_index)
        {
            if (ZoneIsNearPlayer(map->zones[zone_index], player_positions)) active_zone_indices.push_back(zone_index);
        }
    }

    const size_t zones_to_visit =
        has_players
            ? std::min(active_zone_indices.size(), std::max<size_t>(1, game_config::open_active_spawn_zones_per_tick))
            : std::min(zone_count, idle_zones_per_tick);

    zone_work.reserve(zones_to_visit);
    for (size_t visit_index = 0; visit_index < zones_to_visit; ++visit_index)
    {
        const size_t zone_index =
            has_players ? active_zone_indices[(m_spawn_zone_cursor + visit_index) % active_zone_indices.size()]
                        : (m_idle_spawn_zone_cursor + visit_index) % zone_count;
        const FlorrBtMap::Zone& zone = map->zones[zone_index];
        const bool single_spawn_zone = IsSingleSpawnZone(zone);
        if (single_spawn_zone && SingleSpawnZoneMobAlive(world, zone_index)) continue;

        const int target_mobs = ZoneTargetMobCount(zone);
        if (target_mobs <= 0) continue;
        zone_work.push_back({ zone_index, &zone, ZoneBounds(zone), target_mobs, 0, single_spawn_zone });
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

        const int max_spawn_for_zone =
            has_players ? active_spawn_limit : std::max(1, game_config::open_idle_spawn_per_zone_tick);
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
            switch (PickSpawnPosition(world, zone, spawn_radius, pending_spawns, player_positions, pos))
            {
            case spawn_position_status::Found:
                break;
            case spawn_position_status::NoPosition:
                continue;
            case spawn_position_status::Blocked:
                continue;
            }

            auto mob = CreateMob(mob_type, &world, pos, rarity);
            if (mob)
            {
                CMobBase* raw_mob = dynamic_cast<CMobBase*>(world.InsertEntity(std::move(mob)));
                if (raw_mob)
                {
                    if (single_spawn_zone) RememberSingleSpawnZoneMob(zone_index, raw_mob);
                    BroadcastMobSpawnReport(world, *raw_mob, "has spawned");
                }
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
    }

    if (has_players && !active_zone_indices.empty())
        m_spawn_zone_cursor = (m_spawn_zone_cursor + zones_to_visit) % active_zone_indices.size();
    else if (!has_players) m_idle_spawn_zone_cursor = (m_idle_spawn_zone_cursor + zones_to_visit) % zone_count;
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
    m_single_spawn_zone_mobs[zone_index] = { entity->m_id, entity->m_generation };
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

void COpenController::ModifyTalentContext(CGameWorld&, CPlayer*, ETalentEvent, STalentContext&) {}

bool COpenController::IsSquadPlayerInWorld(const CGameWorld& world, const CPlayer& player) const
{
    if (!player.IsAuthenticated() || !player.IsConnected()) return false;
    const CEntity* entity = player.GetEntity();
    return entity && entity->GameWorld() == &world;
}

void COpenController::EnsureSquadPlayer(std::uint32_t player_id) { m_squad_parent.try_emplace(player_id, player_id); }

std::uint32_t COpenController::FindSquadRoot(std::uint32_t player_id)
{
    auto node = m_squad_parent.find(player_id);
    if (node == m_squad_parent.end()) return player_id;

    std::vector<std::uint32_t> path;
    std::uint32_t current = player_id;
    while (true)
    {
        path.push_back(current);
        auto current_node = m_squad_parent.find(current);
        if (current_node == m_squad_parent.end()) break;

        const std::uint32_t parent = current_node->second;
        if (parent == current) break;
        if (std::find(path.begin(), path.end(), parent) != path.end())
        {
            current = player_id;
            m_squad_parent[player_id] = player_id;
            break;
        }
        current = parent;
    }

    for (std::uint32_t id : path)
    {
        if (auto it = m_squad_parent.find(id); it != m_squad_parent.end()) it->second = current;
    }
    return current;
}

std::vector<std::uint32_t> COpenController::GetSquadMemberIds(std::uint32_t root_id)
{
    std::vector<std::uint32_t> members;
    for (const auto& [player_id, _] : m_squad_parent)
    {
        if (FindSquadRoot(player_id) == root_id) members.push_back(player_id);
    }
    std::sort(members.begin(), members.end());
    return members;
}

void COpenController::PruneSquads(CGameWorld& world)
{
    CGameContext* context = world.GameContext();
    if (!context)
    {
        m_squad_parent.clear();
        return;
    }

    std::vector<std::uint32_t> active_ids;
    std::unordered_set<std::uint32_t> active_id_set;
    active_ids.reserve(context->Players().size());
    for (const auto& player : context->Players())
    {
        if (!player || !IsSquadPlayerInWorld(world, *player)) continue;
        active_ids.push_back(player->GetId());
        active_id_set.insert(player->GetId());
        EnsureSquadPlayer(player->GetId());
    }

    std::unordered_map<std::uint32_t, std::vector<std::uint32_t>> components;
    for (std::uint32_t player_id : active_ids)
        components[FindSquadRoot(player_id)].push_back(player_id);

    m_squad_parent.clear();
    for (auto& [old_root, members] : components)
    {
        if (members.empty()) continue;
        std::uint32_t new_root =
            active_id_set.contains(old_root) ? old_root : *std::min_element(members.begin(), members.end());
        for (std::uint32_t player_id : members)
            m_squad_parent.emplace(player_id, new_root);
    }
}

std::uint32_t COpenController::GetSquadRootId(CGameWorld& world, CPlayer& player)
{
    PruneSquads(world);
    if (!IsSquadPlayerInWorld(world, player)) return player.GetId();
    EnsureSquadPlayer(player.GetId());
    return FindSquadRoot(player.GetId());
}

std::vector<CPlayer*> COpenController::GetSquadPlayerList(CGameWorld& world, CPlayer& player)
{
    PruneSquads(world);
    if (!IsSquadPlayerInWorld(world, player)) return {};

    EnsureSquadPlayer(player.GetId());
    const std::vector<std::uint32_t> member_ids = GetSquadMemberIds(FindSquadRoot(player.GetId()));
    CGameContext* context = world.GameContext();
    if (!context) return {};

    std::unordered_map<std::uint32_t, CPlayer*> players_by_id;
    for (const auto& candidate : context->Players())
    {
        if (candidate && IsSquadPlayerInWorld(world, *candidate))
            players_by_id.emplace(candidate->GetId(), candidate.get());
    }

    std::vector<CPlayer*> players;
    players.reserve(member_ids.size());
    for (std::uint32_t member_id : member_ids)
    {
        if (auto it = players_by_id.find(member_id); it != players_by_id.end()) players.push_back(it->second);
    }
    return players;
}

bool COpenController::TrySquad(CGameWorld& world, CPlayer& joining_player, CPlayer& target_player)
{
    PruneSquads(world);
    if (&joining_player == &target_player || !IsSquadPlayerInWorld(world, joining_player) ||
        !IsSquadPlayerInWorld(world, target_player))
        return false;

    EnsureSquadPlayer(joining_player.GetId());
    EnsureSquadPlayer(target_player.GetId());

    const std::uint32_t joining_root = FindSquadRoot(joining_player.GetId());
    const std::uint32_t target_root = FindSquadRoot(target_player.GetId());
    if (joining_root == target_root || GetSquadMemberIds(joining_root).size() != 1) return false;

    const size_t max_squad_size = std::max<size_t>(1, game_config::open_controller_max_squad_size);
    if (GetSquadMemberIds(target_root).size() >= max_squad_size) return false;

    m_squad_parent[joining_player.GetId()] = target_root;
    return true;
}

bool COpenController::TryLeaveSquad(CGameWorld& world, CPlayer& player)
{
    PruneSquads(world);
    if (!IsSquadPlayerInWorld(world, player)) return false;

    EnsureSquadPlayer(player.GetId());
    const std::uint32_t root = FindSquadRoot(player.GetId());
    const std::vector<std::uint32_t> members = GetSquadMemberIds(root);
    if (members.size() <= 1) return false;

    if (player.GetId() != root)
    {
        m_squad_parent[player.GetId()] = player.GetId();
        return true;
    }

    auto successor = std::find_if(members.begin(), members.end(),
                                  [&player](std::uint32_t member_id) { return member_id != player.GetId(); });
    if (successor == members.end()) return false;

    for (std::uint32_t member_id : members)
        m_squad_parent[member_id] = member_id == player.GetId() ? player.GetId() : *successor;
    return true;
}

void COpenController::OnEntityDie(CGameWorld& world, CEntity* entity)
{
    ForgetSingleSpawnZoneMob(entity);

    auto* mob = dynamic_cast<CMobBase*>(entity);
    if (!mob) return;
    if (mob->m_mob_type == EMobType::SummonedBeetle || mob->m_mob_type == EMobType::SummonedSoldierAnt) return;
    if (IsPlayerOwnedSummon(world, *mob)) return;

    BroadcastMobDefeatReport(world, *mob);

    std::vector<lootable_player> lootable_players = FindLootablePlayers(*this, world, *mob);
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
            if (log_loot_issues) LogSuperLootIssue(*mob, "Skipped lootable null player");
            continue;
        }

        const std::vector<SDropRate>& rates = QueryDropRates(mob->m_mob_type, mob->GetRarity());
        if (rates.empty())
        {
            if (log_loot_issues) LogSuperLootIssue(*mob, "No drop table");
            continue;
        }

        std::vector<SDropRate> drops = RollDrops(mob->m_mob_type, mob->GetRarity());
        if (drops.empty())
        {
            if (log_loot_issues)
                LOG_WARN("loot", "Rolled zero drops mob=" + std::string(GetRarityName(mob->GetRarity())) + " " +
                                     std::string(GetMobTypeName(mob->m_mob_type)) + " id=" + std::to_string(mob->m_id) +
                                     " player=" + lootable.player->GetName() + "#" +
                                     std::to_string(lootable.player->GetId()) + " lootables=" +
                                     LootableSummary(lootable_players) + " damage_sources=" + DamageSummary(*mob));
            continue;
        }

        size_t drop_index = 0;
        const size_t player_drop_count = drops.size();
        for (const SDropRate& drop : drops)
        {
            sf::Vector2f drop_pos = mob->m_pos + DropSpreadOffset(drop_index++, player_drop_count);
            auto drop_entity =
                std::make_unique<CDrop>(&world, drop_pos, drop.type, drop.rarity, lootable.player->GetId());
            world.InsertEntity(std::move(drop_entity));
        }
        if (log_loot_issues && mob->m_mob_type == EMobType::AntHole)
        {
            LOG_INFO("loot", "Generated AntHole drops mob=" + std::string(GetRarityName(mob->GetRarity())) +
                                 " id=" + std::to_string(mob->m_id) + " player=" + lootable.player->GetName() + "#" +
                                 std::to_string(lootable.player->GetId()) + " drops=" + DropSummary(drops));
        }
    }
}
