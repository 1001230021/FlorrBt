#include "melee_controller.h"
#include "../../../Shared/game_config.h"
#include "../../HotReload/snapshot_archive.h"
#include "../entities/petals/petal.h"
#include "../entities/projectile.h"
#include "../gameworld.h"
#include "../state_zone.h"
#include "../states/states.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <random>
#include <unordered_set>
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
    std::unordered_set<const CEntity*> carried_leaf_pieces;
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
    float center_distance = Distance(source->m_pos, target->m_pos);
    float edge_distance =
        std::max(0.f, center_distance - std::max(0.f, source->m_radius) - std::max(0.f, target->m_radius));
    return edge_distance * edge_distance;
}

float TargetSearchQueryRange(CMobBase* mob, float edge_range)
{
    if (!mob) return edge_range;
    float max_target_radius = std::max(game_config::default_flower_radius,
                                       game_config::mob_queen_ant_radius *
                                           game_config::MobRadiusScaleForLevel(GetLevel(ERarity::Primordial)));
    return edge_range + std::max(0.f, mob->m_radius) + max_target_radius;
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

    if (cooldown < 0) cooldown = entity ? std::max(0, entity->m_id) % interval : 0;

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
    if (target->m_is_marked_for_des || target->IsDead() || !target->CanCollide()) return true;
    if (IsDiggingEntity(target)) return true;
    if (target == mob) return true;
    if (target->m_id == ignored_id || target->m_id == ignored_owner_id) return true;
    const auto* target_mob = dynamic_cast<const CMobBase*>(target);
    if (mob->GetMobType() == EMobType::Bee && target_mob && target_mob->GetMobType() == EMobType::AntHole)
        return true;
    if (CheckTeam(target->m_team, mob->m_team)) return true;
    if ((target->m_team == 0 || mob->m_team == 0) && ShareRootOwner(mob, target)) return true;
    if (BlocksNullifiedInteraction(mob, target)) return true;
    if (dynamic_cast<const CProjectile*>(target)) return true;
    return false;
}

bool IsValidHoneyTarget(const CMobBase* mob, const CEntity* target, int ignored_id = -1, int ignored_owner_id = -1)
{
    if (!mob || !target) return false;
    if (!mob->GameWorld() || target->GameWorld() != mob->GameWorld()) return false;
    if (target->m_is_marked_for_des || target->IsDead() || !target->CanCollide()) return false;
    if (IsDiggingEntity(target)) return false;
    if (target == mob) return false;
    if (target->m_id == ignored_id || target->m_id == ignored_owner_id) return false;
    if (CheckTeam(target->m_team, mob->m_team)) return false;
    if ((target->m_team == 0 || mob->m_team == 0) && ShareRootOwner(mob, target)) return false;
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
    return mob->GameWorld()->SegmentBlockedByWall(mob->m_pos, target->m_pos);
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
    mob->GameWorld()->GetSpatialGrid().ForEachInRange(mob->m_pos, query_range, [&](CEntity* candidate) {
        if (!IsValidHoneyTarget(mob, candidate, ignored_id, ignored_owner_id)) return;
        float dist_sq = TargetEdgeDistanceSq(mob, candidate);
        if (dist_sq > search_range_sq) return;
        AddMeleeTargetCandidate(candidates, candidate, dist_sq, candidate_limit);
    });
    return FirstVisibleMeleeTarget(mob, candidates);
}

CEntity* FindClosestMeleeTarget(CMobBase* mob, float search_range, int ignored_id = -1, int ignored_owner_id = -1)
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
    mob->GameWorld()->GetSpatialGrid().ForEachInRange(mob->m_pos, query_range, [&](CEntity* candidate) {
        float dist_sq = TargetEdgeDistanceSq(mob, candidate);
        if (dist_sq > search_range_sq) return;

        if (IsValidHoneyTarget(mob, candidate, ignored_id, ignored_owner_id))
        {
            AddMeleeTargetCandidate(honey_candidates, candidate, dist_sq, candidate_limit);
            return;
        }

        if (IsInvalidMeleeTarget(mob, candidate, ignored_id, ignored_owner_id)) return;

        float detection_multiplier = 1.f;
        if (auto* flower = dynamic_cast<const CFlower*>(candidate))
            detection_multiplier = flower->m_final_stats.detection_multiplier;
        float range = search_range * detection_multiplier;
        if (dist_sq > range * range) return;

        AddMeleeTargetCandidate(target_candidates, candidate, dist_sq, candidate_limit);
    });
    if (CEntity* honey = FirstVisibleMeleeTarget(mob, honey_candidates)) return honey;
    return FirstVisibleMeleeTarget(mob, target_candidates);
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

        const sf::Vector2f end = mob->m_pos + direction * travel;
        if (travel > game_config::entity_collision_epsilon &&
            mob->GameWorld()->SweptCircleBlockedByWall(mob->m_pos, end, missile_radius))
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
    mob->GameWorld()->ForEachEntityInEdgeRange(mob->m_pos, query_range, [&](CEntity* candidate) {
        if (!candidate || !candidate->IsEntityType(EEntityType::Mob)) return;
        if (IsInvalidMeleeTarget(mob, candidate)) return;

        const sf::Vector2f relative = candidate->m_pos - mob->m_pos;
        const float distance_sq = LengthSq(relative);
        const float distance = std::sqrt(std::max(0.f, distance_sq));
        const float hit_radius = std::max(0.f, missile_radius) + std::max(0.f, candidate->m_radius);
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
        aim_candidates.push_back({ mob->m_has_facing ? mob->m_facing_angle : 0.f, initial_score });

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
    sf::Vector2f delta = target->m_pos - mob->m_pos;
    if (LengthSq(delta) <= game_config::entity_collision_epsilon * game_config::entity_collision_epsilon) return;
    mob->m_facing_angle = std::atan2(delta.y, delta.x);
    mob->m_has_facing = true;
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
    writer.Field("carried_leaf_piece_world_id", m_carried_leaf_piece_world_id);
    writer.Field("carried_leaf_piece_id", m_carried_leaf_piece_id);
    writer.UInt64("carried_leaf_piece_generation", m_carried_leaf_piece_generation);
    writer.Field("leaf_piece_attack_timer", m_leaf_piece_attack_timer);
    writer.Field("leaf_piece_target_scan_cooldown", m_leaf_piece_target_scan_cooldown);
}

bool CLeafcutterSoldierController::RestoreSnapshot(const CSnapshotReader& reader, std::uint32_t version,
                                                   std::string& error)
{
    if (!CMeleeController::RestoreSnapshot(reader, version, error)) return false;
    m_leaf_piece_target_world_id = reader.UInt32("leaf_piece_target_world_id");
    m_leaf_piece_target_id = reader.Int("leaf_piece_target_id", -1);
    m_leaf_piece_target_generation = reader.UInt64("leaf_piece_target_generation");
    m_carried_leaf_piece_world_id = reader.UInt32("carried_leaf_piece_world_id");
    m_carried_leaf_piece_id = reader.Int("carried_leaf_piece_id", -1);
    m_carried_leaf_piece_generation = reader.UInt64("carried_leaf_piece_generation");
    m_leaf_piece_attack_timer = reader.Float("leaf_piece_attack_timer");
    m_leaf_piece_target_scan_cooldown = reader.Int("leaf_piece_target_scan_cooldown", -1);
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
    writer.Field("skill3_captured_world_id", m_skill3_captured_world_id);
    writer.Field("skill3_captured_id", m_skill3_captured_id);
    writer.UInt64("skill3_captured_generation", m_skill3_captured_generation);
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
    m_skill3_captured_world_id = reader.UInt32("skill3_captured_world_id");
    m_skill3_captured_id = reader.Int("skill3_captured_id", -1);
    m_skill3_captured_generation = reader.UInt64("skill3_captured_generation");
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
    PickRandomTargetPosNear(mob, mob->m_pos, half_range);
}

void CMeleeController::PickRandomTargetPosNear(CMobBase* mob, const sf::Vector2f& center, float half_range)
{
    if (!mob) return;
    ResetWanderProgress();
    m_random_idle = false;
    m_random_idle_timer = 0.f;

    if (CheckChance(game_config::melee_random_idle_chance))
    {
        m_target_pos = mob->m_pos;
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
        if (world && world->SweptCircleBlockedByWall(mob->m_pos, candidate, collision_radius)) continue;

        m_target_pos = candidate;
        m_has_random_target_pos = true;
        return;
    }

    m_target_pos = mob->m_pos;
    m_has_random_target_pos = true;
    m_random_idle = true;
}

bool CMeleeController::IsRandomIdleDone(float dt)
{
    if (!m_random_idle) return false;
    m_random_idle_timer += dt;
    return m_random_idle_timer >= game_config::melee_random_idle_time;
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
    if (DistanceSq(mob->m_pos, m_target_pos) <= mob->m_radius * mob->m_radius)
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
        m_wander_progress_anchor = mob->m_pos;
        m_wander_progress_timer = 0.f;
        m_wander_progress_initialized = true;
        return false;
    }
    if (DistanceSq(mob->m_pos, m_wander_progress_anchor) >= progress_distance * progress_distance)
    {
        m_wander_progress_anchor = mob->m_pos;
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
    m_target_id = target ? target->m_id : -1;
    m_target_generation = target ? target->m_generation : 0;
}

void CMeleeController::ClearTarget() { SetTarget(nullptr); }

void CMeleeController::LoseTarget(CMobBase* mob)
{
    ClearTarget();
    m_target_pos = mob ? mob->m_pos : sf::Vector2f{};
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

bool CMeleeController::ShouldRunTargetScan(CMobBase* mob)
{
    return ShouldRunEntityTargetScan(m_target_scan_cooldown, mob);
}

bool CMeleeController::TryAcquireWanderTarget(CMobBase* mob, float search_range, int ignored_id, int ignored_owner_id)
{
    if (!ShouldRunTargetScan(mob)) return false;

    CEntity* target = FindClosestMeleeTarget(mob, search_range, ignored_id, ignored_owner_id);
    if (!target) return false;

    SetTarget(target);
    target = ResolveTarget(mob);
    if (!target) return false;

    m_target_pos = target->m_pos;
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

    m_target_pos = target->m_pos;
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
    }
    bool reached_random_target = !target && ReachedOrStalledRandomTarget(mob, dt);
    if (!target && m_has_random_target_pos && !reached_random_target && TryAcquireWanderTarget(mob, stats->horizon))
        target = ResolveTarget(mob);

    bool can_random_retarget = !target && m_change_target_count >= game_config::melee_target_time;
    float retarget_chance = game_config::melee_retarget_chance_multiplier * m_change_target_count * dt /
                            (game_config::melee_target_time * game_config::melee_target_time);
    retarget_chance = std::clamp(retarget_chance, 0.f, 1.f);
    bool should_retarget = target_invalid || !m_has_random_target_pos || reached_random_target ||
                           (can_random_retarget && CheckChance(retarget_chance));
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

    if ((target = ResolveTarget(mob))) m_target_pos = target->m_pos;
    mob->MoveTowards(m_target_pos, dt);
}

CEntity* CLeafcutterSoldierController::ResolveLeafPieceReference(const CMobBase* mob, std::uint32_t world_id,
                                                                 int entity_id, std::uint64_t generation) const
{
    if (!mob || !mob->GameWorld() || mob->m_is_marked_for_des || mob->IsDead() || entity_id < 0 || generation == 0)
        return nullptr;

    CGameWorld* world = mob->GameWorld();
    if (world->GetId() != world_id) return nullptr;

    CEntity* entity = world->GetEntity(entity_id, generation);
    const auto* leaf_piece = dynamic_cast<const CMobBase*>(entity);
    if (!leaf_piece || leaf_piece->GetMobType() != EMobType::LeafPiece || leaf_piece->m_is_marked_for_des ||
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

CEntity* CLeafcutterSoldierController::ResolveCarriedLeafPiece(const CMobBase* mob) const
{
    return ResolveLeafPieceReference(mob, m_carried_leaf_piece_world_id, m_carried_leaf_piece_id,
                                     m_carried_leaf_piece_generation);
}

bool CLeafcutterSoldierController::IsEligibleLeafPiece(const CMobBase* mob, const CEntity* entity,
                                                       bool reject_carried) const
{
    if (!mob || !entity || entity == mob || entity->GameWorld() != mob->GameWorld() || entity->m_is_marked_for_des ||
        entity->IsDead())
        return false;

    const auto* leaf_piece = dynamic_cast<const CMobBase*>(entity);
    if (!leaf_piece || leaf_piece->GetMobType() != EMobType::LeafPiece) return false;
    if (reject_carried && IsLeafPieceCarried(mob, leaf_piece)) return false;

    const int common_level = GetLevel(ERarity::Common);
    const int soldier_level = std::max(common_level, GetLevel(mob->GetRarity()));
    const int minimum_leaf_level = std::max(common_level, soldier_level - 1);
    const int leaf_level = GetLevel(leaf_piece->GetRarity());
    return leaf_level >= minimum_leaf_level && leaf_level <= soldier_level;
}

bool CLeafcutterSoldierController::IsLeafPieceCarried(const CMobBase* mob, const CEntity* leaf_piece) const
{
    if (!mob || !leaf_piece || !mob->GameWorld() || leaf_piece->GameWorld() != mob->GameWorld()) return false;

    bool carried = false;
    mob->GameWorld()->ForEachEntity([&](CEntity* entity) {
        if (carried) return;
        const auto* soldier = dynamic_cast<const CMobBase*>(entity);
        if (!soldier || soldier->GetMobType() != EMobType::LeafcutterSoldier || soldier->m_is_marked_for_des ||
            soldier->IsDead())
            return;

        const auto* controller = dynamic_cast<const CLeafcutterSoldierController*>(soldier->GetController());
        carried = controller && controller->IsCarryingLeafPiece(soldier, leaf_piece);
    });
    return carried;
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
    m_leaf_piece_target_id = leaf_piece->m_id;
    m_leaf_piece_target_generation = leaf_piece->m_generation;
}

void CLeafcutterSoldierController::ClearCarriedLeafPiece()
{
    m_carried_leaf_piece_world_id = 0;
    m_carried_leaf_piece_id = -1;
    m_carried_leaf_piece_generation = 0;
}

void CLeafcutterSoldierController::SetCarriedLeafPiece(CEntity* leaf_piece)
{
    if (!leaf_piece || !leaf_piece->GameWorld())
    {
        ClearCarriedLeafPiece();
        return;
    }

    m_carried_leaf_piece_world_id = leaf_piece->GameWorld()->GetId();
    m_carried_leaf_piece_id = leaf_piece->m_id;
    m_carried_leaf_piece_generation = leaf_piece->m_generation;
}

bool CLeafcutterSoldierController::TryAcquireLeafPieceTarget(CMobBase* mob, float search_range)
{
    if (!mob || !mob->GameWorld() || search_range <= 0.f) return false;
    if (!ShouldRunEntityTargetScan(m_leaf_piece_target_scan_cooldown, mob)) return false;

    auto& carried_leaf_pieces = g_melee_scan_scratch.carried_leaf_pieces;
    carried_leaf_pieces.clear();
    mob->GameWorld()->ForEachEntity([&](CEntity* entity) {
        const auto* soldier = dynamic_cast<const CMobBase*>(entity);
        if (!soldier || soldier->GetMobType() != EMobType::LeafcutterSoldier || soldier->m_is_marked_for_des ||
            soldier->IsDead())
            return;

        const auto* controller = dynamic_cast<const CLeafcutterSoldierController*>(soldier->GetController());
        if (!controller) return;
        if (const CEntity* carried = controller->ResolveCarriedLeafPiece(soldier)) carried_leaf_pieces.insert(carried);
    });

    const int carried_id = m_carried_leaf_piece_id;
    const std::uint64_t carried_generation = m_carried_leaf_piece_generation;
    CEntity* leaf_piece = mob->GameWorld()->FindClosestEntity(
        mob->m_pos, search_range,
        [this, mob, carried_id, carried_generation, &carried_leaf_pieces](const CEntity* entity) {
            if (!IsEligibleLeafPiece(mob, entity, false)) return false;
            if (carried_leaf_pieces.find(entity) != carried_leaf_pieces.end()) return false;
            if (entity->m_id == carried_id && entity->m_generation == carried_generation) return false;
            return !IsMeleeTargetBlockedByWall(mob, entity);
        });
    if (!leaf_piece) return false;

    SetLeafPieceTarget(leaf_piece);
    return true;
}

bool CLeafcutterSoldierController::TryCarryLeafPiece(CMobBase* mob)
{
    if (!mob || m_leaf_piece_attack_timer > 0.f) return false;

    CEntity* leaf_piece = ResolveLeafPieceTarget(mob);
    if (!leaf_piece) return false;

    const float configured_attack_radius =
        std::max(0.f, mob->m_radius * game_config::mob_leafcutter_soldier_leaf_piece_attack_radius_multiplier);
    const float contact_radius = std::max(0.f, mob->m_radius) + std::max(0.f, leaf_piece->m_radius) +
                                 std::max(0.f, game_config::entity_collision_epsilon);
    const float attack_radius = std::max(configured_attack_radius, contact_radius);
    if (DistanceSq(mob->m_pos, leaf_piece->m_pos) > attack_radius * attack_radius) return false;

    SetCarriedLeafPiece(leaf_piece);
    ClearLeafPieceTarget();
    m_leaf_piece_attack_timer = std::max(0.f, game_config::mob_leafcutter_soldier_leaf_piece_attack_interval);
    m_has_random_target_pos = false;
    m_random_idle = false;
    m_random_idle_timer = 0.f;
    return true;
}

void CLeafcutterSoldierController::ApplyCarryingTurn(CMobBase* mob, float previous_angle, bool had_facing,
                                                     float dt) const
{
    if (!mob || !had_facing || !mob->m_has_facing) return;

    const float desired_angle = mob->m_facing_angle;
    float angle_delta = std::atan2(std::sin(desired_angle - previous_angle), std::cos(desired_angle - previous_angle));
    const float max_turn = std::max(0.f, game_config::mob_leafcutter_soldier_leaf_piece_turn_speed) * std::max(0.f, dt);
    angle_delta = std::clamp(angle_delta, -max_turn, max_turn);
    mob->m_facing_angle = std::atan2(std::sin(previous_angle + angle_delta), std::cos(previous_angle + angle_delta));

    const float speed = Length(mob->m_vel);
    if (speed > game_config::entity_collision_epsilon)
        mob->m_vel = sf::Vector2f(std::cos(mob->m_facing_angle), std::sin(mob->m_facing_angle)) * speed;
}

void CLeafcutterSoldierController::OnTick(CMobBase* mob, float dt)
{
    if (!mob || !mob->GameWorld()) return;

    const SMobStats* stats = mob->GetFinalStats();
    if (!stats) return;

    m_leaf_piece_attack_timer = std::max(0.f, m_leaf_piece_attack_timer - std::max(0.f, dt));
    CEntity* carried_leaf_piece = ResolveCarriedLeafPiece(mob);
    if (!carried_leaf_piece) ClearCarriedLeafPiece();

    CEntity* leaf_piece_target = ResolveLeafPieceTarget(mob);
    const float leaf_piece_search_range = std::max(0.f, stats->horizon);
    if (leaf_piece_target &&
        DistanceSq(mob->m_pos, leaf_piece_target->m_pos) > leaf_piece_search_range * leaf_piece_search_range)
    {
        ClearLeafPieceTarget();
        leaf_piece_target = nullptr;
    } else if (!leaf_piece_target)
    {
        ClearLeafPieceTarget();
    }

    CEntity* combat_target = ResolveTarget(mob);

    if (!carried_leaf_piece && !combat_target)
    {
        if (!leaf_piece_target && TryAcquireLeafPieceTarget(mob, leaf_piece_search_range))
            leaf_piece_target = ResolveLeafPieceTarget(mob);
        if (leaf_piece_target)
        {
            m_target_pos = leaf_piece_target->m_pos;
            m_change_target_count = 0.f;
            m_has_random_target_pos = true;
            m_random_idle = false;
            m_random_idle_timer = 0.f;
        }
    }

    const bool was_carrying = carried_leaf_piece != nullptr;
    const float previous_angle = mob->m_facing_angle;
    const bool had_facing = mob->m_has_facing;
    CMeleeController::OnTick(mob, dt);
    if (was_carrying) ApplyCarryingTurn(mob, previous_angle, had_facing, dt);

    if (!ResolveTarget(mob) && !ResolveCarriedLeafPiece(mob)) TryCarryLeafPiece(mob);
}

bool CLeafcutterSoldierController::IsCarryingLeafPiece(const CMobBase* mob) const
{
    return ResolveCarriedLeafPiece(mob) != nullptr;
}

bool CLeafcutterSoldierController::IsCarryingLeafPiece(const CMobBase* mob, const CEntity* leaf_piece) const
{
    return leaf_piece && ResolveCarriedLeafPiece(mob) == leaf_piece;
}

void CLeafcutterSoldierController::SyncCarriedLeafPiece(CMobBase* mob)
{
    if (!mob || mob->m_is_marked_for_des || mob->IsDead())
    {
        ClearCarriedLeafPiece();
        return;
    }

    CEntity* leaf_piece = ResolveCarriedLeafPiece(mob);
    if (!leaf_piece)
    {
        ClearCarriedLeafPiece();
        return;
    }

    const float angle = mob->m_has_facing ? mob->m_facing_angle : 0.f;
    const sf::Vector2f forward(std::cos(angle), std::sin(angle));
    const float contact_distance = std::max(0.f, mob->m_radius) + std::max(0.f, leaf_piece->m_radius);
    leaf_piece->m_pos = mob->m_pos + forward * contact_distance;
    if (auto* leaf_piece_mob = dynamic_cast<CMobBase*>(leaf_piece)) leaf_piece_mob->m_vel = { 0.f, 0.f };
}

void CLeafcutterSoldierController::ResolveCarriedLeafPieceConstraint(CMobBase* mob)
{
    if (!mob || mob->m_is_marked_for_des || mob->IsDead() || !mob->GameWorld())
    {
        ClearCarriedLeafPiece();
        return;
    }

    CEntity* leaf_piece = ResolveCarriedLeafPiece(mob);
    auto* leaf_piece_mob = dynamic_cast<CMobBase*>(leaf_piece);
    if (!leaf_piece || !leaf_piece_mob)
    {
        ClearCarriedLeafPiece();
        return;
    }

    const float angle = mob->m_has_facing ? mob->m_facing_angle : 0.f;
    const sf::Vector2f forward(std::cos(angle), std::sin(angle));
    const float contact_distance = std::max(0.f, mob->m_radius) + std::max(0.f, leaf_piece->m_radius);
    const sf::Vector2f desired_leaf_pos = mob->m_pos + forward * contact_distance;
    const sf::Vector2f error = leaf_piece->m_pos - desired_leaf_pos;
    const float error_sq = LengthSq(error);
    const float epsilon = std::max(0.f, game_config::entity_collision_epsilon);
    leaf_piece_mob->m_vel = { 0.f, 0.f };
    if (error_sq <= epsilon * epsilon)
    {
        leaf_piece->m_pos = desired_leaf_pos;
        return;
    }

    auto inverse_mass = [epsilon](float mass) {
        if (!std::isfinite(mass)) return 0.f;
        return mass > epsilon ? 1.f / mass : 1.f;
    };
    const float soldier_inverse_mass = inverse_mass(mob->m_mass);
    const float leaf_inverse_mass = inverse_mass(leaf_piece->m_mass);
    const float inverse_mass_sum = soldier_inverse_mass + leaf_inverse_mass;
    const float soldier_weight = inverse_mass_sum > epsilon ? soldier_inverse_mass / inverse_mass_sum : 0.5f;
    const float leaf_weight = inverse_mass_sum > epsilon ? leaf_inverse_mass / inverse_mass_sum : 0.5f;

    CGameWorld* world = mob->GameWorld();
    auto is_clear = [world](sf::Vector2f pos, float radius) {
        return !world->CircleBlockedByWall(pos, std::max(0.f, radius));
    };

    const sf::Vector2f weighted_soldier_pos = mob->m_pos + error * soldier_weight;
    const sf::Vector2f weighted_leaf_pos = leaf_piece->m_pos - error * leaf_weight;
    const bool weighted_soldier_blocked = !is_clear(weighted_soldier_pos, mob->WallCollisionRadius());
    const bool weighted_leaf_blocked = !is_clear(weighted_leaf_pos, leaf_piece->WallCollisionRadius());
    bool leaf_wall_pushed_soldier = false;

    if (!weighted_soldier_blocked && !weighted_leaf_blocked)
    {
        mob->m_pos = weighted_soldier_pos;
        leaf_piece->m_pos = weighted_leaf_pos;
    } else if (weighted_leaf_blocked && is_clear(mob->m_pos + error, mob->WallCollisionRadius()))
    {
        mob->m_pos += error;
        leaf_wall_pushed_soldier = true;
    } else if (weighted_soldier_blocked &&
               is_clear(leaf_piece->m_pos - error, leaf_piece->WallCollisionRadius()))
    {
        leaf_piece->m_pos -= error;
    } else if (is_clear(mob->m_pos + error, mob->WallCollisionRadius()))
    {
        mob->m_pos += error;
        leaf_wall_pushed_soldier = weighted_leaf_blocked;
    } else if (is_clear(leaf_piece->m_pos - error, leaf_piece->WallCollisionRadius()))
    {
        leaf_piece->m_pos -= error;
    } else
    {
        return;
    }

    if (leaf_wall_pushed_soldier)
    {
        const float error_length = std::sqrt(error_sq);
        const sf::Vector2f wall_normal = error / error_length;
        const float velocity_into_wall = mob->m_vel.x * wall_normal.x + mob->m_vel.y * wall_normal.y;
        if (velocity_into_wall < 0.f) mob->m_vel -= wall_normal * velocity_into_wall;
    }
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
    if (entity->m_id != m_owner_id) return false;
    return m_owner_generation == 0 || entity->m_generation == m_owner_generation;
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
    if (!owner || owner->m_is_marked_for_des || owner->IsDead())
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

    float owner_dist_sq = DistanceSq(mob->m_pos, owner->m_pos);
    float range_scale = std::max(1.f, mob->m_radius / std::max(1.f, game_config::mob_summoned_beetle_radius));
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
        m_target_pos = owner->m_pos;
        m_has_random_target_pos = true;
        m_random_idle = false;
        m_random_idle_timer = 0.f;
        m_change_target_count = 0.f;
        mob->MoveTowards(owner->m_pos, dt);
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
        owner_owner_id = root_owner ? root_owner->m_id : -1;
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
    if (TryAcquireHoneyTarget(mob, search_range, owner->m_id, owner_owner_id))
    {
        target = ResolveTarget(mob);
        target_invalid = false;
    }
    bool reached_random_target = !target && ReachedOrStalledRandomTarget(mob, dt);
    if (!target && m_has_random_target_pos && !reached_random_target &&
        TryAcquireWanderTarget(mob, search_range, owner->m_id, owner_owner_id))
        target = ResolveTarget(mob);

    bool can_random_retarget = !target && m_change_target_count >= game_config::melee_target_time;
    float retarget_chance = game_config::melee_retarget_chance_multiplier * m_change_target_count * dt /
                            (game_config::melee_target_time * game_config::melee_target_time);
    retarget_chance = std::clamp(retarget_chance, 0.f, 1.f);

    if (target_invalid || !m_has_random_target_pos || reached_random_target ||
        (can_random_retarget && CheckChance(retarget_chance)))
    {
        m_change_target_count = 0.f;
        m_target_los_check_timer = 0.f;
        m_has_random_target_pos = false;
        m_random_idle = false;
        m_random_idle_timer = 0.f;

        if (TryAcquireWanderTarget(mob, search_range, owner->m_id, owner_owner_id)) target = ResolveTarget(mob);
        else
        {
            float wander_half_range = std::max(mob->m_radius * game_config::mob_summoned_wander_radius_multiplier,
                                               follow_range * game_config::mob_summoned_wander_follow_range_multiplier);
            PickRandomTargetPosNear(mob, owner->m_pos, wander_half_range);
        }
    }

    if ((target = ResolveTarget(mob))) m_target_pos = target->m_pos;
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

    if (target)
    {
        set_attack_state(target);
        m_target_pos = target->m_pos;
        mob->MoveTowards(m_target_pos, dt);
        return;
    }

    set_attack_state(nullptr);

    bool reached_random_target = ReachedOrStalledRandomTarget(mob, dt);

    if (!target &&
        (!m_has_random_target_pos || reached_random_target || m_change_target_count >= game_config::melee_target_time))
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
    m_target_pos = attacker->m_pos;
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
    if (!mob || !mob->GameWorld() || mob->IsDead() || mob->m_is_marked_for_des) return;

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

    const float range = std::max(0.f, mob->m_radius) + std::max(0.f, game_config::psionic_connection_range);
    if (range <= game_config::entity_collision_epsilon) return;
    const float range_sq = range * range;

    std::vector<CFlower*> targets;
    mob->GameWorld()->GetSpatialGrid().ForEachInRange(mob->m_pos, range, [&](CEntity* entity) {
        if (!entity || entity == mob || entity->m_is_marked_for_des || entity->IsDead()) return;
        auto* flower = dynamic_cast<CFlower*>(entity);
        if (!flower || !CheckTeam(flower->m_team, mob->m_team)) return;
        if (DistanceSq(flower->m_pos, mob->m_pos) > range_sq) return;
        targets.push_back(flower);
    });

    for (CFlower* flower : targets)
    {
        if (!flower || flower->m_is_marked_for_des || flower->IsDead()) continue;

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
        m_target_pos = target->m_pos;
        mob->MoveTowards(m_target_pos, dt);
        return;
    }

    bool reached_random_target = ReachedOrStalledRandomTarget(mob, dt);

    if (!target &&
        (!m_has_random_target_pos || reached_random_target || m_change_target_count >= game_config::melee_target_time))
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
    if (!mob || !target || target->m_is_marked_for_des || target->IsDead()) return;

    const SMobStats* stats = mob->GetFinalStats();
    if (!stats) return;

    float attack_range =
        std::max(mob->m_radius * game_config::mob_hornet_attack_range_radius_multiplier, stats->horizon);
    if (DistanceSq(mob->m_pos, target->m_pos) > attack_range * attack_range) return;

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
    if (!target || target->m_is_marked_for_des || target->IsDead()) return;

    m_web_timer += dt;
    float interval = std::max(game_config::server_fixed_dt, game_config::mob_spider_web_interval);
    if (m_web_timer < interval) return;
    m_web_timer = std::fmod(m_web_timer, interval);

    auto web = std::make_unique<CSpiderWebZone>(mob->GameWorld(), mob->m_pos, mob->m_radius, mob,
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

    bool can_random_retarget = !target && m_change_target_count >= game_config::melee_target_time;
    float retarget_chance = game_config::melee_retarget_chance_multiplier * m_change_target_count * dt /
                            (game_config::melee_target_time * game_config::melee_target_time);
    retarget_chance = std::clamp(retarget_chance, 0.f, 1.f);

    bool should_retarget = target_invalid || !m_has_random_target_pos || reached_random_target ||
                           (can_random_retarget && CheckChance(retarget_chance));
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

    m_target_pos = target->m_pos;
    FaceTarget(mob, target);

    sf::Vector2f to_target = target->m_pos - mob->m_pos;
    float target_dist = Length(to_target);
    float stop_distance = game_config::mob_hornet_stop_distance + mob->m_radius;
    if (target_dist <= stop_distance) mob->MoveTowards(mob->m_pos, dt);
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

    bool can_random_retarget = !target && m_change_target_count >= game_config::melee_target_time;
    float retarget_chance = game_config::melee_retarget_chance_multiplier * m_change_target_count * dt /
                            (game_config::melee_target_time * game_config::melee_target_time);
    retarget_chance = std::clamp(retarget_chance, 0.f, 1.f);

    bool should_retarget = target_invalid || !m_has_random_target_pos || reached_random_target ||
                           (can_random_retarget && CheckChance(retarget_chance));
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

    m_target_pos = target->m_pos;
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

    const float stop_distance = mob->m_radius + game_config::default_petal_neutral_reach +
                                std::max(0.f, stats->reach);
    const float target_distance = Distance(mob->m_pos, target->m_pos);
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
        const float exit_distance = stop_distance + std::max(0.f, mob->m_radius) *
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
    const int target_id = target ? target->m_id : -1;
    const std::uint64_t generation = target ? target->m_generation : 0;
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

    sf::Vector2f outward = mob->m_pos - target->m_pos;
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

    const float lookahead = std::max(stop_distance, std::max(0.f, mob->m_radius) * 2.f);
    mob->MoveTowards(mob->m_pos + move_direction * lookahead, dt);
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
            float base_angle = mob->m_has_facing ? mob->m_facing_angle : 0.f;
            sf::Vector2f forward = { std::cos(base_angle), std::sin(base_angle) };
            if (LengthSq(forward) <= game_config::entity_collision_epsilon * game_config::entity_collision_epsilon)
                forward = { 1.f, 0.f };
            sf::Vector2f left = { -forward.y, forward.x };
            float side = CheckChance(0.5) ? -1.f : 1.f;
            m_skill2_orbit_radius = std::max(mob->m_radius * game_config::mob_hornet_skill2_orbit_radius_multiplier,
                                             game_config::entity_collision_epsilon);
            m_skill2_orbit_center = mob->m_pos + left * (side * m_skill2_orbit_radius);
            m_skill2_orbit_angle =
                std::atan2(mob->m_pos.y - m_skill2_orbit_center.y, mob->m_pos.x - m_skill2_orbit_center.x);
            m_skill2_orbit_dir = side;
            m_skill2_remaining_angle = game_config::pi * game_config::mob_hornet_skill2_arc_pi_multiplier;
            m_skill2_tangent_dir = { -std::sin(m_skill2_orbit_angle) * m_skill2_orbit_dir,
                                     std::cos(m_skill2_orbit_angle) * m_skill2_orbit_dir };
            mob->m_vel = { 0.f, 0.f };
            mob->m_facing_angle = std::atan2(m_skill2_tangent_dir.y, m_skill2_tangent_dir.x);
            mob->m_has_facing = true;
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
        sf::Vector2f drift = mob->m_pos - expected_pos;
        if (LengthSq(drift) > game_config::entity_collision_epsilon * game_config::entity_collision_epsilon)
            m_skill2_orbit_center += drift;

        sf::Vector2f origin = mob->m_pos;
        m_skill2_orbit_angle += m_skill2_orbit_dir * step;
        m_skill2_remaining_angle = std::max(0.f, m_skill2_remaining_angle - step);

        sf::Vector2f radial = { std::cos(m_skill2_orbit_angle), std::sin(m_skill2_orbit_angle) };
        m_skill2_tangent_dir = { -radial.y * m_skill2_orbit_dir, radial.x * m_skill2_orbit_dir };
        sf::Vector2f target_pos = m_skill2_orbit_center + radial * m_skill2_orbit_radius;
        float safe_dt = std::max(game_config::entity_collision_epsilon, dt);
        mob->m_vel = (target_pos - mob->m_pos) / safe_dt;
        mob->m_facing_angle = std::atan2(m_skill2_tangent_dir.y, m_skill2_tangent_dir.x);
        mob->m_has_facing = true;

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
        auto* captured = world && world->GetId() == m_skill3_captured_world_id && m_skill3_captured_id >= 0
                             ? dynamic_cast<CMobBase*>(
                                   world->GetEntity(m_skill3_captured_id, m_skill3_captured_generation))
                             : nullptr;
        if (!target || target->m_is_marked_for_des || target->IsDead())
        {
            LoseTarget(mob);
            FinishCurrentAction(mob);
            return;
        }
        if (!captured || captured->m_is_marked_for_des || captured->IsDead())
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

        sf::Vector2f mount_dir = { std::cos(mob->m_facing_angle), std::sin(mob->m_facing_angle) };
        if (LengthSq(mount_dir) <= game_config::entity_collision_epsilon * game_config::entity_collision_epsilon)
            mount_dir = { -m_skill3_launch_direction.x, -m_skill3_launch_direction.y };
        else
            mount_dir = NormalizeOrZero(mount_dir);
        if (LengthSq(mount_dir) <= game_config::entity_collision_epsilon * game_config::entity_collision_epsilon)
            mount_dir = { 1.f, 0.f };

        // The caster faces away from the attack target, so the captured mob is held on its back.
        const sf::Vector2f back_dir = -mount_dir;
        m_skill3_launch_pos = mob->m_pos + back_dir * (mob->m_radius * game_config::mob_hornet_missile_attach_offset);
        captured->m_pos = m_skill3_launch_pos;
        captured->m_prev_pos = m_skill3_launch_pos;
        captured->m_vel = { 0.f, 0.f };
        captured->m_facing_angle = std::atan2(mount_dir.y, mount_dir.x);
        captured->m_has_facing = true;

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
            captured->m_facing_angle = std::atan2(direction.y, direction.x);
            captured->m_has_facing = true;
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
    if (!target || target->m_is_marked_for_des || target->IsDead()) return;

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
                (mob->m_radius / std::max(game_config::entity_collision_epsilon, game_config::mob_hornet_radius));
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

    bool can_random_retarget = !target && m_change_target_count >= game_config::melee_target_time;
    float retarget_chance = game_config::melee_retarget_chance_multiplier * m_change_target_count * dt /
                            (game_config::melee_target_time * game_config::melee_target_time);
    retarget_chance = std::clamp(retarget_chance, 0.f, 1.f);

    bool should_retarget = target_invalid || !m_has_random_target_pos || reached_random_target ||
                           (can_random_retarget && CheckChance(retarget_chance));
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

    m_target_pos = target->m_pos;
    FaceTarget(mob, target);

    sf::Vector2f to_target = target->m_pos - mob->m_pos;
    float target_dist = Length(to_target);
    float stop_distance = game_config::mob_hornet_stop_distance + mob->m_radius;
    if (target_dist <= stop_distance) mob->MoveTowards(mob->m_pos, dt);
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
                mob->m_radius * game_config::mob_hornet_skill1_spawn_distance_min_radius_multiplier,
                std::max(mob->m_radius * game_config::mob_hornet_skill1_spawn_distance_safe_radius_multiplier,
                         mob->m_radius * game_config::mob_hornet_skill1_spawn_distance_max_radius_multiplier));
            sf::Vector2f pos = mob->m_pos + sf::Vector2f(std::cos(angle), std::sin(angle)) * distance;
            auto hornet = CreateMob(EMobType::Hornet, mob->GameWorld(), pos, rarity);
            if (!hornet) continue;
            hornet->m_team = mob->m_team;
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
    if (!target || target->m_is_marked_for_des || target->IsDead()) return false;

    CEntity* captured =
        FindHighestHornetInRange(mob, mob->m_radius * game_config::mob_hornet_skill3_grab_range_radius_multiplier);
    if (!captured) return false;

    auto* hornet = dynamic_cast<IHornetMob*>(mob);
    if (!hornet) return false;

    sf::Vector2f launch_direction = NormalizeOrZero(target->m_pos - mob->m_pos);
    if (LengthSq(launch_direction) <= game_config::entity_collision_epsilon * game_config::entity_collision_epsilon)
        launch_direction = { 1.f, 0.f };
    m_skill3_launch_direction = launch_direction;
    mob->m_facing_angle = std::atan2(-launch_direction.y, -launch_direction.x);
    mob->m_has_facing = true;
    hornet->SetMissileGenerationSuppressed(true, true);
    m_skill3_missile_suppressed = true;

    m_skill3_target_world_id = target->GameWorld()->GetId();
    m_skill3_target_id = target->m_id;
    m_skill3_target_generation = target->m_generation;
    m_skill3_captured_world_id = captured->GameWorld()->GetId();
    m_skill3_captured_id = captured->m_id;
    m_skill3_captured_generation = captured->m_generation;
    m_skill3_captured_prev_skip_tick = captured->m_skip_world_tick;
    m_skill3_has_captured_prev_skip_tick = true;
    captured->m_skip_world_tick = true;
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
    sf::Vector2f pos = origin + rear_direction * (mob->m_radius * game_config::mob_hornet_rear_spawn_radius_multiplier);
    auto missile =
        std::make_unique<CMissile>(mob->GameWorld(), pos, radius, rear_direction, HornetMissileSpeed(missile_rarity),
                                   damage, health, game_config::mob_hornet_skill2_missile_lifetime, mob);
    missile->m_team = mob->m_team;
    missile->m_mass = game_config::MobProjectileMassForLevel(GetLevel(missile_rarity));
    return mob->GameWorld()->InsertEntity(std::move(missile)) != nullptr;
}

void CSpecialHornetController::FinishCurrentAction(CMobBase* mob)
{
    CGameWorld* world = mob ? mob->GameWorld() : nullptr;
    if (m_skill3_has_captured_prev_skip_tick && world && world->GetId() == m_skill3_captured_world_id &&
        m_skill3_captured_id >= 0)
    {
        if (CEntity* captured = world->GetEntity(m_skill3_captured_id, m_skill3_captured_generation))
            captured->m_skip_world_tick = m_skill3_captured_prev_skip_tick;
    }
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
    m_skill3_captured_world_id = 0;
    m_skill3_captured_id = -1;
    m_skill3_captured_generation = 0;
    m_skill3_captured_prev_skip_tick = false;
    m_skill3_has_captured_prev_skip_tick = false;
    m_skill3_launch_direction = { 1.f, 0.f };
    if (m_skill3_missile_suppressed)
    {
        if (auto* hornet = dynamic_cast<IHornetMob*>(mob)) hornet->SetMissileGenerationSuppressed(false, false);
        m_skill3_missile_suppressed = false;
    }
    if (mob) mob->m_vel *= game_config::mob_stop_damping;
}

CEntity* CSpecialHornetController::FindHighestHornetInRange(CMobBase* mob, float range)
{
    if (!mob || !mob->GameWorld() || range <= 0.f) return nullptr;

    CEntity* best = nullptr;
    int best_rank = 0;
    float best_dist_sq = std::numeric_limits<float>::max();
    float range_sq = range * range;
    int caster_rank = GetRarityValueRank(mob->GetRarity());
    mob->GameWorld()->GetSpatialGrid().ForEachInRange(mob->m_pos, range, [&](CEntity* candidate) {
        auto* hornet = dynamic_cast<CMobBase*>(candidate);
        if (!hornet || hornet == mob || hornet->GetMobType() != EMobType::Hornet) return;
        if (hornet->m_is_marked_for_des || hornet->IsDead()) return;
        int rank = GetRarityValueRank(hornet->GetRarity());
        if (rank == caster_rank) return;
        if (rank < GetRarityValueRank(ERarity::Ultra)) return;
        float dist_sq = DistanceSq(mob->m_pos, hornet->m_pos);
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
    mob->GameWorld()->GetSpatialGrid().ForEachInRange(mob->m_pos, range, [&](CEntity* candidate) {
        const auto* player = dynamic_cast<const CPlayerFlower*>(candidate);
        if (!player || player->m_is_marked_for_des || player->IsDead()) return;
        if (DistanceSq(mob->m_pos, player->m_pos) <= range_sq) ++count;
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
        if (hornet->m_is_marked_for_des || hornet->IsDead()) return;
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
    float radius = std::max(1.f, mob->m_radius * game_config::mob_bumblebee_pollen_radius_multiplier);

    auto pollen = std::make_unique<CPollenProjectile>(mob->GameWorld(), mob->m_pos, radius, damage, health,
                                                      game_config::mob_bumblebee_pollen_lifetime, mass, mob);
    pollen->m_team = mob->m_team;
    mob->GameWorld()->InsertEntity(std::move(pollen));
}

void CBumbleBeeController::SetHoneyTarget(CEntity* target)
{
    m_p_honey_target = target;
    m_honey_target_world_id = target && target->GameWorld() ? target->GameWorld()->GetId() : 0;
    m_honey_target_id = target ? target->m_id : -1;
    m_honey_target_generation = target ? target->m_generation : 0;
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
        m_heading = mob->m_has_facing ? mob->m_facing_angle : GetLimitedRng(-game_config::pi, game_config::pi);
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
        mob->MoveTowards(honey_target->m_pos, dt);
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
            mob->m_pos +
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
