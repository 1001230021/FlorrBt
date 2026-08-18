#include "melee_controller.h"
#include "../../../Shared/game_config.h"
#include "../../HotReload/snapshot_archive.h"
#include "../entities/mob.h"
#include "../entities/petals/petal.h"
#include "../entities/projectile.h"
#include "../gameworld.h"
#include "../../server.h"
#include "../state_zone.h"
#include "../states/states.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <random>
#include <vector>

namespace
{
struct melee_target_candidate
{
    float dist_sq = std::numeric_limits<float>::max();
    CEntity* target = nullptr;
};

struct hornet_aim_target
{
    sf::Vector2f relative = { 0.f, 0.f };
    float hit_radius = 0.f;
    float weight = 0.f;
};

struct hornet_aim_event
{
    float angle = 0.f;
    float delta = 0.f;
};

struct hornet_aim_candidate
{
    float angle = 0.f;
    float upper_score = 0.f;
};

struct melee_scan_scratch
{
    std::vector<melee_target_candidate> honey_candidates;
    std::vector<melee_target_candidate> target_candidates;
    std::vector<hornet_aim_target> hornet_aim_targets;
    std::vector<hornet_aim_event> hornet_aim_events;
    std::vector<hornet_aim_candidate> hornet_aim_candidates;
};

thread_local melee_scan_scratch g_melee_scan_scratch;
constexpr float mecha_flower_orbit_exit_hysteresis_radius_multiplier = 0.25f;

bool IsMeleeTargetBlockedByWall(CMobBase* mob, const CEntity* target);

sf::Vector2f NormalizeOrZero(sf::Vector2f value)
{
    const float length = Length(value);
    if (length <= game_config::entity_collision_epsilon) return { 0.f, 0.f };
    return value / length;
}

float DotProduct(sf::Vector2f lhs, sf::Vector2f rhs) { return lhs.x * rhs.x + lhs.y * rhs.y; }

float TargetEdgeDistanceSq(const CEntity* source, const CEntity* target)
{
    if (!source || !target) return std::numeric_limits<float>::max();
    float center_distance = Distance(source->Pos(), target->Pos());
    float edge_distance =
        std::max(0.f, center_distance - std::max(0.f, source->Radius()) - std::max(0.f, target->Radius()));
    return edge_distance * edge_distance;
}

float TargetSearchQueryRange(CMobBase* mob, float edge_range)
{
    if (!mob) return edge_range;
    float max_target_radius = std::max(game_config::default_flower_radius,
                                       game_config::mob_queen_ant_radius *
                                           game_config::MobRadiusScaleForLevel(GetLevel(ERarity::Primordial)));
    return edge_range + std::max(0.f, mob->Radius()) + max_target_radius;
}

float HornetMissileSpeed(ERarity rarity)
{
    int level = GetLevel(rarity);
    return game_config::mob_hornet_missile_speed *
           (1.f + game_config::mob_hornet_missile_speed_level_step * static_cast<float>(level));
}

float HornetSkill2AngularSpeed(float speed, float orbit_radius)
{
    float angular_speed = speed / std::max(game_config::entity_collision_epsilon, orbit_radius);
    if (game_config::mob_hornet_skill2_max_duration > game_config::entity_collision_epsilon)
    {
        float capped_speed = game_config::pi * game_config::mob_hornet_skill2_arc_pi_multiplier /
                             game_config::mob_hornet_skill2_max_duration;
        angular_speed = std::max(angular_speed, capped_speed);
    }
    return angular_speed;
}

int TargetScanIntervalTicks() { return std::max(1, game_config::melee_target_scan_ticks); }

bool ShouldRunEntityTargetScan(int& cooldown, const CEntity* entity, int interval)
{
    interval = std::max(1, interval);
    if (interval <= 1)
    {
        cooldown = 0;
        return true;
    }

    if (cooldown < 0) cooldown = entity ? std::max(0, entity->Id()) % interval : 0;

    if (cooldown > 0)
    {
        --cooldown;
        return false;
    }

    cooldown = interval - 1;
    return true;
}

bool ShouldRunEntityTargetScan(int& cooldown, const CEntity* entity)
{
    return ShouldRunEntityTargetScan(cooldown, entity, TargetScanIntervalTicks());
}

void AddMeleeTargetCandidate(std::vector<melee_target_candidate>& candidates, CEntity* target, float dist_sq,
                             size_t candidate_limit)
{
    if (!target || candidate_limit == 0) return;

    auto insertion = std::lower_bound(
        candidates.begin(), candidates.end(), dist_sq,
        [](const melee_target_candidate& candidate, float distance) { return candidate.dist_sq < distance; });
    if (candidates.size() >= candidate_limit && insertion == candidates.end()) return;
    candidates.insert(insertion, { dist_sq, target });
    if (candidates.size() > candidate_limit) candidates.pop_back();
}

CEntity* FirstVisibleMeleeTarget(CMobBase* mob, const std::vector<melee_target_candidate>& candidates)
{
    for (const melee_target_candidate& candidate : candidates)
    {
        if (!IsMeleeTargetBlockedByWall(mob, candidate.target)) return candidate.target;
    }
    return nullptr;
}

bool IsInvalidMeleeTarget(const CMobBase* mob, const CEntity* target, int ignored_id = -1, int ignored_owner_id = -1)
{
    if (!mob || !target) return true;
    if (!mob->GameWorld() || target->GameWorld() != mob->GameWorld()) return true;
    if (target->IsMarkedForDes() || target->IsDead() || !target->CanCollide()) return true;
    if (IsDiggingEntity(target)) return true;
    if (target == mob) return true;
    if (target->Id() == ignored_id || target->Id() == ignored_owner_id) return true;
    const auto* target_mob = dynamic_cast<const CMobBase*>(target);
    if (mob->GetMobType() == EMobType::Bee && target_mob && target_mob->GetMobType() == EMobType::AntHole)
        return true;
    if (CheckTeam(target->Team(), mob->Team())) return true;
    if ((target->Team() == 0 || mob->Team() == 0) && ShareRootOwner(mob, target)) return true;
    if (BlocksNullifiedInteraction(mob, target)) return true;
    if (dynamic_cast<const CProjectile*>(target)) return true;
    return false;
}

bool IsValidHoneyTarget(const CMobBase* mob, const CEntity* target, int ignored_id = -1, int ignored_owner_id = -1)
{
    if (!mob || !target) return false;
    if (!mob->GameWorld() || target->GameWorld() != mob->GameWorld()) return false;
    if (target->IsMarkedForDes() || target->IsDead() || !target->CanCollide()) return false;
    if (IsDiggingEntity(target)) return false;
    if (target == mob) return false;
    if (target->Id() == ignored_id || target->Id() == ignored_owner_id) return false;
    if (CheckTeam(target->Team(), mob->Team())) return false;
    if ((target->Team() == 0 || mob->Team() == 0) && ShareRootOwner(mob, target)) return false;
    if (BlocksNullifiedInteraction(mob, target)) return false;

    auto* petal = dynamic_cast<const CPetal*>(target);
    if (!petal || petal->GetPetalType() != EPetalType::Honey) return false;

    int honey_level = std::max(GetLevel(ERarity::Common), GetLevel(petal->m_rarity));
    int max_attracted_level = std::max(GetLevel(ERarity::Common), honey_level - 1);
    return GetLevel(mob->GetRarity()) <= max_attracted_level;
}

bool IsMeleeTargetBlockedByWall(CMobBase* mob, const CEntity* target)
{
    if (!mob || !target || !mob->GameWorld()) return false;
    if (target->GameWorld() != mob->GameWorld()) return true;
    if (IsDiggingEntity(target)) return true;
    return mob->GameWorld()->SegmentBlockedByWall(mob->Pos(), target->Pos());
}

bool IsUnavailableMeleeTarget(CMobBase* mob, const CEntity* target, int ignored_id = -1, int ignored_owner_id = -1)
{
    if (IsValidHoneyTarget(mob, target, ignored_id, ignored_owner_id)) return IsMeleeTargetBlockedByWall(mob, target);
    return IsInvalidMeleeTarget(mob, target, ignored_id, ignored_owner_id) || IsMeleeTargetBlockedByWall(mob, target);
}

bool IsCurrentMeleeTargetUnavailable(CMobBase* mob, const CEntity* target, float search_range, float dt,
                                     float& los_check_timer, int ignored_id = -1, int ignored_owner_id = -1)
{
    if (!mob || !target || !mob->GameWorld() || target->GameWorld() != mob->GameWorld()) return true;
    if (IsDiggingEntity(target)) return true;

    const bool valid_honey = IsValidHoneyTarget(mob, target, ignored_id, ignored_owner_id);
    float retention_range = std::max(0.f, search_range);
    if (valid_honey)
    {
        retention_range = std::max(retention_range, game_config::default_honey_attract_range);
    } else if (auto* flower = dynamic_cast<const CFlower*>(target))
    {
        retention_range *= std::max(0.f, flower->m_final_stats.detection_multiplier);
        retention_range += std::max(0.f, flower->m_final_stats.detection_range_bonus);
    }
    if (TargetEdgeDistanceSq(mob, target) > retention_range * retention_range) return true;

    if (valid_honey)
    {
        los_check_timer += dt;
        if (los_check_timer < game_config::melee_target_los_recheck_interval) return false;
        los_check_timer = 0.f;
        return IsMeleeTargetBlockedByWall(mob, target);
    }

    if (IsInvalidMeleeTarget(mob, target, ignored_id, ignored_owner_id)) return true;

    los_check_timer += dt;
    if (los_check_timer < game_config::melee_target_los_recheck_interval) return false;
    los_check_timer = 0.f;
    return IsMeleeTargetBlockedByWall(mob, target);
}

CEntity* FindClosestHoneyTarget(CMobBase* mob, float search_range, int ignored_id = -1, int ignored_owner_id = -1)
{
    if (!mob || !mob->GameWorld()) return nullptr;
    if (search_range <= 0.f) return nullptr;

    float search_range_sq = search_range * search_range;
    float query_range = TargetSearchQueryRange(mob, search_range);
    const size_t candidate_limit = std::max<size_t>(1, game_config::melee_target_los_candidate_limit);
    auto& candidates = g_melee_scan_scratch.honey_candidates;
    candidates.clear();
    candidates.reserve(candidate_limit);
    mob->GameWorld()->GetSpatialGrid().ForEachInRange(mob->Pos(), query_range, [&](CEntity* candidate) {
        if (!IsValidHoneyTarget(mob, candidate, ignored_id, ignored_owner_id)) return;
        float dist_sq = TargetEdgeDistanceSq(mob, candidate);
        if (dist_sq > search_range_sq) return;
        AddMeleeTargetCandidate(candidates, candidate, dist_sq, candidate_limit);
    });
    return FirstVisibleMeleeTarget(mob, candidates);
}

// Flowers lit by a bulb are directly targetable by neutral mobs up to the
// mob's own search range plus the bulb's radius bonus. Scanned with a wider
// query, but only bulb flowers pass the per-flower range check, so non-bulb
// flowers never leak into the neutral mob's target list.
CEntity* FindClosestBulbTarget(CMobBase* mob, float search_range, int ignored_id = -1, int ignored_owner_id = -1)
{
    if (!mob || !mob->GameWorld()) return nullptr;
    if (search_range <= 0.f) return nullptr;

    const float max_bulb_bonus =
        game_config::default_bulb_detect_radius +
        game_config::default_bulb_detect_radius_step * static_cast<float>(GetLevel(ERarity::Primordial) - 1);
    const float query_range = TargetSearchQueryRange(mob, search_range + max_bulb_bonus);
    const size_t candidate_limit = std::max<size_t>(1, game_config::melee_target_los_candidate_limit);
    auto& candidates = g_melee_scan_scratch.target_candidates;
    candidates.clear();
    candidates.reserve(candidate_limit);
    mob->GameWorld()->GetSpatialGrid().ForEachInRange(mob->Pos(), query_range, [&](CEntity* candidate) {
        if (!candidate || candidate->IsMarkedForDes() || candidate->IsDead() || !candidate->CanCollide()) return;
        if (candidate->Id() == ignored_id || candidate->Id() == ignored_owner_id) return;
        if (CheckTeam(candidate->Team(), mob->Team())) return;
        if ((candidate->Team() == 0 || mob->Team() == 0) && ShareRootOwner(mob, candidate)) return;
        if (BlocksNullifiedInteraction(mob, candidate)) return;
        const auto* flower = dynamic_cast<const CFlower*>(candidate);
        if (!flower || !flower->GetFinalStats()) return;
        const float bonus = std::max(0.f, flower->GetFinalStats()->detection_range_bonus);
        if (bonus <= 0.f) return;
        const float range = std::max(0.f, search_range) + bonus;
        const float dist_sq = TargetEdgeDistanceSq(mob, candidate);
        if (dist_sq > range * range) return;
        AddMeleeTargetCandidate(candidates, candidate, dist_sq, candidate_limit);
    });
    return FirstVisibleMeleeTarget(mob, candidates);
}

template <typename TAdditionalTargetFilter>
CEntity* FindClosestMeleeTarget(CMobBase* mob, float search_range, int ignored_id, int ignored_owner_id,
                                TAdditionalTargetFilter&& additional_target_filter)
{
    if (!mob || !mob->GameWorld() || search_range <= 0.f) return nullptr;

    float search_range_sq = search_range * search_range;
    float query_range = TargetSearchQueryRange(mob, search_range);
    const size_t candidate_limit = std::max<size_t>(1, game_config::melee_target_los_candidate_limit);
    auto& honey_candidates = g_melee_scan_scratch.honey_candidates;
    auto& target_candidates = g_melee_scan_scratch.target_candidates;
    honey_candidates.clear();
    target_candidates.clear();
    honey_candidates.reserve(candidate_limit);
    target_candidates.reserve(candidate_limit);
    mob->GameWorld()->GetSpatialGrid().ForEachInRange(mob->Pos(), query_range, [&](CEntity* candidate) {
        float dist_sq = TargetEdgeDistanceSq(mob, candidate);
        if (dist_sq > search_range_sq) return;

        if (IsValidHoneyTarget(mob, candidate, ignored_id, ignored_owner_id))
        {
            AddMeleeTargetCandidate(honey_candidates, candidate, dist_sq, candidate_limit);
            return;
        }

        if (additional_target_filter(candidate))
        {
            AddMeleeTargetCandidate(target_candidates, candidate, dist_sq, candidate_limit);
            return;
        }

        if (IsInvalidMeleeTarget(mob, candidate, ignored_id, ignored_owner_id)) return;

        float detection_multiplier = 1.f;
        if (auto* flower = dynamic_cast<const CFlower*>(candidate))
            detection_multiplier = flower->m_final_stats.detection_multiplier;
        float range = search_range * detection_multiplier;
        if (auto* flower = dynamic_cast<const CFlower*>(candidate))
            range += std::max(0.f, flower->m_final_stats.detection_range_bonus);
        if (dist_sq > range * range) return;

        AddMeleeTargetCandidate(target_candidates, candidate, dist_sq, candidate_limit);
    });
    if (CEntity* honey = FirstVisibleMeleeTarget(mob, honey_candidates)) return honey;
    return FirstVisibleMeleeTarget(mob, target_candidates);
}

CEntity* FindClosestMeleeTarget(CMobBase* mob, float search_range, int ignored_id = -1, int ignored_owner_id = -1)
{
    return FindClosestMeleeTarget(mob, search_range, ignored_id, ignored_owner_id,
                                  [](const CEntity*) { return false; });
}

float NormalizeAimAngle(float angle)
{
    const float full_turn = 2.f * game_config::pi;
    while (angle < -game_config::pi) angle += full_turn;
    while (angle >= game_config::pi) angle -= full_turn;
    return angle;
}

float ScoreHornetAimDirection(CMobBase* mob, sf::Vector2f direction, float missile_radius, float max_range,
                              const std::vector<hornet_aim_target>& targets)
{
    if (!mob || !mob->GameWorld()) return 0.f;

    float score = 0.f;
    for (const hornet_aim_target& target : targets)
    {
        const float along = DotProduct(target.relative, direction);
        const float travel = std::clamp(along, 0.f, max_range);
        const sf::Vector2f offset = target.relative - direction * travel;
        if (LengthSq(offset) > target.hit_radius * target.hit_radius) continue;

        const sf::Vector2f end = mob->Pos() + direction * travel;
        if (travel > game_config::entity_collision_epsilon &&
            mob->GameWorld()->SweptCircleBlockedByWall(mob->Pos(), end, missile_radius))
            continue;
        score += target.weight;
    }
    return score;
}

bool FindBestHornetAimDirection(CMobBase* mob, float missile_radius, float missile_speed, float lifetime,
                                sf::Vector2f& result)
{
    if (!mob || !mob->GameWorld()) return false;
    const float max_range = std::max(0.f, missile_speed) * std::max(0.f, lifetime);
    if (max_range <= game_config::entity_collision_epsilon) return false;

    auto& targets = g_melee_scan_scratch.hornet_aim_targets;
    auto& events = g_melee_scan_scratch.hornet_aim_events;
    auto& aim_candidates = g_melee_scan_scratch.hornet_aim_candidates;
    targets.clear();
    events.clear();
    aim_candidates.clear();

    float initial_score = 0.f;
    const float query_range = max_range + std::max(0.f, missile_radius);
    mob->GameWorld()->ForEachEntityInEdgeRange(mob->Pos(), query_range, [&](CEntity* candidate) {
        if (!candidate || !candidate->IsEntityType(EEntityType::Mob)) return;
        if (IsInvalidMeleeTarget(mob, candidate)) return;

        const sf::Vector2f relative = candidate->Pos() - mob->Pos();
        const float distance_sq = LengthSq(relative);
        const float distance = std::sqrt(std::max(0.f, distance_sq));
        const float hit_radius = std::max(0.f, missile_radius) + std::max(0.f, candidate->Radius());
        if (distance > max_range + hit_radius) return;

        const float first_contact_distance = std::max(0.f, distance - hit_radius);
        const float distance_ratio = std::clamp(first_contact_distance / max_range, 0.f, 1.f);
        const float weight = std::sqrt(std::max(0.f, 1.f - distance_ratio * distance_ratio));
        if (weight <= game_config::entity_collision_epsilon) return;
        targets.push_back({ relative, hit_radius, weight });

        if (distance <= hit_radius)
        {
            initial_score += weight;
            return;
        }

        float half_angle = 0.f;
        if (distance <= max_range)
        {
            half_angle = std::asin(std::clamp(hit_radius / distance, 0.f, 1.f));
        } else
        {
            const float cosine =
                (distance_sq + max_range * max_range - hit_radius * hit_radius) /
                std::max(game_config::entity_collision_epsilon, 2.f * distance * max_range);
            half_angle = std::acos(std::clamp(cosine, -1.f, 1.f));
        }

        const float center = std::atan2(relative.y, relative.x);
        const float start = NormalizeAimAngle(center - half_angle);
        const float end = NormalizeAimAngle(center + half_angle);
        if (start <= end)
        {
            events.push_back({ start, weight });
            events.push_back({ end, -weight });
        } else
        {
            initial_score += weight;
            events.push_back({ end, -weight });
            events.push_back({ start, weight });
        }
    });
    if (targets.empty()) return false;

    std::sort(events.begin(), events.end(), [](const hornet_aim_event& lhs, const hornet_aim_event& rhs) {
        return lhs.angle < rhs.angle;
    });

    float score = initial_score;
    float previous_angle = -game_config::pi;
    size_t event_index = 0;
    while (event_index < events.size())
    {
        const float event_angle = events[event_index].angle;
        if (event_angle - previous_angle > game_config::entity_collision_epsilon)
            aim_candidates.push_back({ (previous_angle + event_angle) * 0.5f, score });

        float delta = 0.f;
        while (event_index < events.size() &&
               std::abs(events[event_index].angle - event_angle) <= game_config::entity_collision_epsilon)
        {
            delta += events[event_index].delta;
            ++event_index;
        }
        score += delta;
        previous_angle = event_angle;
    }
    if (game_config::pi - previous_angle > game_config::entity_collision_epsilon)
        aim_candidates.push_back({ (previous_angle + game_config::pi) * 0.5f, score });
    if (aim_candidates.empty())
        aim_candidates.push_back({ mob->HasFacing() ? mob->FacingAngle() : 0.f, initial_score });

    std::sort(aim_candidates.begin(), aim_candidates.end(), [](const hornet_aim_candidate& lhs,
                                                               const hornet_aim_candidate& rhs) {
        return lhs.upper_score > rhs.upper_score;
    });

    const int candidate_limit = std::max(1, game_config::mob_hornet_aim_candidate_limit);
    float best_score = 0.f;
    int checked = 0;
    for (const hornet_aim_candidate& candidate : aim_candidates)
    {
        if (checked >= candidate_limit || candidate.upper_score <= best_score) break;
        ++checked;
        const sf::Vector2f direction = { std::cos(candidate.angle), std::sin(candidate.angle) };
        const float exact_score = ScoreHornetAimDirection(mob, direction, missile_radius, max_range, targets);
        if (exact_score <= best_score) continue;
        best_score = exact_score;
        result = direction;
    }
    return best_score > game_config::entity_collision_epsilon;
}

void FaceTarget(CMobBase* mob, const CEntity* target)
{
    if (!mob || !target) return;
    if (mob->IsFacingLocked()) return;
    sf::Vector2f delta = target->Pos() - mob->Pos();
    if (LengthSq(delta) <= game_config::entity_collision_epsilon * game_config::entity_collision_epsilon) return;
    mob->FacingAngle() = std::atan2(delta.y, delta.x);
    mob->HasFacing() = true;
}
} // namespace

void CMeleeController::CaptureSnapshot(CSnapshotWriter& writer) const
{
    writer.Field("target_world_id", m_target_world_id);
    writer.Field("target_id", m_target_id);
    writer.UInt64("target_generation", m_target_generation);
    writer.Field("target_pos", m_target_pos);
    writer.Field("change_target_count", m_change_target_count);
    writer.Field("target_los_check_timer", m_target_los_check_timer);
    writer.Field("has_random_target_pos", m_has_random_target_pos);
    writer.Field("random_idle", m_random_idle);
    writer.Field("random_idle_timer", m_random_idle_timer);
    writer.Field("wander_progress_anchor", m_wander_progress_anchor);
    writer.Field("wander_progress_timer", m_wander_progress_timer);
    writer.Field("wander_progress_initialized", m_wander_progress_initialized);
    writer.Field("target_scan_cooldown", m_target_scan_cooldown);
    writer.Field("honey_target_scan_cooldown", m_honey_target_scan_cooldown);
}

bool CMeleeController::RestoreSnapshot(const CSnapshotReader& reader, std::uint32_t version, std::string& error)
{
    if (version > SnapshotVersion())
    {
        error = "Unsupported melee controller snapshot version " + std::to_string(version);
        return false;
    }
    m_p_target = nullptr;
    m_target_world_id = reader.UInt32("target_world_id");
    m_target_id = reader.Int("target_id", -1);
    m_target_generation = reader.UInt64("target_generation");
    m_target_pos = reader.Vector2("target_pos");
    m_change_target_count = reader.Float("change_target_count", game_config::melee_target_time);
    m_target_los_check_timer = reader.Float("target_los_check_timer");
    m_has_random_target_pos = reader.Bool("has_random_target_pos");
    m_random_idle = reader.Bool("random_idle");
    m_random_idle_timer = reader.Float("random_idle_timer");
    m_wander_progress_anchor = reader.Vector2("wander_progress_anchor", m_target_pos);
    m_wander_progress_timer = reader.Float("wander_progress_timer");
    m_wander_progress_initialized = reader.Bool("wander_progress_initialized");
    m_target_scan_cooldown = reader.Int("target_scan_cooldown", -1);
    m_honey_target_scan_cooldown = reader.Int("honey_target_scan_cooldown", -1);
    return true;
}

void CLeafcutterSoldierController::CaptureSnapshot(CSnapshotWriter& writer) const
{
    CMeleeController::CaptureSnapshot(writer);
    writer.Field("leaf_piece_target_world_id", m_leaf_piece_target_world_id);
    writer.Field("leaf_piece_target_id", m_leaf_piece_target_id);
    writer.UInt64("leaf_piece_target_generation", m_leaf_piece_target_generation);
    CaptureCarriedSnapshot(writer);
}

bool CLeafcutterSoldierController::RestoreSnapshot(const CSnapshotReader& reader, std::uint32_t version,
                                                   std::string& error)
{
    if (!CMeleeController::RestoreSnapshot(reader, version, error)) return false;
    m_leaf_piece_target_world_id = reader.UInt32("leaf_piece_target_world_id");
    m_leaf_piece_target_id = reader.Int("leaf_piece_target_id", -1);
    m_leaf_piece_target_generation = reader.UInt64("leaf_piece_target_generation");
    RestoreCarriedSnapshot(reader);
    return true;
}

void CSummonedMeleeController::CaptureSnapshot(CSnapshotWriter& writer) const
{
    CMeleeController::CaptureSnapshot(writer);
    writer.Field("owner_id", m_owner_id);
    writer.UInt64("owner_generation", m_owner_generation);
    writer.Field("persist_after_owner_death", m_persist_after_owner_death);
    writer.Field("owner_lost", m_owner_lost);
}

bool CSummonedMeleeController::RestoreSnapshot(const CSnapshotReader& reader, std::uint32_t version,
                                               std::string& error)
{
    if (!CMeleeController::RestoreSnapshot(reader, version, error)) return false;
    m_owner_id = reader.Int("owner_id", -1);
    m_owner_generation = reader.UInt64("owner_generation");
    m_persist_after_owner_death = reader.Bool("persist_after_owner_death");
    m_owner_lost = reader.Bool("owner_lost");
    return true;
}

void CSpiderController::CaptureSnapshot(CSnapshotWriter& writer) const
{
    CMeleeController::CaptureSnapshot(writer);
    writer.Field("web_timer", m_web_timer);
}

bool CSpiderController::RestoreSnapshot(const CSnapshotReader& reader, std::uint32_t version, std::string& error)
{
    if (!CMeleeController::RestoreSnapshot(reader, version, error)) return false;
    m_web_timer = reader.Float("web_timer");
    return true;
}

void CSpecialHornetController::CaptureSnapshot(CSnapshotWriter& writer) const
{
    CMeleeController::CaptureSnapshot(writer);
    writer.Field("state", static_cast<int>(m_state));
    writer.Field("state_timer", m_state_timer);
    writer.Field("action_timer", m_action_timer);
    writer.Field("fire_timer", m_fire_timer);
    writer.Field("skill2_orbit_center", m_skill2_orbit_center);
    writer.Field("skill2_orbit_radius", m_skill2_orbit_radius);
    writer.Field("skill2_orbit_angle", m_skill2_orbit_angle);
    writer.Field("skill2_orbit_dir", m_skill2_orbit_dir);
    writer.Field("skill2_remaining_angle", m_skill2_remaining_angle);
    writer.Field("skill2_tangent_dir", m_skill2_tangent_dir);
    writer.Field("cycle_phase", m_cycle_phase);
    writer.Field("cycle_attack_count", m_cycle_attack_count);
writer.Field("skill3_target_world_id", m_skill3_target_world_id);
    writer.Field("skill3_target_id", m_skill3_target_id);
    writer.UInt64("skill3_target_generation", m_skill3_target_generation);
    CaptureCarriedSnapshot(writer);
    writer.Field("skill3_launch_pos", m_skill3_launch_pos);
    writer.Field("skill3_launch_direction", m_skill3_launch_direction);
    writer.Field("skill3_captured_prev_skip_tick", m_skill3_captured_prev_skip_tick);
    writer.Field("skill3_has_captured_prev_skip_tick", m_skill3_has_captured_prev_skip_tick);
    writer.Field("skill3_missile_suppressed", m_skill3_missile_suppressed);
}

bool CSpecialHornetController::RestoreSnapshot(const CSnapshotReader& reader, std::uint32_t version,
                                               std::string& error)
{
    if (!CMeleeController::RestoreSnapshot(reader, version, error)) return false;
    const int state = reader.Int("state");
    m_state = state >= static_cast<int>(EState::Idle) && state <= static_cast<int>(EState::Skill3Charge)
                  ? static_cast<EState>(state)
                  : EState::Idle;
    m_state_timer = reader.Float("state_timer");
    m_action_timer = reader.Float("action_timer");
    m_fire_timer = reader.Float("fire_timer");
    m_skill2_orbit_center = reader.Vector2("skill2_orbit_center");
    m_skill2_orbit_radius = reader.Float("skill2_orbit_radius");
    m_skill2_orbit_angle = reader.Float("skill2_orbit_angle");
    m_skill2_orbit_dir = reader.Float("skill2_orbit_dir", 1.f);
    m_skill2_remaining_angle = reader.Float("skill2_remaining_angle");
    m_skill2_tangent_dir = reader.Vector2("skill2_tangent_dir", { 1.f, 0.f });
    m_cycle_phase = reader.Int("cycle_phase");
    m_cycle_attack_count = reader.Int("cycle_attack_count");
m_skill3_target_world_id = reader.UInt32("skill3_target_world_id");
    m_skill3_target_id = reader.Int("skill3_target_id", -1);
    m_skill3_target_generation = reader.UInt64("skill3_target_generation");
    RestoreCarriedSnapshot(reader);
    m_skill3_launch_pos = reader.Vector2("skill3_launch_pos");
    m_skill3_launch_direction = reader.Vector2("skill3_launch_direction", { 1.f, 0.f });
    m_skill3_captured_prev_skip_tick = reader.Bool("skill3_captured_prev_skip_tick");
    m_skill3_has_captured_prev_skip_tick = reader.Bool("skill3_has_captured_prev_skip_tick");
    m_skill3_missile_suppressed = reader.Bool("skill3_missile_suppressed");
    return true;
}

void CBumbleBeeController::CaptureSnapshot(CSnapshotWriter& writer) const
{
    writer.Field("heading", m_heading);
    writer.Field("turn_timer", m_turn_timer);
    writer.Field("wave_timer", m_wave_timer);
    writer.Field("pollen_timer", m_pollen_timer);
    writer.Field("honey_los_check_timer", m_honey_los_check_timer);
    writer.Field("honey_target_scan_cooldown", m_honey_target_scan_cooldown);
    writer.Field("honey_target_world_id", m_honey_target_world_id);
    writer.Field("honey_target_id", m_honey_target_id);
    writer.UInt64("honey_target_generation", m_honey_target_generation);
    writer.Field("initialized", m_initialized);
}

bool CBumbleBeeController::RestoreSnapshot(const CSnapshotReader& reader, std::uint32_t version, std::string& error)
{
    if (version > SnapshotVersion())
    {
        error = "Unsupported bumble bee controller snapshot version " + std::to_string(version);
        return false;
    }
    m_heading = reader.Float("heading");
    m_turn_timer = reader.Float("turn_timer");
    m_wave_timer = reader.Float("wave_timer");
    m_pollen_timer = reader.Float("pollen_timer");
    m_honey_los_check_timer = reader.Float("honey_los_check_timer");
    m_honey_target_scan_cooldown = reader.Int("honey_target_scan_cooldown", -1);
    m_p_honey_target = nullptr;
    m_honey_target_world_id = reader.UInt32("honey_target_world_id");
    m_honey_target_id = reader.Int("honey_target_id", -1);
    m_honey_target_generation = reader.UInt64("honey_target_generation");
    m_initialized = reader.Bool("initialized");
    return true;
}

void CMeleeController::PickRandomTargetPos(CMobBase* mob, const SMobStats& stats)
{
    if (!mob) return;

    float half_range = stats.horizon / game_config::melee_random_wander_divisor;
    PickRandomTargetPosNear(mob, mob->Pos(), half_range);
}

void CMeleeController::PickRandomTargetPosNear(CMobBase* mob, const sf::Vector2f& center, float half_range)
{
    if (!mob) return;
    ResetWanderProgress();
    m_random_idle = false;
    m_random_idle_timer = 0.f;

    if (CheckChance(game_config::melee_random_idle_chance))
    {
        m_target_pos = mob->Pos();
        m_has_random_target_pos = true;
        m_random_idle = true;
        return;
    }

    half_range = std::max(0.f, half_range);
    sf::Vector2f min_pos = center - sf::Vector2f(half_range, half_range);
    CGameWorld* world = mob->GameWorld();
    float collision_radius = std::max(0.f, mob->WallCollisionRadius());
    int attempts = std::max(1, game_config::melee_random_wander_candidate_attempts);
    for (int attempt = 0; attempt < attempts; ++attempt)
    {
        sf::Vector2f candidate =
            min_pos + sf::Vector2f(GetLimitedRng(0.f, half_range * 2.f), GetLimitedRng(0.f, half_range * 2.f));
        if (world && world->SweptCircleBlockedByWall(mob->Pos(), candidate, collision_radius)) continue;

        m_target_pos = candidate;
        m_has_random_target_pos = true;
        return;
    }

    m_target_pos = mob->Pos();
    m_has_random_target_pos = true;
    m_random_idle = true;
}

bool CMeleeController::IsRandomIdleDone(float dt)
{
    if (!m_random_idle) return false;
    m_random_idle_timer += dt;
    return m_random_idle_timer >= game_config::melee_random_idle_time;
}

bool CMeleeController::ShouldSwitchRandomWanderTarget(float dt) const
{
    if (!m_has_random_target_pos || m_random_idle || dt <= 0.f) return false;

    const float target_time =
        std::max(game_config::entity_collision_epsilon,
                 std::max(game_config::server_fixed_dt, game_config::melee_target_time));
    if (m_change_target_count >= target_time) return true;

    const float retarget_chance = game_config::melee_retarget_chance_multiplier *
                                  std::max(0.f, m_change_target_count) * dt / (target_time * target_time);
    return CheckChance(std::clamp(retarget_chance, 0.f, 1.f));
}

void CMeleeController::ResetWanderProgress()
{
    m_wander_progress_anchor = { 0.f, 0.f };
    m_wander_progress_timer = 0.f;
    m_wander_progress_initialized = false;
}

bool CMeleeController::ReachedOrStalledRandomTarget(CMobBase* mob, float dt)
{
    if (!mob || !m_has_random_target_pos)
    {
        ResetWanderProgress();
        return false;
    }
    if (m_random_idle)
    {
        ResetWanderProgress();
        return IsRandomIdleDone(dt);
    }
    if (DistanceSq(mob->Pos(), m_target_pos) <= mob->Radius() * mob->Radius())
    {
        ResetWanderProgress();
        return true;
    }

    float stuck_time = game_config::melee_wander_stuck_time;
    if (stuck_time <= 0.f) return false;

    float progress_distance =
        std::max(game_config::entity_collision_epsilon,
                 mob->WallCollisionRadius() * std::max(0.f, game_config::melee_wander_progress_radius_multiplier));
    if (!m_wander_progress_initialized)
    {
        m_wander_progress_anchor = mob->Pos();
        m_wander_progress_timer = 0.f;
        m_wander_progress_initialized = true;
        return false;
    }
    if (DistanceSq(mob->Pos(), m_wander_progress_anchor) >= progress_distance * progress_distance)
    {
        m_wander_progress_anchor = mob->Pos();
        m_wander_progress_timer = 0.f;
        return false;
    }

    m_wander_progress_timer += std::max(0.f, dt);
    if (m_wander_progress_timer < stuck_time) return false;

    ResetWanderProgress();
    return true;
}

void CMeleeController::SetTarget(CEntity* target)
{
    if (target) ResetWanderProgress();
    m_p_target = target;
    m_target_world_id = target && target->GameWorld() ? target->GameWorld()->GetId() : 0;
    m_target_id = target ? target->Id() : -1;
    m_target_generation = target ? target->Generation() : 0;
}

void CMeleeController::ClearTarget() { SetTarget(nullptr); }

void CMeleeController::LoseTarget(CMobBase* mob)
{
    ClearTarget();
    m_target_pos = mob ? mob->Pos() : sf::Vector2f{};
    m_change_target_count = game_config::melee_target_time;
    m_target_los_check_timer = 0.f;
    m_has_random_target_pos = false;
    m_random_idle = false;
    m_random_idle_timer = 0.f;
    ResetWanderProgress();
}

CEntity* CMeleeController::ResolveTarget(CMobBase* mob)
{
    if (!m_p_target && m_target_id < 0) return nullptr;
    CGameWorld* world = mob ? mob->GameWorld() : nullptr;
    CEntity* resolved = world && world->GetId() == m_target_world_id && m_target_id >= 0
                            ? world->GetEntity(m_target_id, m_target_generation)
                            : nullptr;
    if (!resolved || resolved->GameWorld() != world)
    {
        LoseTarget(mob);
        return nullptr;
    }
    m_p_target = resolved;
    return m_p_target;
}

CEntity* CMeleeController::FindTargetCandidate(CMobBase* mob, float search_range, int ignored_id,
                                                int ignored_owner_id)
{
    return FindClosestMeleeTarget(mob, search_range, ignored_id, ignored_owner_id);
}

bool CMeleeController::IsTargetUnavailable(CMobBase* mob, const CEntity* target, float search_range, float dt,
                                            float& los_check_timer, int ignored_id, int ignored_owner_id)
{
    return IsCurrentMeleeTargetUnavailable(mob, target, search_range, dt, los_check_timer, ignored_id,
                                           ignored_owner_id);
}

bool CMeleeController::ShouldRunTargetScan(CMobBase* mob)
{
    return ShouldRunEntityTargetScan(m_target_scan_cooldown, mob);
}

bool CMeleeController::TryAcquireWanderTarget(CMobBase* mob, float search_range, int ignored_id, int ignored_owner_id)
{
    if (!ShouldRunTargetScan(mob)) return false;

    CEntity* target = FindTargetCandidate(mob, search_range, ignored_id, ignored_owner_id);
    if (!target) return false;

    SetTarget(target);
    target = ResolveTarget(mob);
    if (!target) return false;

    m_target_pos = target->Pos();
    m_change_target_count = 0.f;
    m_target_los_check_timer = 0.f;
    m_has_random_target_pos = true;
    m_random_idle = false;
    m_random_idle_timer = 0.f;
    return true;
}

bool CMeleeController::TryAcquireHoneyTarget(CMobBase* mob, float search_range, int ignored_id, int ignored_owner_id)
{
    if (!ShouldRunEntityTargetScan(m_honey_target_scan_cooldown, mob, game_config::honey_target_scan_interval_ticks))
        return false;

    search_range = std::max(search_range, game_config::default_honey_attract_range);
    CEntity* target = FindClosestHoneyTarget(mob, search_range, ignored_id, ignored_owner_id);
    if (!target) return false;

    SetTarget(target);
    target = ResolveTarget(mob);
    if (!target) return false;

    m_target_pos = target->Pos();
    m_change_target_count = 0.f;
    m_target_los_check_timer = 0.f;
    m_has_random_target_pos = true;
    m_random_idle = false;
    m_random_idle_timer = 0.f;
    return true;
}

bool CMeleeController::TryAcquireBulbTarget(CMobBase* mob, float search_range, int ignored_id, int ignored_owner_id)
{
    if (!mob || !mob->GameWorld() || search_range <= 0.f) return false;
    if (!ShouldRunEntityTargetScan(m_target_scan_cooldown, mob)) return false;

    CEntity* target = FindClosestBulbTarget(mob, search_range, ignored_id, ignored_owner_id);
    if (!target) return false;

    SetTarget(target);
    target = ResolveTarget(mob);
    if (!target) return false;

    m_target_pos = target->Pos();
    m_change_target_count = 0.f;
    m_target_los_check_timer = 0.f;
    m_has_random_target_pos = true;
    m_random_idle = false;
    m_random_idle_timer = 0.f;
    return true;
}

void CMeleeController::OnTick(CMobBase* mob, float dt)
{
    if (!mob || !mob->GameWorld()) return;

    const SMobStats* stats = mob->GetFinalStats();
    if (!stats) return;

    m_change_target_count += dt;
    CEntity* target = ResolveTarget(mob);

    bool target_invalid = target && IsTargetUnavailable(mob, target, stats->horizon, dt, m_target_los_check_timer);
    if (target_invalid)
    {
        LoseTarget(mob);
        target = nullptr;
    }
    if (TryAcquireHoneyTarget(mob, stats->horizon))
    {
        target = ResolveTarget(mob);
    }
    bool reached_random_target = !target && ReachedOrStalledRandomTarget(mob, dt);
    if (!target && m_has_random_target_pos && !reached_random_target && TryAcquireWanderTarget(mob, stats->horizon))
        target = ResolveTarget(mob);

    const bool switch_random_target =
        !target && !reached_random_target && ShouldSwitchRandomWanderTarget(dt);
    bool should_retarget = target_invalid || !m_has_random_target_pos || reached_random_target ||
                           switch_random_target;
    if (should_retarget)
    {
        m_change_target_count = 0.f;
        m_target_los_check_timer = 0.f;
        m_has_random_target_pos = false;
        m_random_idle = false;
        m_random_idle_timer = 0.f;

        if (TryAcquireWanderTarget(mob, stats->horizon)) target = ResolveTarget(mob);
        else PickRandomTargetPos(mob, *stats);
    }

    if ((target = ResolveTarget(mob))) m_target_pos = target->Pos();
    mob->MoveTowards(m_target_pos, dt);
}

CEntity* CLeafcutterSoldierController::ResolveLeafPieceReference(const CMobBase* mob, std::uint32_t world_id,
                                                                 int entity_id, std::uint64_t generation) const
{
    if (!mob || !mob->GameWorld() || mob->IsMarkedForDes() || mob->IsDead() || entity_id < 0 || generation == 0)
        return nullptr;

    CGameWorld* world = mob->GameWorld();
    if (world->GetId() != world_id) return nullptr;

    CEntity* entity = world->GetEntity(entity_id, generation);
    const auto* leaf_piece = dynamic_cast<const CMobBase*>(entity);
    if (!leaf_piece || leaf_piece->GetMobType() != EMobType::LeafPiece || leaf_piece->IsMarkedForDes() ||
        leaf_piece->IsDead())
        return nullptr;
    return entity;
}

CEntity* CLeafcutterSoldierController::ResolveLeafPieceTarget(const CMobBase* mob) const
{
    CEntity* target = ResolveLeafPieceReference(mob, m_leaf_piece_target_world_id, m_leaf_piece_target_id,
                                                 m_leaf_piece_target_generation);
    return IsEligibleLeafPiece(mob, target) ? target : nullptr;
}

CEntity* CLeafcutterSoldierController::FindTargetCandidate(CMobBase* mob, float search_range, int ignored_id,
                                                            int ignored_owner_id)
{
    if (!mob || !mob->GameWorld() || search_range <= 0.f) return nullptr;

    const int carried_id = m_carried_id;
    const std::uint64_t carried_generation = m_carried_generation;
    return FindClosestMeleeTarget(
        mob, search_range, ignored_id, ignored_owner_id,
        [this, mob, ignored_id, ignored_owner_id, carried_id, carried_generation](const CEntity* entity) {
            if (!IsEligibleLeafPiece(mob, entity, false)) return false;
            if (entity->Id() == ignored_id || entity->Id() == ignored_owner_id) return false;
            if (entity->Id() == carried_id && entity->Generation() == carried_generation) return false;
            return !mob->GameWorld()->IsCarriedEntity(*entity);
        });
}

bool CLeafcutterSoldierController::IsTargetUnavailable(CMobBase* mob, const CEntity* target, float search_range,
                                                        float dt, float& los_check_timer, int ignored_id,
                                                        int ignored_owner_id)
{
    const auto* target_mob = dynamic_cast<const CMobBase*>(target);
    if (!target_mob || target_mob->GetMobType() != EMobType::LeafPiece)
        return CMeleeController::IsTargetUnavailable(mob, target, search_range, dt, los_check_timer, ignored_id,
                                                     ignored_owner_id);

    if (!IsEligibleLeafPiece(mob, target)) return true;
    const float retention_range = std::max(0.f, search_range);
    if (TargetEdgeDistanceSq(mob, target) > retention_range * retention_range) return true;

    los_check_timer += dt;
    if (los_check_timer < game_config::melee_target_los_recheck_interval) return false;
    los_check_timer = 0.f;
    return IsMeleeTargetBlockedByWall(mob, target);
}

bool CLeafcutterSoldierController::IsEligibleLeafPiece(const CMobBase* mob, const CEntity* entity,
                                                       bool reject_carried) const
{
    if (!mob || !entity || entity == mob || entity->GameWorld() != mob->GameWorld() || entity->IsMarkedForDes() ||
        entity->IsDead())
        return false;

    const auto* leaf_piece = dynamic_cast<const CMobBase*>(entity);
    if (!leaf_piece || leaf_piece->GetMobType() != EMobType::LeafPiece) return false;
    if (reject_carried && mob->GameWorld()->IsCarriedEntity(*leaf_piece)) return false;

    const int common_level = GetLevel(ERarity::Common);
    const int soldier_level = std::max(common_level, GetLevel(mob->GetRarity()));
    const int minimum_leaf_level = std::max(common_level, soldier_level - 1);
    const int leaf_level = GetLevel(leaf_piece->GetRarity());
    return leaf_level >= minimum_leaf_level && leaf_level <= soldier_level;
}

void CLeafcutterSoldierController::ClearLeafPieceTarget()
{
    m_leaf_piece_target_world_id = 0;
    m_leaf_piece_target_id = -1;
    m_leaf_piece_target_generation = 0;
}

void CLeafcutterSoldierController::SetLeafPieceTarget(CEntity* leaf_piece)
{
    if (!leaf_piece || !leaf_piece->GameWorld())
    {
        ClearLeafPieceTarget();
        return;
    }

    m_leaf_piece_target_world_id = leaf_piece->GameWorld()->GetId();
    m_leaf_piece_target_id = leaf_piece->Id();
    m_leaf_piece_target_generation = leaf_piece->Generation();
}

bool CLeafcutterSoldierController::CanCarry(const CMobBase* mob, const CEntity* carried) const
{
    return IsEligibleLeafPiece(mob, carried, true);
}

float CLeafcutterSoldierController::CarryContactRadius(const CMobBase* mob, const CEntity* carried) const
{
    (void)carried;
    return std::max(0.f, mob->Radius() * game_config::mob_leafcutter_soldier_leaf_piece_attack_radius_multiplier);
}

float CLeafcutterSoldierController::CarryPickupCooldown() const
{
    return std::max(0.f, game_config::mob_leafcutter_soldier_leaf_piece_attack_interval);
}

void CLeafcutterSoldierController::OnCarryChanged(CMobBase* mob, CEntity* carried, bool now_carrying)
{
    (void)mob;
    (void)carried;
    if (!now_carrying) return;
    ClearTarget();
    ClearLeafPieceTarget();
    m_has_random_target_pos = false;
    m_random_idle = false;
    m_random_idle_timer = 0.f;
}

void CLeafcutterSoldierController::OnTick(CMobBase* mob, float dt)
{
    if (!mob || !mob->GameWorld()) return;

    TickCarried(mob, dt);
    CEntity* carried = GetCarried(mob);

    CEntity* target = ResolveTarget(mob);
    CEntity* leaf_piece_target = ResolveLeafPieceTarget(mob);
    if (!carried && !target && leaf_piece_target)
    {
        SetTarget(leaf_piece_target);
        target = ResolveTarget(mob);
    }
    if (carried || !leaf_piece_target) ClearLeafPieceTarget();

    const bool was_carrying = carried != nullptr;
    const float previous_angle = mob->FacingAngle();
    const bool had_facing = mob->HasFacing();
    CMeleeController::OnTick(mob, dt);
    if (was_carrying) ApplyCarryingTurn(mob, previous_angle, had_facing, dt);

    target = ResolveTarget(mob);
    const auto* target_mob = dynamic_cast<const CMobBase*>(target);
    const bool targeting_leaf_piece = target_mob && target_mob->GetMobType() == EMobType::LeafPiece;
    if (targeting_leaf_piece) SetLeafPieceTarget(target);
    else ClearLeafPieceTarget();

    if (targeting_leaf_piece && !GetCarried(mob)) TryPickup(mob, target);
}

// ============ Summoned ============

CEntity* CSummonedMeleeController::GetOwner(CGameWorld* world) const
{
    if (!world || m_owner_id < 0) return nullptr;
    if (m_owner_generation != 0) return world->GetEntity(m_owner_id, m_owner_generation);
    return world->GetEntity(m_owner_id);
}

bool CSummonedMeleeController::IsOwnedBy(const CEntity* entity) const
{
    if (!entity || m_owner_id < 0) return false;
    if (entity->Id() != m_owner_id) return false;
    return m_owner_generation == 0 || entity->Generation() == m_owner_generation;
}

void CSummonedMeleeController::OnTick(CMobBase* mob, float dt)
{
    if (!mob || !mob->GameWorld())
    {
        return;
    }

    if (m_owner_lost)
    {
        CMeleeController::OnTick(mob, dt);
        return;
    }

    CEntity* owner = GetOwner(mob->GameWorld());
    if (!owner || owner->IsMarkedForDes() || owner->IsDead())
    {
        if (m_persist_after_owner_death)
        {
            m_owner_lost = true;
            LoseTarget(mob);
            CMeleeController::OnTick(mob, dt);
            return;
        }
        mob->MarkForDestroy(EEntityRemovalReason::OwnerRemoved);
        return;
    }

    float owner_dist_sq = DistanceSq(mob->Pos(), owner->Pos());
    float range_scale = std::max(1.f, mob->Radius() / std::max(1.f, game_config::mob_summoned_beetle_radius));
    float hard_range = game_config::mob_summoned_beetle_hard_range * range_scale;
    if (owner_dist_sq > hard_range * hard_range)
    {
        mob->MarkForDestroy(EEntityRemovalReason::Despawned);
        return;
    }

    float follow_range = game_config::mob_summoned_beetle_follow_range * range_scale;
    if (owner_dist_sq > follow_range * follow_range)
    {
        SetTarget(owner);
        m_target_pos = owner->Pos();
        m_has_random_target_pos = true;
        m_random_idle = false;
        m_random_idle_timer = 0.f;
        m_change_target_count = 0.f;
        mob->MoveTowards(owner->Pos(), dt);
        return;
    }

    const SMobStats* stats = mob->GetFinalStats();
    if (!stats) return;

    m_change_target_count += dt;
    CEntity* target = ResolveTarget(mob);

    int owner_owner_id = -1;
    if (auto* owner_projectile = dynamic_cast<CProjectile*>(owner))
    {
        CEntity* root_owner = owner_projectile->GetOwner();
        owner_owner_id = root_owner ? root_owner->Id() : -1;
    }

    float search_range = stats->horizon * game_config::mob_summoned_search_range_multiplier;
    bool target_invalid =
        target && IsCurrentMeleeTargetUnavailable(mob, target, search_range, dt, m_target_los_check_timer,
                                                   m_owner_id, owner_owner_id);
    if (target_invalid)
    {
        LoseTarget(mob);
        target = nullptr;
    }
    if (TryAcquireHoneyTarget(mob, search_range, owner->Id(), owner_owner_id))
    {
        target = ResolveTarget(mob);
        target_invalid = false;
    }
    bool reached_random_target = !target && ReachedOrStalledRandomTarget(mob, dt);
    if (!target && m_has_random_target_pos && !reached_random_target &&
        TryAcquireWanderTarget(mob, search_range, owner->Id(), owner_owner_id))
        target = ResolveTarget(mob);

    const bool switch_random_target =
        !target && !reached_random_target && ShouldSwitchRandomWanderTarget(dt);

    if (target_invalid || !m_has_random_target_pos || reached_random_target ||
        switch_random_target)
    {
        m_change_target_count = 0.f;
        m_target_los_check_timer = 0.f;
        m_has_random_target_pos = false;
        m_random_idle = false;
        m_random_idle_timer = 0.f;

        if (TryAcquireWanderTarget(mob, search_range, owner->Id(), owner_owner_id)) target = ResolveTarget(mob);
        else
        {
            float wander_half_range = std::max(mob->Radius() * game_config::mob_summoned_wander_radius_multiplier,
                                               follow_range * game_config::mob_summoned_wander_follow_range_multiplier);
            PickRandomTargetPosNear(mob, owner->Pos(), wander_half_range);
        }
    }

    if ((target = ResolveTarget(mob))) m_target_pos = target->Pos();
    mob->MoveTowards(m_target_pos, dt);
}

// ============ Neutral ============

void CNeutralMeleeController::OnTick(CMobBase* mob, float dt)
{
    if (!mob || !mob->GameWorld()) return;

    const SMobStats* stats = mob->GetFinalStats();
    if (!stats) return;

    const auto set_attack_state = [mob](CEntity* target) {
        auto* attackable = dynamic_cast<IAttackableMob*>(mob);
        if (!attackable) return;
        attackable->SetAttacking(target && !IsValidHoneyTarget(mob, target));
        attackable->SetDefending(false);
    };

    m_change_target_count += dt;
    CEntity* target = ResolveTarget(mob);

    if (target && IsCurrentMeleeTargetUnavailable(mob, target, stats->horizon, dt, m_target_los_check_timer))
    {
        LoseTarget(mob);
        target = nullptr;
        m_target_los_check_timer = 0.f;
        m_has_random_target_pos = false;
        m_random_idle = false;
        m_random_idle_timer = 0.f;
    }
    if (TryAcquireHoneyTarget(mob, stats->horizon))
    {
        target = ResolveTarget(mob);
    }
    if (!target && TryAcquireBulbTarget(mob, stats->horizon))
    {
        target = ResolveTarget(mob);
    }

    if (target)
    {
        set_attack_state(target);
        m_target_pos = target->Pos();
        mob->MoveTowards(m_target_pos, dt);
        return;
    }

    set_attack_state(nullptr);

    bool reached_random_target = ReachedOrStalledRandomTarget(mob, dt);
    const bool switch_random_target =
        !reached_random_target &&
        (m_change_target_count >= game_config::melee_target_time || ShouldSwitchRandomWanderTarget(dt));

    if (!m_has_random_target_pos || reached_random_target || switch_random_target)
    {
        m_change_target_count = 0.f;
        PickRandomTargetPos(mob, *stats);
    }
    mob->MoveTowards(m_target_pos, dt);
}

void CNeutralMeleeController::OnDamaged(CMobBase* mob, CEntity* attacker)
{
    if (!mob || !attacker) return;
    if (IsUnavailableMeleeTarget(mob, attacker)) return;

    SetTarget(attacker);
    m_target_pos = attacker->Pos();
    m_has_random_target_pos = true;
    m_random_idle = false;
    m_random_idle_timer = 0.f;
    m_change_target_count = 0.f;
}

namespace
{
struct overmind_ban_profile
{
    float interval = 0.f;
    float duration = 0.f;
};

overmind_ban_profile OvermindBanProfile(ERarity rarity)
{
    switch (rarity)
    {
    case ERarity::Super:
        return { std::max(0.f, game_config::mob_termite_overmind_ban_interval_super),
                 std::max(0.f, game_config::mob_termite_overmind_ban_duration_super) };
    case ERarity::Eternal:
    case ERarity::Unique:
        return { std::max(0.f, game_config::mob_termite_overmind_ban_interval_eternal),
                 std::max(0.f, game_config::mob_termite_overmind_ban_duration_eternal) };
    case ERarity::Primordial:
        return { std::max(0.f, game_config::mob_termite_overmind_ban_interval_primordial),
                 std::max(0.f, game_config::mob_termite_overmind_ban_duration_primordial) };
    default:
        return {};
    }
}
} // namespace

void CTermiteOvermindController::OnTick(CMobBase* mob, float dt)
{
    CNeutralMeleeController::OnTick(mob, dt);
    if (!mob || !mob->GameWorld() || mob->IsDead() || mob->IsMarkedForDes()) return;

    const overmind_ban_profile profile = OvermindBanProfile(mob->GetRarity());
    if (profile.interval <= game_config::entity_collision_epsilon ||
        profile.duration <= game_config::entity_collision_epsilon)
        return;

    if (!m_ban_skill_initialized)
    {
        m_ban_skill_timer = profile.interval;
        m_ban_skill_initialized = true;
        return;
    }

    m_ban_skill_timer -= std::max(0.f, dt);
    if (m_ban_skill_timer > 0.f) return;

    const float remainder = std::fmod(std::max(0.f, -m_ban_skill_timer), profile.interval);
    m_ban_skill_timer = profile.interval - remainder;
    if (m_ban_skill_timer <= game_config::entity_collision_epsilon) m_ban_skill_timer = profile.interval;
    ApplyBanSkill(mob, profile.duration);
}

void CTermiteOvermindController::ApplyBanSkill(CMobBase* mob, float duration) const
{
    if (!mob || !mob->GameWorld() || duration <= game_config::entity_collision_epsilon) return;

    const float range = std::max(0.f, mob->Radius()) + std::max(0.f, game_config::psionic_connection_range);
    if (range <= game_config::entity_collision_epsilon) return;
    const float range_sq = range * range;

    std::vector<CFlower*> targets;
    mob->GameWorld()->GetSpatialGrid().ForEachInRange(mob->Pos(), range, [&](CEntity* entity) {
        if (!entity || entity == mob || entity->IsMarkedForDes() || entity->IsDead()) return;
        auto* flower = dynamic_cast<CFlower*>(entity);
        if (!flower || !CheckTeam(flower->Team(), mob->Team())) return;
        if (DistanceSq(flower->Pos(), mob->Pos()) > range_sq) return;
        targets.push_back(flower);
    });

    for (CFlower* flower : targets)
    {
        if (!flower || flower->IsMarkedForDes() || flower->IsDead()) continue;

        std::vector<int> eligible_slots;
        const auto& slots = flower->GetSlots();
        eligible_slots.reserve(slots.size());
        for (size_t index = 0; index < slots.size(); ++index)
        {
            const CPetalSlot& slot = slots[index];
            if (!slot.m_available || slot.m_banned) continue;

            bool has_ban_state = false;
            for (const auto& state : flower->GetStates())
            {
                const auto* ban_state = state ? dynamic_cast<const CBanSlotState*>(state.get()) : nullptr;
                if (ban_state && ban_state->GetSlotIndex() == static_cast<int>(index))
                {
                    has_ban_state = true;
                    break;
                }
            }
            if (!has_ban_state) eligible_slots.push_back(static_cast<int>(index));
        }
        if (eligible_slots.empty()) continue;

        std::uniform_int_distribution<size_t> slot_dist(0, eligible_slots.size() - 1);
        const int slot_index = eligible_slots[slot_dist(GetRng())];
        flower->AddState(std::make_unique<CBanSlotState>(flower, duration, slot_index, mob->GetRarity()));
    }
}

void CTermiteOvermindController::CaptureSnapshot(CSnapshotWriter& writer) const
{
    CMeleeController::CaptureSnapshot(writer);
    writer.Field("ban_skill_timer", m_ban_skill_timer);
    writer.Field("ban_skill_initialized", m_ban_skill_initialized);
}

bool CTermiteOvermindController::RestoreSnapshot(const CSnapshotReader& reader, std::uint32_t version,
                                                 std::string& error)
{
    if (!CMeleeController::RestoreSnapshot(reader, version, error)) return false;
    m_ban_skill_timer = reader.Float("ban_skill_timer");
    m_ban_skill_initialized = reader.Bool("ban_skill_initialized");
    return true;
}

// ============ Random Wander ============

void CRandomWanderController::OnTick(CMobBase* mob, float dt)
{
    if (!mob || !mob->GameWorld()) return;

    const SMobStats* stats = mob->GetFinalStats();
    if (!stats) return;

    m_change_target_count += dt;
    CEntity* target = ResolveTarget(mob);

    if (target && IsCurrentMeleeTargetUnavailable(mob, target, stats->horizon, dt, m_target_los_check_timer))
    {
        LoseTarget(mob);
        target = nullptr;
        m_target_los_check_timer = 0.f;
        m_has_random_target_pos = false;
        m_random_idle = false;
        m_random_idle_timer = 0.f;
    }
    if (TryAcquireHoneyTarget(mob, stats->horizon))
    {
        target = ResolveTarget(mob);
    }

    if (target)
    {
        m_target_pos = target->Pos();
        mob->MoveTowards(m_target_pos, dt);
        return;
    }

    bool reached_random_target = ReachedOrStalledRandomTarget(mob, dt);
    const bool switch_random_target =
        !reached_random_target &&
        (m_change_target_count >= game_config::melee_target_time || ShouldSwitchRandomWanderTarget(dt));

    if (!m_has_random_target_pos || reached_random_target || switch_random_target)
    {
        m_change_target_count = 0.f;
        PickRandomTargetPos(mob, *stats);
    }
    mob->MoveTowards(m_target_pos, dt);
}

// ============ Queen Ant ============

void CQueenAntController::OnTick(CMobBase* mob, float dt)
{
    CMeleeController::OnTick(mob, dt);
    CEntity* target = ResolveTarget(mob);
    if (!mob || !target || target->IsMarkedForDes() || target->IsDead()) return;

    const SMobStats* stats = mob->GetFinalStats();
    if (!stats) return;

    float attack_range =
        std::max(mob->Radius() * game_config::mob_hornet_attack_range_radius_multiplier, stats->horizon);
    if (DistanceSq(mob->Pos(), target->Pos()) > attack_range * attack_range) return;

    if (auto* attackable = dynamic_cast<IAttackableMob*>(mob))
    {
        if (attackable->TryAttack(target)) mob->m_vel = { 0.f, 0.f };
    }
}

// ============ Spider ============

void CSpiderController::OnTick(CMobBase* mob, float dt)
{
    CMeleeController::OnTick(mob, dt);
    if (!mob || !mob->GameWorld()) return;
    if (GetLevel(mob->GetRarity()) < GetLevel(ERarity::Legendary)) return;
    CEntity* target = ResolveTarget(mob);
    if (!target || target->IsMarkedForDes() || target->IsDead()) return;

    m_web_timer += dt;
    float interval = std::max(game_config::server_fixed_dt, game_config::mob_spider_web_interval);
    if (m_web_timer < interval) return;
    m_web_timer = std::fmod(m_web_timer, interval);

    auto web = std::make_unique<CSpiderWebZone>(mob->GameWorld(), mob->Pos(), mob->Radius(), mob,
                                                game_config::mob_spider_web_lifetime,
                                                game_config::mob_spider_web_speed_multiplier);
    mob->GameWorld()->InsertEntity(std::move(web));
}

// ============ Hornet Ranged ============

void CHornetRangedController::OnTick(CMobBase* mob, float dt)
{
    if (!mob || !mob->GameWorld()) return;

    const SMobStats* stats = mob->GetFinalStats();
    if (!stats) return;

    m_change_target_count += dt;
    CEntity* target = ResolveTarget(mob);

    bool target_invalid =
        target && IsCurrentMeleeTargetUnavailable(mob, target, stats->horizon, dt, m_target_los_check_timer);
    if (target_invalid)
    {
        LoseTarget(mob);
        target = nullptr;
    }
    if (TryAcquireHoneyTarget(mob, stats->horizon))
    {
        target = ResolveTarget(mob);
        target_invalid = false;
    }
    bool reached_random_target = !target && ReachedOrStalledRandomTarget(mob, dt);
    if (!target && m_has_random_target_pos && !reached_random_target && TryAcquireWanderTarget(mob, stats->horizon))
        target = ResolveTarget(mob);

    const bool switch_random_target =
        !target && !reached_random_target && ShouldSwitchRandomWanderTarget(dt);

    bool should_retarget = target_invalid || !m_has_random_target_pos || reached_random_target ||
                           switch_random_target;
    if (should_retarget)
    {
        m_change_target_count = 0.f;
        m_target_los_check_timer = 0.f;
        m_has_random_target_pos = false;
        m_random_idle = false;
        m_random_idle_timer = 0.f;

        if (TryAcquireWanderTarget(mob, stats->horizon)) target = ResolveTarget(mob);
        else PickRandomTargetPos(mob, *stats);
    }

    if (!(target = ResolveTarget(mob)))
    {
        mob->MoveTowards(m_target_pos, dt);
        return;
    }

    m_target_pos = target->Pos();
    FaceTarget(mob, target);

    sf::Vector2f to_target = target->Pos() - mob->Pos();
    float target_dist = Length(to_target);
    float stop_distance = game_config::mob_hornet_stop_distance + mob->Radius();
    if (target_dist <= stop_distance) mob->MoveTowards(mob->Pos(), dt);
    else mob->MoveTowards(m_target_pos, dt);

    float missile_range = HornetMissileSpeed(mob->GetRarity()) * game_config::default_missile_lifetime;
    if (TargetEdgeDistanceSq(mob, target) <=
        missile_range * missile_range * game_config::mob_hornet_missile_close_range_squared_multiplier)
    {
        if (auto* skill_caster = dynamic_cast<ISkillCasterMob*>(mob)) skill_caster->TryCastSkill(0, target);
    }
}

// ============ Mecha Flower Ranged ============

void CMechaFlowerRangedController::OnTick(CMobBase* mob, float dt)
{
    auto* flower = dynamic_cast<CFlower*>(mob);
    if (!flower || !mob->GameWorld()) return;

    const SFlowerStats* stats = flower->GetFinalStats();
    if (!stats) return;

    m_change_target_count += dt;
    CEntity* target = ResolveTarget(mob);

    bool target_invalid =
        target && IsCurrentMeleeTargetUnavailable(mob, target, stats->horizon, dt, m_target_los_check_timer);
    if (target_invalid)
    {
        LoseTarget(mob);
        target = nullptr;
    }
    if (TryAcquireHoneyTarget(mob, stats->horizon))
    {
        target = ResolveTarget(mob);
        target_invalid = false;
    }

    bool reached_random_target = !target && ReachedOrStalledRandomTarget(mob, dt);
    if (!target && m_has_random_target_pos && !reached_random_target && TryAcquireWanderTarget(mob, stats->horizon))
        target = ResolveTarget(mob);

    const bool switch_random_target =
        !target && !reached_random_target && ShouldSwitchRandomWanderTarget(dt);

    bool should_retarget = target_invalid || !m_has_random_target_pos || reached_random_target ||
                           switch_random_target;
    if (should_retarget)
    {
        m_change_target_count = 0.f;
        m_target_los_check_timer = 0.f;
        m_has_random_target_pos = false;
        m_random_idle = false;
        m_random_idle_timer = 0.f;

        if (TryAcquireWanderTarget(mob, stats->horizon)) target = ResolveTarget(mob);
        else PickRandomTargetPos(mob, *stats);
    }

    target = ResolveTarget(mob);
    if (!target)
    {
        flower->SetAttacking(false);
        flower->SetDefending(false);
        ClearOrbitTarget();
        mob->MoveTowards(m_target_pos, dt);
        return;
    }

    m_target_pos = target->Pos();
    if (IsValidHoneyTarget(mob, target))
    {
        flower->SetAttacking(false);
        flower->SetDefending(false);
        ClearOrbitTarget();
        mob->MoveTowards(m_target_pos, dt);
        return;
    }

    flower->SetAttacking(true);
    flower->SetDefending(false);
    SyncOrbitTarget(target);

    const float stop_distance = mob->Radius() + game_config::default_petal_neutral_reach +
                                std::max(0.f, stats->reach);
    const float target_distance = Distance(mob->Pos(), target->Pos());
    if (!m_orbiting)
    {
        if (target_distance > stop_distance)
        {
            mob->MoveTowards(m_target_pos, dt);
            return;
        }
        m_orbiting = true;
        if (m_orbit_direction == 0.f) m_orbit_direction = CheckChance(0.5) ? 1.f : -1.f;
    } else
    {
        const float exit_distance = stop_distance + std::max(0.f, mob->Radius()) *
                                                        mecha_flower_orbit_exit_hysteresis_radius_multiplier;
        if (target_distance > exit_distance)
        {
            m_orbiting = false;
            mob->MoveTowards(m_target_pos, dt);
            return;
        }
    }

    MoveAroundTarget(mob, target, stop_distance, dt);
}

void CMechaFlowerRangedController::ClearOrbitTarget()
{
    m_orbiting = false;
    m_orbit_direction = 0.f;
    m_orbit_target_world_id = 0;
    m_orbit_target_id = -1;
    m_orbit_target_generation = 0;
}

void CMechaFlowerRangedController::SyncOrbitTarget(const CEntity* target)
{
    const std::uint32_t world_id = target && target->GameWorld() ? target->GameWorld()->GetId() : 0;
    const int target_id = target ? target->Id() : -1;
    const std::uint64_t generation = target ? target->Generation() : 0;
    if (world_id == m_orbit_target_world_id && target_id == m_orbit_target_id &&
        generation == m_orbit_target_generation)
        return;

    m_orbiting = false;
    m_orbit_direction = 0.f;
    m_orbit_target_world_id = world_id;
    m_orbit_target_id = target_id;
    m_orbit_target_generation = generation;
}

void CMechaFlowerRangedController::MoveAroundTarget(CMobBase* mob, const CEntity* target, float stop_distance,
                                                    float dt) const
{
    if (!mob || !target) return;

    sf::Vector2f outward = mob->Pos() - target->Pos();
    const float distance = Length(outward);
    if (distance > game_config::entity_collision_epsilon) outward /= distance;
    else outward = { 1.f, 0.f };

    const sf::Vector2f tangent = { -outward.y * m_orbit_direction, outward.x * m_orbit_direction };
    const float safe_stop_distance = std::max(stop_distance, game_config::entity_collision_epsilon);
    const float radial_error = std::clamp((distance - stop_distance) / safe_stop_distance, -1.f, 1.f);
    sf::Vector2f move_direction = tangent - outward * radial_error;
    const float move_length = Length(move_direction);
    if (move_length <= game_config::entity_collision_epsilon) return;
    move_direction /= move_length;

    const float lookahead = std::max(stop_distance, std::max(0.f, mob->Radius()) * 2.f);
    mob->MoveTowards(mob->Pos() + move_direction * lookahead, dt);
}

void CMechaFlowerRangedController::CaptureSnapshot(CSnapshotWriter& writer) const
{
    CMeleeController::CaptureSnapshot(writer);
    writer.Field("orbiting", m_orbiting);
    writer.Field("orbit_direction", m_orbit_direction);
    writer.Field("orbit_target_world_id", m_orbit_target_world_id);
    writer.Field("orbit_target_id", m_orbit_target_id);
    writer.UInt64("orbit_target_generation", m_orbit_target_generation);
}

bool CMechaFlowerRangedController::RestoreSnapshot(const CSnapshotReader& reader, std::uint32_t version,
                                                   std::string& error)
{
    if (!CMeleeController::RestoreSnapshot(reader, version, error)) return false;

    m_orbiting = reader.Bool("orbiting");
    const float direction = reader.Float("orbit_direction");
    m_orbit_direction = std::isfinite(direction) && direction != 0.f ? (direction > 0.f ? 1.f : -1.f) : 0.f;
    m_orbit_target_world_id = reader.UInt32("orbit_target_world_id");
    m_orbit_target_id = reader.Int("orbit_target_id", -1);
    m_orbit_target_generation = reader.UInt64("orbit_target_generation");
    if (m_orbit_target_id < 0 || m_orbit_target_generation == 0)
        ClearOrbitTarget();
    else if (m_orbit_direction == 0.f)
        m_orbiting = false;
    return true;
}

// ============ Special Hornet ============

void CSpecialHornetController::OnTick(CMobBase* mob, float dt)
{
    if (!mob || !mob->GameWorld()) return;

    if (auto* skill_caster = dynamic_cast<ISkillCasterMob*>(mob))
    {
        if (skill_caster->IsSkillBusy() && m_state == EState::Idle)
        {
            TickMovementAndTarget(mob, dt);
            return;
        }
    }

    switch (m_state)
    {
    case EState::Skill1Windup:
        m_state_timer = std::max(0.f, m_state_timer - dt);
        mob->m_vel *= game_config::mob_stop_damping;
        if (m_state_timer <= 0.f)
        {
            PerformSkill1(mob);
            FinishCurrentAction(mob);
        }
        return;
    case EState::Skill2Windup:
        m_state_timer = std::max(0.f, m_state_timer - dt);
        mob->m_vel *= game_config::mob_stop_damping;
        if (m_state_timer <= 0.f)
        {
            const SMobStats* stats = mob->GetFinalStats();
            float speed =
                stats ? stats->max_velocity * game_config::mob_hornet_skill2_move_speed_multiplier
                      : game_config::mob_hornet_max_velocity * game_config::mob_hornet_skill2_move_speed_multiplier;
            float base_angle = mob->HasFacing() ? mob->FacingAngle() : 0.f;
            sf::Vector2f forward = { std::cos(base_angle), std::sin(base_angle) };
            if (LengthSq(forward) <= game_config::entity_collision_epsilon * game_config::entity_collision_epsilon)
                forward = { 1.f, 0.f };
            sf::Vector2f left = { -forward.y, forward.x };
            float side = CheckChance(0.5) ? -1.f : 1.f;
            m_skill2_orbit_radius = std::max(mob->Radius() * game_config::mob_hornet_skill2_orbit_radius_multiplier,
                                             game_config::entity_collision_epsilon);
            m_skill2_orbit_center = mob->Pos() + left * (side * m_skill2_orbit_radius);
            m_skill2_orbit_angle =
                std::atan2(mob->Pos().y - m_skill2_orbit_center.y, mob->Pos().x - m_skill2_orbit_center.x);
            m_skill2_orbit_dir = side;
            m_skill2_remaining_angle = game_config::pi * game_config::mob_hornet_skill2_arc_pi_multiplier;
            m_skill2_tangent_dir = { -std::sin(m_skill2_orbit_angle) * m_skill2_orbit_dir,
                                     std::cos(m_skill2_orbit_angle) * m_skill2_orbit_dir };
            mob->m_vel = { 0.f, 0.f };
            mob->FacingAngle() = std::atan2(m_skill2_tangent_dir.y, m_skill2_tangent_dir.x);
            mob->HasFacing() = true;
            m_state = EState::Skill2Dash;
            float angular_speed = HornetSkill2AngularSpeed(speed, m_skill2_orbit_radius);
            m_state_timer = m_skill2_remaining_angle / std::max(game_config::entity_collision_epsilon, angular_speed);
            m_action_timer = m_state_timer;
            m_fire_timer = 0.f;
        }
        return;
    case EState::Skill2Dash: {
        const SMobStats* stats = mob->GetFinalStats();
        float speed = stats
                          ? stats->max_velocity * game_config::mob_hornet_skill2_move_speed_multiplier
                          : game_config::mob_hornet_max_velocity * game_config::mob_hornet_skill2_move_speed_multiplier;
        float angular_speed = HornetSkill2AngularSpeed(speed, m_skill2_orbit_radius);
        float step = std::min(m_skill2_remaining_angle, angular_speed * dt);
        sf::Vector2f current_radial = { std::cos(m_skill2_orbit_angle), std::sin(m_skill2_orbit_angle) };
        sf::Vector2f expected_pos = m_skill2_orbit_center + current_radial * m_skill2_orbit_radius;
        sf::Vector2f drift = mob->Pos() - expected_pos;
        if (LengthSq(drift) > game_config::entity_collision_epsilon * game_config::entity_collision_epsilon)
            m_skill2_orbit_center += drift;

        sf::Vector2f origin = mob->Pos();
        m_skill2_orbit_angle += m_skill2_orbit_dir * step;
        m_skill2_remaining_angle = std::max(0.f, m_skill2_remaining_angle - step);

        sf::Vector2f radial = { std::cos(m_skill2_orbit_angle), std::sin(m_skill2_orbit_angle) };
        m_skill2_tangent_dir = { -radial.y * m_skill2_orbit_dir, radial.x * m_skill2_orbit_dir };
        sf::Vector2f target_pos = m_skill2_orbit_center + radial * m_skill2_orbit_radius;
        float safe_dt = std::max(game_config::entity_collision_epsilon, dt);
        mob->m_vel = (target_pos - mob->Pos()) / safe_dt;
        mob->FacingAngle() = std::atan2(m_skill2_tangent_dir.y, m_skill2_tangent_dir.x);
        mob->HasFacing() = true;

        m_fire_timer -= dt;
        while (m_fire_timer <= 0.f && m_skill2_remaining_angle > 0.f)
        {
            FireSkill2Missile(mob, origin);
            m_fire_timer += std::max(game_config::server_fixed_dt, game_config::mob_hornet_skill2_missile_interval);
        }

        if (m_skill2_remaining_angle <= 0.f)
        {
            m_state = EState::Skill2Pause;
            m_state_timer = std::max(0.f, game_config::mob_hornet_skill2_pause_time);
        } else
        {
            m_state_timer = std::max(0.f, m_state_timer - dt);
        }
    }
        return;
    case EState::Skill2Pause:
        m_state_timer = std::max(0.f, m_state_timer - dt);
        mob->m_vel = { 0.f, 0.f };
        if (m_state_timer <= 0.f) FinishCurrentAction(mob);
        return;
    case EState::Skill3Charge: {
        CGameWorld* world = mob->GameWorld();
CEntity* target = world && world->GetId() == m_skill3_target_world_id && m_skill3_target_id >= 0
                              ? world->GetEntity(m_skill3_target_id, m_skill3_target_generation)
                              : nullptr;
        auto* captured = dynamic_cast<CMobBase*>(GetCarried(mob));
        if (!target || target->IsMarkedForDes() || target->IsDead())
        {
            LoseTarget(mob);
            FinishCurrentAction(mob);
            return;
        }
        if (!captured || captured->IsMarkedForDes() || captured->IsDead())
        {
            FinishCurrentAction(mob);
            return;
        }
        const SMobStats* mob_stats = mob->GetFinalStats();
        if (!mob_stats ||
            IsCurrentMeleeTargetUnavailable(mob, target, mob_stats->horizon, dt, m_target_los_check_timer))
        {
            LoseTarget(mob);
            FinishCurrentAction(mob);
            return;
        }

        sf::Vector2f mount_dir = { std::cos(mob->FacingAngle()), std::sin(mob->FacingAngle()) };
        if (LengthSq(mount_dir) <= game_config::entity_collision_epsilon * game_config::entity_collision_epsilon)
            mount_dir = { -m_skill3_launch_direction.x, -m_skill3_launch_direction.y };
        else
            mount_dir = NormalizeOrZero(mount_dir);
        if (LengthSq(mount_dir) <= game_config::entity_collision_epsilon * game_config::entity_collision_epsilon)
            mount_dir = { 1.f, 0.f };

        // The caster faces away from the attack target, so the captured mob is held on its back.
        const sf::Vector2f back_dir = -mount_dir;
        m_skill3_launch_pos = mob->Pos() + back_dir * (mob->Radius() * game_config::mob_hornet_missile_attach_offset);
        captured->Pos() = m_skill3_launch_pos;
        captured->PrevPos() = m_skill3_launch_pos;
        captured->m_vel = { 0.f, 0.f };
        captured->FacingAngle() = std::atan2(mount_dir.y, mount_dir.x);
        captured->HasFacing() = true;

        m_state_timer = std::max(0.f, m_state_timer - dt);
        if (m_state_timer <= 0.f)
        {
            sf::Vector2f direction = NormalizeOrZero(m_skill3_launch_direction);
            if (LengthSq(direction) <= game_config::entity_collision_epsilon * game_config::entity_collision_epsilon)
                direction = back_dir;

            const SMobStats* stats = captured->GetFinalStats();
            float launch_speed = (stats ? stats->max_velocity : game_config::mob_hornet_max_velocity) *
                                 game_config::mob_hornet_skill3_launch_speed_multiplier;
            captured->m_vel = direction * launch_speed;
            captured->FacingAngle() = std::atan2(direction.y, direction.x);
            captured->HasFacing() = true;
            FinishCurrentAction(mob);
        }
    }
        return;
    case EState::Idle:
    default:
        break;
    }

    TickMovementAndTarget(mob, dt);

    CEntity* target = ResolveTarget(mob);
    if (!target || target->IsMarkedForDes() || target->IsDead()) return;

    float missile_range = HornetMissileSpeed(mob->GetRarity()) * game_config::default_missile_lifetime;
    if (TargetEdgeDistanceSq(mob, target) >
        missile_range * missile_range * game_config::mob_hornet_missile_close_range_squared_multiplier)
        return;

    auto* skill_caster = dynamic_cast<ISkillCasterMob*>(mob);
    if (!skill_caster) return;
    if (!skill_caster->CanCastSkill(0)) return;

    const int phase_skill = m_cycle_phase == 0 ? 1 : 2;
    bool phase_skill_attempted = false;
    if (m_cycle_attack_count >= std::max(1, game_config::mob_hornet_skill_cycle_attacks))
    {
        phase_skill_attempted = true;
        bool started = phase_skill == 1 ? TryStartSkill1(mob) : TryStartSkill2(mob);
        if (started)
        {
            m_cycle_attack_count = 0;
            m_cycle_phase = 1 - m_cycle_phase;
            return;
        }
    }

    if (!phase_skill_attempted && CheckChance(game_config::mob_hornet_skill3_cast_chance) && TryStartSkill3(mob))
    {
        ++m_cycle_attack_count;
        return;
    }

    bool fired = false;
    if (IsAtLeastRarity(mob->GetRarity(), ERarity::Eternal))
    {
        auto* hornet = dynamic_cast<IHornetMob*>(mob);
        if (hornet)
        {
            sf::Vector2f aim_direction = { 0.f, 0.f };
            const float missile_speed = HornetMissileSpeed(mob->GetRarity());
            const float missile_radius =
                game_config::mob_hornet_missile_radius *
                (mob->Radius() / std::max(game_config::entity_collision_epsilon, game_config::mob_hornet_radius));
            if (FindBestHornetAimDirection(mob, missile_radius, missile_speed, game_config::default_missile_lifetime,
                                            aim_direction))
                fired = hornet->TryCastMissileInDirection(aim_direction);
        }
    }
    if (!fired) fired = skill_caster->TryCastSkill(0, target);
    if (fired)
    {
        if (phase_skill_attempted)
        {
            m_cycle_attack_count = 0;
            m_cycle_phase = 1 - m_cycle_phase;
        } else
        {
            ++m_cycle_attack_count;
        }
    }
}

void CSpecialHornetController::TickMovementAndTarget(CMobBase* mob, float dt)
{
    if (!mob || !mob->GameWorld()) return;

    const SMobStats* stats = mob->GetFinalStats();
    if (!stats) return;

    m_change_target_count += dt;
    CEntity* target = ResolveTarget(mob);

    bool target_invalid =
        target && IsCurrentMeleeTargetUnavailable(mob, target, stats->horizon, dt, m_target_los_check_timer);
    if (target_invalid)
    {
        LoseTarget(mob);
        target = nullptr;
    }
    if (TryAcquireHoneyTarget(mob, stats->horizon))
    {
        target = ResolveTarget(mob);
        target_invalid = false;
    }
    bool reached_random_target = !target && ReachedOrStalledRandomTarget(mob, dt);
    if (!target && m_has_random_target_pos && !reached_random_target && TryAcquireWanderTarget(mob, stats->horizon))
        target = ResolveTarget(mob);

    const bool switch_random_target =
        !target && !reached_random_target && ShouldSwitchRandomWanderTarget(dt);

    bool should_retarget = target_invalid || !m_has_random_target_pos || reached_random_target ||
                           switch_random_target;
    if (should_retarget)
    {
        m_change_target_count = 0.f;
        m_target_los_check_timer = 0.f;
        m_has_random_target_pos = false;
        m_random_idle = false;
        m_random_idle_timer = 0.f;

        if (TryAcquireWanderTarget(mob, stats->horizon)) target = ResolveTarget(mob);
        else PickRandomTargetPos(mob, *stats);
    }

    if (!(target = ResolveTarget(mob)))
    {
        mob->MoveTowards(m_target_pos, dt);
        return;
    }

    m_target_pos = target->Pos();
    FaceTarget(mob, target);

    sf::Vector2f to_target = target->Pos() - mob->Pos();
    float target_dist = Length(to_target);
    float stop_distance = game_config::mob_hornet_stop_distance + mob->Radius();
    if (target_dist <= stop_distance) mob->MoveTowards(mob->Pos(), dt);
    else mob->MoveTowards(m_target_pos, dt);
}

bool CSpecialHornetController::TryStartSkill1(CMobBase* mob)
{
    if (!mob || !mob->GameWorld()) return false;
    if (CountNearbyPlayers(mob, game_config::mob_hornet_skill1_view_range) <=
        game_config::mob_hornet_skill1_min_nearby_players)
        return false;

    m_state = EState::Skill1Windup;
    m_state_timer = game_config::mob_hornet_skill_windup_time;
    if (auto* skill_caster = dynamic_cast<ISkillCasterMob*>(mob))
    {
        if (skill_caster->TryCastSkill(1, nullptr)) return true;
    }
    FinishCurrentAction(mob);
    return false;
}

void CSpecialHornetController::PerformSkill1(CMobBase* mob)
{
    if (!mob || !mob->GameWorld()) return;

    int nearby = CountNearbyPlayers(mob, game_config::mob_hornet_skill1_view_range);
    int summon_count =
        std::max(1, static_cast<int>(std::floor(static_cast<float>(nearby) /
                                                    std::max(game_config::entity_collision_epsilon,
                                                             game_config::mob_hornet_skill1_players_per_summon) +
                                                game_config::mob_hornet_skill1_summon_rounding_offset)));
    int summon_cap = game_config::mob_hornet_skill1_summon_cap_super;
    if (mob->GetRarity() == ERarity::Primordial) summon_cap = game_config::mob_hornet_skill1_summon_cap_primordial;
    else if (mob->GetRarity() == ERarity::Eternal || mob->GetRarity() == ERarity::Unique)
        summon_cap = game_config::mob_hornet_skill1_summon_cap_eternal;
    summon_count = std::min(summon_count, std::max(0, summon_cap));
    int et_limit = mob->GetRarity() == ERarity::Primordial
                       ? std::max(0, game_config::mob_hornet_skill1_et_cap -
                                         CountExistingHornets(mob->GameWorld(), ERarity::Eternal))
                       : 0;
    int s_cap = mob->GetRarity() == ERarity::Primordial ? game_config::mob_hornet_skill1_super_cap : 1;
    int s_limit = std::max(0, s_cap - CountExistingHornets(mob->GameWorld(), ERarity::Super));

    for (int i = 0; i < summon_count; ++i)
    {
        ERarity rarity = PickSkill1SummonRarity(mob->GetRarity(), et_limit, s_limit);
        for (int attempt = 0; attempt < std::max(1, game_config::mob_hornet_skill1_spawn_position_attempts); ++attempt)
        {
            float angle = GetLimitedRng(-game_config::pi, game_config::pi);
            float distance = GetLimitedRng(
                mob->Radius() * game_config::mob_hornet_skill1_spawn_distance_min_radius_multiplier,
                std::max(mob->Radius() * game_config::mob_hornet_skill1_spawn_distance_safe_radius_multiplier,
                         mob->Radius() * game_config::mob_hornet_skill1_spawn_distance_max_radius_multiplier));
            sf::Vector2f pos = mob->Pos() + sf::Vector2f(std::cos(angle), std::sin(angle)) * distance;
            auto hornet = CreateMob(EMobType::Hornet, mob->GameWorld(), pos, rarity);
            if (!hornet) continue;
            hornet->Team() = mob->Team();
            if (mob->GameWorld()->CircleBlockedByWall(pos, hornet->WallCollisionRadius())) continue;
            mob->GameWorld()->InsertEntity(std::move(hornet));
            break;
        }
    }
}

bool CSpecialHornetController::TryStartSkill2(CMobBase* mob)
{
    if (!mob || !mob->GameWorld()) return false;

    m_state = EState::Skill2Windup;
    m_state_timer = game_config::mob_hornet_skill_windup_time;
    if (auto* skill_caster = dynamic_cast<ISkillCasterMob*>(mob))
    {
        if (skill_caster->TryCastSkill(2, nullptr)) return true;
    }
    FinishCurrentAction(mob);
    return false;
}

bool CSpecialHornetController::TryStartSkill3(CMobBase* mob)
{
    if (!mob || !mob->GameWorld()) return false;
    if (!IsAtLeastRarity(mob->GetRarity(), ERarity::Eternal)) return false;

    CEntity* target = ResolveTarget(mob);
    if (!target || target->IsMarkedForDes() || target->IsDead()) return false;

CEntity* captured =
        FindHighestHornetInRange(mob, mob->Radius() * game_config::mob_hornet_skill3_grab_range_radius_multiplier);
    if (!captured) return false;

    auto* hornet = dynamic_cast<IHornetMob*>(mob);
    if (!hornet) return false;
    if (!TryPickup(mob, captured)) return false;

    sf::Vector2f launch_direction = NormalizeOrZero(target->Pos() - mob->Pos());
    if (LengthSq(launch_direction) <= game_config::entity_collision_epsilon * game_config::entity_collision_epsilon)
        launch_direction = { 1.f, 0.f };
    m_skill3_launch_direction = launch_direction;
    mob->FacingAngle() = std::atan2(-launch_direction.y, -launch_direction.x);
    mob->HasFacing() = true;

    m_skill3_target_world_id = target->GameWorld()->GetId();
    m_skill3_target_id = target->Id();
    m_skill3_target_generation = target->Generation();
    m_state = EState::Skill3Charge;
    m_state_timer = game_config::mob_hornet_skill3_charge_time;
    if (auto* skill_caster = dynamic_cast<ISkillCasterMob*>(mob))
    {
        if (skill_caster->TryCastSkill(3, target)) return true;
    }
    FinishCurrentAction(mob);
    return false;
}

bool CSpecialHornetController::FireSkill2Missile(CMobBase* mob, const sf::Vector2f& origin)
{
    if (!mob || !mob->GameWorld()) return false;

    ERarity missile_rarity = PrevHornetRarity(mob->GetRarity());
    float radius = HornetMissileRadius(mob, missile_rarity);
    float damage = HornetMissileDamage(missile_rarity);
    float health = HornetMissileHealth(missile_rarity);
    sf::Vector2f direction = m_skill2_tangent_dir;
    if (LengthSq(direction) <= game_config::entity_collision_epsilon * game_config::entity_collision_epsilon)
        direction = { 1.f, 0.f };

    sf::Vector2f rear_direction = -direction;
    sf::Vector2f pos = origin + rear_direction * (mob->Radius() * game_config::mob_hornet_rear_spawn_radius_multiplier);
    auto missile =
        std::make_unique<CMissile>(mob->GameWorld(), pos, radius, rear_direction, HornetMissileSpeed(missile_rarity),
                                   damage, health, game_config::mob_hornet_skill2_missile_lifetime, mob);
    missile->Team() = mob->Team();
    missile->Mass() = game_config::MobProjectileMassForLevel(GetLevel(missile_rarity));
    return mob->GameWorld()->InsertEntity(std::move(missile)) != nullptr;
}

void CSpecialHornetController::FinishCurrentAction(CMobBase* mob)
{
    DropCarried(mob);
    m_state = EState::Idle;
    m_state_timer = 0.f;
    m_action_timer = 0.f;
    m_fire_timer = 0.f;
    m_skill2_orbit_center = { 0.f, 0.f };
    m_skill2_orbit_radius = 0.f;
    m_skill2_orbit_angle = 0.f;
    m_skill2_orbit_dir = 1.f;
    m_skill2_remaining_angle = 0.f;
    m_skill2_tangent_dir = { 1.f, 0.f };
    m_skill3_target_world_id = 0;
    m_skill3_target_id = -1;
    m_skill3_target_generation = 0;
    m_skill3_captured_prev_skip_tick = false;
    m_skill3_has_captured_prev_skip_tick = false;
    m_skill3_launch_direction = { 1.f, 0.f };
    if (mob) mob->m_vel *= game_config::mob_stop_damping;
}

bool CSpecialHornetController::CanCarry(const CMobBase* mob, const CEntity* carried) const
{
    if (!mob || !carried || carried == mob || carried->GameWorld() != mob->GameWorld() || carried->IsMarkedForDes() ||
        carried->IsDead())
        return false;

    const auto* hornet = dynamic_cast<const CMobBase*>(carried);
    if (!hornet || hornet->GetMobType() != EMobType::Hornet) return false;
    const int caster_rank = GetRarityValueRank(mob->GetRarity());
    const int carried_rank = GetRarityValueRank(hornet->GetRarity());
    if (carried_rank == caster_rank) return false;
    return carried_rank >= GetRarityValueRank(ERarity::Ultra);
}

float CSpecialHornetController::CarryContactRadius(const CMobBase* mob, const CEntity* carried) const
{
    (void)carried;
    return std::max(0.f, mob->Radius()) * game_config::mob_hornet_skill3_grab_range_radius_multiplier;
}

void CSpecialHornetController::OnCarryChanged(CMobBase* mob, CEntity* carried, bool now_carrying)
{
    if (now_carrying)
    {
        if (auto* hornet_mob = dynamic_cast<IHornetMob*>(mob))
            hornet_mob->SetMissileGenerationSuppressed(true, true);
        m_skill3_missile_suppressed = true;
        m_skill3_captured_prev_skip_tick = carried->SkipWorldTick();
        m_skill3_has_captured_prev_skip_tick = true;
        carried->SkipWorldTick() = true;
        return;
    }

    if (carried && m_skill3_has_captured_prev_skip_tick)
        carried->SkipWorldTick() = m_skill3_captured_prev_skip_tick;
    m_skill3_has_captured_prev_skip_tick = false;
    if (m_skill3_missile_suppressed)
    {
        if (auto* hornet_mob = dynamic_cast<IHornetMob*>(mob))
            hornet_mob->SetMissileGenerationSuppressed(false, false);
        m_skill3_missile_suppressed = false;
    }
}

void CSpecialHornetController::ResolveCarriedConstraint(CMobBase* mob)
{
    (void)mob;
}

CEntity* CSpecialHornetController::FindHighestHornetInRange(CMobBase* mob, float range)
{
    if (!mob || !mob->GameWorld() || range <= 0.f) return nullptr;

    CEntity* best = nullptr;
    int best_rank = 0;
    float best_dist_sq = std::numeric_limits<float>::max();
    float range_sq = range * range;
    int caster_rank = GetRarityValueRank(mob->GetRarity());
    mob->GameWorld()->GetSpatialGrid().ForEachInRange(mob->Pos(), range, [&](CEntity* candidate) {
        auto* hornet = dynamic_cast<CMobBase*>(candidate);
        if (!hornet || hornet == mob || hornet->GetMobType() != EMobType::Hornet) return;
        if (hornet->IsMarkedForDes() || hornet->IsDead()) return;
        int rank = GetRarityValueRank(hornet->GetRarity());
        if (rank == caster_rank) return;
        if (rank < GetRarityValueRank(ERarity::Ultra)) return;
        float dist_sq = DistanceSq(mob->Pos(), hornet->Pos());
        if (dist_sq > range_sq) return;
        if (rank > best_rank || (rank == best_rank && dist_sq < best_dist_sq))
        {
            best = hornet;
            best_rank = rank;
            best_dist_sq = dist_sq;
        }
    });
    return best;
}

int CSpecialHornetController::CountNearbyPlayers(CMobBase* mob, float range) const
{
    if (!mob || !mob->GameWorld() || range <= 0.f) return 0;

    int count = 0;
    float range_sq = range * range;
    mob->GameWorld()->GetSpatialGrid().ForEachInRange(mob->Pos(), range, [&](CEntity* candidate) {
        const auto* player = dynamic_cast<const CPlayerFlower*>(candidate);
        if (!player || player->IsMarkedForDes() || player->IsDead()) return;
        if (DistanceSq(mob->Pos(), player->Pos()) <= range_sq) ++count;
    });
    return count;
}

int CSpecialHornetController::CountExistingHornets(CGameWorld* world, ERarity rarity) const
{
    if (!world) return 0;

    int count = 0;
    world->ForEachEntity([&](CEntity* entity) {
        const auto* hornet = dynamic_cast<const CMobBase*>(entity);
        if (!hornet || hornet->GetMobType() != EMobType::Hornet) return;
        if (hornet->IsMarkedForDes() || hornet->IsDead()) return;
        if (hornet->GetRarity() == rarity) ++count;
    });
    return count;
}

ERarity CSpecialHornetController::PrevHornetRarity(ERarity rarity)
{
    if (rarity == ERarity::Primordial) return ERarity::Eternal;
    if (rarity == ERarity::Unique || rarity == ERarity::Eternal) return ERarity::Super;
    if (rarity == ERarity::Exotic) return ERarity::Common;
    if (rarity == ERarity::Super) return ERarity::Ultra;
    if (rarity == ERarity::Ultra) return ERarity::Mythic;
    if (rarity == ERarity::Mythic) return ERarity::Legendary;
    if (rarity == ERarity::Legendary) return ERarity::Epic;
    if (rarity == ERarity::Epic) return ERarity::Rare;
    if (rarity == ERarity::Rare) return ERarity::Unusual;
    return ERarity::Common;
}

ERarity CSpecialHornetController::PickSkill1SummonRarity(ERarity caster_rarity, int& et_limit, int& s_limit)
{
    ERarity rarity = ERarity::Ultra;
    if (caster_rarity == ERarity::Primordial)
    {
        if (CheckChance(game_config::mob_hornet_skill1_eternal_chance)) rarity = ERarity::Eternal;
        else if (CheckChance(game_config::mob_hornet_skill1_super_chance_after_eternal)) rarity = ERarity::Super;
    } else if (caster_rarity == ERarity::Eternal || caster_rarity == ERarity::Unique)
    {
        if (CheckChance(game_config::mob_hornet_skill1_super_chance)) rarity = ERarity::Super;
    }

    if (rarity == ERarity::Eternal && et_limit <= 0) rarity = ERarity::Super;
    if (rarity == ERarity::Super && s_limit <= 0) rarity = ERarity::Ultra;
    if (rarity == ERarity::Eternal) --et_limit;
    if (rarity == ERarity::Super) --s_limit;
    return rarity;
}

float CSpecialHornetController::HornetMissileRadius(CMobBase* mob, ERarity)
{
    if (!mob) return game_config::mob_hornet_missile_radius;
    int level = std::max(1, GetLevel(mob->GetRarity()) - 1);
    float radius_scale = game_config::MobRadiusScaleForLevel(level);
    return game_config::mob_hornet_missile_radius * radius_scale;
}

float CSpecialHornetController::HornetMissileDamage(ERarity rarity)
{
    return game_config::mob_hornet_missile_base_damage *
           std::pow(game_config::mob_damage_scale_base, static_cast<float>(GetLevel(rarity) - 1));
}

float CSpecialHornetController::HornetMissileHealth(ERarity rarity)
{
    return game_config::mob_hornet_missile_base_health *
           game_config::MobHealthScaleForLevel(GetLevel(rarity));
}

// ============ Bumble Bee ============

void CBumbleBeeController::PickTurnTimer()
{
    float min_time = std::max(0.f, game_config::mob_bumblebee_turn_interval_min);
    float max_time = std::max(min_time, game_config::mob_bumblebee_turn_interval_max);
    m_turn_timer = GetLimitedRng(min_time, max_time);
}

void CBumbleBeeController::SpawnPollen(CMobBase* mob) const
{
    if (!mob || !mob->GameWorld()) return;

    int level = std::max(1, GetLevel(mob->GetRarity()));
    float damage = game_config::mob_bumblebee_pollen_base_damage *
                   std::pow(game_config::mob_damage_scale_base, static_cast<float>(level - 1));
    float health = game_config::mob_bumblebee_pollen_base_health *
                   game_config::MobHealthScaleForLevel(level);
    float mass = game_config::MobProjectileMassForLevel(level);
    float radius = std::max(1.f, mob->Radius() * game_config::mob_bumblebee_pollen_radius_multiplier);

    auto pollen = std::make_unique<CPollenProjectile>(mob->GameWorld(), mob->Pos(), radius, damage, health,
                                                      game_config::mob_bumblebee_pollen_lifetime, mass, mob);
    pollen->Team() = mob->Team();
    mob->GameWorld()->InsertEntity(std::move(pollen));
}

void CBumbleBeeController::SetHoneyTarget(CEntity* target)
{
    m_p_honey_target = target;
    m_honey_target_world_id = target && target->GameWorld() ? target->GameWorld()->GetId() : 0;
    m_honey_target_id = target ? target->Id() : -1;
    m_honey_target_generation = target ? target->Generation() : 0;
}

void CBumbleBeeController::ClearHoneyTarget() { SetHoneyTarget(nullptr); }

CEntity* CBumbleBeeController::ResolveHoneyTarget(CMobBase* mob)
{
    if (!m_p_honey_target && m_honey_target_id < 0) return nullptr;
    CGameWorld* world = mob ? mob->GameWorld() : nullptr;
    CEntity* resolved = world && world->GetId() == m_honey_target_world_id && m_honey_target_id >= 0
                            ? world->GetEntity(m_honey_target_id, m_honey_target_generation)
                            : nullptr;
    if (!resolved || resolved->GameWorld() != world)
    {
        ClearHoneyTarget();
        return nullptr;
    }
    m_p_honey_target = resolved;
    return m_p_honey_target;
}

void CBumbleBeeController::OnTick(CMobBase* mob, float dt)
{
    if (!mob || !mob->GameWorld()) return;

    const SMobStats* stats = mob->GetFinalStats();
    if (!stats) return;

    if (!m_initialized)
    {
        m_heading = mob->HasFacing() ? mob->FacingAngle() : GetLimitedRng(-game_config::pi, game_config::pi);
        PickTurnTimer();
        m_initialized = true;
    }

    if (LengthSq(mob->m_vel) > game_config::entity_collision_epsilon * game_config::entity_collision_epsilon)
        m_heading = std::atan2(mob->m_vel.y, mob->m_vel.x);

    CEntity* honey_target = ResolveHoneyTarget(mob);
    const float honey_search_range = std::max(stats->horizon, game_config::default_honey_attract_range);
    if (honey_target &&
        IsCurrentMeleeTargetUnavailable(mob, honey_target, honey_search_range, dt, m_honey_los_check_timer))
    {
        ClearHoneyTarget();
        honey_target = nullptr;
        m_honey_los_check_timer = 0.f;
    }
    if (!honey_target &&
        ShouldRunEntityTargetScan(m_honey_target_scan_cooldown, mob, game_config::honey_target_scan_interval_ticks))
    {
        SetHoneyTarget(FindClosestHoneyTarget(mob, honey_search_range));
        honey_target = ResolveHoneyTarget(mob);
    }

    if (honey_target)
    {
        FaceTarget(mob, honey_target);
        mob->MoveTowards(honey_target->Pos(), dt);
    } else
    {
        m_turn_timer -= dt;
        if (m_turn_timer <= 0.f)
        {
            float max_angle = std::max(0.f, game_config::mob_bumblebee_turn_max_angle);
            m_heading += GetLimitedRng(-max_angle, max_angle);
            PickTurnTimer();
        }

        m_wave_timer += dt;
        float wave = std::sin(m_wave_timer * game_config::mob_bee_wave_frequency) * game_config::mob_bee_wave_strength *
                     game_config::mob_bumblebee_wave_strength_multiplier;
        sf::Vector2f target =
            mob->Pos() +
            sf::Vector2f(std::cos(m_heading + wave), std::sin(m_heading + wave)) *
                std::max(game_config::mob_bumblebee_wander_horizon_min,
                         stats->max_velocity * game_config::mob_bumblebee_wander_horizon_velocity_multiplier);
        mob->MoveTowards(target, dt);
    }

    m_pollen_timer += dt;
    float interval = std::max(game_config::server_fixed_dt, game_config::mob_bumblebee_pollen_interval);
    if (m_pollen_timer >= interval)
    {
        m_pollen_timer = std::fmod(m_pollen_timer, interval);
        SpawnPollen(mob);
    }
}

void CFireflyController::OnTick(CMobBase* mob, float dt)
{
    CNeutralMeleeController::OnTick(mob, dt);

    const ERarity rarity = mob->GetRarity();
    if (rarity != ERarity::Eternal && rarity != ERarity::Unique && rarity != ERarity::Primordial) return;
    m_skill_timer -= std::max(0.f, dt);
    if (m_skill_timer > 0.f) return;
m_skill_timer = rarity == ERarity::Primordial ? 15.f : 30.f;
    TrySummonSkill(mob);
}

void CFireflyController::CaptureSnapshot(CSnapshotWriter& writer) const
{
    CMeleeController::CaptureSnapshot(writer);
    writer.Field("skill_timer", m_skill_timer);
}

bool CFireflyController::RestoreSnapshot(const CSnapshotReader& reader, std::uint32_t version, std::string& error)
{
    if (!CMeleeController::RestoreSnapshot(reader, version, error)) return false;
    m_skill_timer = reader.Float("skill_timer", m_skill_timer);
    return true;
}

void CFireflyController::TrySummonSkill(CMobBase* mob)
{
    CEntity* target = ResolveTarget(mob);
    if (!target) return; // Only release the skill with a target in view.
    CGameWorld* world = mob->GameWorld();
    if (!world) return;

    ERarity minion_rarity = static_cast<ERarity>(static_cast<int>(mob->GetRarity()) - 1);
    if (minion_rarity == ERarity::Unique) minion_rarity = ERarity::Eternal;
    const int limit = mob->GetRarity() == ERarity::Primordial ? game_config::mob_firefly_summon_limit_primordial
                                                              : game_config::mob_firefly_summon_limit_eternal;
    if (CountNearbyFireflies(mob, minion_rarity) >= limit) return;

    const float range = std::min(game_config::simulation_active_view_radius_cap, game_config::WorldUnits(400.f));
    const float angle = GetLimitedRng(0.f, 2.f * game_config::pi);
    const float dist = GetLimitedRng(mob->Radius() * 2.f, range);
    const sf::Vector2f spawn_pos = mob->Pos() + sf::Vector2f(std::cos(angle), std::sin(angle)) * dist;

    auto created = CreateMob(EMobType::Firefly, world, spawn_pos, minion_rarity);
    if (!created) return;
    CMobBase* raw_mob = dynamic_cast<CMobBase*>(world->InsertEntity(std::move(created)));
    if (!raw_mob) return;
    raw_mob->AddTag(EEntityTag::NoDefeatRewards);

    if (auto* controller = dynamic_cast<CFireflyController*>(raw_mob->GetController()))
        controller->SetTarget(target);

    if (CServer::MeetsPetalReportRarity(raw_mob->GetRarity(), game_config::min_mob_spawn_report_rarity) &&
        CServer::GetInstance())
        CServer::GetInstance()->BroadcastMobReport("has been summoned", raw_mob->GetRarity(), "Firefly");
}

int CFireflyController::CountNearbyFireflies(const CMobBase* mob, ERarity rarity) const
{
    CGameWorld* world = mob->GameWorld();
    if (!world) return 0;
    int count = 0;
    const float range = game_config::simulation_active_view_radius_cap;
    world->GetSpatialGrid().ForEachInRange(mob->Pos(), range, [&](const CEntity* entity) {
        const auto* candidate = dynamic_cast<const CMobBase*>(entity);
        if (candidate && candidate->GetMobType() == EMobType::Firefly && candidate->GetRarity() == rarity &&
            !candidate->IsMarkedForDes())
            ++count;
    });
    return count;
}
