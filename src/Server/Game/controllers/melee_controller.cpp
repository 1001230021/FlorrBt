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

struct melee_target_candidate
{
    float dist_sq = std::numeric_limits<float>::max();
    CEntity* target = nullptr;
};

bool IsMeleeTargetBlockedByWall(CMobBase* mob, const CEntity* target);

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
    if (target == mob) return true;
    if (target->m_id == ignored_id || target->m_id == ignored_owner_id) return true;
    if (CheckTeam(target->m_team, mob->m_team)) return true;
    if (BlocksNullifiedInteraction(mob, target)) return true;
    if (dynamic_cast<const CProjectile*>(target)) return true;
    return false;
}

bool IsValidHoneyTarget(const CMobBase* mob, const CEntity* target, int ignored_id = -1, int ignored_owner_id = -1)
{
    if (!mob || !target) return false;
    if (target->m_is_marked_for_des || target->IsDead() || !target->CanCollide()) return false;
    if (target == mob) return false;
    if (target->m_id == ignored_id || target->m_id == ignored_owner_id) return false;
    if (CheckTeam(target->m_team, mob->m_team)) return false;
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
    std::array<melee_target_candidate, melee_target_los_candidate_limit> candidates;
    mob->GameWorld()->GetSpatialGrid().ForEachInRange(mob->m_pos, search_range, [&](CEntity* candidate)
    {
        if (!IsValidHoneyTarget(mob, candidate, ignored_id, ignored_owner_id)) return;
        float dist_sq = DistanceSq(mob->m_pos, candidate->m_pos);
        if (dist_sq > search_range_sq) return;
        AddMeleeTargetCandidate(candidates, candidate, dist_sq);
    });
    return FirstVisibleMeleeTarget(mob, candidates);
}

CEntity* FindClosestMeleeTarget(CMobBase* mob, float search_range, int ignored_id = -1, int ignored_owner_id = -1)
{
    if (!mob || !mob->GameWorld() || search_range <= 0.f) return nullptr;

    float search_range_sq = search_range * search_range;
    std::array<melee_target_candidate, melee_target_los_candidate_limit> honey_candidates;
    std::array<melee_target_candidate, melee_target_los_candidate_limit> target_candidates;
    mob->GameWorld()->GetSpatialGrid().ForEachInRange(mob->m_pos, search_range, [&](CEntity* candidate)
    {
        float dist_sq = DistanceSq(mob->m_pos, candidate->m_pos);
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

void CMeleeController::OnTick(CMobBase* mob, float dt)
{
    if (!mob || !mob->GameWorld()) return;

    const SMobStats* stats = mob->GetFinalStats();
    if (!stats) return;

    m_change_target_count += dt;

    bool target_invalid = m_p_target &&
                          IsCurrentMeleeTargetUnavailable(mob, m_p_target, dt, m_target_los_check_timer);
    bool reached_random_target = !m_p_target && m_has_random_target_pos &&
                                 (m_random_idle ? IsRandomIdleDone(dt) :
                                  DistanceSq(mob->m_pos, m_target_pos) <= mob->m_radius * mob->m_radius);
    bool can_random_retarget = !m_p_target && m_change_target_count >= game_config::melee_target_time;
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

        CEntity* p_best_target = FindClosestMeleeTarget(mob, stats->horizon);

        m_p_target = p_best_target;
        if (m_p_target)
        {
            m_target_pos = m_p_target->m_pos;
            m_has_random_target_pos = true;
            m_random_idle = false;
            m_random_idle_timer = 0.f;
        } else {
            PickRandomTargetPos(mob, *stats);
        }
    }

    if (m_p_target) m_target_pos = m_p_target->m_pos;
    mob->MoveTowards(m_target_pos, dt);
}

// ============ Summoned ============

void CSummonedMeleeController::OnTick(CMobBase* mob, float dt)
{
    if (!mob || !mob->GameWorld())
    {
        return;
    }

    CEntity* owner = mob->GameWorld()->GetEntity(m_owner_id);
    if (!owner || owner->m_is_marked_for_des)
    {
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
        m_p_target = owner;
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

    int owner_owner_id = -1;
    if (auto* owner_projectile = dynamic_cast<CProjectile*>(owner)) owner_owner_id = owner_projectile->m_owner_id;

    bool target_invalid = m_p_target &&
                          IsCurrentMeleeTargetUnavailable(mob, m_p_target, dt, m_target_los_check_timer,
                                                          m_owner_id, owner_owner_id);
    bool reached_random_target = !m_p_target && m_has_random_target_pos &&
                                 (m_random_idle ? IsRandomIdleDone(dt) :
                                  DistanceSq(mob->m_pos, m_target_pos) <= mob->m_radius * mob->m_radius);
    bool can_random_retarget = !m_p_target && m_change_target_count >= game_config::melee_target_time;
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

        float search_range = stats->horizon * game_config::mob_summoned_search_range_multiplier;
        CEntity* p_best_target = FindClosestMeleeTarget(mob, search_range, owner->m_id, owner_owner_id);

        m_p_target = p_best_target;
        if (m_p_target)
        {
            m_target_pos = m_p_target->m_pos;
            m_has_random_target_pos = true;
            m_random_idle = false;
            m_random_idle_timer = 0.f;
        } else {
            float wander_half_range = std::max(mob->m_radius * game_config::mob_summoned_wander_radius_multiplier,
                                               follow_range * game_config::mob_summoned_wander_follow_range_multiplier);
            PickRandomTargetPosNear(mob, owner->m_pos, wander_half_range);
        }
    }

    if (m_p_target) m_target_pos = m_p_target->m_pos;
    mob->MoveTowards(m_target_pos, dt);
}

// ============ Neutral ============

void CNeutralMeleeController::OnTick(CMobBase* mob, float dt)
{
    if (!mob || !mob->GameWorld()) return;

    const SMobStats* stats = mob->GetFinalStats();
    if (!stats) return;

    m_change_target_count += dt;

    if (m_p_target && IsCurrentMeleeTargetUnavailable(mob, m_p_target, dt, m_target_los_check_timer))
    {
        m_p_target = nullptr;
        m_target_los_check_timer = 0.f;
        m_has_random_target_pos = false;
        m_random_idle = false;
        m_random_idle_timer = 0.f;
    }

    if (m_p_target)
    {
        m_target_pos = m_p_target->m_pos;
        mob->MoveTowards(m_target_pos, dt);
        return;
    }

    bool reached_random_target = m_has_random_target_pos &&
                                 (m_random_idle ? IsRandomIdleDone(dt) :
                                  DistanceSq(mob->m_pos, m_target_pos) <= mob->m_radius * mob->m_radius);
    if (!m_has_random_target_pos || reached_random_target || m_change_target_count >= game_config::melee_target_time)
    {
        m_change_target_count = 0.f;
        if (CEntity* honey = FindClosestHoneyTarget(mob, stats->horizon))
        {
            m_p_target = honey;
            m_target_pos = honey->m_pos;
            m_has_random_target_pos = true;
            m_random_idle = false;
            m_random_idle_timer = 0.f;
        } else if (!m_has_random_target_pos || reached_random_target) {
            PickRandomTargetPos(mob, *stats);
        }
    }
    mob->MoveTowards(m_target_pos, dt);
}

void CNeutralMeleeController::OnDamaged(CMobBase* mob, CEntity* attacker)
{
    if (!mob || !attacker) return;
    if (IsUnavailableMeleeTarget(mob, attacker)) return;

    m_p_target = attacker;
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

    if (m_p_target && IsCurrentMeleeTargetUnavailable(mob, m_p_target, dt, m_target_los_check_timer))
    {
        m_p_target = nullptr;
        m_target_los_check_timer = 0.f;
        m_has_random_target_pos = false;
        m_random_idle = false;
        m_random_idle_timer = 0.f;
    }

    if (m_p_target)
    {
        m_target_pos = m_p_target->m_pos;
        mob->MoveTowards(m_target_pos, dt);
        return;
    }

    bool reached_random_target = m_has_random_target_pos &&
                                 (m_random_idle ? IsRandomIdleDone(dt) :
                                  DistanceSq(mob->m_pos, m_target_pos) <= mob->m_radius * mob->m_radius);
    if (!m_has_random_target_pos || reached_random_target || m_change_target_count >= game_config::melee_target_time)
    {
        m_change_target_count = 0.f;
        if (CEntity* honey = FindClosestHoneyTarget(mob, stats->horizon))
        {
            m_p_target = honey;
            m_target_pos = honey->m_pos;
            m_has_random_target_pos = true;
            m_random_idle = false;
            m_random_idle_timer = 0.f;
        } else if (!m_has_random_target_pos || reached_random_target) {
            PickRandomTargetPos(mob, *stats);
        }
    }
    mob->MoveTowards(m_target_pos, dt);
}

// ============ Queen Ant ============

void CQueenAntController::OnTick(CMobBase* mob, float dt)
{
    CMeleeController::OnTick(mob, dt);
    if (!mob || !m_p_target || m_p_target->m_is_marked_for_des || m_p_target->IsDead()) return;

    const SMobStats* stats = mob->GetFinalStats();
    if (!stats) return;

    float attack_range = std::max(mob->m_radius * 4.f, stats->horizon);
    if (DistanceSq(mob->m_pos, m_p_target->m_pos) > attack_range * attack_range) return;

    if (auto* attackable = dynamic_cast<IAttackableMob*>(mob))
    {
        if (attackable->TryAttack(m_p_target))
            mob->m_vel = {0.f, 0.f};
    }
}

// ============ Spider ============

void CSpiderController::OnTick(CMobBase* mob, float dt)
{
    CMeleeController::OnTick(mob, dt);
    if (!mob || !mob->GameWorld()) return;
    if (GetLevel(mob->GetRarity()) < GetLevel(ERarity::Legendary)) return;
    if (!m_p_target || m_p_target->m_is_marked_for_des || m_p_target->IsDead()) return;

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

    bool target_invalid = m_p_target &&
                          IsCurrentMeleeTargetUnavailable(mob, m_p_target, dt, m_target_los_check_timer);
    bool reached_random_target = !m_p_target && m_has_random_target_pos &&
                                 (m_random_idle ? IsRandomIdleDone(dt) :
                                  DistanceSq(mob->m_pos, m_target_pos) <= mob->m_radius * mob->m_radius);
    bool can_random_retarget = !m_p_target && m_change_target_count >= game_config::melee_target_time;
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

        m_p_target = FindClosestMeleeTarget(mob, stats->horizon);
        if (m_p_target)
        {
            m_target_pos = m_p_target->m_pos;
            m_has_random_target_pos = true;
        } else {
            PickRandomTargetPos(mob, *stats);
        }
    }

    if (!m_p_target)
    {
        mob->MoveTowards(m_target_pos, dt);
        return;
    }

    m_target_pos = m_p_target->m_pos;
    FaceTarget(mob, m_p_target);

    sf::Vector2f to_target = m_p_target->m_pos - mob->m_pos;
    float target_dist = Length(to_target);
    float stop_distance = 128.f + mob->m_radius;
    if (target_dist <= stop_distance)
        mob->MoveTowards(mob->m_pos, dt);
    else
        mob->MoveTowards(m_target_pos, dt);

    float missile_range = game_config::mob_hornet_missile_speed * game_config::default_missile_lifetime;
    if (target_dist <= missile_range * 0.5f)
    {
        if (auto* attackable = dynamic_cast<IAttackableMob*>(mob))
            attackable->TryAttack(m_p_target);
    }
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

    m_honey_target_timer += dt;
    if (m_p_honey_target &&
        IsCurrentMeleeTargetUnavailable(mob, m_p_honey_target, dt, m_honey_los_check_timer))
    {
        m_p_honey_target = nullptr;
        m_honey_los_check_timer = 0.f;
        m_honey_target_timer = game_config::melee_target_time;
    }
    if (!m_p_honey_target && m_honey_target_timer >= game_config::melee_target_time)
    {
        m_honey_target_timer = 0.f;
        m_p_honey_target = FindClosestHoneyTarget(mob, stats->horizon);
    }

    if (m_p_honey_target)
    {
        FaceTarget(mob, m_p_honey_target);
        mob->MoveTowards(m_p_honey_target->m_pos, dt);
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
