#include "melee_controller.h"
#include "../entities/petals/petal.h"
#include "../entities/projectile.h"
#include "../gameworld.h"
#include "../state_zone.h"
#include "../states/states.h"
#include "../../../Shared/game_config.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <memory>

namespace
{
constexpr float melee_target_los_recheck_interval = 0.25f;
constexpr size_t melee_target_los_candidate_limit = 8;
constexpr int honey_target_scan_interval_ticks = 16;

struct melee_target_candidate
{
    float dist_sq = std::numeric_limits<float>::max();
    CEntity* target = nullptr;
};

bool IsMeleeTargetBlockedByWall(CMobBase* mob, const CEntity* target);

float TargetEdgeDistanceSq(const CEntity* source, const CEntity* target)
{
    if (!source || !target) return std::numeric_limits<float>::max();
    float center_distance = Distance(source->m_pos, target->m_pos);
    float edge_distance = std::max(0.f, center_distance - std::max(0.f, source->m_radius) - std::max(0.f, target->m_radius));
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
    int level = rarity == ERarity::Exotic ? GetLevel(ERarity::Ultra) : GetLevel(rarity);
    return game_config::mob_hornet_missile_speed * (1.f + 0.05f * static_cast<float>(level));
}

float HornetSkill2AngularSpeed(float speed, float orbit_radius)
{
    float angular_speed = speed / std::max(game_config::entity_collision_epsilon, orbit_radius);
    if (game_config::mob_hornet_skill2_max_duration > game_config::entity_collision_epsilon)
    {
        float capped_speed = game_config::pi * 5.f / game_config::mob_hornet_skill2_max_duration;
        angular_speed = std::max(angular_speed, capped_speed);
    }
    return angular_speed;
}

int TargetScanIntervalTicks()
{
    return std::max(1, game_config::melee_target_scan_ticks);
}

bool ShouldRunEntityTargetScan(int& cooldown, const CEntity* entity, int interval)
{
    interval = std::max(1, interval);
    if (interval <= 1)
    {
        cooldown = 0;
        return true;
    }

    if (cooldown < 0)
        cooldown = entity ? std::max(0, entity->m_id) % interval : 0;

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

void AddMeleeTargetCandidate(std::array<melee_target_candidate, melee_target_los_candidate_limit>& candidates,
                             CEntity* target, float dist_sq)
{
    if (!target || dist_sq >= candidates.back().dist_sq) return;

    candidates.back() = {dist_sq, target};
    for (size_t i = candidates.size() - 1; i > 0 && candidates[i].dist_sq < candidates[i - 1].dist_sq; --i)
        std::swap(candidates[i], candidates[i - 1]);
}

CEntity* FirstVisibleMeleeTarget(CMobBase* mob,
                                 const std::array<melee_target_candidate, melee_target_los_candidate_limit>& candidates)
{
    for (const melee_target_candidate& candidate : candidates)
    {
        if (!candidate.target) break;
        if (!IsMeleeTargetBlockedByWall(mob, candidate.target)) return candidate.target;
    }
    return nullptr;
}

bool IsInvalidMeleeTarget(const CMobBase* mob, const CEntity* target, int ignored_id = -1, int ignored_owner_id = -1)
{
    if (!mob || !target) return true;
    if (target->m_is_marked_for_des || target->IsDead() || !target->CanCollide()) return true;
    if (IsDiggingEntity(target)) return true;
    if (target == mob) return true;
    if (target->m_id == ignored_id || target->m_id == ignored_owner_id) return true;
    if (CheckTeam(target->m_team, mob->m_team)) return true;
    if ((target->m_team == 0 || mob->m_team == 0) && ShareRootOwner(mob, target)) return true;
    if (BlocksNullifiedInteraction(mob, target)) return true;
    if (dynamic_cast<const CProjectile*>(target)) return true;
    return false;
}

bool IsValidHoneyTarget(const CMobBase* mob, const CEntity* target, int ignored_id = -1, int ignored_owner_id = -1)
{
    if (!mob || !target) return false;
    if (target->m_is_marked_for_des || target->IsDead() || !target->CanCollide()) return false;
    if (IsDiggingEntity(target)) return false;
    if (target == mob) return false;
    if (target->m_id == ignored_id || target->m_id == ignored_owner_id) return false;
    if (CheckTeam(target->m_team, mob->m_team)) return false;
    if ((target->m_team == 0 || mob->m_team == 0) && ShareRootOwner(mob, target)) return false;
    if (BlocksNullifiedInteraction(mob, target)) return false;

    auto* petal = dynamic_cast<const CPetal*>(target);
    if (!petal || petal->m_type != EPetalType::Honey) return false;

    int honey_level = std::max(GetLevel(ERarity::Common), GetLevel(petal->m_rarity));
    int max_attracted_level = std::max(GetLevel(ERarity::Common), honey_level - 1);
    return GetLevel(mob->GetRarity()) <= max_attracted_level;
}

bool IsMeleeTargetBlockedByWall(CMobBase* mob, const CEntity* target)
{
    if (!mob || !target || !mob->GameWorld()) return false;
    if (IsDiggingEntity(target)) return true;
    return mob->GameWorld()->SegmentBlockedByWall(mob->m_pos, target->m_pos);
}

bool IsUnavailableMeleeTarget(CMobBase* mob, const CEntity* target, int ignored_id = -1, int ignored_owner_id = -1)
{
    if (IsValidHoneyTarget(mob, target, ignored_id, ignored_owner_id))
        return IsMeleeTargetBlockedByWall(mob, target);
    return IsInvalidMeleeTarget(mob, target, ignored_id, ignored_owner_id) ||
           IsMeleeTargetBlockedByWall(mob, target);
}

bool IsCurrentMeleeTargetUnavailable(CMobBase* mob, const CEntity* target, float dt, float& los_check_timer,
                                     int ignored_id = -1, int ignored_owner_id = -1)
{
    if (IsDiggingEntity(target)) return true;

    if (IsValidHoneyTarget(mob, target, ignored_id, ignored_owner_id))
    {
        los_check_timer += dt;
        if (los_check_timer < melee_target_los_recheck_interval) return false;
        los_check_timer = 0.f;
        return IsMeleeTargetBlockedByWall(mob, target);
    }

    if (IsInvalidMeleeTarget(mob, target, ignored_id, ignored_owner_id)) return true;

    los_check_timer += dt;
    if (los_check_timer < melee_target_los_recheck_interval) return false;
    los_check_timer = 0.f;
    return IsMeleeTargetBlockedByWall(mob, target);
}

CEntity* FindClosestHoneyTarget(CMobBase* mob, float search_range, int ignored_id = -1, int ignored_owner_id = -1)
{
    if (!mob || !mob->GameWorld()) return nullptr;
    if (search_range <= 0.f) return nullptr;

    float search_range_sq = search_range * search_range;
    float query_range = TargetSearchQueryRange(mob, search_range);
    std::array<melee_target_candidate, melee_target_los_candidate_limit> candidates;
    mob->GameWorld()->GetSpatialGrid().ForEachInRange(mob->m_pos, query_range, [&](CEntity* candidate)
    {
        if (!IsValidHoneyTarget(mob, candidate, ignored_id, ignored_owner_id)) return;
        float dist_sq = TargetEdgeDistanceSq(mob, candidate);
        if (dist_sq > search_range_sq) return;
        AddMeleeTargetCandidate(candidates, candidate, dist_sq);
    });
    return FirstVisibleMeleeTarget(mob, candidates);
}

CEntity* FindClosestMeleeTarget(CMobBase* mob, float search_range, int ignored_id = -1, int ignored_owner_id = -1)
{
    if (!mob || !mob->GameWorld() || search_range <= 0.f) return nullptr;

    float search_range_sq = search_range * search_range;
    float query_range = TargetSearchQueryRange(mob, search_range);
    std::array<melee_target_candidate, melee_target_los_candidate_limit> honey_candidates;
    std::array<melee_target_candidate, melee_target_los_candidate_limit> target_candidates;
    mob->GameWorld()->GetSpatialGrid().ForEachInRange(mob->m_pos, query_range, [&](CEntity* candidate)
    {
        float dist_sq = TargetEdgeDistanceSq(mob, candidate);
        if (dist_sq > search_range_sq) return;

        if (IsValidHoneyTarget(mob, candidate, ignored_id, ignored_owner_id))
        {
            AddMeleeTargetCandidate(honey_candidates, candidate, dist_sq);
            return;
        }

        if (IsInvalidMeleeTarget(mob, candidate, ignored_id, ignored_owner_id)) return;

        float detection_multiplier = 1.f;
        if (auto* flower = dynamic_cast<const CFlower*>(candidate)) detection_multiplier = flower->m_final_stats.detection_multiplier;
        float range = search_range * detection_multiplier;
        if (dist_sq > range * range) return;

        AddMeleeTargetCandidate(target_candidates, candidate, dist_sq);
    });
    if (CEntity* honey = FirstVisibleMeleeTarget(mob, honey_candidates)) return honey;
    return FirstVisibleMeleeTarget(mob, target_candidates);
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
    m_target_pos = min_pos + sf::Vector2f(GetLimitedRng(0.f, half_range * 2.f), GetLimitedRng(0.f, half_range * 2.f));
    m_has_random_target_pos = true;
}

bool CMeleeController::IsRandomIdleDone(float dt)
{
    if (!m_random_idle) return false;
    m_random_idle_timer += dt;
    return m_random_idle_timer >= game_config::melee_random_idle_time;
}

void CMeleeController::SetTarget(CEntity* target)
{
    m_p_target = target;
    m_target_id = target ? target->m_id : -1;
    m_target_generation = target ? target->m_generation : 0;
}

void CMeleeController::ClearTarget()
{
    SetTarget(nullptr);
}

CEntity* CMeleeController::ResolveTarget(CMobBase* mob)
{
    if (!m_p_target && m_target_id < 0) return nullptr;
    CGameWorld* world = mob ? mob->GameWorld() : nullptr;
    CEntity* resolved = world && m_target_id >= 0 ? world->GetEntity(m_target_id, m_target_generation) : nullptr;
    if (!resolved)
    {
        ClearTarget();
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
    if (!ShouldRunEntityTargetScan(m_honey_target_scan_cooldown, mob, honey_target_scan_interval_ticks)) return false;

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

    bool target_invalid = target &&
                          IsCurrentMeleeTargetUnavailable(mob, target, dt, m_target_los_check_timer);
    if (target_invalid)
    {
        ClearTarget();
        target = nullptr;
    }
    if (TryAcquireHoneyTarget(mob, stats->horizon))
    {
        target = ResolveTarget(mob);
    }
    bool reached_random_target = !target && m_has_random_target_pos &&
                                 (m_random_idle ? IsRandomIdleDone(dt) :
                                  DistanceSq(mob->m_pos, m_target_pos) <= mob->m_radius * mob->m_radius);
    if (!target && m_has_random_target_pos && !reached_random_target && TryAcquireWanderTarget(mob, stats->horizon))
        target = ResolveTarget(mob);

    bool can_random_retarget = !target && m_change_target_count >= game_config::melee_target_time;
    float retarget_chance = game_config::melee_retarget_chance_multiplier * m_change_target_count * dt /
                            (game_config::melee_target_time * game_config::melee_target_time);
    retarget_chance = std::clamp(retarget_chance, 0.f, 1.f);
    bool should_retarget =
        target_invalid || !m_has_random_target_pos || reached_random_target || (can_random_retarget && CheckChance(retarget_chance));
    if (should_retarget)
    {
        m_change_target_count = 0.f;
        m_target_los_check_timer = 0.f;
        m_has_random_target_pos = false;
        m_random_idle = false;
        m_random_idle_timer = 0.f;

        if (TryAcquireWanderTarget(mob, stats->horizon))
            target = ResolveTarget(mob);
        else
            PickRandomTargetPos(mob, *stats);
    }

    if ((target = ResolveTarget(mob))) m_target_pos = target->m_pos;
    mob->MoveTowards(m_target_pos, dt);
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

    CEntity* owner = GetOwner(mob->GameWorld());
    if (!owner || owner->m_is_marked_for_des)
    {
        if (m_persist_after_owner_death)
        {
            SetTarget(nullptr);
            CMeleeController::OnTick(mob, dt);
            return;
        }
        mob->m_is_marked_for_des = true;
        return;
    }

    float owner_dist_sq = DistanceSq(mob->m_pos, owner->m_pos);
    float range_scale = std::max(1.f, mob->m_radius / std::max(1.f, game_config::mob_summoned_beetle_radius));
    float hard_range = game_config::mob_summoned_beetle_hard_range * range_scale;
    if (owner_dist_sq > hard_range * hard_range)
    {
        mob->m_is_marked_for_des = true;
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

    bool target_invalid = target &&
                          IsCurrentMeleeTargetUnavailable(mob, target, dt, m_target_los_check_timer,
                                                          m_owner_id, owner_owner_id);
    float search_range = stats->horizon * game_config::mob_summoned_search_range_multiplier;
    if (target_invalid)
    {
        ClearTarget();
        target = nullptr;
    }
    if (TryAcquireHoneyTarget(mob, search_range, owner->m_id, owner_owner_id))
    {
        target = ResolveTarget(mob);
        target_invalid = false;
    }
    bool reached_random_target = !target && m_has_random_target_pos &&
                                 (m_random_idle ? IsRandomIdleDone(dt) :
                                  DistanceSq(mob->m_pos, m_target_pos) <= mob->m_radius * mob->m_radius);
    if (!target && m_has_random_target_pos && !reached_random_target &&
        TryAcquireWanderTarget(mob, search_range, owner->m_id, owner_owner_id))
        target = ResolveTarget(mob);

    bool can_random_retarget = !target && m_change_target_count >= game_config::melee_target_time;
    float retarget_chance = game_config::melee_retarget_chance_multiplier * m_change_target_count * dt /
                            (game_config::melee_target_time * game_config::melee_target_time);
    retarget_chance = std::clamp(retarget_chance, 0.f, 1.f);

    if (target_invalid || !m_has_random_target_pos || reached_random_target || (can_random_retarget && CheckChance(retarget_chance)))
    {
        m_change_target_count = 0.f;
        m_target_los_check_timer = 0.f;
        m_has_random_target_pos = false;
        m_random_idle = false;
        m_random_idle_timer = 0.f;

        if (TryAcquireWanderTarget(mob, search_range, owner->m_id, owner_owner_id))
            target = ResolveTarget(mob);
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

    m_change_target_count += dt;
    CEntity* target = ResolveTarget(mob);

    if (target && IsCurrentMeleeTargetUnavailable(mob, target, dt, m_target_los_check_timer))
    {
        ClearTarget();
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

    bool reached_random_target = m_has_random_target_pos &&
                                 (m_random_idle ? IsRandomIdleDone(dt) :
                                  DistanceSq(mob->m_pos, m_target_pos) <= mob->m_radius * mob->m_radius);

    if (!target && (!m_has_random_target_pos || reached_random_target || m_change_target_count >= game_config::melee_target_time))
    {
        m_change_target_count = 0.f;
        if (!m_has_random_target_pos || reached_random_target)
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

// ============ Random Wander ============

void CRandomWanderController::OnTick(CMobBase* mob, float dt)
{
    if (!mob || !mob->GameWorld()) return;

    const SMobStats* stats = mob->GetFinalStats();
    if (!stats) return;

    m_change_target_count += dt;
    CEntity* target = ResolveTarget(mob);

    if (target && IsCurrentMeleeTargetUnavailable(mob, target, dt, m_target_los_check_timer))
    {
        ClearTarget();
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

    bool reached_random_target = m_has_random_target_pos &&
                                 (m_random_idle ? IsRandomIdleDone(dt) :
                                  DistanceSq(mob->m_pos, m_target_pos) <= mob->m_radius * mob->m_radius);

    if (!target && (!m_has_random_target_pos || reached_random_target || m_change_target_count >= game_config::melee_target_time))
    {
        m_change_target_count = 0.f;
        if (!m_has_random_target_pos || reached_random_target)
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

    float attack_range = std::max(mob->m_radius * 4.f, stats->horizon);
    if (DistanceSq(mob->m_pos, target->m_pos) > attack_range * attack_range) return;

    if (auto* attackable = dynamic_cast<IAttackableMob*>(mob))
    {
        if (attackable->TryAttack(target))
            mob->m_vel = {0.f, 0.f};
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

    auto web = std::make_unique<CSpiderWebZone>(
        mob->GameWorld(), mob->m_pos, mob->m_radius, mob,
        game_config::mob_spider_web_lifetime, game_config::mob_spider_web_speed_multiplier);
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

    bool target_invalid = target &&
                          IsCurrentMeleeTargetUnavailable(mob, target, dt, m_target_los_check_timer);
    if (target_invalid)
    {
        ClearTarget();
        target = nullptr;
    }
    if (TryAcquireHoneyTarget(mob, stats->horizon))
    {
        target = ResolveTarget(mob);
        target_invalid = false;
    }
    bool reached_random_target = !target && m_has_random_target_pos &&
                                 (m_random_idle ? IsRandomIdleDone(dt) :
                                  DistanceSq(mob->m_pos, m_target_pos) <= mob->m_radius * mob->m_radius);
    if (!target && m_has_random_target_pos && !reached_random_target && TryAcquireWanderTarget(mob, stats->horizon))
        target = ResolveTarget(mob);

    bool can_random_retarget = !target && m_change_target_count >= game_config::melee_target_time;
    float retarget_chance = game_config::melee_retarget_chance_multiplier * m_change_target_count * dt /
                            (game_config::melee_target_time * game_config::melee_target_time);
    retarget_chance = std::clamp(retarget_chance, 0.f, 1.f);

    bool should_retarget =
        target_invalid || !m_has_random_target_pos || reached_random_target || (can_random_retarget && CheckChance(retarget_chance));
    if (should_retarget)
    {
        m_change_target_count = 0.f;
        m_target_los_check_timer = 0.f;
        m_has_random_target_pos = false;
        m_random_idle = false;
        m_random_idle_timer = 0.f;

        if (TryAcquireWanderTarget(mob, stats->horizon))
            target = ResolveTarget(mob);
        else
            PickRandomTargetPos(mob, *stats);
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
    float stop_distance = 128.f + mob->m_radius;
    if (target_dist <= stop_distance)
        mob->MoveTowards(mob->m_pos, dt);
    else
        mob->MoveTowards(m_target_pos, dt);

    float missile_range = HornetMissileSpeed(mob->GetRarity()) * game_config::default_missile_lifetime;
    if (TargetEdgeDistanceSq(mob, target) <= missile_range * missile_range * 0.25f)
    {
        if (auto* skill_caster = dynamic_cast<ISkillCasterMob*>(mob))
            skill_caster->TryCastSkill(0, target);
    }
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
            float speed = stats ? stats->max_velocity * 2.f : game_config::mob_hornet_max_velocity * 2.f;
            float base_angle = mob->m_has_facing ? mob->m_facing_angle : 0.f;
            sf::Vector2f forward = {std::cos(base_angle), std::sin(base_angle)};
            if (LengthSq(forward) <= game_config::entity_collision_epsilon * game_config::entity_collision_epsilon)
                forward = {1.f, 0.f};
            sf::Vector2f left = {-forward.y, forward.x};
            float side = CheckChance(0.5) ? -1.f : 1.f;
            m_skill2_orbit_radius = std::max(mob->m_radius * 3.f, game_config::entity_collision_epsilon);
            m_skill2_orbit_center = mob->m_pos + left * (side * m_skill2_orbit_radius);
            m_skill2_orbit_angle = std::atan2(mob->m_pos.y - m_skill2_orbit_center.y,
                                              mob->m_pos.x - m_skill2_orbit_center.x);
            m_skill2_orbit_dir = side;
            m_skill2_remaining_angle = game_config::pi * 5.f;
            m_skill2_tangent_dir = {-std::sin(m_skill2_orbit_angle) * m_skill2_orbit_dir,
                                     std::cos(m_skill2_orbit_angle) * m_skill2_orbit_dir};
            mob->m_vel = {0.f, 0.f};
            mob->m_facing_angle = std::atan2(m_skill2_tangent_dir.y, m_skill2_tangent_dir.x);
            mob->m_has_facing = true;
            m_state = EState::Skill2Dash;
            float angular_speed = HornetSkill2AngularSpeed(speed, m_skill2_orbit_radius);
            m_state_timer = m_skill2_remaining_angle / std::max(game_config::entity_collision_epsilon, angular_speed);
            m_action_timer = m_state_timer;
            m_fire_timer = 0.f;
        }
        return;
    case EState::Skill2Dash:
        {
            const SMobStats* stats = mob->GetFinalStats();
            float speed = stats ? stats->max_velocity * 2.f : game_config::mob_hornet_max_velocity * 2.f;
            float angular_speed = HornetSkill2AngularSpeed(speed, m_skill2_orbit_radius);
            float step = std::min(m_skill2_remaining_angle, angular_speed * dt);
            sf::Vector2f current_radial = {std::cos(m_skill2_orbit_angle), std::sin(m_skill2_orbit_angle)};
            sf::Vector2f expected_pos = m_skill2_orbit_center + current_radial * m_skill2_orbit_radius;
            sf::Vector2f drift = mob->m_pos - expected_pos;
            if (LengthSq(drift) > game_config::entity_collision_epsilon * game_config::entity_collision_epsilon)
                m_skill2_orbit_center += drift;

            sf::Vector2f origin = mob->m_pos;
            m_skill2_orbit_angle += m_skill2_orbit_dir * step;
            m_skill2_remaining_angle = std::max(0.f, m_skill2_remaining_angle - step);

            sf::Vector2f radial = {std::cos(m_skill2_orbit_angle), std::sin(m_skill2_orbit_angle)};
            m_skill2_tangent_dir = {-radial.y * m_skill2_orbit_dir, radial.x * m_skill2_orbit_dir};
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
            } else {
                m_state_timer = std::max(0.f, m_state_timer - dt);
            }
        }
        return;
    case EState::Skill2Pause:
        m_state_timer = std::max(0.f, m_state_timer - dt);
        mob->m_vel = {0.f, 0.f};
        if (m_state_timer <= 0.f) FinishCurrentAction(mob);
        return;
    case EState::Skill3Charge:
        {
            CGameWorld* world = mob->GameWorld();
            CEntity* target = world && m_skill3_target_id >= 0 ?
                world->GetEntity(m_skill3_target_id, m_skill3_target_generation) : nullptr;
            auto* captured = world && m_skill3_captured_id >= 0 ?
                dynamic_cast<CMobBase*>(world->GetEntity(m_skill3_captured_id, m_skill3_captured_generation)) : nullptr;
            if (!target || target->m_is_marked_for_des || target->IsDead() ||
                !captured || captured->m_is_marked_for_des || captured->IsDead())
            {
                FinishCurrentAction(mob);
                return;
            }

            sf::Vector2f mount_dir = {std::cos(mob->m_facing_angle), std::sin(mob->m_facing_angle)};
            if (LengthSq(mount_dir) <= game_config::entity_collision_epsilon * game_config::entity_collision_epsilon)
                mount_dir = {1.f, 0.f};
            m_skill3_launch_pos = mob->m_pos + mount_dir * (mob->m_radius * game_config::mob_hornet_missile_attach_offset);
            captured->m_pos = m_skill3_launch_pos;
            captured->m_prev_pos = m_skill3_launch_pos;
            captured->m_vel = {0.f, 0.f};
            captured->m_facing_angle = std::atan2(mount_dir.y, mount_dir.x);
            captured->m_has_facing = true;

            m_state_timer = std::max(0.f, m_state_timer - dt);
            if (m_state_timer <= 0.f)
            {
                sf::Vector2f direction = target->m_pos - mob->m_pos;
                if (LengthSq(direction) <= game_config::entity_collision_epsilon * game_config::entity_collision_epsilon)
                    direction = mount_dir;
                else
                    direction /= Length(direction);

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
    if (TargetEdgeDistanceSq(mob, target) > missile_range * missile_range * 0.25f) return;

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

    if (!phase_skill_attempted && CheckChance(0.10) && TryStartSkill3(mob))
    {
        ++m_cycle_attack_count;
        return;
    }

    if (skill_caster->TryCastSkill(0, target))
    {
        if (phase_skill_attempted)
        {
            m_cycle_attack_count = 0;
            m_cycle_phase = 1 - m_cycle_phase;
        } else {
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

    bool target_invalid = target &&
                          IsCurrentMeleeTargetUnavailable(mob, target, dt, m_target_los_check_timer);
    if (target_invalid)
    {
        ClearTarget();
        target = nullptr;
    }
    if (TryAcquireHoneyTarget(mob, stats->horizon))
    {
        target = ResolveTarget(mob);
        target_invalid = false;
    }
    bool reached_random_target = !target && m_has_random_target_pos &&
                                 (m_random_idle ? IsRandomIdleDone(dt) :
                                  DistanceSq(mob->m_pos, m_target_pos) <= mob->m_radius * mob->m_radius);
    if (!target && m_has_random_target_pos && !reached_random_target && TryAcquireWanderTarget(mob, stats->horizon))
        target = ResolveTarget(mob);

    bool can_random_retarget = !target && m_change_target_count >= game_config::melee_target_time;
    float retarget_chance = game_config::melee_retarget_chance_multiplier * m_change_target_count * dt /
                            (game_config::melee_target_time * game_config::melee_target_time);
    retarget_chance = std::clamp(retarget_chance, 0.f, 1.f);

    bool should_retarget =
        target_invalid || !m_has_random_target_pos || reached_random_target || (can_random_retarget && CheckChance(retarget_chance));
    if (should_retarget)
    {
        m_change_target_count = 0.f;
        m_target_los_check_timer = 0.f;
        m_has_random_target_pos = false;
        m_random_idle = false;
        m_random_idle_timer = 0.f;

        if (TryAcquireWanderTarget(mob, stats->horizon))
            target = ResolveTarget(mob);
        else
            PickRandomTargetPos(mob, *stats);
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
    float stop_distance = 128.f + mob->m_radius;
    if (target_dist <= stop_distance)
        mob->MoveTowards(mob->m_pos, dt);
    else
        mob->MoveTowards(m_target_pos, dt);
}

bool CSpecialHornetController::TryStartSkill1(CMobBase* mob)
{
    if (!mob || !mob->GameWorld()) return false;
    if (CountNearbyPlayers(mob, game_config::mob_hornet_skill1_view_range) <= 10) return false;

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
    int summon_count = std::max(1, static_cast<int>(std::floor(static_cast<float>(nearby) / 5.f + 0.5f)));
    int summon_cap = game_config::mob_hornet_skill1_summon_cap_super;
    if (mob->GetRarity() == ERarity::Primordial)
        summon_cap = game_config::mob_hornet_skill1_summon_cap_primordial;
    else if (mob->GetRarity() == ERarity::Eternal || mob->GetRarity() == ERarity::Unique)
        summon_cap = game_config::mob_hornet_skill1_summon_cap_eternal;
    summon_count = std::min(summon_count, std::max(0, summon_cap));
    int et_limit = mob->GetRarity() == ERarity::Primordial ?
        std::max(0, game_config::mob_hornet_skill1_et_cap - CountExistingHornets(mob->GameWorld(), ERarity::Eternal)) : 0;
    int s_cap = mob->GetRarity() == ERarity::Primordial ? game_config::mob_hornet_skill1_super_cap : 1;
    int s_limit = std::max(0, s_cap - CountExistingHornets(mob->GameWorld(), ERarity::Super));

    for (int i = 0; i < summon_count; ++i)
    {
        ERarity rarity = PickSkill1SummonRarity(mob->GetRarity(), et_limit, s_limit);
        constexpr int spawn_attempts = 16;
        for (int attempt = 0; attempt < spawn_attempts; ++attempt)
        {
            float angle = GetLimitedRng(-game_config::pi, game_config::pi);
            float distance = GetLimitedRng(mob->m_radius * 1.4f, std::max(mob->m_radius * 1.5f, mob->m_radius * 4.f));
            sf::Vector2f pos = mob->m_pos + sf::Vector2f(std::cos(angle), std::sin(angle)) * distance;
            auto hornet = CreateMob(EMobType::Hornet, mob->GameWorld(), pos, rarity);
            if (!hornet) continue;
            hornet->m_team = mob->m_team;
            if (mob->GameWorld()->CircleBlockedByWall(pos, hornet->m_radius)) continue;
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

    CEntity* captured = FindHighestHornetInRange(mob, mob->m_radius * 2.f);
    if (!captured) return false;

    m_skill3_target_id = target->m_id;
    m_skill3_target_generation = target->m_generation;
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
        direction = {1.f, 0.f};

    sf::Vector2f rear_direction = -direction;
    sf::Vector2f pos = origin + rear_direction * (mob->m_radius * 0.5f);
    auto missile = std::make_unique<CMissile>(mob->GameWorld(), pos, radius, rear_direction,
                                              HornetMissileSpeed(missile_rarity), damage, health,
                                              game_config::mob_hornet_skill2_missile_lifetime, mob);
    missile->m_team = mob->m_team;
    return mob->GameWorld()->InsertEntity(std::move(missile)) != nullptr;
}

void CSpecialHornetController::FinishCurrentAction(CMobBase* mob)
{
    CGameWorld* world = mob ? mob->GameWorld() : nullptr;
    if (m_skill3_has_captured_prev_skip_tick && world && m_skill3_captured_id >= 0)
    {
        if (CEntity* captured = world->GetEntity(m_skill3_captured_id, m_skill3_captured_generation))
            captured->m_skip_world_tick = m_skill3_captured_prev_skip_tick;
    }
    m_state = EState::Idle;
    m_state_timer = 0.f;
    m_action_timer = 0.f;
    m_fire_timer = 0.f;
    m_skill2_orbit_center = {0.f, 0.f};
    m_skill2_orbit_radius = 0.f;
    m_skill2_orbit_angle = 0.f;
    m_skill2_orbit_dir = 1.f;
    m_skill2_remaining_angle = 0.f;
    m_skill2_tangent_dir = {1.f, 0.f};
    m_skill3_target_id = -1;
    m_skill3_target_generation = 0;
    m_skill3_captured_id = -1;
    m_skill3_captured_generation = 0;
    m_skill3_captured_prev_skip_tick = false;
    m_skill3_has_captured_prev_skip_tick = false;
    if (mob) mob->m_vel *= game_config::mob_stop_damping;
}

CEntity* CSpecialHornetController::FindHighestHornetInRange(CMobBase* mob, float range)
{
    if (!mob || !mob->GameWorld() || range <= 0.f) return nullptr;

    CEntity* best = nullptr;
    float best_rank = 0.f;
    float best_dist_sq = std::numeric_limits<float>::max();
    float range_sq = range * range;
    float caster_rank = GetRaritySortRank(mob->GetRarity());
    mob->GameWorld()->GetSpatialGrid().ForEachInRange(mob->m_pos, range, [&](CEntity* candidate)
    {
        auto* hornet = dynamic_cast<CMobBase*>(candidate);
        if (!hornet || hornet == mob || hornet->m_mob_type != EMobType::Hornet) return;
        if (hornet->m_is_marked_for_des || hornet->IsDead()) return;
        float rank = GetRaritySortRank(hornet->GetRarity());
        if (rank == caster_rank) return;
        if (!IsAtLeastRarity(hornet->GetRarity(), ERarity::Ultra)) return;
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
    mob->GameWorld()->GetSpatialGrid().ForEachInRange(mob->m_pos, range, [&](CEntity* candidate)
    {
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
    world->ForEachEntity([&](CEntity* entity)
    {
        const auto* hornet = dynamic_cast<const CMobBase*>(entity);
        if (!hornet || hornet->m_mob_type != EMobType::Hornet) return;
        if (hornet->m_is_marked_for_des || hornet->IsDead()) return;
        if (hornet->GetRarity() == rarity) ++count;
    });
    return count;
}

ERarity CSpecialHornetController::PrevHornetRarity(ERarity rarity)
{
    if (rarity == ERarity::Primordial) return ERarity::Eternal;
    if (rarity == ERarity::Unique || rarity == ERarity::Eternal) return ERarity::Super;
    if (rarity == ERarity::Super || rarity == ERarity::Exotic) return ERarity::Ultra;
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
        if (CheckChance(0.08)) rarity = ERarity::Eternal;
        else if (CheckChance(0.32 / 0.92)) rarity = ERarity::Super;
    } else if (caster_rarity == ERarity::Eternal || caster_rarity == ERarity::Unique) {
        if (CheckChance(0.08)) rarity = ERarity::Super;
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
           std::pow(3.f, static_cast<float>(GetLevel(rarity) - 1));
}

float CSpecialHornetController::HornetMissileHealth(ERarity rarity)
{
    return game_config::mob_hornet_missile_base_health *
           std::pow(5.f, static_cast<float>(GetLevel(rarity) - 1));
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
    float damage = game_config::mob_bumblebee_pollen_base_damage * std::pow(3.f, static_cast<float>(level - 1));
    float health = game_config::mob_bumblebee_pollen_base_health * std::pow(5.f, static_cast<float>(level - 1));
    float mass = std::pow(2.f, static_cast<float>(level - 1));
    float radius = std::max(1.f, mob->m_radius * game_config::mob_bumblebee_pollen_radius_multiplier);

    auto pollen = std::make_unique<CPollenProjectile>(mob->GameWorld(), mob->m_pos, radius, damage, health,
                                                      game_config::mob_bumblebee_pollen_lifetime, mass, mob);
    pollen->m_team = mob->m_team;
    mob->GameWorld()->InsertEntity(std::move(pollen));
}

void CBumbleBeeController::SetHoneyTarget(CEntity* target)
{
    m_p_honey_target = target;
    m_honey_target_id = target ? target->m_id : -1;
    m_honey_target_generation = target ? target->m_generation : 0;
}

void CBumbleBeeController::ClearHoneyTarget()
{
    SetHoneyTarget(nullptr);
}

CEntity* CBumbleBeeController::ResolveHoneyTarget(CMobBase* mob)
{
    if (!m_p_honey_target && m_honey_target_id < 0) return nullptr;
    CGameWorld* world = mob ? mob->GameWorld() : nullptr;
    CEntity* resolved = world && m_honey_target_id >= 0 ?
        world->GetEntity(m_honey_target_id, m_honey_target_generation) : nullptr;
    if (!resolved)
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
    if (honey_target &&
        IsCurrentMeleeTargetUnavailable(mob, honey_target, dt, m_honey_los_check_timer))
    {
        ClearHoneyTarget();
        honey_target = nullptr;
        m_honey_los_check_timer = 0.f;
    }
    if (!honey_target && ShouldRunEntityTargetScan(m_honey_target_scan_cooldown, mob, honey_target_scan_interval_ticks))
    {
        SetHoneyTarget(FindClosestHoneyTarget(mob, std::max(stats->horizon, game_config::default_honey_attract_range)));
        honey_target = ResolveHoneyTarget(mob);
    }

    if (honey_target)
    {
        FaceTarget(mob, honey_target);
        mob->MoveTowards(honey_target->m_pos, dt);
    } else {
        m_turn_timer -= dt;
        if (m_turn_timer <= 0.f)
        {
            float max_angle = std::max(0.f, game_config::mob_bumblebee_turn_max_angle);
            m_heading += GetLimitedRng(-max_angle, max_angle);
            PickTurnTimer();
        }

        m_wave_timer += dt;
        float wave = std::sin(m_wave_timer * game_config::mob_bee_wave_frequency) *
                     game_config::mob_bee_wave_strength * 0.55f;
        sf::Vector2f target = mob->m_pos + sf::Vector2f(std::cos(m_heading + wave), std::sin(m_heading + wave)) *
                              std::max(256.f, stats->max_velocity * 4.f);
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
