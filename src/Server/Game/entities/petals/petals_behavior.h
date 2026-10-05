#pragma once
#include "../../../../Shared/game_config.h"
#include "../../../../Shared/math_types.h"
#include "../../../../Shared/petal_motion.h"
#include "../../controllers/melee_controller.h"
#include "../../gameworld.h"
#include "../../state_zone.h"
#include "../../states/states.h"
#include "petal.h"
#include "petal_slot.h"
#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <optional>

inline SFlowerStats EmptyFlowerStats()
{
    SFlowerStats stats;
    stats.max_health = 0.f;
    stats.armor = 0.f;
    stats.damage = 0.f;
    stats.radius = 0.f;
    stats.mass = 0.f;
    stats.horizon = 0.f;
    stats.max_absorb_range = 0.f;
    stats.detection_multiplier = 1.f;
    stats.detection_range_bonus = 0.f;
    stats.max_velocity = 0.f;
    stats.acceleration = 0.f;
    stats.extra_hit_num = 0;
    stats.max_health_multiplier = 1.f;
    stats.max_mana = 0.f;
    stats.mana_regen = 0.f;
    stats.health_regen = 0.f;
    stats.defense_health_regen = 0.f;
    stats.recovery_multiplier = 1.f;
    stats.tridmgbonus = 0.f;
    stats.reach = 0.f;
    stats.petal_attraction_range = 0.f;
    stats.petal_dmg_multiplier = 1.f;
    stats.petal_reload_multiplier = 1.f;
    stats.petal_health_multiplier = 1.f;
    stats.petal_medicine_multiplier = 1.f;
    stats.healing_received_multiplier = 1.f;
    stats.overheal_to_shield = 0.f;
    stats.mult_summoned_health = 1.f;
    stats.mult_summoned_damage = 1.f;
    stats.poison_damage_multiplier = 1.f;
    stats.poison_duration_multiplier = 1.f;
    stats.body_poison_damage_multiplier = 0.f;
    stats.body_poison_duration = 0.f;
    stats.petal_extra_hit_num = 0;
    stats.petal_hit_compression_power = 0.f;
    stats.petal_swap_min_reload = game_config::default_petal_swap_min_reload;
    stats.petal_rotation_speed = 0.f;
    stats.petal_rotation_quantized = false;
    stats.petal_rotation_mode = EPetalRotationMode::Orbit;
    return stats;
}

inline float GetPetalRarityScale(ERarity rarity)
{
    return std::pow(game_config::default_petal_pow, static_cast<float>(GetLevel(rarity) - 1));
}

inline float RoseMedicineScale(ERarity rarity);

inline int GetPetalLevel(ERarity rarity) { return GetLevel(rarity); }

inline float GetPetalValueLevel(ERarity rarity) { return GetRarityValueLevel(rarity); }

inline float GetSpecialPetalLevel(ERarity rarity)
{
    return static_cast<float>(GetLevel(rarity));
}

inline float TriBonusDamage(ERarity rarity)
{
    return game_config::default_triangle_bonus_damage * GetPetalRarityScale(rarity);
}

inline float GetBaseOrbitRadius(const CPetal* owner, const CFlower* flower)
{
    if (!owner || !flower || !flower->GetFinalStats()) return 0.f;
    CEntity* anchor = owner->GetOrbitAnchor();
    if (!anchor || anchor == flower) return flower->GetFinalStats()->radius;
    return std::max(0.f, anchor->Radius());
}

inline float GetOrbitReach(const CPetal* owner, const CFlower* flower, bool include_mode_offset)
{
    if (!flower || !flower->GetFinalStats()) return 0.f;

    float reach = game_config::default_petal_neutral_reach;
    const EPetalType type = owner ? owner->GetPetalType() : EPetalType::None;
    const bool heavy = type == EPetalType::Heavy;
    const bool base_only = owner && UsesBaseAndDefenseReach(type);
    const bool ignores_bonus = owner && IgnoresReachBonus(type);

    // Existing special petals still opt out of attack/Third-Eye reach, but
    // every non-Heavy petal consumes the defensive reach reduction.
    if (flower->m_attacking && !base_only && !ignores_bonus)
        reach += flower->GetFinalStats()->reach + game_config::default_petal_attack_offset;
    if (include_mode_offset && flower->m_defending && !heavy)
        reach += game_config::default_petal_defend_offset;
    return reach;
}

inline float GetOrbitReach(const CFlower* flower, bool include_mode_offset)
{
    return GetOrbitReach(nullptr, flower, include_mode_offset);
}

inline float GetNeutralOrbitDistance(const CPetal* owner, const CFlower* flower)
{
    if (!owner || !flower || !flower->GetFinalStats()) return 0.f;
    float reach = game_config::default_petal_neutral_reach;
    if (flower->m_defending && owner->GetPetalType() != EPetalType::Heavy)
        reach += game_config::default_petal_defend_offset;
    return GetBaseOrbitRadius(owner, flower) + game_config::default_petal_orbit_radius +
           reach;
}

inline float GetOrbitDistance(const CPetal* owner, const CFlower* flower)
{
    if (!owner || !flower || !flower->GetFinalStats()) return 0.f;

    float distance = GetBaseOrbitRadius(owner, flower) + game_config::default_petal_orbit_radius +
                     GetOrbitReach(owner, flower, true);
    return distance;
}

inline florrbt::Vector2f GetOrbitCenter(const CPetal* owner, CFlower* flower)
{
    if (!owner || !flower) return {};
    CEntity* anchor = owner->GetOrbitAnchor();
    return anchor ? anchor->Pos() : flower->Pos();
}

inline float GetClosedClusterSpacing(const CPetal* owner)
{
    if (!owner) return game_config::default_petal_closed_cluster_spacing;

    switch (owner->GetPetalType())
    {
    case EPetalType::Dust:
        return game_config::default_petal_closed_cluster_spacing_dust;
    case EPetalType::AntEgg:
    case EPetalType::BeetleEgg:
    case EPetalType::Dahlia:
        return game_config::default_petal_closed_cluster_spacing_compact;
    default:
        return game_config::default_petal_closed_cluster_spacing;
    }
}

inline float GetClosedClusterRadius(const CPetal* owner, int copies_in_group)
{
    if (!owner || copies_in_group <= 1) return 0.f;

    const float sin_step = std::max(game_config::default_petal_closed_cluster_sin_min,
                                    std::sin(game_config::pi / static_cast<float>(copies_in_group)));
    const float adjacent_center_distance = owner->Radius() * GetClosedClusterSpacing(owner);
    return adjacent_center_distance / (2.0f * sin_step);
}

inline std::optional<florrbt::Vector2f> GetGlobalOrbitPosition(CPetal* owner, CFlower* flower, float orbit_distance, bool is_open)
{
    if (!owner || !flower) return std::nullopt;

    int total_copies = flower->m_total_copies;
    int start_index = flower->GetStartCopyIndex(owner->m_slot_index);
    if (start_index < 0 || total_copies <= 0) return std::nullopt;

    florrbt::Vector2f global;
    florrbt::Vector2f orbit_center = GetOrbitCenter(owner, flower);
    if // yinyang旋轉模式
        (flower->GetFinalStats()->petal_rotation_mode == EPetalRotationMode::YinYang && flower->GetYinYangCount() > 0)
    {
        int columns = flower->GetYinYangColumnCount();
        if (columns <= 0) return std::nullopt;

        int layout_index = start_index;
        if (is_open) layout_index += owner->m_copy_index;

        int column_index = layout_index % columns;
        int layer_index = layout_index / columns;
        float angle =
            flower->GetPetalRotationAngle() + (2.0f * game_config::pi * column_index) / static_cast<float>(columns);
        float layer_distance = static_cast<float>(layer_index) * game_config::default_petal_orbit_radius;
        global = orbit_center + florrbt::Vector2f(std::cos(angle), std::sin(angle)) * (orbit_distance + layer_distance);
        if (!is_open)
        {
            int copies_in_group = owner->m_max_slot_num;
            if (copies_in_group <= 0) copies_in_group = owner->m_base_petal_stats.copy;
            if (copies_in_group <= 0) return std::nullopt;

            float spread_radius = GetClosedClusterRadius(owner, copies_in_group);
            float sub_angle = (2.0f * game_config::pi * owner->m_copy_index) / static_cast<float>(copies_in_group);
            global += florrbt::Vector2f(std::cos(sub_angle), std::sin(sub_angle)) * spread_radius;
        }

    } else if (is_open)
    { // 非yinyang
        float angle = flower->GetPetalRotationAngle() +
                      (2.0f * game_config::pi * (start_index + owner->m_copy_index)) / static_cast<float>(total_copies);
        global = orbit_center + florrbt::Vector2f(std::cos(angle), std::sin(angle)) * orbit_distance;
    } else
    {
        float group_angle =
            flower->GetPetalRotationAngle() + (2.0f * game_config::pi * start_index) / static_cast<float>(total_copies);
        florrbt::Vector2f group_global =
            orbit_center + florrbt::Vector2f(std::cos(group_angle), std::sin(group_angle)) * orbit_distance;
        int copies_in_group = owner->m_max_slot_num;
        if (copies_in_group <= 0) copies_in_group = owner->m_base_petal_stats.copy;
        if (copies_in_group <= 0) return std::nullopt;

        float spread_radius = GetClosedClusterRadius(owner, copies_in_group);
        float sub_angle = (2.0f * game_config::pi * owner->m_copy_index) / static_cast<float>(copies_in_group);
        global = group_global + florrbt::Vector2f(std::cos(sub_angle), std::sin(sub_angle)) * spread_radius;
    }

    return global;
}

inline void MovePetalToOrbit(CPetal* owner, CFlower* flower, float orbit_distance, float k, bool is_open)
{
    if (!owner || !flower) return;
    std::optional<florrbt::Vector2f> global = GetGlobalOrbitPosition(owner, flower, orbit_distance, is_open);
    if (!global) return;

    florrbt::Vector2f delta = *global - owner->Pos();
    float effective_k = k * game_config::default_petal_follow_k_multiplier *
                        std::max(0.f, game_config::default_petal_follow_multiplier);
    if (owner->m_spawn_flight_boost)
    {
        if (LengthSq(delta) <= (owner->Radius() * owner->Radius())) owner->m_spawn_flight_boost = false;
    }
    owner->m_vel = delta * effective_k;
}

inline bool IsPetalTargetSuppressed(const CPetal* owner, const CEntity* entity)
{
    if (!owner || !entity || owner->m_target_release_timer <= game_config::entity_collision_epsilon) return false;
    return owner->m_target_release_id == entity->Id() &&
           owner->m_target_release_generation == entity->Generation();
}

inline bool IsValidPetalEnemyTarget(const CPetal* owner, const CFlower* flower, const CEntity* entity)
{
    if (!owner || !flower || !entity) return false;
    if (IsPetalTargetSuppressed(owner, entity)) return false;
    if (entity->IsMarkedForDes() || entity->IsDead() || !entity->CanCollide()) return false;
    if (entity == owner || entity == flower) return false;
    if (IsDiggingEntity(entity)) return false;
    if (dynamic_cast<const CPetal*>(entity)) return false;
    if (CheckTeam(entity->Team(), flower->Team())) return false;
    if ((entity->Team() == 0 || flower->Team() == 0) && ShareRootOwner(owner, entity)) return false;
    if (BlocksNullifiedInteraction(owner, entity)) return false;
    return dynamic_cast<const CMobBase*>(entity) != nullptr;
}

inline float GetEdgeDistance(const florrbt::Vector2f& center, const CEntity* entity)
{
    if (!entity) return std::numeric_limits<float>::max();
    return std::max(0.f, Distance(center, entity->Pos()) - entity->Radius());
}

inline bool IsTargetInRange(const florrbt::Vector2f& center, float range, const CEntity* entity)
{
    return entity && GetEdgeDistance(center, entity) <= range;
}

inline void SetPetalTarget(CPetal* owner, const CEntity* target)
{
    if (!owner) return;
    owner->m_target_entity_id = target ? target->Id() : -1;
    owner->m_target_entity_generation = target ? target->Generation() : 0;
}

inline void ClearPetalTarget(CPetal* owner)
{
    SetPetalTarget(owner, nullptr);
    if (owner && owner->m_target_release_timer <= game_config::entity_collision_epsilon)
    {
        owner->m_target_release_id = -1;
        owner->m_target_release_generation = 0;
    }
}

inline CEntity* GetCachedTarget(CPetal* owner, CFlower* flower, const florrbt::Vector2f& center, float range,
                                     const std::function<bool(const CEntity*)>& filter)
{
    if (!owner || !flower || !flower->GameWorld() || owner->m_target_entity_id < 0) return nullptr;

    CEntity* target = owner->m_target_entity_generation != 0
                          ? flower->GameWorld()->GetEntity(owner->m_target_entity_id, owner->m_target_entity_generation)
                          : flower->GameWorld()->GetEntity(owner->m_target_entity_id);
    if (!target)
    {
        ClearPetalTarget(owner);
        return nullptr;
    }
    // Keep the last target identity during the short release window.  Some
    // behaviors use it for the contact effect even though homing is disabled.
    if (IsPetalTargetSuppressed(owner, target)) return nullptr;
    if (!filter(target) || !IsTargetInRange(center, range, target))
    {
        ClearPetalTarget(owner);
        return nullptr;
    }
    return target;
}

inline CEntity* FindClosestTarget(CPetal* owner, CFlower* flower, const florrbt::Vector2f& center, float range,
                                       const std::function<bool(const CEntity*)>& filter)
{
    if (!owner || !flower) return nullptr;

    CGameWorld* world = flower->GameWorld();
    if (!world) return nullptr;
    if (range <= 0.0f) return nullptr;

    return world->FindClosestEntityByEdge(center, range, filter);
}

inline float GetEnemyTargetRange(const CPetal* owner, const CFlower* flower, bool include_final_bonus)
{
    if (!owner) return 0.f;

    float range = owner->Radius() + game_config::default_petal_attraction_range;
    if (include_final_bonus && flower && flower->GetFinalStats())
        range += flower->GetFinalStats()->petal_attraction_range;
    return range;
}

inline CEntity* FindTarget(CPetal* owner, CFlower* flower)
{
    if (!owner || !flower || !flower->GetFinalStats()) return nullptr;
    if (owner->GetPetalType() == EPetalType::Glass)
    {
        ClearPetalTarget(owner);
        return nullptr;
    }

    float range = GetEnemyTargetRange(owner, flower, true);
    auto filter = [owner, flower](const CEntity* entity) -> bool {
        return IsValidPetalEnemyTarget(owner, flower, entity);
    };

    return FindClosestTarget(owner, flower, owner->Pos(), range, filter);
}

inline void SuppressPetalTarget(CPetal* owner, const CEntity* target)
{
    if (!owner || !target) return;
    SetPetalTarget(owner, target);
    owner->m_target_release_id = target->Id();
    owner->m_target_release_generation = target->Generation();
    owner->m_target_release_timer = std::max(0.f, game_config::default_petal_target_release_cooldown);
}

inline bool ReleasePetalTargetIfClose(CPetal* owner, const CEntity* target)
{
    if (!owner || !target) return false;
    const float release_distance = std::max(0.f, game_config::default_petal_target_release_distance);
    if (GetEdgeDistance(owner->Pos(), target) > release_distance) return false;
    SuppressPetalTarget(owner, target);
    return true;
}

inline florrbt::Vector2f GetSeekVelocity(const CPetal* owner, const CEntity* target, float velocity)
{
    if (!owner || !target || velocity <= 0.f) return {};

    florrbt::Vector2f delta = target->Pos() - owner->Pos();
    if (LengthSq(delta) <= game_config::entity_collision_epsilon * game_config::entity_collision_epsilon) return {};

    float len = Length(delta);
    if (len <= game_config::entity_collision_epsilon) return {};

    return delta / len * velocity;
}

inline florrbt::Vector2f GetEntityVelocity(const CEntity* target)
{
    if (const auto* mob = dynamic_cast<const CMobBase*>(target)) return mob->m_vel;
    if (const auto* projectile = dynamic_cast<const CProjectile*>(target)) return projectile->m_vel;
    return { 0.f, 0.f };
}

inline void ResetAttractionMotion(CPetal* owner)
{
    if (!owner) return;
    owner->m_attraction_start_pos = {};
    owner->m_attraction_start_velocity = {};
    owner->m_attraction_elapsed = 0.f;
    owner->m_attraction_active = false;
}

inline void BeginAttractionMotion(CPetal* owner)
{
    if (!owner) return;
    owner->m_attraction_start_pos = owner->Pos();
    owner->m_attraction_start_velocity = owner->m_vel;
    owner->m_attraction_elapsed = 0.f;
    owner->m_attraction_active = true;
}

inline florrbt::Vector2f EvaluateAttractionCurve(const florrbt::Vector2f& start,
                                                  const florrbt::Vector2f& start_tangent,
                                                  const florrbt::Vector2f& end,
                                                  const florrbt::Vector2f& end_tangent, float progress)
{
    const float t = std::clamp(progress, 0.f, 1.f);
    const float t2 = t * t;
    const float t3 = t2 * t;
    const float start_weight = 2.f * t3 - 3.f * t2 + 1.f;
    const float start_tangent_weight = t3 - 2.f * t2 + t;
    const float end_weight = -2.f * t3 + 3.f * t2;
    const float end_tangent_weight = t3 - t2;
    return start * start_weight + start_tangent * start_tangent_weight + end * end_weight +
           end_tangent * end_tangent_weight;
}

// Follow a moving target along a short cubic path.  The first derivative at
// departure is the petal's live orbit velocity, so the path naturally bends
// inward instead of teleporting onto a straight radial line.  Once the curve
// is complete the petal tracks the target until normal contact handling
// destroys it (or, for Yggdrasil, while it channels on the corpse).
inline void AdvanceAttractionMotion(CPetal* owner, const CEntity* target, float duration, float dt,
                                    bool restart)
{
    if (!owner || !target) return;
    if (restart || !owner->m_attraction_active) BeginAttractionMotion(owner);

    const float elapsed_dt = std::isfinite(dt) ? std::max(0.f, dt) : 0.f;
    const float safe_duration = std::max(game_config::entity_collision_epsilon, duration);
    if (elapsed_dt <= game_config::entity_collision_epsilon)
    {
        owner->m_vel = {};
        return;
    }

    const florrbt::Vector2f target_velocity = GetEntityVelocity(target);
    if (owner->m_attraction_elapsed >= safe_duration)
    {
        owner->m_vel = target_velocity + (target->Pos() - owner->Pos()) / elapsed_dt;
        return;
    }

    const float next_elapsed = std::min(safe_duration, owner->m_attraction_elapsed + elapsed_dt);
    const float remaining = std::max(0.f, safe_duration - owner->m_attraction_elapsed);
    const florrbt::Vector2f predicted_end = target->Pos() + target_velocity * remaining;
    const float momentum = std::max(0.f, game_config::default_healing_petal_target_arc_momentum);
    const florrbt::Vector2f start_tangent = owner->m_attraction_start_velocity * safe_duration * momentum;
    const florrbt::Vector2f end_tangent = target_velocity * safe_duration;
    const florrbt::Vector2f next_pos =
        EvaluateAttractionCurve(owner->m_attraction_start_pos, start_tangent, predicted_end, end_tangent,
                                next_elapsed / safe_duration);
    owner->m_vel = (next_pos - owner->Pos()) / elapsed_dt;
    owner->m_attraction_elapsed = next_elapsed;
}

inline float GetDefaultTargetRangeScale()
{
    const float base_range =
        std::max(game_config::entity_collision_epsilon, game_config::default_lentil_petal_attraction_range);
    return std::max(1.f, game_config::stats_default_flower_petal_attraction_range / base_range);
}

inline float GetMaximumTargetRangeScale()
{
    const float primordial_lentils =
        static_cast<float>(std::max(0, game_config::default_petal_target_max_primordial_lentils));
    return GetDefaultTargetRangeScale() + primordial_lentils * GetPetalValueLevel(ERarity::Primordial);
}

inline float GetTargetRangeScale(const CFlower* flower)
{
    const float base_range =
        std::max(game_config::entity_collision_epsilon, game_config::default_lentil_petal_attraction_range);
    float range_scale = GetDefaultTargetRangeScale();
    if (flower && flower->GetFinalStats()) range_scale = flower->GetFinalStats()->petal_attraction_range / base_range;
    return std::clamp(range_scale, GetDefaultTargetRangeScale(), GetMaximumTargetRangeScale());
}

inline float GetTargetAcceleration(const CFlower* flower,
                                     float multiplier = game_config::default_petal_target_acceleration_multiplier,
                                     bool include_range_scale = true)
{
    float acceleration = game_config::default_acceleration;
    if (flower && flower->GetFinalStats())
        acceleration = std::max(acceleration, flower->GetFinalStats()->acceleration);

    const float range_scale = include_range_scale ? GetTargetRangeScale(flower) : 1.f;
    return acceleration * multiplier * range_scale * game_config::default_petal_target_acceleration_scale;
}

inline float GetLockedTargetAcceleration(const CFlower* flower,
                                           float multiplier = game_config::default_petal_target_acceleration_multiplier,
                                           bool include_range_scale = true)
{
    return GetTargetAcceleration(flower, multiplier, include_range_scale) *
           game_config::default_petal_locked_target_acceleration_scale;
}

inline florrbt::Vector2f GetGlobalPullVelocity(CPetal* owner, CFlower* flower, float orbit_distance, float k, bool is_open)
{
    if (!owner || !flower) return {};

    std::optional<florrbt::Vector2f> global = GetGlobalOrbitPosition(owner, flower, orbit_distance, is_open);
    if (!global) return {};

    return (*global - owner->Pos()) * k * game_config::default_petal_target_orbit_tether;
}

inline florrbt::Vector2f GetTargetReturnVelocity(CPetal* owner, CFlower* flower, float orbit_distance, float k,
                                                 bool is_open)
{
    if (!owner || !flower) return {};

    const std::optional<florrbt::Vector2f> global = GetGlobalOrbitPosition(owner, flower, orbit_distance, is_open);
    if (!global) return {};

    const florrbt::Vector2f delta = *global - owner->Pos();
    const float distance = Length(delta);
    if (distance <= game_config::entity_collision_epsilon) return {};

    // The old target pull was only a weak linear tether.  Once a petal had
    // touched a mob, target acceleration could therefore cancel it forever.
    // Treat the return as a spring: farther from the orbit means a rapidly
    // larger velocity, capped to a safe one-tick displacement.
    const float reference = std::max({ 1.f, std::max(0.f, orbit_distance), owner->Radius() });
    const float ratio = std::clamp(distance / reference, 0.f, 8.f);
    const float gain = 1.f + std::max(0.f, game_config::default_petal_target_return_distance_gain) * ratio * ratio;
    float speed = distance * std::max(0.f, k) * std::max(0.f, game_config::default_petal_target_orbit_tether) * gain;
    const float max_speed = std::max(0.f, game_config::default_petal_target_return_max_speed);
    if (max_speed > game_config::entity_collision_epsilon) speed = std::min(speed, max_speed);
    return delta / distance * speed;
}

inline florrbt::Vector2f ConstrainTargetVelocity(const CPetal* owner, const CFlower* flower, const CEntity* target,
                                                 float acceleration_multiplier, florrbt::Vector2f target_velocity,
                                                 florrbt::Vector2f global_pull_velocity, float dt)
{
    if (!owner || !target) return global_pull_velocity;

    const float target_speed = Length(target_velocity);
    if (target_speed > game_config::entity_collision_epsilon)
    {
        // Homing speed is clamped to [1x, 2x] of the locked-target
        // acceleration: the petal always closes at a controlled rate instead
        // of scaling wildly with the flower's attraction range.
        const float base_velocity = GetLockedTargetAcceleration(flower, acceleration_multiplier, false);
        const float min_velocity = std::max(0.f, base_velocity);
        const float max_velocity = std::max(min_velocity, base_velocity * 2.f);
        target_velocity *= std::clamp(target_speed, min_velocity, max_velocity) / target_speed;
    }

    florrbt::Vector2f actual_velocity = target_velocity + global_pull_velocity;

    if (dt <= game_config::entity_collision_epsilon) return actual_velocity;

    const florrbt::Vector2f predicted_target = target->Pos() + GetEntityVelocity(target) * dt;
    const florrbt::Vector2f to_target = predicted_target - owner->Pos();
    const float distance = Length(to_target);
    if (distance <= game_config::entity_collision_epsilon) return GetEntityVelocity(target);

    const florrbt::Vector2f direction = to_target / distance;
    const float approach_speed = actual_velocity.x * direction.x + actual_velocity.y * direction.y;
    if (approach_speed <= 0.f) return actual_velocity;
    if (approach_speed * dt + game_config::entity_collision_epsilon < distance) return actual_velocity;

    target_velocity = direction * (distance / dt) - global_pull_velocity;
    return target_velocity + global_pull_velocity;
}

inline void MovePetalToOrbitAndAttract(CPetal* owner, CFlower* flower, float orbit_distance, float k, bool is_open,
                                     float dt, float acceleration_multiplier = 1.f)
{
    CEntity* target = FindTarget(owner, flower);
    if (!owner || !target)
    {
        if (!owner || owner->m_target_release_timer <= game_config::entity_collision_epsilon)
            ClearPetalTarget(owner);
        MovePetalToOrbit(owner, flower, orbit_distance, k, is_open);
        return;
    }

    SetPetalTarget(owner, target);
    if (ReleasePetalTargetIfClose(owner, target))
    {
        owner->m_vel = GetTargetReturnVelocity(owner, flower, orbit_distance, k, is_open);
        return;
    }
    const florrbt::Vector2f target_velocity =
        GetSeekVelocity(owner, target, GetLockedTargetAcceleration(flower, acceleration_multiplier, true));
    const florrbt::Vector2f global_pull_velocity =
        GetTargetReturnVelocity(owner, flower, orbit_distance, k, is_open);
    owner->m_vel = ConstrainTargetVelocity(owner, flower, target, acceleration_multiplier, target_velocity,
                                                 global_pull_velocity, dt);
}

// Released auxiliary petals do not own a slot index in the flower layout, but
// may still need the normal seek/return behavior.  This variant accepts their
// remembered orbit point directly instead of asking the layout for one.
inline void MovePetalToOrbitPointAndAttract(CPetal* owner, CFlower* flower,
                                            const florrbt::Vector2f& orbit_point, float orbit_distance, float k,
                                            float dt, float acceleration_multiplier = 1.f)
{
    if (!owner || !flower) return;

    auto return_velocity = [owner, &orbit_point, orbit_distance, k]() {
        const florrbt::Vector2f delta = orbit_point - owner->Pos();
        const float distance = Length(delta);
        if (distance <= game_config::entity_collision_epsilon) return florrbt::Vector2f{};
        const float reference = std::max({ 1.f, std::max(0.f, orbit_distance), owner->Radius() });
        const float ratio = std::clamp(distance / reference, 0.f, 8.f);
        const float gain =
            1.f + std::max(0.f, game_config::default_petal_target_return_distance_gain) * ratio * ratio;
        float speed = distance * std::max(0.f, k) *
                      std::max(0.f, game_config::default_petal_target_orbit_tether) * gain;
        const float max_speed = std::max(0.f, game_config::default_petal_target_return_max_speed);
        if (max_speed > game_config::entity_collision_epsilon) speed = std::min(speed, max_speed);
        return delta / distance * speed;
    };

    CEntity* target = FindTarget(owner, flower);
    if (!target)
    {
        if (owner->m_target_release_timer <= game_config::entity_collision_epsilon) ClearPetalTarget(owner);
        owner->m_vel = (orbit_point - owner->Pos()) * std::max(0.f, k) *
                       std::max(0.f, game_config::default_petal_follow_k_multiplier) *
                       std::max(0.f, game_config::default_petal_follow_multiplier);
        return;
    }

    SetPetalTarget(owner, target);
    if (ReleasePetalTargetIfClose(owner, target))
    {
        owner->m_vel = return_velocity();
        return;
    }

    const florrbt::Vector2f target_velocity =
        GetSeekVelocity(owner, target, GetLockedTargetAcceleration(flower, acceleration_multiplier, true));
    owner->m_vel = ConstrainTargetVelocity(owner, flower, target, acceleration_multiplier, target_velocity,
                                           return_velocity(), dt);
}

// (Heavy petal movement fully restores the shared orbit/seek path below.)

bool RegisterPetals(std::string& error);

class CAirBehavior : public CPetalBehavior
{
  public:
    bool IsOpen() const override { return true; }

    SFlowerStats GetStats(ERarity rarity) const override
    {
        SFlowerStats stats = EmptyFlowerStats();
        float level = GetPetalValueLevel(rarity);
        stats.radius = game_config::default_air_base_radius * level;
        stats.mass = game_config::default_air_base_mass;
        return stats;
    }

    SPetalStats GetPetalStats(ERarity) const override
    {
        SPetalStats stats;
        stats.stack = false;
        stats.reload = 0.f;
        stats.preload = 0.f;
        stats.copy = 0;
        stats.radius = 0.f;
        return stats;
    }

    void OnTick(CPetal*, ERarity, CFlower*, float) override {}
    void OnFlowerTakeDamage(CPetal*, ERarity, CFlower*, float&, EDamageType, CEntity*) override {}
    void OnPetalSpawned(CPetal*, ERarity, CFlower*) override {}
    void OnPetalDestroyed(CPetal*, ERarity, CFlower*) override {}
};

class CBrushBehavior : public CPetalBehavior
{
  public:
    bool IsOpen() const override { return true; }

    SFlowerStats GetStats(ERarity) const override { return EmptyFlowerStats(); }

    // The brush is a slot-only petal (no live entity): the mutable-terrain
    // controller drives the painting while the flower attacks.
    SPetalStats GetPetalStats(ERarity) const override
    {
        SPetalStats stats;
        stats.stack = false;
        stats.reload = game_config::default_brush_reload;
        stats.preload = game_config::default_brush_reload;
        stats.copy = 0;
        stats.radius = 0.f;
        return stats;
    }

    void OnTick(CPetal*, ERarity, CFlower*, float) override {}
    void OnFlowerTakeDamage(CPetal*, ERarity, CFlower*, float&, EDamageType, CEntity*) override {}
    void OnPetalSpawned(CPetal*, ERarity, CFlower*) override {}
    void OnPetalDestroyed(CPetal*, ERarity, CFlower*) override {}
};

class CBloodSacrificeBehavior : public CPetalBehavior
{
  public:
    SFlowerStats GetStats(ERarity) const override { return EmptyFlowerStats(); }

    SPetalStats GetPetalStats(ERarity) const override
    {
        SPetalStats stats;
        stats.stack = false;
        stats.reload = 0.f;
        stats.preload = 0.f;
        stats.copy = static_cast<int>(game_config::default_blood_sacrifice_copy);
        stats.radius = game_config::default_blood_sacrifice_base_radius;
        return stats;
    }

    void OnTick(CPetal*, ERarity, CFlower*, float) override {}
    void OnFlowerTakeDamage(CPetal*, ERarity, CFlower*, float&, EDamageType, CEntity*) override {}
    void OnPetalSpawned(CPetal*, ERarity, CFlower*) override {}
    void OnPetalDestroyed(CPetal*, ERarity, CFlower*) override {}
};

class CCorruptionBehavior : public CPetalBehavior
{
  public:
    SFlowerStats GetStats(ERarity) const override { return EmptyFlowerStats(); }

    SPetalStats GetPetalStats(ERarity) const override
    {
        SPetalStats stats;
        stats.stack = false;
        stats.reload = 0.f;
        stats.preload = 0.f;
        stats.copy = static_cast<int>(game_config::default_corruption_copy);
        stats.radius = game_config::default_corruption_base_radius;
        return stats;
    }

    void OnTick(CPetal*, ERarity, CFlower*, float) override {}
    void OnFlowerTakeDamage(CPetal*, ERarity, CFlower*, float&, EDamageType, CEntity*) override {}
    void OnPetalSpawned(CPetal* owner, ERarity, CFlower*) override
    {
        if (!owner) return;
        owner->FacingAngle() = GetLimitedRng(-game_config::pi, game_config::pi);
        owner->HasFacing() = true;
    }
    void OnPetalDestroyed(CPetal*, ERarity, CFlower*) override {}
};

class CBandageBehavior : public CPetalBehavior
{
  public:
    SFlowerStats GetStats(ERarity) const override { return EmptyFlowerStats(); }

    SPetalStats GetPetalStats(ERarity) const override
    {
        SPetalStats stats;
        stats.stack = false;
        stats.reload = 0.f;
        stats.preload = 0.f;
        stats.copy = static_cast<int>(game_config::default_bandage_copy);
        stats.radius = game_config::default_bandage_base_radius;
        return stats;
    }

    void OnTick(CPetal*, ERarity, CFlower*, float) override {}
    void OnFlowerTakeDamage(CPetal*, ERarity, CFlower*, float&, EDamageType, CEntity*) override {}
    void OnPetalSpawned(CPetal*, ERarity, CFlower*) override {}
    void OnPetalDestroyed(CPetal*, ERarity, CFlower*) override {}
};
inline float AntennaeHorizonMultiplier(ERarity rarity)
{
    switch (rarity)
    {
    case ERarity::Common:
        return game_config::default_antennae_horizon_common;
    case ERarity::Unusual:
        return game_config::default_antennae_horizon_unusual;
    case ERarity::Rare:
        return game_config::default_antennae_horizon_rare;
    case ERarity::Epic:
        return game_config::default_antennae_horizon_epic;
    case ERarity::Legendary:
        return game_config::default_antennae_horizon_legendary;
    case ERarity::Mythic:
        return game_config::default_antennae_horizon_mythic;
    case ERarity::Ultra:
        return game_config::default_antennae_horizon_ultra;
    case ERarity::Exotic:
        return game_config::default_antennae_horizon_common;
    case ERarity::Super:
        return game_config::default_antennae_horizon_super;
    case ERarity::Eternal:
        return game_config::default_antennae_horizon_eternal;
    case ERarity::Unique:
        return game_config::default_antennae_horizon_unique;
    case ERarity::Primordial:
        return game_config::default_antennae_horizon_primordial;
    default:
        return 0.f;
    }
}

inline float AntEggReload(ERarity rarity)
{
    switch (rarity)
    {
    case ERarity::Common:
        return game_config::default_antegg_reload_common;
    case ERarity::Unusual:
        return game_config::default_antegg_reload_unusual;
    case ERarity::Rare:
        return game_config::default_antegg_reload_rare;
    case ERarity::Epic:
        return game_config::default_antegg_reload_epic;
    case ERarity::Legendary:
        return game_config::default_antegg_reload_legendary;
    case ERarity::Mythic:
        return game_config::default_antegg_reload_mythic;
    case ERarity::Ultra:
        return game_config::default_antegg_reload_ultra;
    case ERarity::Exotic:
        return game_config::default_antegg_reload_common;
    case ERarity::Super:
        return game_config::default_antegg_reload_super;
    case ERarity::Eternal:
        return game_config::default_antegg_reload_eternal;
    case ERarity::Unique:
        return game_config::default_antegg_reload_unique;
    case ERarity::Primordial:
        return game_config::default_antegg_reload_primordial;
    default:
        return 0.f;
    }
}

class CAntennaeBehavior : public CPetalBehavior
{
  public:
    bool IsOpen() const override { return true; }

    SFlowerStats GetStats(ERarity rarity) const override
    {
        SFlowerStats stats = EmptyFlowerStats();
        stats.horizon = game_config::default_horizon * AntennaeHorizonMultiplier(rarity);
        return stats;
    }

    SPetalStats GetPetalStats(ERarity) const override
    {
        SPetalStats stats;
        stats.stack = false;
        stats.reload = 0.f;
        stats.preload = 0.f;
        stats.copy = 0;
        stats.radius = 0.f;
        return stats;
    }

    void OnTick(CPetal*, ERarity, CFlower*, float) override {}
    void OnFlowerTakeDamage(CPetal*, ERarity, CFlower*, float&, EDamageType, CEntity*) override {}
    void OnPetalSpawned(CPetal*, ERarity, CFlower*) override {}
    void OnPetalDestroyed(CPetal*, ERarity, CFlower*) override {}
};

class CDouliBehavior final : public CPetalBehavior
{
  public:
    bool IsOpen() const override { return true; }

    SFlowerStats GetStats(ERarity) const override { return EmptyFlowerStats(); }

    SPetalStats GetPetalStats(ERarity) const override
    {
        SPetalStats stats;
        stats.stack = false;
        stats.reload = 0.f;
        stats.preload = 0.f;
        stats.copy = game_config::default_douli_copy;
        stats.radius = 0.f;
        return stats;
    }

    void OnTick(CPetal*, ERarity, CFlower*, float) override {}
    void OnFlowerTakeDamage(CPetal*, ERarity, CFlower*, float&, EDamageType, CEntity*) override {}
    void OnPetalSpawned(CPetal*, ERarity, CFlower*) override {}
    void OnPetalDestroyed(CPetal*, ERarity, CFlower*) override {}
};

inline float ThirdEyeReachBonus(ERarity rarity)
{
    switch (rarity)
    {
    case ERarity::Legendary:
        return game_config::default_third_eye_reach_legendary;
    case ERarity::Mythic:
        return game_config::default_third_eye_reach_mythic;
    case ERarity::Ultra:
        return game_config::default_third_eye_reach_ultra;
    case ERarity::Exotic:
        return 0.f;
    case ERarity::Super:
        return game_config::default_third_eye_reach_super;
    case ERarity::Eternal:
        return game_config::default_third_eye_reach_eternal;
    case ERarity::Unique:
        return game_config::default_third_eye_reach_unique;
    case ERarity::Primordial:
        return game_config::default_third_eye_reach_primordial;
    default:
        return 0.f;
    }
}

class CThirdEyeBehavior : public CPetalBehavior
{
  public:
    bool IsOpen() const override { return true; }

    SFlowerStats GetStats(ERarity rarity) const override
    {
        SFlowerStats stats = EmptyFlowerStats();
        stats.reach = ThirdEyeReachBonus(rarity);
        return stats;
    }

    SPetalStats GetPetalStats(ERarity) const override
    {
        SPetalStats stats;
        stats.stack = false;
        stats.reload = 0.f;
        stats.preload = 0.f;
        stats.copy = 0;
        stats.radius = 0.f;
        return stats;
    }

    void OnTick(CPetal*, ERarity, CFlower*, float) override {}
    void OnFlowerTakeDamage(CPetal*, ERarity, CFlower*, float&, EDamageType, CEntity*) override {}
    void OnPetalSpawned(CPetal*, ERarity, CFlower*) override {}
    void OnPetalDestroyed(CPetal*, ERarity, CFlower*) override {}
};

class CBasicBehavior : public CPetalBehavior
{
  public:
    bool IsOpen() const override { return true; }

    SFlowerStats GetStats(ERarity) const override { return EmptyFlowerStats(); }

    SPetalStats GetPetalStats(ERarity rarity) const override
    {
        SPetalStats stats;
        stats.damage = game_config::default_basic_base_damage * GetPetalRarityScale(rarity);
        stats.health = game_config::default_basic_base_health * GetPetalRarityScale(rarity);
        stats.reload = game_config::default_basic_reload;
        stats.preload = game_config::default_basic_reload;
        stats.copy = static_cast<int>(game_config::default_basic_copy);
        stats.mass = game_config::default_basic_mass;
        stats.radius = game_config::default_basic_base_radius;
        return stats;
    }

    void OnTick(CPetal* owner, ERarity, CFlower* flower, float dt) override
    {
        float orbit_distance = GetOrbitDistance(owner, flower);
        MovePetalToOrbitAndAttract(owner, flower, orbit_distance, game_config::default_petal_orbit_k, true, dt);
    }

    void OnFlowerTakeDamage(CPetal*, ERarity, CFlower*, float&, EDamageType, CEntity*) override {}
    void OnPetalSpawned(CPetal*, ERarity, CFlower*) override {}
    void OnPetalDestroyed(CPetal*, ERarity, CFlower*) override {}
};

inline int LightCopy(ERarity rarity)
{
    int level = GetLevel(rarity);
    if (level <= GetLevel(ERarity::Common)) return game_config::default_light_copy_common;
    if (level <= GetLevel(ERarity::Rare)) return game_config::default_light_copy_rare;
    if (level <= GetLevel(ERarity::Legendary)) return game_config::default_light_copy_legendary;
    return game_config::default_light_copy_high;
}

inline int PollenCopy(ERarity rarity)
{
    int level = GetLevel(rarity);
    if (level <= GetLevel(ERarity::Common)) return game_config::default_pollen_copy_common;
    if (level <= GetLevel(ERarity::Rare)) return game_config::default_pollen_copy_rare;
    return game_config::default_pollen_copy_high;
}

class CLightBehavior : public CPetalBehavior
{
  public:
    bool IsOpen() const override { return true; }

    SFlowerStats GetStats(ERarity) const override { return EmptyFlowerStats(); }

    SPetalStats GetPetalStats(ERarity rarity) const override
    {
        int copy = LightCopy(rarity);
        float scale = GetPetalRarityScale(rarity);

        SPetalStats stats;
        stats.damage = game_config::default_light_base_damage * scale;
        stats.health = game_config::default_light_base_health * scale;
        stats.reload = game_config::default_light_reload;
        stats.preload = game_config::default_light_reload;
        stats.copy = copy;
        stats.mass = game_config::default_light_mass;
        stats.radius = game_config::default_light_base_radius;
        return stats;
    }

    void OnTick(CPetal* owner, ERarity, CFlower* flower, float dt) override
    {
        MovePetalToOrbitAndAttract(owner, flower, GetOrbitDistance(owner, flower), game_config::default_petal_orbit_k,
                                 true, dt);
    }

    void OnFlowerTakeDamage(CPetal*, ERarity, CFlower*, float&, EDamageType, CEntity*) override {}
    void OnPetalSpawned(CPetal*, ERarity, CFlower*) override {}
    void OnPetalDestroyed(CPetal*, ERarity, CFlower*) override {}
};

class CCornBehavior : public CPetalBehavior
{
  public:
    bool IsOpen() const override { return true; }

    SFlowerStats GetStats(ERarity) const override { return EmptyFlowerStats(); }

    SPetalStats GetPetalStats(ERarity rarity) const override
    {
        SPetalStats stats;
        stats.damage = game_config::default_corn_base_damage * GetPetalRarityScale(rarity);
        stats.health = game_config::default_corn_base_health * GetPetalRarityScale(rarity);
        stats.reload = game_config::default_corn_reload;
        stats.preload = game_config::default_corn_reload;
        stats.copy = static_cast<int>(game_config::default_corn_copy);
        stats.mass = game_config::default_corn_mass;
        stats.radius = game_config::default_corn_base_radius;
        return stats;
    }

    void OnTick(CPetal* owner, ERarity, CFlower* flower, float dt) override
    {
        MovePetalToOrbitAndAttract(owner, flower, GetOrbitDistance(owner, flower), game_config::default_petal_orbit_k,
                                 true, dt);
    }

    void OnFlowerTakeDamage(CPetal*, ERarity, CFlower*, float&, EDamageType, CEntity*) override {}
    void OnPetalSpawned(CPetal*, ERarity, CFlower*) override {}
    void OnPetalDestroyed(CPetal*, ERarity, CFlower*) override {}
};

class CRiceBehavior : public CPetalBehavior
{
  public:
    bool IsOpen() const override { return true; }

    SFlowerStats GetStats(ERarity) const override { return EmptyFlowerStats(); }

    SPetalStats GetPetalStats(ERarity rarity) const override
    {
        SPetalStats stats;
        stats.damage = game_config::default_rice_base_damage * GetPetalRarityScale(rarity);
        stats.health = game_config::default_rice_base_health * GetPetalRarityScale(rarity);
        stats.reload = game_config::default_rice_reload;
        stats.preload = game_config::default_rice_reload;
        stats.copy = static_cast<int>(game_config::default_rice_copy);
        stats.mass = game_config::default_rice_mass;
        stats.radius = game_config::default_rice_base_radius;
        return stats;
    }

    void OnTick(CPetal* owner, ERarity, CFlower* flower, float dt) override
    {
        MovePetalToOrbitAndAttract(owner, flower, GetOrbitDistance(owner, flower), game_config::default_petal_orbit_k,
                                 true, dt);
    }

    void OnFlowerTakeDamage(CPetal*, ERarity, CFlower*, float&, EDamageType, CEntity*) override {}
    void OnPetalSpawned(CPetal*, ERarity, CFlower*) override {}
    void OnPetalDestroyed(CPetal*, ERarity, CFlower*) override {}
};

class CRockPetalBehavior : public CPetalBehavior
{
  public:
    bool IsOpen() const override { return true; }

    SFlowerStats GetStats(ERarity) const override { return EmptyFlowerStats(); }

    SPetalStats GetPetalStats(ERarity rarity) const override
    {
        SPetalStats stats;
        stats.damage = game_config::default_rock_petal_base_damage * GetPetalRarityScale(rarity);
        stats.health = game_config::default_rock_petal_base_health * GetPetalRarityScale(rarity);
        stats.reload = game_config::default_rock_petal_reload;
        stats.preload = game_config::default_rock_petal_reload;
        stats.copy = static_cast<int>(game_config::default_rock_petal_copy);
        stats.mass = game_config::default_rock_petal_mass;
        stats.radius = game_config::default_rock_petal_base_radius;
        return stats;
    }

    void OnTick(CPetal* owner, ERarity, CFlower* flower, float dt) override
    {
        MovePetalToOrbitAndAttract(owner, flower, GetOrbitDistance(owner, flower), game_config::default_petal_orbit_k,
                                 true, dt);
    }

    void OnFlowerTakeDamage(CPetal*, ERarity, CFlower*, float&, EDamageType, CEntity*) override {}
    void OnPetalSpawned(CPetal*, ERarity, CFlower*) override {}
    void OnPetalDestroyed(CPetal*, ERarity, CFlower*) override {}
};

class CCactusBehavior : public CPetalBehavior
{
  public:
    bool IsOpen() const override { return true; }
    EPetalBonusMode GetBonusMode() const override { return EPetalBonusMode::ReloadKeepsBonus; }

    SFlowerStats GetStats(ERarity rarity) const override
    {
        SFlowerStats stats = EmptyFlowerStats();
        stats.max_health = game_config::default_cactus_flower_health * GetPetalRarityScale(rarity);
        return stats;
    }

    SPetalStats GetPetalStats(ERarity rarity) const override
    {
        SPetalStats stats;
        stats.damage = game_config::default_cactus_base_damage * GetPetalRarityScale(rarity);
        stats.health = game_config::default_cactus_base_health * GetPetalRarityScale(rarity);
        stats.reload = game_config::default_cactus_reload;
        stats.preload = game_config::default_cactus_reload;
        stats.copy = static_cast<int>(game_config::default_cactus_copy);
        stats.mass = game_config::default_cactus_mass;
        stats.radius = game_config::default_cactus_base_radius;
        return stats;
    }

    void OnTick(CPetal* owner, ERarity, CFlower* flower, float dt) override
    {
        MovePetalToOrbitAndAttract(owner, flower, GetOrbitDistance(owner, flower), game_config::default_petal_orbit_k,
                                 true, dt);
    }

    void OnFlowerTakeDamage(CPetal*, ERarity, CFlower*, float&, EDamageType, CEntity*) override {}
    void OnPetalSpawned(CPetal*, ERarity, CFlower*) override {}
    void OnPetalDestroyed(CPetal*, ERarity, CFlower*) override {}
};

class CSoilBehavior : public CPetalBehavior
{
  public:
    bool IsOpen() const override { return true; }
    EPetalBonusMode GetBonusMode() const override { return EPetalBonusMode::ReloadKeepsBonus; }

    SFlowerStats GetStats(ERarity rarity) const override
    {
        SFlowerStats stats = EmptyFlowerStats();
        stats.max_health = game_config::default_soil_flower_health * GetPetalRarityScale(rarity);
        stats.radius = game_config::default_soil_flower_radius;
        return stats;
    }

    SPetalStats GetPetalStats(ERarity rarity) const override
    {
        SPetalStats stats;
        stats.damage = game_config::default_soil_base_damage * GetPetalRarityScale(rarity);
        stats.health = game_config::default_soil_base_health * GetPetalRarityScale(rarity);
        stats.reload = game_config::default_soil_reload;
        stats.preload = game_config::default_soil_reload;
        stats.copy = static_cast<int>(game_config::default_soil_copy);
        stats.mass = game_config::default_soil_mass;
        stats.radius = game_config::default_soil_base_radius;
        return stats;
    }

    void OnTick(CPetal* owner, ERarity, CFlower* flower, float dt) override
    {
        MovePetalToOrbitAndAttract(owner, flower, GetOrbitDistance(owner, flower), game_config::default_petal_orbit_k,
                                 true, dt);
    }

    void OnFlowerTakeDamage(CPetal*, ERarity, CFlower*, float&, EDamageType, CEntity*) override {}
    void OnPetalSpawned(CPetal*, ERarity, CFlower*) override {}
    void OnPetalDestroyed(CPetal*, ERarity, CFlower*) override {}
};

inline float BasilHealingReceivedBonus(ERarity rarity)
{
    switch (rarity)
    {
    case ERarity::Common:
        return game_config::default_basil_healing_received_common;
    case ERarity::Unusual:
        return game_config::default_basil_healing_received_unusual;
    case ERarity::Rare:
        return game_config::default_basil_healing_received_rare;
    case ERarity::Epic:
        return game_config::default_basil_healing_received_epic;
    case ERarity::Legendary:
        return game_config::default_basil_healing_received_legendary;
    case ERarity::Mythic:
        return game_config::default_basil_healing_received_mythic;
    case ERarity::Ultra:
        return game_config::default_basil_healing_received_ultra;
    case ERarity::Exotic:
        return game_config::default_basil_healing_received_common;
    case ERarity::Super:
        return game_config::default_basil_healing_received_super;
    case ERarity::Eternal:
        return game_config::default_basil_healing_received_eternal;
    case ERarity::Unique:
        return game_config::default_basil_healing_received_unique;
    case ERarity::Primordial:
        return game_config::default_basil_healing_received_primordial;
    default:
        return 0.0f;
    }
}

class CBasilBehavior : public CPetalBehavior
{
  public:
    bool IsOpen() const override { return true; }

    SFlowerStats GetStats(ERarity rarity) const override
    {
        SFlowerStats stats = EmptyFlowerStats();
        stats.healing_received_multiplier = 1.0f + BasilHealingReceivedBonus(rarity);
        return stats;
    }

    SPetalStats GetPetalStats(ERarity rarity) const override
    {
        SPetalStats stats;
        stats.damage = 0.f;
        stats.health = game_config::default_basil_base_health * GetPetalRarityScale(rarity);
        stats.reload = game_config::default_basil_reload;
        stats.preload = game_config::default_basil_reload;
        stats.copy = static_cast<int>(game_config::default_basil_copy);
        stats.mass = game_config::default_basil_mass;
        stats.radius = game_config::default_basil_base_radius;
        return stats;
    }

    void OnTick(CPetal* owner, ERarity, CFlower* flower, float) override
    {
        MovePetalToOrbit(owner, flower, GetNeutralOrbitDistance(owner, flower), game_config::default_petal_orbit_k,
                       true);
    }

    void OnFlowerTakeDamage(CPetal*, ERarity, CFlower*, float&, EDamageType, CEntity*) override {}
    void OnPetalSpawned(CPetal*, ERarity, CFlower*) override {}
    void OnPetalDestroyed(CPetal*, ERarity, CFlower*) override {}
};

inline float LightningChainDamage(ERarity rarity)
{
    return game_config::default_lightning_base_damage * GetPetalRarityScale(rarity);
}

inline int LightningChainBounces(ERarity rarity)
{
    return static_cast<int>(game_config::default_lightning_bounces) + GetPetalLevel(rarity) - 1;
}

inline float BatteryChainDamage(ERarity rarity)
{
    return game_config::default_battery_base_damage * GetPetalRarityScale(rarity);
}

inline int BatteryChainBounces(ERarity rarity)
{
    return static_cast<int>(game_config::default_battery_bounces) + GetPetalLevel(rarity) - 1;
}

class CLightningBehavior : public CPetalBehavior
{
  public:
    bool IsOpen() const override { return true; }

    SFlowerStats GetStats(ERarity) const override { return EmptyFlowerStats(); }

    SPetalStats GetPetalStats(ERarity rarity) const override
    {
        SPetalStats stats;
        stats.damage = game_config::default_lightning_base_damage * GetPetalRarityScale(rarity);
        stats.health = game_config::default_lightning_base_health * GetPetalRarityScale(rarity);
        stats.reload = game_config::default_lightning_reload;
        stats.preload = game_config::default_lightning_reload;
        stats.copy = static_cast<int>(game_config::default_lightning_copy);
        stats.mass = game_config::default_lightning_mass;
        stats.radius = game_config::default_lightning_base_radius;
        return stats;
    }

    void OnTick(CPetal* owner, ERarity rarity, CFlower* flower, float dt) override
    {
        // Contact ended: release the accumulated chain once.
        if (owner && owner->m_chain_target_id >= 0 && owner->m_chain_hit_count > 0)
        {
            CGameWorld* world = owner->GameWorld();
            CEntity* target =
                world ? world->GetEntity(owner->m_chain_target_id, owner->m_chain_target_generation) : nullptr;
            if (!target || target->IsMarkedForDes() || target->IsDead() ||
                !IsStillTouching(owner, target))
                ReleaseChain(owner, rarity, flower);
        }
        MovePetalToOrbitAndAttract(owner, flower, GetOrbitDistance(owner, flower), game_config::default_petal_orbit_k,
                                 true, dt);
    }

    void OnFlowerTakeDamage(CPetal*, ERarity, CFlower*, float&, EDamageType, CEntity*) override {}
    void OnPetalSpawned(CPetal* owner, ERarity, CFlower*) override
    {
        if (!owner) return;
        owner->m_chain_target_id = -1;
        owner->m_chain_target_generation = 0;
        owner->m_chain_hit_count = 0;
    }

    void OnPetalDestroyed(CPetal* owner, ERarity rarity, CFlower* flower) override
    {
        if (owner && owner->m_chain_hit_count > 0) ReleaseChain(owner, rarity, flower);
    }

    void OnPetalHit(CPetal* owner, ERarity rarity, CEntity* target, float& damage) override
    {
        // The hit deals its normal collision damage as-is; the chain is only
        // booked here and fires when the contact session ends.
        if (!owner || !target || damage <= 0.f) return;
        if (!dynamic_cast<const CMobBase*>(target)) return;
        if (CheckTeam(owner->Team(), target->Team())) return;

        if (owner->m_chain_target_id != target->Id() ||
            owner->m_chain_target_generation != target->Generation())
        {
            // Switched target mid-swing: flush the previous session first.
            if (owner->m_chain_hit_count > 0) ReleaseChain(owner, rarity, owner->GetOwner());
            owner->m_chain_target_id = target->Id();
            owner->m_chain_target_generation = target->Generation();
            owner->m_chain_hit_count = 1;
        } else ++owner->m_chain_hit_count;
    }

  private:
    static bool IsStillTouching(const CPetal* owner, const CEntity* target)
    {
        if (!owner || !target) return false;
        const float contact = owner->Radius() + target->Radius();
        return LengthSq(target->Pos() - owner->Pos()) <= contact * contact + game_config::entity_collision_epsilon;
    }

    static void ReleaseChain(CPetal* owner, ERarity rarity, CEntity* flower)
    {
        if (!owner || !owner->GameWorld()) return;
        const int count = owner->m_chain_hit_count;
        owner->m_chain_target_id = -1;
        owner->m_chain_target_generation = 0;
        owner->m_chain_hit_count = 0;
        if (count <= 0) return;
        if (!flower) return;

        // The chain fires from the petal position even when the recorded
        // target was one-shot: it arcs to whatever hostiles remain nearby.
        owner->GameWorld()->ChainLightning(*owner, LightningChainDamage(rarity) * static_cast<float>(count),
                                           LightningChainBounces(rarity), flower);
    }
};

class CBatteryBehavior : public CPetalBehavior
{
  public:
    bool IsOpen() const override { return true; }

    SFlowerStats GetStats(ERarity) const override { return EmptyFlowerStats(); }

    SPetalStats GetPetalStats(ERarity rarity) const override
    {
        SPetalStats stats;
        stats.damage = 0.f;
        stats.health = game_config::default_battery_base_health * GetPetalRarityScale(rarity);
        stats.reload = game_config::default_battery_reload;
        stats.preload = game_config::default_battery_reload;
        stats.copy = static_cast<int>(game_config::default_battery_copy);
        stats.mass = game_config::default_battery_mass;
        stats.radius = game_config::default_battery_base_radius;
        return stats;
    }

    void OnTick(CPetal* owner, ERarity, CFlower* flower, float) override
    {
        MovePetalToOrbit(owner, flower, GetNeutralOrbitDistance(owner, flower), game_config::default_petal_orbit_k,
                       true);
    }

    void OnFlowerTakeDamage(CPetal* owner, ERarity rarity, CFlower* flower, float& dmg, EDamageType damage_type,
                            CEntity*) override
    {
        if (!owner || !flower) return;
        if (dmg <= 0.f || damage_type == EDamageType::Poison) return;
        auto* battery = dynamic_cast<CBatteryPetal*>(owner);
        if (!battery || battery->m_charges_remaining <= 0) return;

        --battery->m_charges_remaining;

        CGameWorld* world = owner->GameWorld();
        if (world)
            world->ChainLightning(*flower, BatteryChainDamage(rarity), BatteryChainBounces(rarity), flower);

        if (battery->m_charges_remaining <= 0) battery->Health() = 0.f;
    }

    void OnPetalSpawned(CPetal* owner, ERarity, CFlower*) override
    {
        if (auto* battery = dynamic_cast<CBatteryPetal*>(owner))
            battery->m_charges_remaining = static_cast<int>(game_config::default_battery_charges);
    }

    void OnPetalDestroyed(CPetal*, ERarity, CFlower*) override {}
};

inline float BulbRadius(ERarity rarity)
{
    return game_config::default_bulb_detect_radius +
           game_config::default_bulb_detect_radius_step * static_cast<float>(GetLevel(rarity) - 1);
}

class CBulbBehavior : public CPetalBehavior
{
  public:
    bool IsOpen() const override { return true; }
    EPetalBonusMode GetBonusMode() const override { return EPetalBonusMode::PreloadKeepsBonus; }

    SFlowerStats GetStats(ERarity rarity) const override
    {
        SFlowerStats stats = EmptyFlowerStats();
        stats.detection_range_bonus = BulbRadius(rarity);
        return stats;
    }

    SPetalStats GetPetalStats(ERarity rarity) const override
    {
        SPetalStats stats;
        stats.damage = game_config::default_bulb_base_damage * GetPetalRarityScale(rarity);
        stats.health = game_config::default_bulb_base_health * GetPetalRarityScale(rarity);
        stats.reload = game_config::default_bulb_reload;
        stats.preload = game_config::default_bulb_preload;
        stats.copy = static_cast<int>(game_config::default_bulb_copy);
        stats.mass = game_config::default_bulb_mass;
        stats.radius = game_config::default_basic_base_radius;
        stats.stack = false;
        return stats;
    }

    void OnTick(CPetal* owner, ERarity, CFlower* flower, float) override
    {
        MovePetalToOrbit(owner, flower, GetNeutralOrbitDistance(owner, flower), game_config::default_petal_orbit_k,
                       true);
    }

    void OnFlowerTakeDamage(CPetal*, ERarity, CFlower*, float&, EDamageType, CEntity*) override {}
    void OnPetalSpawned(CPetal*, ERarity, CFlower*) override {}
    void OnPetalDestroyed(CPetal*, ERarity, CFlower*) override {}
};

class CBoneBehavior : public CPetalBehavior
{
  public:
    bool IsOpen() const override { return true; }

    SFlowerStats GetStats(ERarity) const override { return EmptyFlowerStats(); }

    SPetalStats GetPetalStats(ERarity rarity) const override
    {
        SPetalStats stats;
        stats.damage = game_config::default_bone_base_damage * GetPetalRarityScale(rarity);
        stats.health = game_config::default_bone_base_health * GetPetalRarityScale(rarity);
        stats.armor = game_config::default_bone_base_armor * GetPetalRarityScale(rarity);
        stats.reload = game_config::default_bone_reload;
        stats.preload = game_config::default_bone_reload;
        stats.copy = static_cast<int>(game_config::default_bone_copy);
        stats.mass = game_config::default_bone_mass;
        stats.radius = game_config::default_bone_base_radius;
        return stats;
    }

    void OnTick(CPetal* owner, ERarity, CFlower* flower, float dt) override
    {
        MovePetalToOrbitAndAttract(owner, flower, GetOrbitDistance(owner, flower), game_config::default_petal_orbit_k,
                                 true, dt);
    }

    void OnFlowerTakeDamage(CPetal*, ERarity, CFlower*, float&, EDamageType, CEntity*) override {}
    void OnPetalSpawned(CPetal*, ERarity, CFlower*) override {}
    void OnPetalDestroyed(CPetal*, ERarity, CFlower*) override {}
};

class CCoinBehavior : public CPetalBehavior
{
  public:
    bool IsOpen() const override { return true; }

    SFlowerStats GetStats(ERarity) const override { return EmptyFlowerStats(); }

    SPetalStats GetPetalStats(ERarity rarity) const override
    {
        SPetalStats stats;
        stats.damage = game_config::default_coin_base_damage * GetPetalRarityScale(rarity);
        stats.health = game_config::default_coin_base_health * GetPetalRarityScale(rarity);
        stats.reload = game_config::default_coin_reload;
        stats.preload = game_config::default_coin_reload;
        stats.copy = static_cast<int>(game_config::default_coin_copy);
        stats.mass = game_config::default_coin_mass;
        stats.radius = game_config::default_coin_base_radius;
        return stats;
    }

    void OnTick(CPetal* owner, ERarity, CFlower* flower, float dt) override
    {
        MovePetalToOrbitAndAttract(owner, flower, GetOrbitDistance(owner, flower), game_config::default_petal_orbit_k,
                                 true, dt);
    }

    void OnFlowerTakeDamage(CPetal*, ERarity, CFlower*, float&, EDamageType, CEntity*) override {}
    void OnPetalSpawned(CPetal*, ERarity, CFlower*) override {}
    void OnPetalDestroyed(CPetal*, ERarity, CFlower*) override {}
};

class CHeavyBehavior : public CPetalBehavior
{
  public:
    bool IsOpen() const override { return true; }

    SFlowerStats GetStats(ERarity) const override
    {
        SFlowerStats stats = EmptyFlowerStats();
        stats.petal_rotation_speed = game_config::default_heavy_rotation_speed;
        return stats;
    }

    SPetalStats GetPetalStats(ERarity rarity) const override
    {
        float level = GetPetalValueLevel(rarity);
        SPetalStats stats;
        stats.damage = game_config::default_heavy_base_damage * GetPetalRarityScale(rarity);
        stats.health = game_config::default_heavy_base_health * GetPetalRarityScale(rarity);
        stats.reload = game_config::default_heavy_reload;
        stats.preload = game_config::default_heavy_reload;
        stats.copy = static_cast<int>(game_config::default_heavy_copy);
        float mass_level_sq = level * level;
        stats.mass = mass_level_sq * game_config::default_heavy_mass_multiplier;
        stats.radius = game_config::default_heavy_base_radius;
        return stats;
    }

    void OnTick(CPetal* owner, ERarity, CFlower* flower, float dt) override
    {
        if (flower && flower->m_defending)
        {
            float orbit_distance = GetBaseOrbitRadius(owner, flower) + game_config::default_petal_orbit_radius +
                                   GetOrbitReach(owner, flower, false);
            MovePetalToOrbitAndAttract(owner, flower, orbit_distance, game_config::default_petal_orbit_k, true, dt);
        }
        else
        {
            MovePetalToOrbitAndAttract(owner, flower, GetOrbitDistance(owner, flower),
                                     game_config::default_petal_orbit_k, true, dt);
        }

        if (!owner || !flower || !flower->GameWorld() || owner->m_target_entity_id < 0) return;

        CEntity* target = owner->m_target_entity_generation != 0
                              ? flower->GameWorld()->GetEntity(owner->m_target_entity_id,
                                                               owner->m_target_entity_generation)
                              : flower->GameWorld()->GetEntity(owner->m_target_entity_id);
        if (!target) return;

        const float distance = Distance(owner->Pos(), target->Pos());
        const float contact = owner->Radius() + target->Radius();
        const float slow_range = contact + std::max(0.f, game_config::default_heavy_target_slowdown_range);
        if (distance >= slow_range) return;

        const float t = std::clamp((distance - contact) /
                                       std::max(game_config::entity_collision_epsilon, slow_range - contact),
                                   0.f, 1.f);
        const float scale = game_config::default_heavy_target_min_speed_scale +
                            (1.f - game_config::default_heavy_target_min_speed_scale) * t;
        // Heavy pressure slows the target it hit, not the petal itself.
        if (auto* mob = dynamic_cast<CMobBase*>(target)) mob->m_vel *= scale;
    }

    void OnPetalHit(CPetal* owner, ERarity, CEntity* target, float&) override { SetPetalTarget(owner, target); }

    void OnFlowerTakeDamage(CPetal*, ERarity, CFlower*, float&, EDamageType, CEntity*) override {}
    void OnPetalSpawned(CPetal*, ERarity, CFlower*) override {}
    void OnPetalDestroyed(CPetal*, ERarity, CFlower*) override {}
};

class CFasterBehavior : public CPetalBehavior
{
  public:
    bool IsOpen() const override { return true; }

    EPetalBonusMode GetBonusMode() const override { return EPetalBonusMode::ReloadKeepsBonus; }

    SFlowerStats GetStats(ERarity rarity) const override
    {
        SFlowerStats stats = EmptyFlowerStats();
        stats.petal_rotation_speed = game_config::default_faster_rotation_speed_base +
                                     game_config::default_faster_rotation_speed_level * GetPetalValueLevel(rarity);
        return stats;
    }

    SPetalStats GetPetalStats(ERarity rarity) const override
    {
        SPetalStats stats;
        stats.damage = game_config::default_faster_base_damage * GetPetalRarityScale(rarity);
        stats.health = game_config::default_faster_base_health * GetPetalRarityScale(rarity);
        stats.reload = game_config::default_faster_reload;
        stats.preload = game_config::default_faster_reload;
        stats.copy = static_cast<int>(game_config::default_faster_copy);
        stats.mass = game_config::default_faster_mass;
        stats.radius = game_config::default_faster_base_radius;
        return stats;
    }

    void OnTick(CPetal* owner, ERarity, CFlower* flower, float dt) override
    {
        MovePetalToOrbitAndAttract(owner, flower, GetOrbitDistance(owner, flower), game_config::default_petal_orbit_k,
                                 true, dt);
    }

    void OnFlowerTakeDamage(CPetal*, ERarity, CFlower*, float&, EDamageType, CEntity*) override {}
    void OnPetalSpawned(CPetal*, ERarity, CFlower*) override {}
    void OnPetalDestroyed(CPetal*, ERarity, CFlower*) override {}
};

class CGlassBehavior : public CPetalBehavior
{
  public:
    bool IsOpen() const override { return true; }

    SFlowerStats GetStats(ERarity) const override { return EmptyFlowerStats(); }

    SPetalStats GetPetalStats(ERarity rarity) const override
    {
        SPetalStats stats;
        stats.damage = game_config::default_glass_base_damage * GetPetalRarityScale(rarity);
        stats.health = game_config::default_glass_base_health * GetPetalRarityScale(rarity);
        stats.reload = game_config::default_glass_reload;
        stats.preload = game_config::default_glass_reload;
        stats.copy = static_cast<int>(game_config::default_glass_copy);
        stats.mass = game_config::default_glass_mass;
        stats.radius = game_config::default_glass_base_radius;
        return stats;
    }

    void OnTick(CPetal* owner, ERarity, CFlower* flower, float dt) override
    {
        auto* glass = dynamic_cast<CGlassPetal*>(owner);
        if (glass)
        {
            for (auto it = glass->m_hit_cooldowns.begin(); it != glass->m_hit_cooldowns.end();)
            {
                it->second -= dt;
                if (it->second <= 0.f) it = glass->m_hit_cooldowns.erase(it);
                else ++it;
            }
        }

        MovePetalToOrbitAndAttract(owner, flower, GetOrbitDistance(owner, flower), game_config::default_petal_orbit_k,
                                 true, dt);
    }

    void OnPetalHit(CPetal* owner, ERarity, CEntity* target, float& damage) override
    {
        auto* glass = dynamic_cast<CGlassPetal*>(owner);
        if (!glass || !target) return;
        auto it = glass->m_hit_cooldowns.find(target->Id());
        if (it != glass->m_hit_cooldowns.end() && it->second > 0.f)
        {
            damage = 0.f;
            return;
        }

        damage = glass->m_final_petal_stats.damage;
        glass->m_hit_cooldowns[target->Id()] = game_config::default_glass_hit_cooldown;
    }

    void OnFlowerTakeDamage(CPetal*, ERarity, CFlower*, float&, EDamageType, CEntity*) override {}
    void OnPetalSpawned(CPetal*, ERarity, CFlower*) override {}
    void OnPetalDestroyed(CPetal*, ERarity, CFlower*) override {}
};

class CStingerBehavior : public CPetalBehavior
{
  public:
    bool IsOpen() const override { return false; }

    SFlowerStats GetStats(ERarity) const override { return EmptyFlowerStats(); }

    SPetalStats GetPetalStats(ERarity rarity) const override
    {
        int copy = GetLevel(rarity) > GetLevel(ERarity::Legendary) ? 3 : 1;

        SPetalStats stats;
        stats.damage = game_config::default_stinger_base_damage * GetPetalRarityScale(rarity);
        stats.health = game_config::default_stinger_base_health;
        stats.reload = game_config::default_stinger_reload;
        stats.preload = game_config::default_stinger_reload;
        stats.copy = copy;
        stats.mass = game_config::default_stinger_mass;
        stats.radius = game_config::default_stinger_base_radius;
        return stats;
    }

    void OnTick(CPetal* owner, ERarity, CFlower* flower, float dt) override
    {
        MovePetalToOrbitAndAttract(owner, flower, GetOrbitDistance(owner, flower), game_config::default_petal_orbit_k,
                                 false, dt);
    }

    void OnFlowerTakeDamage(CPetal*, ERarity, CFlower*, float&, EDamageType, CEntity*) override {}
    void OnPetalSpawned(CPetal*, ERarity, CFlower*) override {}
    void OnPetalDestroyed(CPetal*, ERarity, CFlower*) override {}
};

inline float BeetleEggReload(ERarity rarity)
{
    switch (rarity)
    {
    case ERarity::Common:
        return game_config::default_beetleegg_reload_common;
    case ERarity::Unusual:
        return game_config::default_beetleegg_reload_unusual;
    case ERarity::Rare:
        return game_config::default_beetleegg_reload_rare;
    case ERarity::Epic:
        return game_config::default_beetleegg_reload_epic;
    case ERarity::Legendary:
        return game_config::default_beetleegg_reload_legendary;
    case ERarity::Mythic:
        return game_config::default_beetleegg_reload_mythic;
    case ERarity::Ultra:
        return game_config::default_beetleegg_reload_ultra;
    case ERarity::Exotic:
        return game_config::default_beetleegg_reload_common;
    case ERarity::Super:
        return game_config::default_beetleegg_reload_super;
    case ERarity::Eternal:
        return game_config::default_beetleegg_reload_eternal;
    case ERarity::Unique:
        return game_config::default_beetleegg_reload_unique;
    case ERarity::Primordial:
        return game_config::default_beetleegg_reload_primordial;
    default:
        return 0.f;
    }
}

inline float BrokenEggSummonedHealthMultiplier(ERarity rarity)
{
    const float first_level = static_cast<float>(GetLevel(ERarity::Common));
    const float pivot_level = static_cast<float>(GetLevel(ERarity::Ultra));
    const float last_level = static_cast<float>(GetLevel(ERarity::Primordial));
    float level = std::clamp(GetSpecialPetalLevel(rarity), first_level, last_level);
    if (level <= pivot_level)
    {
        float t = (level - first_level) / (pivot_level - first_level);
        return game_config::default_brokenegg_summoned_health_common +
               (game_config::default_brokenegg_summoned_health_ultra -
                game_config::default_brokenegg_summoned_health_common) *
                   t;
    }
    float t = (level - pivot_level) / (last_level - pivot_level);
    return game_config::default_brokenegg_summoned_health_ultra +
           (game_config::default_brokenegg_summoned_health_primordial -
            game_config::default_brokenegg_summoned_health_ultra) *
               t;
}

inline float BrokenEggSummonedDamageMultiplier(ERarity rarity)
{
    const float first_level = static_cast<float>(GetLevel(ERarity::Common));
    const float pivot_level = static_cast<float>(GetLevel(ERarity::Ultra));
    const float last_level = static_cast<float>(GetLevel(ERarity::Primordial));
    float level = std::clamp(GetSpecialPetalLevel(rarity), first_level, last_level);
    if (level <= pivot_level)
    {
        float t = (level - first_level) / (pivot_level - first_level);
        return game_config::default_brokenegg_summoned_damage_common +
               (game_config::default_brokenegg_summoned_damage_ultra -
                game_config::default_brokenegg_summoned_damage_common) *
                   t;
    }
    float t = (level - pivot_level) / (last_level - pivot_level);
    return game_config::default_brokenegg_summoned_damage_ultra +
           (game_config::default_brokenegg_summoned_damage_primordial -
            game_config::default_brokenegg_summoned_damage_ultra) *
               t;
}

inline bool HasSummonStatMultiplier(const CFlower* flower)
{
    const SFlowerStats* stats = flower ? flower->GetFinalStats() : nullptr;
    return stats && (std::abs(stats->mult_summoned_health - 1.f) > game_config::entity_collision_epsilon ||
                     std::abs(stats->mult_summoned_damage - 1.f) > game_config::entity_collision_epsilon);
}

inline void ApplySummonedStatMultipliers(CMobBase* summon, const CFlower* flower)
{
    if (!summon || !flower || !flower->GetFinalStats()) return;

    float health_multiplier = std::max(0.f, flower->GetFinalStats()->mult_summoned_health);
    float damage_multiplier = std::max(0.f, flower->GetFinalStats()->mult_summoned_damage);
    if (std::abs(health_multiplier - 1.f) <= game_config::entity_collision_epsilon &&
        std::abs(damage_multiplier - 1.f) <= game_config::entity_collision_epsilon)
        return;

    if (auto* mob = dynamic_cast<CMob<SMobStats>*>(summon))
    {
        mob->m_base_stats.max_health *= health_multiplier;
        mob->m_final_stats.max_health *= health_multiplier;
        mob->m_base_stats.damage *= damage_multiplier;
        mob->m_final_stats.damage *= damage_multiplier;
    }

    summon->Health() = std::max(1.f, summon->Health() * health_multiplier);
}

inline float BubbleReload(ERarity rarity)
{
    switch (rarity)
    {
    case ERarity::Common:
        return game_config::default_bubble_reload_common;
    case ERarity::Unusual:
        return game_config::default_bubble_reload_unusual;
    case ERarity::Rare:
        return game_config::default_bubble_reload_rare;
    case ERarity::Epic:
        return game_config::default_bubble_reload_epic;
    case ERarity::Legendary:
        return game_config::default_bubble_reload_legendary;
    case ERarity::Mythic:
        return game_config::default_bubble_reload_mythic;
    case ERarity::Ultra:
        return game_config::default_bubble_reload_ultra;
    case ERarity::Exotic:
        return game_config::default_bubble_reload_common;
    case ERarity::Super:
        return game_config::default_bubble_reload_super;
    case ERarity::Eternal:
        return game_config::default_bubble_reload_eternal;
    case ERarity::Unique:
        return game_config::default_bubble_reload_unique;
    case ERarity::Primordial:
        return game_config::default_bubble_reload_primordial;
    default:
        return game_config::default_bubble_reload_common;
    }
}

class CSummonEggBehavior : public CPetalBehavior
{
  public:
    explicit CSummonEggBehavior(EMobType summon_type) : m_summon_type(summon_type) {}

    bool IsOpen() const override { return true; }

    bool CountsForPetalLayout(const CPetal* owner) const override
    {
        const auto* egg = dynamic_cast<const CBeetleEggPetal*>(owner);
        return !egg || !egg->m_has_spawned_summon;
    }

    SFlowerStats GetStats(ERarity) const override { return EmptyFlowerStats(); }

    SPetalStats GetPetalStats(ERarity rarity) const override = 0;

    void OnTick(CPetal* owner, ERarity, CFlower* flower, float dt) override
    {
        auto* egg = dynamic_cast<CBeetleEggPetal*>(owner);
        if (!egg || !flower || !flower->GameWorld()) return;

        if (!egg->m_has_spawned_summon)
        {
            MovePetalToOrbitAndAttract(owner, flower, GetOrbitDistance(owner, flower),
                                     game_config::default_petal_orbit_k, true, dt);
            if (HasSummonStatMultiplier(flower) && owner->m_lifetime >= 1.f) owner->Health() = 0.f;
            return;
        }

        CEntity* summon = egg->m_summon_id >= 0
                              ? flower->GameWorld()->GetEntity(egg->m_summon_id, egg->m_summon_generation)
                              : nullptr;
        if (summon && !summon->IsMarkedForDes())
        {
            owner->Health() = std::max(1.f, summon->Health());
            return;
        }

        egg->m_summon_id = -1;
        egg->m_summon_generation = 0;
        owner->Health() = 0.f;
        owner->MarkForDestroy(EEntityRemovalReason::Despawned);
    }

    void OnFlowerTakeDamage(CPetal*, ERarity, CFlower*, float&, EDamageType, CEntity*) override {}

    void OnPetalSpawned(CPetal* owner, ERarity rarity, CFlower* flower) override
    {
        auto* egg = dynamic_cast<CBeetleEggPetal*>(owner);
        if (!egg) return;

        egg->m_summon_id = -1;
        egg->m_summon_generation = 0;
        egg->m_has_spawned_summon = false;
        owner->Health() = 1.f;
        (void)rarity;
        (void)flower;
    }

    void OnPetalCleared(CPetal* owner, ERarity, CFlower* flower) override
    {
        auto* egg = dynamic_cast<CBeetleEggPetal*>(owner);
        if (!egg || !flower || !flower->GameWorld()) return;

        CEntity* summon = egg->m_summon_id >= 0
                              ? flower->GameWorld()->GetEntity(egg->m_summon_id, egg->m_summon_generation)
                              : nullptr;
        if (summon) summon->MarkForDestroy(EEntityRemovalReason::OwnerRemoved);
        egg->m_summon_id = -1;
        egg->m_summon_generation = 0;
        egg->m_has_spawned_summon = false;
    }

    void OnPetalDestroyed(CPetal* owner, ERarity rarity, CFlower* flower) override
    {
        auto* egg = dynamic_cast<CBeetleEggPetal*>(owner);
        if (!egg || !flower || !flower->GameWorld()) return;
        if (egg->m_has_spawned_summon) return;

        ERarity summon_rarity = LowerSummonRarity(rarity);
        florrbt::Vector2f spawn_pos = owner ? owner->Pos() : flower->Pos();
        EMobType summon_type = ResolveSummonType(flower);
        auto summon = CreateMob(summon_type, flower->GameWorld(), spawn_pos, summon_rarity);
        if (!summon)
        {
            owner->MarkForDestroy(EEntityRemovalReason::Despawned);
            return;
        }

        summon->Team() = flower->Team();
        // Petal-created summons must never recursively generate loot, including summons owned by NPC flowers.
        summon->AddTag(EEntityTag::NoDefeatRewards);
        summon->SetController(std::make_unique<CSummonedMeleeController>(flower));
        CMobBase* raw_summon = dynamic_cast<CMobBase*>(flower->GameWorld()->InsertEntity(std::move(summon)));
        if (!raw_summon)
        {
            owner->MarkForDestroy(EEntityRemovalReason::Despawned);
            return;
        }

        ApplySummonedStatMultipliers(raw_summon, flower);
        egg->m_summon_id = raw_summon->Id();
        egg->m_summon_generation = raw_summon->Generation();
        egg->m_has_spawned_summon = true;
        owner->Health() = std::max(1.f, raw_summon->Health());
    }

    bool ShouldReloadAfterPetalDestroyed(CPetal* owner) const override
    {
        auto* egg = dynamic_cast<CBeetleEggPetal*>(owner);
        return !egg || !egg->m_has_spawned_summon || egg->m_summon_id < 0;
    }

  private:
    EMobType ResolveSummonType(CFlower* flower) const
    {
        if (m_summon_type == EMobType::SummonedSoldierAnt && flower && flower->HasState<CPsionicConnectionState>())
            return EMobType::SoldierTermite;
        return m_summon_type;
    }

    static ERarity LowerSummonRarity(ERarity rarity)
    {
        if (rarity == ERarity::Primordial) return ERarity::Eternal;
        if (rarity == ERarity::Unique || rarity == ERarity::Eternal) return ERarity::Super;
        if (rarity == ERarity::Exotic) return ERarity::Common;

        int value = static_cast<int>(rarity);
        int common = static_cast<int>(ERarity::Common);
        return static_cast<ERarity>(std::max(common, value - 1));
    }

    EMobType m_summon_type = EMobType::None;
};

class CAntEggBehavior : public CSummonEggBehavior
{
  public:
    CAntEggBehavior() : CSummonEggBehavior(EMobType::SummonedSoldierAnt) {}

    SPetalStats GetPetalStats(ERarity rarity) const override
    {
        (void)rarity;
        SPetalStats stats;
        stats.health = game_config::default_antegg_base_health;
        stats.reload = AntEggReload(rarity);
        stats.preload = AntEggReload(rarity);
        stats.copy = static_cast<int>(game_config::default_antegg_copy);
        stats.mass = game_config::default_antegg_mass;
        stats.radius = game_config::default_antegg_base_radius;
        return stats;
    }
};

class CBeetleEggBehavior : public CSummonEggBehavior
{
  public:
    CBeetleEggBehavior() : CSummonEggBehavior(EMobType::SummonedBeetle) {}

    SPetalStats GetPetalStats(ERarity rarity) const override
    {
        (void)rarity;
        SPetalStats stats;
        stats.health = game_config::default_beetleegg_base_health;
        stats.reload = BeetleEggReload(rarity);
        stats.preload = BeetleEggReload(rarity);
        stats.copy = static_cast<int>(game_config::default_beetleegg_copy);
        stats.mass = game_config::default_beetleegg_mass;
        stats.radius = game_config::default_beetleegg_base_radius;
        return stats;
    }
};

inline float LinkerChainLength(ERarity rarity)
{
    const float common = std::max(0.001f, game_config::default_linker_chain_length_common);
    const float primordial = std::max(0.001f, game_config::default_linker_chain_length_primordial);
    // Petal value levels deliberately map Exotic back to Common. Keep chain
    // length aligned with every other Linker stat instead of using display
    // sort rank (where Exotic sits between Ultra and Super).
    const int level = std::clamp(GetLevel(rarity), 1, 10);
    const float t = static_cast<float>(level - 1) / 9.f;
    return std::exp(std::log(common) * (1.f - t) + std::log(primordial) * t);
}

inline float LinkerMass(ERarity rarity)
{
    const int level = std::max(1, GetLevel(rarity));
    const double base_mass = std::max(0.001, static_cast<double>(game_config::default_linker_mass));
    // Keep rarity monotonic even if a malformed runtime config supplies a
    // multiplier below one. The double intermediate prevents early overflow.
    const double rarity_multiplier =
        std::max(1.0, static_cast<double>(game_config::default_linker_mass_rarity_multiplier));
    const double mass = base_mass * std::pow(rarity_multiplier, static_cast<double>(level - 1));
    return static_cast<float>(std::clamp(mass, 0.001, static_cast<double>(std::numeric_limits<float>::max())));
}

class CLinkerBehavior final : public CPetalBehavior
{
  public:
    bool IsOpen() const override { return true; }

    bool CountsForPetalLayout(const CPetal* raw_owner) const override
    {
        const auto* owner = dynamic_cast<const CLinkerPetal*>(raw_owner);
        return !owner || !owner->m_released_from_slot;
    }

    SFlowerStats GetStats(ERarity) const override { return EmptyFlowerStats(); }

    SPetalStats GetPetalStats(ERarity rarity) const override
    {
        SPetalStats stats;
        stats.damage = 0.f;
        stats.health = game_config::default_linker_base_health * GetPetalRarityScale(rarity);
        stats.reload = game_config::default_linker_reload;
        stats.preload = game_config::default_linker_reload;
        stats.copy = static_cast<int>(game_config::default_linker_copy);
        stats.mass = LinkerMass(rarity);
        stats.radius = game_config::default_linker_base_radius;
        return stats;
    }

    void OnTick(CPetal* raw_owner, ERarity, CFlower* flower, float dt) override
    {
        auto* owner = dynamic_cast<CLinkerPetal*>(raw_owner);
        if (!owner || !flower || !flower->GameWorld()) return;
        if (owner->m_chain_id >= 0 &&
            !flower->GameWorld()->GetEntity(owner->m_chain_id, owner->m_chain_generation))
        {
            owner->ResetMissingChain();
            return;
        }

        CEntity* first = nullptr;
        if (owner->m_released_from_slot && owner->m_first_target_id >= 0)
        {
            first = flower->GameWorld()->GetEntity(owner->m_first_target_id, owner->m_first_target_generation);
            if (first && !first->IsDead() && !first->IsMarkedForDes())
            {
                // Never re-lock the endpoint already attached to this Linker;
                // it must be free to seek a second mob or return to its orbit.
                SuppressPetalTarget(owner, first);
            }
            else first = nullptr;
        }

        if (!owner->m_released_from_slot)
        {
            MovePetalToOrbitAndAttract(owner, flower, GetOrbitDistance(owner, flower), game_config::default_petal_orbit_k,
                                       true, dt);
        } else if (!owner->m_chain_finalized && !owner->m_hidden)
        {
            if (!owner->m_has_released_orbit_angle)
            {
                const florrbt::Vector2f center = GetOrbitCenter(owner, flower);
                const florrbt::Vector2f radial = owner->Pos() - center;
                const float angle = LengthSq(radial) > game_config::entity_collision_epsilon *
                                                           game_config::entity_collision_epsilon
                                        ? std::atan2(radial.y, radial.x)
                                        : flower->GetPetalRotationAngle();
                owner->m_released_orbit_angle_offset = std::atan2(
                    std::sin(angle - flower->GetPetalRotationAngle()),
                    std::cos(angle - flower->GetPetalRotationAngle()));
                owner->m_has_released_orbit_angle = true;
            }

            const float orbit_distance = GetOrbitDistance(owner, flower);
            const float orbit_angle = flower->GetPetalRotationAngle() + owner->m_released_orbit_angle_offset;
            const florrbt::Vector2f orbit_point =
                GetOrbitCenter(owner, flower) +
                florrbt::Vector2f(std::cos(orbit_angle), std::sin(orbit_angle)) * orbit_distance;
            MovePetalToOrbitPointAndAttract(owner, flower, orbit_point, orbit_distance,
                                            game_config::default_petal_orbit_k, dt);

            // The flower carries a small share of the released Linker's orbit
            // error, while the Linker inherits a small share of flower motion.
            // This is the soft counterpart of the LCS/LeafPiece constraint.
            const float coupling = std::clamp(game_config::default_linker_flower_coupling, 0.f, 1.f);
            const florrbt::Vector2f flower_velocity = flower->m_vel;
            owner->m_vel += flower_velocity * coupling;
            const float linker_mass = std::max(game_config::entity_collision_epsilon, owner->Mass());
            const float flower_mass = std::max(game_config::entity_collision_epsilon, flower->Mass());
            const float linker_weight = linker_mass / (linker_mass + flower_mass);
            florrbt::Vector2f reaction = (owner->Pos() - orbit_point) *
                                         std::max(0.f, game_config::default_petal_orbit_k) * coupling * linker_weight;
            const float reaction_speed = Length(reaction);
            const float reaction_limit = std::max(0.f, game_config::default_linker_flower_max_reaction_speed);
            if (reaction_limit > game_config::entity_collision_epsilon && reaction_speed > reaction_limit)
                reaction *= reaction_limit / reaction_speed;
            flower->m_vel += reaction;
        }

        // Its visual connector continues to point at the first endpoint even
        // while its translational movement seeks another target or the orbit.
        if (first)
        {
            const florrbt::Vector2f delta = first->Pos() - owner->Pos();
            if (LengthSq(delta) > game_config::entity_collision_epsilon * game_config::entity_collision_epsilon)
            {
                owner->FacingAngle() = std::atan2(delta.y, delta.x);
                owner->HasFacing() = true;
            }
        }
    }

    void OnFlowerTakeDamage(CPetal*, ERarity, CFlower*, float&, EDamageType, CEntity*) override {}
    void OnPetalSpawned(CPetal* raw_owner, ERarity, CFlower*) override
    {
        auto* owner = dynamic_cast<CLinkerPetal*>(raw_owner);
        if (!owner) return;
        owner->m_first_target_id = -1;
        owner->m_first_target_generation = 0;
        owner->m_chain_id = -1;
        owner->m_chain_generation = 0;
        owner->m_saved_chain_health = 0.f;
        owner->m_released_orbit_angle_offset = 0.f;
        owner->m_charges_remaining = 2;
        owner->m_released_from_slot = false;
        owner->m_chain_finalized = false;
        owner->m_has_released_orbit_angle = false;
    }

    void OnPetalCleared(CPetal* raw_owner, ERarity, CFlower* flower) override
    {
        auto* owner = dynamic_cast<CLinkerPetal*>(raw_owner);
        if (!owner || !flower || !flower->GameWorld()) return;
        flower->GameWorld()->BreakChainsForEndpoint(*owner);
    }

    void OnPetalDestroyed(CPetal* raw_owner, ERarity, CFlower* flower) override
    {
        auto* owner = dynamic_cast<CLinkerPetal*>(raw_owner);
        if (!owner || !flower || !flower->GameWorld()) return;
        if (!owner->m_chain_finalized && owner->m_chain_id >= 0)
            flower->GameWorld()->BreakChainsForEndpoint(*owner);
    }

    bool ShouldReloadAfterPetalDestroyed(CPetal* raw_owner) const override
    {
        const auto* owner = dynamic_cast<const CLinkerPetal*>(raw_owner);
        return !owner || owner->m_chain_id < 0;
    }

    void OnPetalHit(CPetal* raw_owner, ERarity rarity, CEntity* target, float& damage) override
    {
        damage = 0.f;
        auto* owner = dynamic_cast<CLinkerPetal*>(raw_owner);
        auto* mob = dynamic_cast<CMobBase*>(target);
        if (!owner || !mob || !owner->GameWorld() || mob->IsDead() || mob->IsMarkedForDes()) return;
        if (owner->m_charges_remaining <= 0) return;
        // Different Linkers may share an endpoint.  Only this Linker's own
        // chain is forbidden from selecting the same endpoint twice (checked
        // below with HasEndpoint when the second charge is consumed).
        if (target == owner->GetOwner()) return;

        CGameWorld* world = owner->GameWorld();
        if (owner->m_chain_id < 0)
        {
            if (auto* flower = dynamic_cast<CFlower*>(owner->GetOwner()))
            {
                const florrbt::Vector2f center = GetOrbitCenter(owner, flower);
                const auto orbit = GetGlobalOrbitPosition(owner, flower, GetOrbitDistance(owner, flower), true);
                const florrbt::Vector2f radial = orbit ? *orbit - center : owner->Pos() - center;
                const float angle = LengthSq(radial) > game_config::entity_collision_epsilon *
                                                           game_config::entity_collision_epsilon
                                        ? std::atan2(radial.y, radial.x)
                                        : flower->GetPetalRotationAngle();
                owner->m_released_orbit_angle_offset = std::atan2(
                    std::sin(angle - flower->GetPetalRotationAngle()),
                    std::cos(angle - flower->GetPetalRotationAngle()));
                owner->m_has_released_orbit_angle = true;
            }
            auto chain = std::make_unique<CChainEntity>(world, *owner, *mob, LinkerChainLength(rarity),
                                                        std::max(1.f, owner->Health()), owner);
            CChainEntity* raw_chain = dynamic_cast<CChainEntity*>(world->InsertEntity(std::move(chain)));
            if (!raw_chain) return;
            owner->m_first_target_id = mob->Id();
            owner->m_first_target_generation = mob->Generation();
            owner->m_chain_id = raw_chain->Id();
            owner->m_chain_generation = raw_chain->Generation();
            owner->m_saved_chain_health = std::max(1.f, owner->Health());
            owner->m_charges_remaining = 1;
            owner->m_released_from_slot = true;
            const florrbt::Vector2f delta = mob->Pos() - owner->Pos();
            if (LengthSq(delta) > game_config::entity_collision_epsilon * game_config::entity_collision_epsilon)
            {
                owner->FacingAngle() = std::atan2(delta.y, delta.x);
                owner->HasFacing() = true;
            }
            if (auto* flower = dynamic_cast<CFlower*>(owner->GetOwner())) flower->MarkFinalStatsDirty();
            return;
        }

        CEntity* chain_entity = world->GetEntity(owner->m_chain_id, owner->m_chain_generation);
        auto* chain = dynamic_cast<CChainEntity*>(chain_entity);
        if (!chain || chain->IsBroken() || owner->m_first_target_id < 0) return;
        CEntity* first_target = world->GetEntity(owner->m_first_target_id, owner->m_first_target_generation);
        if (!first_target || first_target == mob || chain->HasEndpoint(*mob)) return;
        if (!chain->ConvertEndpoint(*owner, *mob)) return;

        owner->m_chain_finalized = true;
        owner->m_charges_remaining = 0;
        owner->m_released_from_slot = true;
        owner->m_first_target_id = first_target->Id();
        owner->m_first_target_generation = first_target->Generation();
        // The Linker is the lease owner of this chain.  Do not destroy it
        // here: CPetal::OnBeforeRemoved() would notify the chain lease while
        // the second hit is still being processed and immediately break the
        // newly converted mob--mob chain.  Keep the slot entity alive but
        // hidden; the chain callback will set its health to zero after the
        // chain actually breaks, allowing the next slot tick to reload it.
        owner->m_hidden = true;
        owner->m_vel = { 0.f, 0.f };
        owner->Health() = std::max(1.f, owner->m_final_petal_stats.health);
        owner->CancelDestroy();
        if (auto* flower = dynamic_cast<CFlower*>(owner->GetOwner())) flower->MarkFinalStatsDirty();
    }
};

class CBrokenEggBehavior : public CPetalBehavior
{
  public:
    EPetalBonusMode GetBonusMode() const override { return EPetalBonusMode::PreloadKeepsBonus; }

    SFlowerStats GetStats(ERarity rarity) const override
    {
        SFlowerStats stats = EmptyFlowerStats();
        stats.mult_summoned_health = BrokenEggSummonedHealthMultiplier(rarity);
        stats.mult_summoned_damage = BrokenEggSummonedDamageMultiplier(rarity);
        return stats;
    }

    SPetalStats GetPetalStats(ERarity) const override
    {
        SPetalStats stats;
        stats.stack = false;
        stats.health = game_config::default_brokenegg_base_health;
        stats.damage = game_config::default_brokenegg_base_damage;
        stats.reload = game_config::default_brokenegg_reload;
        stats.preload = game_config::default_brokenegg_reload;
        stats.copy = static_cast<int>(game_config::default_brokenegg_copy);
        stats.mass = game_config::default_brokenegg_mass;
        stats.radius = game_config::default_brokenegg_base_radius;
        return stats;
    }

    void OnTick(CPetal* owner, ERarity, CFlower* flower, float) override
    {
        MovePetalToOrbit(owner, flower, GetNeutralOrbitDistance(owner, flower), game_config::default_petal_orbit_k,
                       false);
    }

    void OnFlowerTakeDamage(CPetal*, ERarity, CFlower*, float&, EDamageType, CEntity*) override {}
    void OnPetalSpawned(CPetal*, ERarity, CFlower*) override {}
    void OnPetalDestroyed(CPetal*, ERarity, CFlower*) override {}
};

class CBubbleBehavior : public CPetalBehavior
{
  public:
    bool IsOpen() const override { return true; }

    SFlowerStats GetStats(ERarity) const override { return EmptyFlowerStats(); }

    SPetalStats GetPetalStats(ERarity rarity) const override
    {
        (void)rarity;
        SPetalStats stats;
        stats.health = game_config::default_bubble_base_health;
        stats.damage = 0.f;
        stats.reload = BubbleReload(rarity);
        stats.preload = game_config::default_bubble_reload_common;
        stats.copy = static_cast<int>(game_config::default_bubble_copy);
        stats.mass = game_config::default_bubble_mass;
        stats.radius = game_config::default_bubble_base_radius;
        return stats;
    }

    void OnTick(CPetal* owner, ERarity rarity, CFlower* flower, float dt) override
    {
        if (!owner || !flower || owner->Health() <= 0.f || owner->IsMarkedForDes()) return;

        float orbit_distance = GetNeutralOrbitDistance(owner, flower);
        (void)dt;
        MovePetalToOrbit(owner, flower, orbit_distance, game_config::default_petal_orbit_k, true);

        // Defense is sufficient; attacking at the same time must not suppress
        // Bubble. It destroys the bubble only after its prep window (the petal's
        // reload time, which shrinks as rarity rises). The speed boost is
        // granted at the defend->destroy transition, right before removal.
        if (flower->m_defending && owner->m_lifetime >= BubbleReload(rarity))
        {
            ApplyBubbleBoost(owner, flower);
            owner->Health() = 0.f;
            return;
        }
    }

    void OnFlowerTakeDamage(CPetal*, ERarity, CFlower*, float&, EDamageType, CEntity*) override {}
    void OnPetalSpawned(CPetal*, ERarity, CFlower*) override {}

    void OnPetalDestroyed(CPetal*, ERarity, CFlower*) override
    {
        // The boost is applied at the defend->destroy transition in OnTick so
        // accidental deaths (mob contact etc.) never trigger the launch.
    }

  private:
    static void ApplyBubbleBoost(CPetal* owner, CFlower* flower)
    {
        if (!owner || !flower) return;

        CPetal* moon = flower->GetMoonPetal();
        florrbt::Vector2f delta = moon ? moon->Pos() - owner->Pos() : flower->Pos() - owner->Pos();
        float len = Length(delta);
        if (len <= game_config::entity_collision_epsilon)
        {
            delta = { -owner->m_vel.x, -owner->m_vel.y };
            len = Length(delta);
        }
        if (len <= game_config::entity_collision_epsilon) return;

        if (moon) moon->m_vel += delta / len * game_config::default_bubble_boost_speed *
                               std::max(0.f, game_config::default_bubble_moon_boost_multiplier);
        else flower->m_vel += delta / len * game_config::default_bubble_boost_speed;
    }
};

inline float SignedAngleBetween(florrbt::Vector2f base, florrbt::Vector2f target)
{
    float base_len = Length(base);
    float target_len = Length(target);
    if (base_len <= game_config::entity_collision_epsilon || target_len <= game_config::entity_collision_epsilon)
        return game_config::pi;

    base /= base_len;
    target /= target_len;
    float cross = base.x * target.y - base.y * target.x;
    float dot = std::clamp(base.x * target.x + base.y * target.y, -1.f, 1.f);
    return std::atan2(cross, dot);
}

inline CEntity* FindMissileLaunchTarget(CMissilePetal* missile, CFlower* flower, florrbt::Vector2f launch_direction)
{
    if (!missile || !flower || !flower->GameWorld()) return nullptr;

    float max_range = game_config::default_missile_lock_range;
    float max_angle = game_config::default_missile_lock_angle_degrees * game_config::pi / 180.f;
    auto filter = [missile, flower, launch_direction, max_angle](const CEntity* entity) -> bool {
        if (!IsValidPetalEnemyTarget(missile, flower, entity)) return false;
        florrbt::Vector2f to_target = entity->Pos() - missile->Pos();
        return std::abs(SignedAngleBetween(launch_direction, to_target)) <= max_angle;
    };

    return flower->GameWorld()->FindClosestEntityByEdge(missile->Pos(), max_range, filter);
}

class CCogwheelBehavior : public CPetalBehavior
{
  public:
    bool IsOpen() const override { return true; }

    SFlowerStats GetStats(ERarity) const override
    {
        SFlowerStats stats = EmptyFlowerStats();
        stats.petal_rotation_quantized = true;
        return stats;
    }

    SPetalStats GetPetalStats(ERarity rarity) const override
    {
        SPetalStats stats;
        stats.damage = game_config::default_cogwheel_base_damage * GetPetalRarityScale(rarity);
        stats.health = game_config::default_cogwheel_base_health * GetPetalRarityScale(rarity);
        stats.reload = game_config::default_cogwheel_reload;
        stats.preload = game_config::default_cogwheel_reload;
        stats.copy = static_cast<int>(game_config::default_cogwheel_copy);
        stats.mass = game_config::default_cogwheel_mass;
        stats.radius = game_config::default_cogwheel_base_radius;
        return stats;
    }

    void OnTick(CPetal* owner, ERarity, CFlower* flower, float dt) override
    {
        MovePetalToOrbitAndAttract(owner, flower, GetOrbitDistance(owner, flower), game_config::default_petal_orbit_k,
                                 true, dt);
    }

    void OnFlowerTakeDamage(CPetal*, ERarity, CFlower*, float&, EDamageType, CEntity*) override {}
    void OnPetalSpawned(CPetal*, ERarity, CFlower*) override {}
    void OnPetalDestroyed(CPetal*, ERarity, CFlower*) override {}
};

class CDustBehavior : public CPetalBehavior
{
  public:
    bool IsOpen() const override { return m_is_open; }

    SFlowerStats GetStats(ERarity rarity) const override
    {
        SFlowerStats stats = EmptyFlowerStats();
        float level = GetSpecialPetalLevel(rarity);
        stats.petal_reload_multiplier = std::max(game_config::default_petal_stat_reload_multiplier_min,
                                                 1.0f - level * game_config::default_dust_reload_reduction);
        return stats;
    }

    SPetalStats GetPetalStats(ERarity rarity) const override
    {
        SPetalStats stats;
        stats.damage = game_config::default_dust_base_damage * GetPetalRarityScale(rarity);
        stats.health = game_config::default_dust_base_health * GetPetalRarityScale(rarity);
        stats.copy = static_cast<int>(game_config::default_dust_copy);
        stats.mass = game_config::default_dust_mass;
        stats.radius = game_config::default_dust_base_radius;
        return stats;
    }

    void OnTick(CPetal* owner, ERarity, CFlower* flower, float dt) override
    {
        float orbit_distance = GetOrbitDistance(owner, flower);
        m_is_open = flower->m_attacking;
        MovePetalToOrbitAndAttract(owner, flower, orbit_distance, game_config::default_petal_orbit_k, m_is_open, dt);
    }

    void OnFlowerTakeDamage(CPetal*, ERarity, CFlower*, float&, EDamageType, CEntity*) override {}
    void OnPetalSpawned(CPetal*, ERarity, CFlower*) override {}
    void OnPetalDestroyed(CPetal*, ERarity, CFlower*) override {}

  private:
    bool m_is_open = false;
};

class CGoldenLeafBehavior : public CPetalBehavior
{
  public:
    bool IsOpen() const override { return true; }
    EPetalBonusMode GetBonusMode() const override { return EPetalBonusMode::ReloadKeepsBonus; }

    SFlowerStats GetStats(ERarity rarity) const override
    {
        SFlowerStats stats = EmptyFlowerStats();
        float level = GetSpecialPetalLevel(rarity);
        stats.petal_reload_multiplier = std::max(game_config::default_petal_stat_reload_multiplier_min,
                                                 1.0f - level * game_config::default_goldenleaf_base_reload_reduction);
        return stats;
    }

    SPetalStats GetPetalStats(ERarity rarity) const override
    {
        SPetalStats stats;
        stats.damage = game_config::default_goldenleaf_base_damage * GetPetalRarityScale(rarity);
        stats.health = game_config::default_goldenleaf_base_health * GetPetalRarityScale(rarity);
        stats.copy = static_cast<int>(game_config::default_goldenleaf_copy);
        stats.mass = game_config::default_goldenleaf_mass;
        stats.radius = game_config::default_goldenleaf_base_radius;
        return stats;
    }

    void OnTick(CPetal* owner, ERarity, CFlower* flower, float dt) override
    {
        float orbit_distance = GetOrbitDistance(owner, flower);
        MovePetalToOrbitAndAttract(owner, flower, orbit_distance, game_config::default_petal_orbit_k, true, dt);
    }

    void OnFlowerTakeDamage(CPetal*, ERarity, CFlower*, float&, EDamageType, CEntity*) override {}
    void OnPetalSpawned(CPetal*, ERarity, CFlower*) override {}
    void OnPetalDestroyed(CPetal*, ERarity, CFlower*) override {}
};

class CLeafBehavior : public CPetalBehavior
{
  public:
    bool IsOpen() const override { return true; }
    EPetalBonusMode GetBonusMode() const override { return EPetalBonusMode::ReloadKeepsBonus; }

    SFlowerStats GetStats(ERarity rarity) const override
    {
        SFlowerStats stats = EmptyFlowerStats();
        stats.health_regen = game_config::default_leaf_base_regen * RoseMedicineScale(rarity);
        return stats;
    }

    SPetalStats GetPetalStats(ERarity rarity) const override
    {
        SPetalStats stats;
        stats.damage = game_config::default_leaf_base_damage * GetPetalRarityScale(rarity);
        stats.health = game_config::default_leaf_base_health * GetPetalRarityScale(rarity);
        stats.reload = game_config::default_leaf_reload;
        stats.preload = game_config::default_leaf_reload;
        stats.copy = static_cast<int>(game_config::default_leaf_copy);
        stats.mass = game_config::default_leaf_mass;
        stats.radius = game_config::default_leaf_base_radius;
        return stats;
    }

    void OnTick(CPetal* owner, ERarity, CFlower* flower, float dt) override
    {
        MovePetalToOrbitAndAttract(owner, flower, GetOrbitDistance(owner, flower), game_config::default_petal_orbit_k,
                                 true, dt);
    }

    void OnFlowerTakeDamage(CPetal*, ERarity, CFlower*, float&, EDamageType, CEntity*) override {}
    void OnPetalSpawned(CPetal*, ERarity, CFlower*) override {}
    void OnPetalDestroyed(CPetal*, ERarity, CFlower*) override {}
};

class CYuccaBehavior : public CPetalBehavior
{
  public:
    bool IsOpen() const override { return true; }
    EPetalBonusMode GetBonusMode() const override { return EPetalBonusMode::ReloadKeepsBonus; }

    SFlowerStats GetStats(ERarity rarity) const override
    {
        SFlowerStats stats = EmptyFlowerStats();
        stats.defense_health_regen = game_config::default_leaf_base_regen *
                                     game_config::default_yucca_regen_multiplier * RoseMedicineScale(rarity);
        return stats;
    }

    SPetalStats GetPetalStats(ERarity rarity) const override
    {
        SPetalStats stats;
        stats.damage = game_config::default_yucca_base_damage * GetPetalRarityScale(rarity);
        stats.health = game_config::default_yucca_base_health * GetPetalRarityScale(rarity);
        stats.reload = game_config::default_yucca_reload;
        stats.preload = game_config::default_yucca_reload;
        stats.copy = static_cast<int>(game_config::default_yucca_copy);
        stats.mass = game_config::default_yucca_mass;
        stats.radius = game_config::default_yucca_base_radius;
        return stats;
    }

    void OnTick(CPetal* owner, ERarity, CFlower* flower, float dt) override
    {
        MovePetalToOrbitAndAttract(owner, flower, GetOrbitDistance(owner, flower), game_config::default_petal_orbit_k,
                                 true, dt);
    }

    void OnFlowerTakeDamage(CPetal*, ERarity, CFlower*, float&, EDamageType, CEntity*) override {}
    void OnPetalSpawned(CPetal*, ERarity, CFlower*) override {}
    void OnPetalDestroyed(CPetal*, ERarity, CFlower*) override {}
};

// Magnet petal: a yucca-like opener that boosts the pick-up absorb range
// while active (no bonus during preload/reload: AliveOnly). The bonus scales
// linearly with rarity: Common would be 0, Mythic equals the magnetism
// talent range.
inline float MagnetAbsorbBonus(ERarity rarity)
{
    return game_config::default_magnet_bonus_base *
           std::max(0.f, static_cast<float>(GetLevel(rarity)) - 1.f) / 5.f;
}

class CMagnetBehavior : public CPetalBehavior
{
  public:
    bool IsOpen() const override { return true; }
    EPetalBonusMode GetBonusMode() const override { return EPetalBonusMode::AliveOnly; }

    SFlowerStats GetStats(ERarity rarity) const override
    {
        SFlowerStats stats = EmptyFlowerStats();
        // Non-preload activation only: the bonus appears once the petal is
        // fully ready (AliveOnly keeps it off during preload/reload).
        stats.max_absorb_range = MagnetAbsorbBonus(rarity);
        return stats;
    }

    SPetalStats GetPetalStats(ERarity rarity) const override
    {
        SPetalStats stats;
        stats.damage = game_config::default_magnet_base_damage * GetPetalRarityScale(rarity);
        stats.health = game_config::default_magnet_base_health * GetPetalRarityScale(rarity);
        stats.reload = game_config::default_magnet_reload;
        stats.preload = game_config::default_magnet_reload;
        stats.copy = 1;
        stats.mass = game_config::default_magnet_mass;
        stats.radius = game_config::default_magnet_base_radius;
        stats.stack = false; // non-stackable
        return stats;
    }

    void OnTick(CPetal* owner, ERarity, CFlower* flower, float dt) override
    {
        MovePetalToOrbitAndAttract(owner, flower, GetOrbitDistance(owner, flower), game_config::default_petal_orbit_k,
                                 true, dt);
    }

    void OnFlowerTakeDamage(CPetal*, ERarity, CFlower*, float&, EDamageType, CEntity*) override {}
    void OnPetalSpawned(CPetal*, ERarity, CFlower*) override {}
    void OnPetalDestroyed(CPetal*, ERarity, CFlower*) override {}
};

inline int WhiteFungusPetalExtraHits(ERarity rarity){
    switch (rarity)
    {
    case ERarity::Unusual:
        return game_config::default_white_fungus_petal_extra_hits_unusual;
    case ERarity::Rare:
        return game_config::default_white_fungus_petal_extra_hits_rare;
    case ERarity::Epic:
        return game_config::default_white_fungus_petal_extra_hits_epic;
    case ERarity::Legendary:
        return game_config::default_white_fungus_petal_extra_hits_legendary;
    case ERarity::Mythic:
        return game_config::default_white_fungus_petal_extra_hits_mythic;
    case ERarity::Ultra:
        return game_config::default_white_fungus_petal_extra_hits_ultra;
    case ERarity::Exotic:
        return game_config::default_white_fungus_petal_extra_hits_common;
    case ERarity::Super:
        return game_config::default_white_fungus_petal_extra_hits_super;
    case ERarity::Eternal:
        return game_config::default_white_fungus_petal_extra_hits_eternal;
    case ERarity::Unique:
        return game_config::default_white_fungus_petal_extra_hits_unique;
    case ERarity::Primordial:
        return game_config::default_white_fungus_petal_extra_hits_primordial;
    case ERarity::Common:
    default:
        return game_config::default_white_fungus_petal_extra_hits_common;
    }
}

class CFungusBehavior : public CPetalBehavior
{
  public:
    bool IsOpen() const override { return true; }
    EPetalBonusMode GetBonusMode() const override { return EPetalBonusMode::ReloadKeepsBonus; }

    void OnTick(CPetal* owner, ERarity, CFlower* flower, float dt) override
    {
        MovePetalToOrbitAndAttract(owner, flower, GetOrbitDistance(owner, flower), game_config::default_petal_orbit_k,
                                 true, dt);
    }

    void OnFlowerTakeDamage(CPetal*, ERarity, CFlower*, float&, EDamageType, CEntity*) override {}
    void OnPetalSpawned(CPetal*, ERarity, CFlower*) override {}
    void OnPetalDestroyed(CPetal*, ERarity, CFlower*) override {}
};

class CWhiteFungusBehavior final : public CFungusBehavior
{
  public:
    SFlowerStats GetStats(ERarity rarity) const override
    {
        SFlowerStats stats = EmptyFlowerStats();
        stats.petal_extra_hit_num = std::max(
            0, static_cast<int>(std::lround(static_cast<float>(WhiteFungusPetalExtraHits(rarity)) *
                                            game_config::default_white_fungus_petal_extra_hits_multiplier)));
        return stats;
    }

    SPetalStats GetPetalStats(ERarity rarity) const override
    {
        SPetalStats stats;
        stats.damage = game_config::default_white_fungus_base_damage * GetPetalRarityScale(rarity);
        stats.health = game_config::default_white_fungus_base_health * GetPetalRarityScale(rarity);
        stats.reload = game_config::default_white_fungus_reload;
        stats.preload = game_config::default_white_fungus_reload;
        stats.copy = static_cast<int>(game_config::default_white_fungus_copy);
        stats.mass = game_config::default_white_fungus_mass;
        stats.radius = game_config::default_white_fungus_base_radius;
        return stats;
    }
};

class CBlackFungusBehavior final : public CFungusBehavior
{
  public:
    SFlowerStats GetStats(ERarity rarity) const override
    {
        SFlowerStats stats = EmptyFlowerStats();
        stats.petal_hit_compression_power = static_cast<float>(WhiteFungusPetalExtraHits(rarity));
        return stats;
    }

    SPetalStats GetPetalStats(ERarity rarity) const override
    {
        SPetalStats stats;
        stats.damage = game_config::default_black_fungus_base_damage * GetPetalRarityScale(rarity);
        stats.health = game_config::default_black_fungus_base_health * GetPetalRarityScale(rarity);
        stats.reload = game_config::default_black_fungus_reload;
        stats.preload = game_config::default_black_fungus_reload;
        stats.copy = static_cast<int>(game_config::default_black_fungus_copy);
        stats.mass = game_config::default_black_fungus_mass;
        stats.radius = game_config::default_black_fungus_base_radius;
        return stats;
    }
};

inline float RoseMedicineScale(ERarity rarity)
{
    switch (rarity)
    {
    case ERarity::Common:
        return game_config::default_rose_medicine_scale_common;
    case ERarity::Unusual:
        return game_config::default_rose_medicine_scale_unusual;
    case ERarity::Rare:
        return game_config::default_rose_medicine_scale_rare;
    case ERarity::Epic:
        return game_config::default_rose_medicine_scale_epic;
    case ERarity::Legendary:
        return game_config::default_rose_medicine_scale_legendary;
    case ERarity::Mythic:
        return game_config::default_rose_medicine_scale_mythic;
    case ERarity::Ultra:
        return game_config::default_rose_medicine_scale_ultra;
    case ERarity::Exotic:
        return game_config::default_rose_medicine_scale_common;
    case ERarity::Super:
        return game_config::default_rose_medicine_scale_super;
    case ERarity::Eternal:
        return game_config::default_rose_medicine_scale_eternal;
    case ERarity::Unique:
        return game_config::default_rose_medicine_scale_unique;
    case ERarity::Primordial:
        return game_config::default_rose_medicine_scale_primordial;
    default:
        return game_config::default_rose_medicine_scale_common;
    }
}

inline float MobMaxHealth(const CMobBase* mob)
{
    if (!mob) return 0.f;
    const SMobStats* stats = mob->GetFinalStats();
    return stats ? stats->max_health : 0.f;
}

inline bool NeedsMobHealing(const CMobBase* mob)
{
    float max_health = MobMaxHealth(mob);
    return max_health > 0.f && mob->Health() < max_health - game_config::entity_collision_epsilon;
}

enum class EMedicRecoveryNeed
{
    None,
    Shield,
    Health,
};

inline EMedicRecoveryNeed MedicRecoveryNeed(const CMobBase* mob)
{
    if (NeedsMobHealing(mob)) return EMedicRecoveryNeed::Health;

    const auto* flower = dynamic_cast<const CFlower*>(mob);
    const SFlowerStats* stats = flower ? flower->GetFinalStats() : nullptr;
    if (!flower || !stats || stats->overheal_to_shield <= game_config::entity_collision_epsilon ||
        stats->max_health <= 0.f)
        return EMedicRecoveryNeed::None;

    return flower->GetShield() < stats->max_health - game_config::entity_collision_epsilon
               ? EMedicRecoveryNeed::Shield
               : EMedicRecoveryNeed::None;
}

inline float HealingReceivedMultiplier(const CMobBase* mob)
{
    if (!mob) return 0.f;

    float multiplier = std::max(0.f, GetMedicMultiplier(mob));
    if (const auto* flower = dynamic_cast<const CFlower*>(mob))
        multiplier *= std::max(0.f, flower->m_final_stats.healing_received_multiplier);
    return multiplier;
}

inline void ApplyHealing(CMobBase* mob, float amount)
{
    if (!mob || amount <= 0.f) return;
    const float max_health = MobMaxHealth(mob);
    if (max_health <= 0.f) return;
    if (auto* flower = dynamic_cast<CFlower*>(mob))
        flower->Heal(amount);
    else
        mob->Health() = std::min(max_health, mob->Health() + amount);
}

inline void HealMob(CMobBase* mob, float amount)
{
    ApplyHealing(mob, amount * HealingReceivedMultiplier(mob));
}

class CBroccoliBehavior final : public CPetalBehavior
{
  public:
    bool IsOpen() const override { return true; }

    SFlowerStats GetStats(ERarity) const override { return EmptyFlowerStats(); }

    SPetalStats GetPetalStats(ERarity rarity) const override
    {
        SPetalStats stats;
        const float raw_health = game_config::default_broccoli_base_health * RoseMedicineScale(rarity);
        const float rounding_step = game_config::default_broccoli_health_rounding_step;
        stats.damage = game_config::default_broccoli_base_damage * GetPetalRarityScale(rarity);
        stats.health = rounding_step > game_config::entity_collision_epsilon
                           ? std::round(raw_health / rounding_step) * rounding_step
                           : raw_health;
        stats.health = std::max(game_config::entity_collision_epsilon, stats.health);
        stats.medicine = game_config::default_broccoli_petal_health_heal_per_second;
        stats.reload = game_config::default_broccoli_reload;
        stats.preload = game_config::default_broccoli_reload;
        stats.copy = static_cast<int>(game_config::default_broccoli_copy);
        stats.mass = game_config::default_broccoli_mass;
        stats.radius = game_config::default_broccoli_base_radius;
        return stats;
    }

    void OnTick(CPetal* owner, ERarity, CFlower* flower, float dt) override
    {
        ClearPetalTarget(owner);
        MovePetalToOrbit(owner, flower, GetOrbitDistance(owner, flower), game_config::default_petal_orbit_k, true);

        if (!owner || !flower || dt <= 0.f || owner->Health() <= 0.f || flower->IsDead() ||
            !NeedsMobHealing(flower))
            return;

        const float received_multiplier = HealingReceivedMultiplier(flower);
        if (received_multiplier <= game_config::entity_collision_epsilon) return;

        const float missing_health = std::max(0.f, MobMaxHealth(flower) - flower->Health());
        const float healing_per_second =
            owner->m_final_petal_stats.health * owner->m_final_petal_stats.medicine * received_multiplier;
        const float requested_damage = std::min({ owner->Health(), healing_per_second * dt, missing_health });
        if (requested_damage <= game_config::entity_collision_epsilon) return;

        const float health_before_damage = owner->Health();
        owner->TakeDamage(requested_damage, nullptr, EDamageType::Normal);
        const float self_damage = std::max(0.f, health_before_damage - owner->Health());
        ApplyHealing(flower, self_damage);
    }

    void OnFlowerTakeDamage(CPetal*, ERarity, CFlower*, float&, EDamageType, CEntity*) override {}
    void OnPetalSpawned(CPetal*, ERarity, CFlower*) override {}
    void OnPetalDestroyed(CPetal*, ERarity, CFlower*) override {}
};

inline bool RoseCanHealTarget(const CPetal* owner, const CFlower* flower, const CEntity* entity,
                              EMedicRecoveryNeed required_need)
{
    if (!owner || !flower || !entity || entity == owner) return false;
    if (entity->IsMarkedForDes() || entity->IsDead() || !entity->CanCollide()) return false;
    if (!CheckTeam(GetPreCorruptionTeam(entity), GetPreCorruptionTeam(flower))) return false;
    if (BlocksNullifiedInteraction(owner, entity)) return false;

    const auto* mob = dynamic_cast<const CMobBase*>(entity);
    return mob && MedicRecoveryNeed(mob) == required_need;
}

inline float YggdrasilChannelTime(ERarity rarity)
{
    switch (rarity)
    {
    case ERarity::Unusual:
        return game_config::default_yggdrasil_channel_unusual;
    case ERarity::Rare:
        return game_config::default_yggdrasil_channel_rare;
    case ERarity::Epic:
        return game_config::default_yggdrasil_channel_epic;
    case ERarity::Legendary:
        return game_config::default_yggdrasil_channel_legendary;
    case ERarity::Mythic:
        return game_config::default_yggdrasil_channel_mythic;
    case ERarity::Ultra:
        return game_config::default_yggdrasil_channel_ultra;
    case ERarity::Exotic:
        return game_config::default_yggdrasil_channel_common;
    case ERarity::Super:
        return game_config::default_yggdrasil_channel_super;
    case ERarity::Eternal:
        return game_config::default_yggdrasil_channel_eternal;
    case ERarity::Unique:
        return game_config::default_yggdrasil_channel_unique;
    case ERarity::Primordial:
        return game_config::default_yggdrasil_channel_primordial;
    case ERarity::Common:
    default:
        return game_config::default_yggdrasil_channel_common;
    }
}

class CYggdrasilBehavior : public CPetalBehavior
{
  public:
    bool IsOpen() const override { return true; }

    SFlowerStats GetStats(ERarity) const override { return EmptyFlowerStats(); }

    SPetalStats GetPetalStats(ERarity rarity) const override
    {
        SPetalStats stats;
        stats.damage = 0.f;
        stats.health = game_config::default_yggdrasil_base_health * GetPetalRarityScale(rarity);
        stats.medicine = game_config::default_yggdrasil_heal_fraction;
        stats.reload = game_config::default_yggdrasil_preload;
        stats.preload = game_config::default_yggdrasil_preload;
        stats.copy = static_cast<int>(game_config::default_yggdrasil_copy);
        stats.radius = game_config::default_yggdrasil_base_radius;
        return stats;
    }

    static bool IsValidCorpse(const CPetal* owner, const CFlower* flower, const CEntity* entity)
    {
        const auto* corpse = dynamic_cast<const CPlayerFlower*>(entity);
        return owner && flower && corpse && corpse->m_is_dead && !corpse->IsMarkedForDes() &&
               CheckTeam(GetPreCorruptionTeam(corpse), GetPreCorruptionTeam(flower)) &&
               !BlocksNullifiedInteraction(owner, corpse);
    }

    static CEntity* FindCorpse(CPetal* owner, CFlower* flower)
    {
        if (!owner || !flower || !flower->GameWorld() ||
            owner->m_lifetime < game_config::default_healing_petal_target_delay)
            return nullptr;

        const float range = owner->Radius() * game_config::default_yggdrasil_target_range_multiplier;
        if (range <= 0.f) return nullptr;

        const auto can_revive = [owner, flower](const CEntity* entity) { return IsValidCorpse(owner, flower, entity); };
        CEntity* target = GetCachedTarget(owner, flower, owner->Pos(), range, can_revive);
        return target ? target : FindClosestTarget(owner, flower, owner->Pos(), range, can_revive);
    }

    void OnTick(CPetal* raw_owner, ERarity rarity, CFlower* flower, float dt) override
    {
        auto* owner = dynamic_cast<CYggdrasilPetal*>(raw_owner);
        CEntity* target = FindCorpse(owner, flower);
        if (!owner || !flower) return;
        if (!target)
        {
            owner->m_revive_timer = 0.f;
            ResetAttractionMotion(owner);
            ClearPetalTarget(owner);
            MovePetalToOrbit(owner, flower, GetOrbitDistance(owner, flower), game_config::default_petal_orbit_k, true);
            return;
        }

        const bool target_changed =
            owner->m_target_entity_id != target->Id() || owner->m_target_entity_generation != target->Generation();
        SetPetalTarget(owner, target);
        if (target_changed)
        {
            owner->m_revive_timer = 0.f;
        }
        AdvanceAttractionMotion(owner, target, game_config::default_healing_petal_target_move_time, dt,
                                target_changed);

        if (!owner->IsCollision(*target))
        {
            owner->m_revive_timer = 0.f;
            return;
        }

        owner->m_revive_timer += dt;
        if (owner->m_revive_timer < YggdrasilChannelTime(rarity)) return;

        auto* corpse = static_cast<CPlayerFlower*>(target);
        if (!corpse->ReviveFromYggdrasil(1.f))
        {
            owner->m_revive_timer = 0.f;
            ResetAttractionMotion(owner);
            ClearPetalTarget(owner);
            return;
        }
        owner->m_reload_override = YggdrasilChannelTime(rarity);
        owner->m_reload_ignore_multiplier = true;
        owner->Health() = 0.f;
    }

    void OnFlowerTakeDamage(CPetal*, ERarity, CFlower*, float&, EDamageType, CEntity*) override {}
    void OnPetalSpawned(CPetal*, ERarity, CFlower*) override {}
    void OnPetalDestroyed(CPetal* owner, ERarity, CFlower*) override
    {
        if (!owner) return;
        owner->m_reload_override =
            owner->m_reload_override >= 0.f ? owner->m_reload_override : game_config::default_yggdrasil_preload;
        auto* ygg = dynamic_cast<CYggdrasilPetal*>(owner);
        if (ygg)
        {
            ResetAttractionMotion(ygg);
            ygg->m_revive_timer = 0.f;
        }
    }
};

inline CEntity* RoseFindTarget(CPetal* owner, CFlower* flower)
{
    if (!owner || !flower || !flower->GameWorld()) return nullptr;
    if (owner->m_lifetime < game_config::default_healing_petal_target_delay) return nullptr;

    float range = owner->Radius() * game_config::default_healing_petal_target_range_multiplier;
    auto find_target_for_need = [owner, flower, range](EMedicRecoveryNeed need) -> CEntity* {
        auto is_valid_heal_target = [owner, flower, need](const CEntity* entity) -> bool {
            return RoseCanHealTarget(owner, flower, entity, need);
        };

        if (RoseCanHealTarget(owner, flower, flower, need)) return flower;
        if (range <= 0.f) return nullptr;

        CEntity* raw = GetCachedTarget(owner, flower, owner->Pos(), range, is_valid_heal_target);
        return raw ? raw : FindClosestTarget(owner, flower, owner->Pos(), range, is_valid_heal_target);
    };

    if (CEntity* target = find_target_for_need(EMedicRecoveryNeed::Health)) return target;
    if (CEntity* target = find_target_for_need(EMedicRecoveryNeed::Shield)) return target;
    if (range <= 0.f)
    {
        ClearPetalTarget(owner);
    }
    return nullptr;
}

class CRoseBehavior : public CPetalBehavior
{
  public:
    bool IsOpen() const override { return true; }

    SFlowerStats GetStats(ERarity) const override { return EmptyFlowerStats(); }

    SPetalStats GetPetalStats(ERarity rarity) const override
    {
        SPetalStats stats;
        stats.damage = game_config::default_rose_base_damage * GetPetalRarityScale(rarity);
        stats.health = game_config::default_rose_base_health * GetPetalRarityScale(rarity);
        stats.medicine = game_config::default_rose_base_medicine * RoseMedicineScale(rarity);
        stats.reload = game_config::default_rose_reload;
        stats.preload = game_config::default_rose_preload;
        stats.copy = static_cast<int>(game_config::default_rose_copy);
        stats.mass = game_config::default_rose_mass;
        stats.radius = game_config::default_rose_base_radius;
        return stats;
    }

    void OnTick(CPetal* raw_owner, ERarity, CFlower* flower, float dt) override
    {
        auto* owner = dynamic_cast<CRosePetal*>(raw_owner);
        if (!owner) return;

        CEntity* target = RoseFindTarget(owner, flower);
        if (target)
        {
            const bool target_changed = owner->m_target_entity_id != target->Id() ||
                                        owner->m_target_entity_generation != target->Generation();
            SetPetalTarget(owner, target);
            AdvanceAttractionMotion(owner, target, game_config::default_healing_petal_target_move_time, dt,
                                    target_changed);
            return;
        }

        ResetAttractionMotion(owner);
        ClearPetalTarget(owner);
        MovePetalToOrbit(owner, flower, GetOrbitDistance(owner, flower), game_config::default_petal_orbit_k, true);
    }

    void OnFlowerTakeDamage(CPetal*, ERarity, CFlower*, float&, EDamageType, CEntity*) override {}
    void OnPetalSpawned(CPetal* owner, ERarity, CFlower*) override { ResetAttractionMotion(owner); }
    void OnPetalDestroyed(CPetal* owner, ERarity, CFlower*) override
    {
        ResetAttractionMotion(owner);
    }

    void OnPetalHit(CPetal* owner, ERarity, CEntity* target, float& damage) override
    {
        auto* mob = dynamic_cast<CMobBase*>(target);
        CEntity* flower = owner ? owner->GetOwner() : nullptr;
        if (!owner || !flower || !mob) return;
        if (owner->m_lifetime < game_config::default_healing_petal_target_delay ||
            owner->m_target_entity_id != target->Id() || owner->m_target_entity_generation != target->Generation())
        {
            damage = 0.f;
            return;
        }

        if (CheckTeam(GetPreCorruptionTeam(owner), GetPreCorruptionTeam(target)))
        {
            if (MedicRecoveryNeed(mob) == EMedicRecoveryNeed::None)
            {
                damage = 0.f;
                return;
            }
            HealMob(mob, owner->m_final_petal_stats.medicine);
            damage = 0.f;
            owner->Health() = 0.f;
            return;
        }

        damage = owner->m_final_petal_stats.damage;
        owner->Health() = 0.f;
    }
};

class CDahliaBehavior : public CPetalBehavior
{
  public:
    bool IsOpen() const override { return false; }

    SFlowerStats GetStats(ERarity) const override { return EmptyFlowerStats(); }

    SPetalStats GetPetalStats(ERarity rarity) const override
    {
        SPetalStats stats;
        stats.damage = game_config::default_dahlia_base_damage * GetPetalRarityScale(rarity);
        stats.health = game_config::default_dahlia_base_health * GetPetalRarityScale(rarity);
        stats.medicine = game_config::default_dahlia_base_medicine * RoseMedicineScale(rarity);
        stats.reload = game_config::default_dahlia_reload;
        stats.preload = game_config::default_dahlia_reload;
        stats.copy = static_cast<int>(game_config::default_dahlia_copy);
        stats.mass = game_config::default_dahlia_mass;
        stats.radius = game_config::default_dahlia_base_radius;
        return stats;
    }

    void OnTick(CPetal* raw_owner, ERarity, CFlower* flower, float dt) override
    {
        auto* owner = dynamic_cast<CDahliaPetal*>(raw_owner);
        if (!owner) return;

        CEntity* target = RoseFindTarget(owner, flower);
        if (target)
        {
            const bool target_changed = owner->m_target_entity_id != target->Id() ||
                                        owner->m_target_entity_generation != target->Generation();
            SetPetalTarget(owner, target);
            AdvanceAttractionMotion(owner, target, game_config::default_healing_petal_target_move_time, dt,
                                    target_changed);
            return;
        }

        ResetAttractionMotion(owner);
        ClearPetalTarget(owner);
        MovePetalToOrbit(owner, flower, GetOrbitDistance(owner, flower), game_config::default_petal_orbit_k, false);
    }

    void OnFlowerTakeDamage(CPetal*, ERarity, CFlower*, float&, EDamageType, CEntity*) override {}
    void OnPetalSpawned(CPetal* owner, ERarity, CFlower*) override { ResetAttractionMotion(owner); }
    void OnPetalDestroyed(CPetal* owner, ERarity, CFlower*) override
    {
        ResetAttractionMotion(owner);
    }

    void OnPetalHit(CPetal* owner, ERarity, CEntity* target, float& damage) override
    {
        auto* mob = dynamic_cast<CMobBase*>(target);
        if (!owner || !mob) return;
        if (owner->m_lifetime < game_config::default_healing_petal_target_delay ||
            owner->m_target_entity_id != target->Id() || owner->m_target_entity_generation != target->Generation())
        {
            damage = 0.f;
            return;
        }

        if (CheckTeam(GetPreCorruptionTeam(owner), GetPreCorruptionTeam(target)))
        {
            if (MedicRecoveryNeed(mob) == EMedicRecoveryNeed::None)
            {
                damage = 0.f;
                return;
            }
            HealMob(mob, owner->m_final_petal_stats.medicine);
            damage = 0.f;
            owner->Health() = 0.f;
            return;
        }

        damage = owner->m_final_petal_stats.damage;
        owner->Health() = 0.f;
    }
};

class CWingBehavior : public CPetalBehavior
{
  public:
    bool IsOpen() const override { return true; }

    SFlowerStats GetStats(ERarity) const override { return EmptyFlowerStats(); }

    SPetalStats GetPetalStats(ERarity rarity) const override
    {
        SPetalStats stats;
        stats.damage = game_config::default_wing_base_damage * GetPetalRarityScale(rarity);
        stats.health = game_config::default_wing_base_health * GetPetalRarityScale(rarity);
        stats.reload = game_config::default_wing_reload;
        stats.preload = game_config::default_wing_reload;
        stats.copy = static_cast<int>(game_config::default_wing_copy);
        stats.mass = game_config::default_wing_mass;
        stats.radius = game_config::default_wing_base_radius;
        return stats;
    }

    void OnTick(CPetal* owner, ERarity, CFlower* flower, float dt) override
    {
        float distance = GetOrbitDistance(owner, flower);
        if (owner && flower && flower->m_attacking)
        {
            float wave = std::sin(owner->m_lifetime * game_config::pi * 2.f) + game_config::default_wing_wave_offset;
            distance = GetBaseOrbitRadius(owner, flower) + game_config::default_petal_orbit_radius +
                       GetOrbitReach(owner, flower, true) * wave;
        }
        MovePetalToOrbitAndAttract(owner, flower, distance, game_config::default_petal_orbit_k, true, dt);
    }

    void OnFlowerTakeDamage(CPetal*, ERarity, CFlower*, float&, EDamageType, CEntity*) override {}
    void OnPetalSpawned(CPetal*, ERarity, CFlower*) override {}
    void OnPetalDestroyed(CPetal*, ERarity, CFlower*) override {}
};

inline const CPetalPrototype* MimicTargetPrototype(const CPetalSlot* slot, const CFlower* flower);

class CTriangleBehavior : public CPetalBehavior
{
  public:
    bool IsOpen() const override { return true; }
    EPetalBonusMode GetBonusMode() const override { return EPetalBonusMode::PreloadKeepsBonus; }

    SFlowerStats GetStats(ERarity rarity) const override
    {
        SFlowerStats stats = EmptyFlowerStats();
        stats.tridmgbonus = TriBonusDamage(rarity);
        return stats;
    }

    SPetalStats GetPetalStats(ERarity rarity) const override
    {
        SPetalStats stats;
        stats.damage = game_config::default_triangle_base_damage * GetPetalRarityScale(rarity);
        stats.health = game_config::default_triangle_base_health * GetPetalRarityScale(rarity);
        stats.reload = game_config::default_triangle_reload;
        stats.preload = game_config::default_triangle_reload;
        stats.copy = static_cast<int>(game_config::default_triangle_copy);
        stats.mass = game_config::default_triangle_mass;
        stats.radius = game_config::default_triangle_base_radius;
        return stats;
    }

    void OnTick(CPetal* owner, ERarity, CFlower* flower, float dt) override
    {
        MovePetalToOrbitAndAttract(owner, flower, GetOrbitDistance(owner, flower), game_config::default_petal_orbit_k,
                                 true, dt);
    }

    void OnFlowerTakeDamage(CPetal*, ERarity, CFlower*, float&, EDamageType, CEntity*) override {}
    void OnPetalSpawned(CPetal*, ERarity, CFlower*) override {}
    void OnPetalDestroyed(CPetal*, ERarity, CFlower*) override {}
};

class CSawbladeBehavior : public CPetalBehavior
{
  public:
    bool IsOpen() const override { return true; }

    SFlowerStats GetStats(ERarity) const override { return EmptyFlowerStats(); }

    SPetalStats GetPetalStats(ERarity rarity) const override
    {
        SPetalStats stats;
        stats.damage = game_config::default_sawblade_base_damage * GetPetalRarityScale(rarity);
        stats.health = game_config::default_sawblade_base_health * GetPetalRarityScale(rarity);
        stats.reload = game_config::default_sawblade_reload;
        stats.preload = game_config::default_sawblade_reload;
        stats.copy = static_cast<int>(game_config::default_sawblade_copy);
        stats.mass = game_config::default_sawblade_mass;
        stats.radius = game_config::default_sawblade_base_radius;
        return stats;
    }

    void OnTick(CPetal* owner, ERarity, CFlower* flower, float dt) override
    {
        float reach = game_config::default_petal_neutral_reach;
        if (flower && flower->m_attacking) reach += game_config::default_petal_attack_offset;
        if (flower && flower->m_defending) reach += game_config::default_petal_defend_offset;
        float distance = GetBaseOrbitRadius(owner, flower) + game_config::default_petal_orbit_radius + reach;
        MovePetalToOrbitAndAttract(owner, flower, distance, game_config::default_petal_orbit_k, true, dt);
    }

    void OnFlowerTakeDamage(CPetal*, ERarity, CFlower*, float&, EDamageType, CEntity*) override {}
    void OnPetalSpawned(CPetal*, ERarity, CFlower*) override {}
    void OnPetalDestroyed(CPetal*, ERarity, CFlower*) override {}
};

inline float FragmentValue(ERarity rarity)
{
    switch (rarity)
    {
    case ERarity::Ultra:
        return game_config::default_fragment_value_ultra;
    case ERarity::Exotic:
        return GetPetalRarityScale(ERarity::Common);
    case ERarity::Super:
        return game_config::default_fragment_value_super;
    case ERarity::Eternal:
        return game_config::default_fragment_value_eternal;
    case ERarity::Unique:
        return game_config::default_fragment_value_unique;
    case ERarity::Primordial:
        return game_config::default_fragment_value_primordial;
    default:
        return GetPetalRarityScale(rarity);
    }
}

class CFragmentBehavior : public CPetalBehavior
{
  public:
    bool IsOpen() const override { return true; }

    SFlowerStats GetStats(ERarity) const override { return EmptyFlowerStats(); }

    SPetalStats GetPetalStats(ERarity rarity) const override
    {
        SPetalStats stats;
        float value = FragmentValue(rarity);
        stats.damage = value;
        stats.health = value;
        stats.reload = rarity == ERarity::Unique ? game_config::default_fragment_reload_unique
                                                 : game_config::default_fragment_reload;
        stats.preload = stats.reload;
        stats.copy = rarity == ERarity::Unique
                         ? game_config::default_fragment_copy_unique
                         : (GetLevel(rarity) >= GetLevel(ERarity::Ultra) ? game_config::default_fragment_copy_ultra
                                                                         : game_config::default_fragment_copy);
        stats.mass = game_config::default_basic_mass;
        stats.radius = game_config::default_basic_base_radius;
        return stats;
    }

    void OnTick(CPetal* owner, ERarity, CFlower* flower, float dt) override
    {
        MovePetalToOrbitAndAttract(owner, flower, GetOrbitDistance(owner, flower), game_config::default_petal_orbit_k,
                                 true, dt);
    }

    void OnFlowerTakeDamage(CPetal*, ERarity, CFlower*, float&, EDamageType, CEntity*) override {}
    void OnPetalSpawned(CPetal*, ERarity, CFlower*) override {}
    void OnPetalDestroyed(CPetal*, ERarity, CFlower*) override {}
};

inline const CPetalPrototype* MimicTargetPrototype(int slot_index, const CFlower* flower)
{
    if (!flower) return nullptr;
    const auto& slots = flower->GetSlots();
    if (slots.empty()) return nullptr;

    int slot_count = static_cast<int>(slots.size());
    int target_index = slot_index <= 0 ? slot_count - 1 : slot_index - 1;
    if (target_index < 0 || target_index >= slot_count) return nullptr;

    const CPetalPrototype* proto = slots[target_index].m_p_proto;
    if (!proto || proto->m_type == EPetalType::Mimic || proto->m_type == EPetalType::None) return nullptr;
    return proto;
}

inline const CPetalPrototype* MimicTargetPrototype(CPetal* owner, CFlower* flower)
{
    return owner ? MimicTargetPrototype(owner->m_slot_index, flower) : nullptr;
}

inline const CPetalPrototype* MimicTargetPrototype(const CPetalSlot* slot, const CFlower* flower)
{
    return slot ? MimicTargetPrototype(slot->m_slot_index, flower) : nullptr;
}

class CMimicBehavior : public CPetalBehavior
{
  public:
    bool IsOpen() const override { return true; }

    SFlowerStats GetStats(ERarity) const override { return EmptyFlowerStats(); }

    SPetalStats GetPetalStats(ERarity rarity) const override
    {
        (void)rarity;
        SPetalStats stats;
        stats.damage = game_config::default_mimic_base_damage;
        stats.health = game_config::default_mimic_base_health;
        stats.reload = game_config::default_mimic_reload;
        stats.preload = game_config::default_mimic_reload;
        stats.copy = static_cast<int>(game_config::default_mimic_copy);
        stats.mass = game_config::default_mimic_mass;
        stats.radius = game_config::default_mimic_base_radius;
        return stats;
    }

    SFlowerStats GetStatsForSlot(ERarity rarity, const CPetalSlot* slot, const CFlower* flower) const override
    {
        const CPetalPrototype* proto = MimicTargetPrototype(slot, flower);
        if (!proto || !proto->m_p_behavior) return EmptyFlowerStats();
        return proto->m_p_behavior->GetStats(rarity);
    }

    SPetalStats GetSlotPetalStats(ERarity rarity, const CPetalSlot* slot, const CFlower* flower) const override
    {
        const CPetalPrototype* proto = MimicTargetPrototype(slot, flower);
        if (!proto || !proto->m_p_behavior) return GetPetalStats(rarity);
        return proto->m_p_behavior->GetPetalStats(rarity);
    }

    const CPetalPrototype* GetRuntimeSlotPrototype(ERarity, const CPetalSlot* slot,
                                                      const CFlower* flower) const override
    {
        return MimicTargetPrototype(slot, flower);
    }

    EPetalBonusMode GetSlotBonusMode(ERarity, const CPetalSlot* slot, const CFlower* flower) const override
    {
        const CPetalPrototype* proto = MimicTargetPrototype(slot, flower);
        if (!proto || !proto->m_p_behavior) return EPetalBonusMode::AliveOnly;
        return proto->m_p_behavior->GetBonusMode();
    }

    void OnTick(CPetal* owner, ERarity rarity, CFlower* flower, float dt) override
    {
        const CPetalPrototype* proto = MimicTargetPrototype(owner, flower);
        if (!proto || !proto->m_p_behavior)
        {
            MovePetalToOrbit(owner, flower, GetOrbitDistance(owner, flower), game_config::default_petal_orbit_k, true);
            return;
        }

        if (owner && owner->GetPetalType() != proto->m_type) ApplyMimicStats(owner, rarity, proto, flower);
        proto->m_p_behavior->OnTick(owner, rarity, flower, dt);
    }

    void OnFlowerTakeDamage(CPetal* owner, ERarity rarity, CFlower* flower, float& dmg, EDamageType damage_type,
                            CEntity* attacker) override
    {
        const CPetalPrototype* proto =
            owner ? FindPetalPrototype(owner->GetPetalType()) : MimicTargetPrototype(owner, flower);
        if (proto && proto->m_type != EPetalType::Mimic && proto->m_p_behavior)
            proto->m_p_behavior->OnFlowerTakeDamage(owner, rarity, flower, dmg, damage_type, attacker);
    }

    void OnPetalHit(CPetal* owner, ERarity rarity, CEntity* target, float& damage) override
    {
        const CPetalPrototype* proto = owner ? FindPetalPrototype(owner->GetPetalType()) : nullptr;
        if (proto && proto->m_type != EPetalType::Mimic && proto->m_p_behavior)
            proto->m_p_behavior->OnPetalHit(owner, rarity, target, damage);
    }

    void OnPetalSpawned(CPetal* owner, ERarity rarity, CFlower* flower) override
    {
        const CPetalPrototype* proto = MimicTargetPrototype(owner, flower);
        if (!proto || !proto->m_p_behavior) return;
        ApplyMimicStats(owner, rarity, proto, flower);
        proto->m_p_behavior->OnPetalSpawned(owner, rarity, flower);
    }

    void OnPetalCleared(CPetal* owner, ERarity rarity, CFlower* flower) override
    {
        const CPetalPrototype* proto =
            owner ? FindPetalPrototype(owner->GetPetalType()) : MimicTargetPrototype(owner, flower);
        if (proto && proto->m_type != EPetalType::Mimic && proto->m_p_behavior)
            proto->m_p_behavior->OnPetalCleared(owner, rarity, flower);
    }

    void OnPetalDestroyed(CPetal* owner, ERarity rarity, CFlower* flower) override
    {
        const CPetalPrototype* proto =
            owner ? FindPetalPrototype(owner->GetPetalType()) : MimicTargetPrototype(owner, flower);
        if (proto && proto->m_type != EPetalType::Mimic && proto->m_p_behavior)
            proto->m_p_behavior->OnPetalDestroyed(owner, rarity, flower);
    }

    bool ShouldReloadAfterPetalDestroyed(CPetal* owner) const override
    {
        const CPetalPrototype* proto = owner ? FindPetalPrototype(owner->GetPetalType()) : nullptr;
        if (!proto || proto->m_type == EPetalType::Mimic || !proto->m_p_behavior) return true;
        return proto->m_p_behavior->ShouldReloadAfterPetalDestroyed(owner);
    }

  private:
    static void ApplyMimicStats(CPetal* owner, ERarity rarity, const CPetalPrototype* proto, CFlower* flower)
    {
        if (!owner || !proto || !proto->m_p_behavior) return;

        SPetalStats stats = proto->m_p_behavior->GetPetalStats(rarity);
        NormalizePetalStatsPerCopy(stats);
        stats.radius *= game_config::default_mimic_radius_multiplier;
        owner->SetPetalType(proto->m_type);
        owner->m_base_petal_stats = stats;
        owner->m_final_petal_stats = stats;
        owner->m_growth_stats_revision = std::numeric_limits<std::uint64_t>::max();
        if (flower && flower->GetFinalStats())
        {
            const SFlowerStats& flower_stats = *flower->GetFinalStats();
            const float damage_bonus =
                owner->GetPetalType() == EPetalType::Triangle ? flower_stats.tridmgbonus : 0.f;
            owner->m_final_petal_stats.ActedOn(flower_stats, damage_bonus);
        }
        owner->Radius() = stats.radius;
        owner->Mass() = stats.mass;
        owner->Health() = std::min(owner->Health(), owner->m_final_petal_stats.health);
        if (owner->Health() <= 0.f) owner->Health() = owner->m_final_petal_stats.health;
    }
};

class CIrisBehavior : public CPetalBehavior
{
  public:
    bool IsOpen() const override { return true; }

    SFlowerStats GetStats(ERarity) const override { return EmptyFlowerStats(); }

    SPetalStats GetPetalStats(ERarity rarity) const override
    {
        SPetalStats stats;
        stats.damage = game_config::default_iris_base_damage * GetPetalRarityScale(rarity);
        stats.health = game_config::default_iris_base_health * GetPetalRarityScale(rarity);
        stats.reload = game_config::default_iris_reload;
        stats.preload = game_config::default_iris_reload;
        stats.copy = static_cast<int>(game_config::default_iris_copy);
        stats.mass = game_config::default_iris_mass;
        stats.radius = game_config::default_iris_base_radius;
        return stats;
    }

    void OnTick(CPetal* owner, ERarity, CFlower* flower, float dt) override
    {
        MovePetalToOrbitAndAttract(owner, flower, GetOrbitDistance(owner, flower), game_config::default_petal_orbit_k,
                                 true, dt);
    }

    void OnFlowerTakeDamage(CPetal*, ERarity, CFlower*, float&, EDamageType, CEntity*) override {}

    void OnPetalHit(CPetal* owner, ERarity rarity, CEntity* target, float& damage) override
    {
        auto* mob = dynamic_cast<CMobBase*>(target);
        if (!owner || !mob) return;

        if (dynamic_cast<CFlower*>(target)) damage *= game_config::default_iris_flower_damage_multiplier;

        const auto* flower = dynamic_cast<const CFlower*>(owner->GetOwner());
        const SFlowerStats* flower_stats = flower ? flower->GetFinalStats() : nullptr;
        float duration =
            game_config::default_iris_poison_duration * (flower_stats ? flower_stats->poison_duration_multiplier : 1.f);
        float poison_per_second =
            game_config::default_iris_poison_duration > 0.f
                ? game_config::default_iris_poison_total_damage / game_config::default_iris_poison_duration
                : 0.f;
        poison_per_second *= flower_stats ? flower_stats->poison_damage_multiplier : 1.f;
        auto poison = std::make_unique<CPoisonState>(mob, duration, poison_per_second, rarity, owner->GetOwner());
        if (poison->IsValid()) mob->AddState(std::move(poison));
    }

    void OnPetalSpawned(CPetal*, ERarity, CFlower*) override {}
    void OnPetalDestroyed(CPetal*, ERarity, CFlower*) override {}
};
class CLentilBehavior : public CPetalBehavior
{
  public:
    bool IsOpen() const override { return true; }
    EPetalBonusMode GetBonusMode() const override { return EPetalBonusMode::PreloadKeepsBonus; }

    SFlowerStats GetStats(ERarity rarity) const override
    {
        SFlowerStats stats = EmptyFlowerStats();
        stats.petal_attraction_range = game_config::default_lentil_petal_attraction_range * GetPetalValueLevel(rarity);
        return stats;
    }

    SPetalStats GetPetalStats(ERarity rarity) const override
    {
        SPetalStats stats;
        stats.damage = game_config::default_lentil_base_damage * GetPetalRarityScale(rarity);
        stats.health = game_config::default_lentil_base_health * GetPetalRarityScale(rarity);
        stats.reload = game_config::default_lentil_reload;
        stats.preload = game_config::default_lentil_reload;
        stats.copy = static_cast<int>(game_config::default_lentil_copy);
        stats.mass = game_config::default_lentil_mass;
        stats.radius = game_config::default_lentil_base_radius;
        return stats;
    }

    static CEntity* LentilFindTarget(CPetal* owner, CFlower* flower)
    {
        if (!owner || !flower || !flower->GameWorld() || !flower->GetFinalStats()) return nullptr;

        const float range = GetEnemyTargetRange(owner, flower, true);
        if (range <= 0.f) return nullptr;

        auto filter = [owner, flower](const CEntity* entity) -> bool {
            return IsValidPetalEnemyTarget(owner, flower, entity);
        };

        CEntity* raw = GetCachedTarget(owner, flower, owner->Pos(), range, filter);
        if (!raw) raw = FindClosestTarget(owner, flower, owner->Pos(), range, filter);
        return raw;
    }

    void OnTick(CPetal* owner, ERarity, CFlower* flower, float dt) override
    {
        if (!owner || !flower) return;

        const float orbit_distance = GetOrbitDistance(owner, flower);
        CEntity* target = LentilFindTarget(owner, flower);
        MovePetalToOrbit(owner, flower, orbit_distance, game_config::default_petal_orbit_k, true);
        if (target)
        {
            SetPetalTarget(owner, target);
            // A collision should be a brief attack contact, not a permanent
            // homing lock.  Release at roughly half a tile and immediately
            // use the distance-amplified return velocity so Lentil gets back
            // to its orbit instead of being pinned to the mob.
            if (ReleasePetalTargetIfClose(owner, target))
            {
                owner->m_vel = GetTargetReturnVelocity(owner, flower, orbit_distance,
                                                       game_config::default_petal_orbit_k, true);
                return;
            }

            const float acceleration_multiplier = game_config::default_petal_target_acceleration_multiplier;
            const florrbt::Vector2f target_velocity =
                GetSeekVelocity(owner, target, GetLockedTargetAcceleration(flower, acceleration_multiplier));
            const florrbt::Vector2f global_pull_velocity =
                GetTargetReturnVelocity(owner, flower, orbit_distance, game_config::default_petal_orbit_k, true);
            owner->m_vel = ConstrainTargetVelocity(owner, flower, target, acceleration_multiplier,
                                                        target_velocity, global_pull_velocity, dt);
            return;
        }

        // Keep the last contact identity during the short release window;
        // clearing it immediately would let the next search reacquire the
        // same mob on the following tick.
        if (owner->m_target_release_timer <= game_config::entity_collision_epsilon)
            ClearPetalTarget(owner);
    }

    void OnFlowerTakeDamage(CPetal*, ERarity, CFlower*, float&, EDamageType, CEntity*) override {}
    void OnPetalSpawned(CPetal*, ERarity, CFlower*) override {}
    void OnPetalDestroyed(CPetal*, ERarity, CFlower*) override {}
};
inline float MoonVisibleRadius(ERarity rarity)
{
    float level = GetPetalValueLevel(rarity);
    return game_config::default_flower_radius + std::max(0.f, level - 1.f) * game_config::default_moon_radius_step;
}

class CMoonBehavior : public CPetalBehavior
{
  public:
    bool IsOpen() const override { return true; }

    SFlowerStats GetStats(ERarity) const override { return EmptyFlowerStats(); }

    SPetalStats GetPetalStats(ERarity rarity) const override
    {
        float visible_radius = MoonVisibleRadius(rarity);

        SPetalStats stats;
        stats.damage = game_config::default_moon_base_damage * GetPetalRarityScale(rarity);
        stats.health = game_config::default_moon_base_health * GetPetalRarityScale(rarity);
        stats.reload = game_config::default_moon_reload;
        stats.preload = game_config::default_moon_reload;
        stats.copy = static_cast<int>(game_config::default_moon_copy);
        stats.stack = false;
        stats.mass = game_config::default_moon_base_mass;
        stats.radius = visible_radius * 2.f;
        return stats;
    }

    void OnTick(CPetal*, ERarity, CFlower*, float) override {}

    void OnFlowerTakeDamage(CPetal*, ERarity, CFlower*, float&, EDamageType, CEntity*) override {}
    void OnPetalSpawned(CPetal*, ERarity, CFlower*) override {}
    void OnPetalDestroyed(CPetal*, ERarity, CFlower*) override {}
};
class CNullificationBehavior : public CPetalBehavior
{
  public:
    bool IsOpen() const override { return true; }

    SFlowerStats GetStats(ERarity) const override { return EmptyFlowerStats(); }

    SPetalStats GetPetalStats(ERarity) const override
    {
        SPetalStats stats;
        stats.stack = false;
        stats.reload = 0.f;
        stats.preload = 0.f;
        stats.copy = 0;
        stats.radius = 0.f;
        return stats;
    }

    void OnTick(CPetal*, ERarity, CFlower*, float) override {}
    void OnFlowerTakeDamage(CPetal*, ERarity, CFlower*, float&, EDamageType, CEntity*) override {}
    void OnPetalSpawned(CPetal*, ERarity, CFlower*) override {}
    void OnPetalDestroyed(CPetal*, ERarity, CFlower*) override {}
};
class CPincerBehavior : public CPetalBehavior
{
  public:
    bool IsOpen() const override { return true; }

    SFlowerStats GetStats(ERarity) const override { return EmptyFlowerStats(); }

    SPetalStats GetPetalStats(ERarity rarity) const override
    {
        SPetalStats stats;
        stats.damage = game_config::default_pincer_base_damage * GetPetalRarityScale(rarity);
        stats.health = game_config::default_pincer_base_health * GetPetalRarityScale(rarity);
        stats.reload = game_config::default_pincer_reload;
        stats.preload = game_config::default_pincer_reload;
        stats.copy = static_cast<int>(game_config::default_pincer_copy);
        stats.mass = game_config::default_pincer_mass;
        stats.radius = game_config::default_pincer_base_radius;
        return stats;
    }

    void OnTick(CPetal* owner, ERarity, CFlower* flower, float dt) override
    {
        MovePetalToOrbitAndAttract(owner, flower, GetOrbitDistance(owner, flower), game_config::default_petal_orbit_k,
                                 true, dt);
    }

    void OnFlowerTakeDamage(CPetal*, ERarity, CFlower*, float&, EDamageType, CEntity*) override {}

    void OnPetalHit(CPetal* owner, ERarity rarity, CEntity* target, float&) override
    {
        auto* mob = dynamic_cast<CMobBase*>(target);
        if (!owner || !mob) return;

        const auto* flower = dynamic_cast<const CFlower*>(owner->GetOwner());
        const SFlowerStats* flower_stats = flower ? flower->GetFinalStats() : nullptr;
        float poison_duration = game_config::default_pincer_poison_duration *
                                (flower_stats ? flower_stats->poison_duration_multiplier : 1.f);
        float poison_total = game_config::default_pincer_poison_total_damage * GetPetalRarityScale(rarity);
        float poison_per_second = game_config::default_pincer_poison_duration > 0.f
                                      ? poison_total / game_config::default_pincer_poison_duration
                                      : 0.f;
        poison_per_second *= flower_stats ? flower_stats->poison_damage_multiplier : 1.f;
        auto poison =
            std::make_unique<CPoisonState>(mob, poison_duration, poison_per_second, rarity, owner->GetOwner());
        if (poison->IsValid()) mob->AddState(std::move(poison));

        auto slow = std::make_unique<CPincerSpeedReduceState>(mob, game_config::default_pincer_slow_duration, rarity);
        if (slow->IsValid()) mob->AddState(std::move(slow));
    }

    void OnPetalSpawned(CPetal*, ERarity, CFlower*) override {}
    void OnPetalDestroyed(CPetal*, ERarity, CFlower*) override {}
};

inline int DandelionCopy(ERarity rarity)
{
    if (rarity == ERarity::Exotic) return game_config::default_dandelion_copy_common;
    if (IsAtLeastRarity(rarity, ERarity::Super)) return game_config::default_dandelion_copy_super;
    if (IsAtLeastRarity(rarity, ERarity::Mythic)) return game_config::default_dandelion_copy_mythic;
    return game_config::default_dandelion_copy_common;
}

class CDandelionBehavior : public CPetalBehavior
{
  public:
    bool IsOpen() const override { return true; }

    SFlowerStats GetStats(ERarity) const override { return EmptyFlowerStats(); }

    SPetalStats GetPetalStats(ERarity rarity) const override
    {
        SPetalStats stats;
        stats.damage = game_config::default_dandelion_base_damage * GetPetalRarityScale(rarity);
        stats.health = game_config::default_dandelion_base_health * GetPetalRarityScale(rarity);
        stats.reload = game_config::default_dandelion_reload;
        stats.preload = game_config::default_dandelion_reload;
        stats.copy = DandelionCopy(rarity);
        stats.mass = game_config::default_dandelion_mass;
        stats.radius = game_config::default_dandelion_base_radius;
        return stats;
    }

    void OnTick(CPetal* owner, ERarity, CFlower* flower, float dt) override
    {
        if (!owner || !flower) return;

        float distance = GetOrbitDistance(owner, flower);
        if (flower->m_attacking) distance += game_config::default_dandelion_attack_extra_reach;
        MovePetalToOrbitAndAttract(owner, flower, distance, game_config::default_petal_orbit_k, true, dt);

        florrbt::Vector2f direction = owner->Pos() - GetOrbitCenter(owner, flower);
        if (LengthSq(direction) <= game_config::entity_collision_epsilon * game_config::entity_collision_epsilon)
            direction = { std::cos(flower->FacingAngle()), std::sin(flower->FacingAngle()) };
        if (LengthSq(direction) <= game_config::entity_collision_epsilon * game_config::entity_collision_epsilon)
            direction = { 1.f, 0.f };
        owner->FacingAngle() = std::atan2(direction.y, direction.x);
        owner->HasFacing() = true;
    }

    void OnFlowerTakeDamage(CPetal*, ERarity, CFlower*, float&, EDamageType, CEntity*) override {}
    void OnPetalSpawned(CPetal*, ERarity, CFlower*) override {}
    void OnPetalDestroyed(CPetal*, ERarity, CFlower*) override {}

    void OnPetalHit(CPetal*, ERarity rarity, CEntity* target, float&) override
    {
        if (auto* mob = dynamic_cast<CMobBase*>(target)) ApplyDandelionAntiHeal(mob, rarity);
    }
};

class CMissileBehavior : public CPetalBehavior
{
  public:
    bool IsOpen() const override { return true; }

    SFlowerStats GetStats(ERarity) const override { return EmptyFlowerStats(); }

    SPetalStats GetPetalStats(ERarity rarity) const override
    {
        SPetalStats stats;
        stats.damage = game_config::default_missile_base_damage * GetPetalRarityScale(rarity);
        stats.health = game_config::default_missile_base_health * GetPetalRarityScale(rarity);
        stats.reload = game_config::default_missile_reload;
        stats.preload = game_config::default_missile_reload;
        stats.copy = static_cast<int>(game_config::default_missile_copy);
        stats.mass = game_config::default_missile_mass;
        stats.radius = game_config::default_missile_base_radius;
        return stats;
    }

    void OnTick(CPetal* owner, ERarity, CFlower* flower, float dt) override
    {
        auto* missile = dynamic_cast<CMissilePetal*>(owner);
        if (!missile || !flower || missile->m_fired) return;

        MovePetalToOrbitAndAttract(owner, flower, GetOrbitDistance(owner, flower), game_config::default_petal_orbit_k,
                                 true, dt);
        florrbt::Vector2f radial = owner->Pos() - GetOrbitCenter(owner, flower);
        if (LengthSq(radial) <= game_config::entity_collision_epsilon * game_config::entity_collision_epsilon)
            radial = { std::cos(flower->FacingAngle()), std::sin(flower->FacingAngle()) };
        if (LengthSq(radial) <= game_config::entity_collision_epsilon * game_config::entity_collision_epsilon)
            radial = { 1.f, 0.f };
        owner->FacingAngle() = std::atan2(radial.y, radial.x);
        owner->HasFacing() = true;

        if (!flower->m_attacking || owner->m_lifetime < game_config::default_missile_arm_time) return;

        florrbt::Vector2f direction = owner->Pos() - GetOrbitCenter(owner, flower);
        float len = Length(direction);
        if (len <= game_config::entity_collision_epsilon)
        {
            direction = { std::cos(flower->FacingAngle()), std::sin(flower->FacingAngle()) };
            len = Length(direction);
        }
        if (len <= game_config::entity_collision_epsilon) direction = { 1.f, 0.f };
        else direction /= len;

        if (CEntity* target = FindMissileLaunchTarget(missile, flower, direction))
        {
            florrbt::Vector2f to_target = target->Pos() - owner->Pos();
            float target_len = Length(to_target);
            if (target_len > game_config::entity_collision_epsilon) direction = to_target / target_len;
        }

        float flower_speed =
            flower->GetFinalStats() ? flower->GetFinalStats()->max_velocity : game_config::default_max_velocity;
        missile->BeginThrow(direction, flower_speed * game_config::default_missile_speed_multiplier, 0.f, false, true,
                            false);
        float fired_angle = std::atan2(direction.y, direction.x);
        owner->FacingAngle() = fired_angle;
        owner->HasFacing() = true;
        owner->m_detach_from_slot = true;
        if (owner->GetPetalType() == EPetalType::Missile)
        {
            missile->m_fired_angle = fired_angle;
            missile->m_has_fired_angle = true;
        }
        missile->m_fired = true;
        missile->m_fired_lifetime = 0.f;
    }

    void OnFlowerTakeDamage(CPetal*, ERarity, CFlower*, float&, EDamageType, CEntity*) override {}

    void OnPetalHit(CPetal* owner, ERarity, CEntity*, float& damage) override
    {
        auto* missile = dynamic_cast<CMissilePetal*>(owner);
        if (!owner || !missile) return;

        if (!missile->m_fired) damage *= game_config::default_missile_unfired_damage_multiplier;
    }

    void OnPetalSpawned(CPetal*, ERarity, CFlower*) override {}
    void OnPetalDestroyed(CPetal*, ERarity, CFlower*) override {}
};

class CTrapperBehavior final : public CPetalBehavior
{
  public:
    bool IsOpen() const override { return true; }

    SFlowerStats GetStats(ERarity) const override { return EmptyFlowerStats(); }

    SPetalStats GetPetalStats(ERarity rarity) const override
    {
        const float scale = std::pow(std::max(0.f, game_config::default_trapper_stat_growth),
                                     static_cast<float>(GetLevel(rarity) - 1));
        SPetalStats stats;
        stats.damage = game_config::default_trapper_base_damage * scale;
        stats.health = game_config::default_trapper_base_health * scale;
        stats.reload = game_config::default_trapper_reload;
        stats.preload = game_config::default_trapper_reload;
        stats.copy = static_cast<int>(game_config::default_trapper_copy);
        stats.mass = game_config::default_trapper_mass;
        stats.radius = game_config::default_trapper_base_radius;
        return stats;
    }

    void OnTick(CPetal* owner, ERarity rarity, CFlower* flower, float dt) override
    {
        auto* trapper = dynamic_cast<CTrapperPetal*>(owner);
        if (!trapper || !flower) return;

        const float recoil_duration =
            std::max(game_config::entity_collision_epsilon, game_config::default_trapper_recoil_duration);
        const float recoil_ratio = std::clamp(trapper->m_recoil_timer / recoil_duration, 0.f, 1.f);
        trapper->m_recoil_timer = std::max(0.f, trapper->m_recoil_timer - std::max(0.f, dt));

        CEntity* anchor = owner->GetOrbitAnchor();
        const bool mounted_on_moon = anchor && anchor != flower;
        const florrbt::Vector2f mount_center = anchor ? anchor->Pos() : flower->Pos();
        const float flower_radius = flower->GetFinalStats() ? flower->GetFinalStats()->radius : flower->Radius();
        const float flower_mount_distance =
            flower_radius * std::max(0.f, game_config::default_trapper_mount_radius_multiplier);
        const float mount_distance = mounted_on_moon ? anchor->Radius() : flower_mount_distance;
        const float recoil_distance = std::max(0.f, game_config::default_trapper_recoil_distance) * recoil_ratio *
                                      recoil_ratio;
        const float orbit_distance = std::max(0.f, mount_distance - recoil_distance);
        ClearPetalTarget(owner);

        const florrbt::Vector2f layout_center = GetOrbitCenter(owner, flower);
        florrbt::Vector2f direction = owner->Pos() - mount_center;
        if (const std::optional<florrbt::Vector2f> mount = GetGlobalOrbitPosition(owner, flower, orbit_distance, true))
            direction = *mount - layout_center;
        float direction_length = Length(direction);
        if (direction_length <= game_config::entity_collision_epsilon)
        {
            direction = { std::cos(flower->FacingAngle()), std::sin(flower->FacingAngle()) };
            direction_length = Length(direction);
        }
        if (direction_length <= game_config::entity_collision_epsilon) direction = { 1.f, 0.f };
        else direction /= direction_length;

        const florrbt::Vector2f mounted_pos = mount_center + direction * orbit_distance;
        owner->Pos() = mounted_pos;
        owner->PrevPos() = mounted_pos;
        owner->m_vel = {};
        owner->FacingAngle() = std::atan2(direction.y, direction.x);
        owner->HasFacing() = true;
        owner->m_reload_override = game_config::default_trapper_reload;
        if (!flower->m_attacking) return;

        const float reload_multiplier =
            std::max(game_config::default_petal_reload_multiplier_min,
                     flower->GetFinalStats() ? flower->GetFinalStats()->petal_reload_multiplier : 1.f);
        trapper->m_fire_reload_timer -= std::max(0.f, dt) / reload_multiplier;
        if (trapper->m_fire_reload_timer > 0.f) return;
        trapper->m_fire_reload_timer = std::max(game_config::entity_collision_epsilon,
                                                game_config::default_trapper_fire_interval);

        CGameWorld* world = owner->GameWorld();
        if (!world) return;

        const float scale = std::pow(std::max(0.f, game_config::default_trapper_stat_growth),
                                     static_cast<float>(GetLevel(rarity) - 1));
        float trap_damage = game_config::default_trapper_trap_base_damage * scale;
        float trap_health = game_config::default_trapper_trap_base_health * scale;
        if (const SFlowerStats* stats = flower->GetFinalStats())
        {
            trap_damage *= stats->petal_dmg_multiplier;
            trap_health *= stats->petal_health_multiplier;
        }

        const florrbt::Vector2f spawn_pos =
            owner->Pos() + direction * (owner->Radius() + game_config::default_trapper_trap_radius);
        trapper->m_recoil_timer = recoil_duration;
        auto trap = std::make_unique<CTrapProjectile>(
            world, spawn_pos, game_config::default_trapper_trap_radius, direction,
            game_config::default_trapper_trap_speed, trap_damage, trap_health,
            game_config::default_trapper_trap_lifetime, game_config::default_trapper_trap_mass,
            game_config::default_trapper_trap_deceleration, rarity, flower);
        world->InsertEntity(std::move(trap));
    }

    void OnFlowerTakeDamage(CPetal*, ERarity, CFlower*, float&, EDamageType, CEntity*) override {}

    void OnPetalSpawned(CPetal* owner, ERarity, CFlower*) override
    {
        if (auto* trapper = dynamic_cast<CTrapperPetal*>(owner))
        {
            trapper->m_fire_reload_timer = std::max(game_config::entity_collision_epsilon,
                                                    game_config::default_trapper_fire_interval);
            trapper->m_recoil_timer = 0.f;
        }
        if (owner) owner->m_reload_override = game_config::default_trapper_reload;
    }

    void OnPetalDestroyed(CPetal*, ERarity, CFlower*) override {}
};

class CCarrotBehavior : public CMissileBehavior
{
  public:
    SPetalStats GetPetalStats(ERarity rarity) const override
    {
        SPetalStats stats;
        stats.damage = game_config::default_carrot_base_damage * GetPetalRarityScale(rarity);
        stats.health = game_config::default_carrot_base_health * GetPetalRarityScale(rarity);
        stats.reload = game_config::default_carrot_reload;
        stats.preload = game_config::default_carrot_reload;
        stats.copy = static_cast<int>(game_config::default_carrot_copy);
        stats.mass = game_config::default_carrot_mass;
        stats.radius = game_config::default_carrot_base_radius;
        return stats;
    }

    void OnPetalHit(CPetal* owner, ERarity, CEntity*, float& damage) override
    {
        auto* missile = dynamic_cast<CMissilePetal*>(owner);
        if (!owner || !missile) return;
        if (!missile->m_fired) damage *= game_config::default_missile_unfired_damage_multiplier;
    }
};

inline florrbt::Vector2f PetalThrowDirection(CPetal* owner, CFlower* flower)
{
    florrbt::Vector2f direction = owner && flower ? owner->Pos() - flower->Pos() : florrbt::Vector2f{ 0.f, 0.f };
    float len = Length(direction);
    if (len <= game_config::entity_collision_epsilon && flower)
        direction = { std::cos(flower->FacingAngle()), std::sin(flower->FacingAngle()) };
    len = Length(direction);
    if (len <= game_config::entity_collision_epsilon) return { 1.f, 0.f };
    return direction / len;
}

inline float WebRadius(ERarity rarity)
{
    float radius_units = game_config::default_web_radius_common;
    switch (rarity)
    {
    case ERarity::Unusual:
        radius_units = game_config::default_web_radius_unusual;
        break;
    case ERarity::Rare:
        radius_units = game_config::default_web_radius_rare;
        break;
    case ERarity::Epic:
        radius_units = game_config::default_web_radius_epic;
        break;
    case ERarity::Legendary:
        radius_units = game_config::default_web_radius_legendary;
        break;
    case ERarity::Mythic:
        radius_units = game_config::default_web_radius_mythic;
        break;
    case ERarity::Ultra:
        radius_units = game_config::default_web_radius_ultra;
        break;
    case ERarity::Exotic:
        radius_units = game_config::default_web_radius_common;
        break;
    case ERarity::Super:
        radius_units = game_config::default_web_radius_super;
        break;
    case ERarity::Eternal:
        radius_units = game_config::default_web_radius_eternal;
        break;
    case ERarity::Unique:
        radius_units = game_config::default_web_radius_unique;
        break;
    case ERarity::Primordial:
        radius_units = game_config::default_web_radius_primordial;
        break;
    default:
        break;
    }
    return game_config::default_flower_radius * 2.f * radius_units /
           std::max(game_config::entity_collision_epsilon, game_config::default_web_radius_reference);
}

inline float WebSameRaritySoldierAntMass(ERarity rarity)
{
    return game_config::mob_soldier_ant_mass * game_config::GetMobMassScaleForLevel(GetLevel(rarity));
}

inline float WebDesiredSpeedMultiplier(ERarity rarity)
{
    const float first_level = static_cast<float>(GetLevel(ERarity::Common));
    const float last_level = static_cast<float>(GetLevel(ERarity::Primordial));
    float t = std::clamp(GetSpecialPetalLevel(rarity) - first_level, 0.f, last_level - first_level) /
              (last_level - first_level);
    float target_slow =
        game_config::default_web_target_slow_common +
        (game_config::default_web_target_slow_primordial - game_config::default_web_target_slow_common) * t;
    target_slow = std::clamp(target_slow, 0.f, 1.f);

    float target_mass = std::max(game_config::entity_collision_epsilon, WebSameRaritySoldierAntMass(rarity));
    float reference_mass = std::max(game_config::entity_collision_epsilon, game_config::mob_player_flower_mass);
    float numerator = reference_mass * (1.f - target_slow);
    float denominator = target_slow * target_mass + numerator;
    if (denominator <= game_config::entity_collision_epsilon) return 1.f;
    return std::clamp(numerator / denominator, 0.f, 1.f);
}

inline void SpawnPetalWebZone(CPetal* owner, ERarity rarity, CFlower* flower)
{
    CGameWorld* world = owner ? owner->GameWorld() : (flower ? flower->GameWorld() : nullptr);
    if (!world || !owner) return;

    auto zone = std::make_unique<CSpiderWebZone>(world, owner->Pos(), WebRadius(rarity),
                                                 flower ? static_cast<CEntity*>(flower) : static_cast<CEntity*>(owner),
                                                 game_config::default_web_lifetime, WebDesiredSpeedMultiplier(rarity));
    world->InsertEntity(std::move(zone));
}

class CWebBehavior : public CPetalBehavior
{
  public:
    bool IsOpen() const override { return true; }

    SFlowerStats GetStats(ERarity) const override { return EmptyFlowerStats(); }

    SPetalStats GetPetalStats(ERarity rarity) const override
    {
        SPetalStats stats;
        stats.damage = game_config::default_web_base_damage * GetPetalRarityScale(rarity);
        stats.health = game_config::default_web_base_health * GetPetalRarityScale(rarity);
        stats.reload = game_config::default_web_reload;
        stats.preload = game_config::default_web_reload;
        stats.copy = static_cast<int>(game_config::default_web_copy);
        stats.mass = game_config::default_web_mass;
        stats.radius = game_config::default_web_base_radius;
        return stats;
    }

    void OnTick(CPetal* owner, ERarity, CFlower* flower, float dt) override
    {
        auto* thrown = dynamic_cast<CThrownPetal*>(owner);
        if (thrown && thrown->m_thrown) return;

        MovePetalToOrbit(owner, flower, GetNeutralOrbitDistance(owner, flower), game_config::default_petal_orbit_k,
                       true);
        if (!owner || !flower || owner->m_lifetime < game_config::default_web_fire_cooldown) return;
        if (!flower->m_attacking && !flower->m_defending) return;

        float flower_speed =
            flower->GetFinalStats() ? flower->GetFinalStats()->max_velocity : game_config::default_max_velocity;
        float speed_mult = flower->m_attacking ? game_config::default_web_throw_attack_speed
                                               : game_config::default_web_throw_defend_speed;
        if (thrown)
            thrown->BeginThrow(PetalThrowDirection(owner, flower), flower_speed * speed_mult,
                               game_config::default_web_throw_deceleration, true, false, true);
    }

    void OnFlowerTakeDamage(CPetal*, ERarity, CFlower*, float&, EDamageType, CEntity*) override {}
    void OnPetalSpawned(CPetal*, ERarity, CFlower*) override {}

    void OnPetalDestroyed(CPetal* owner, ERarity rarity, CFlower* flower) override
    {
        SpawnPetalWebZone(owner, rarity, flower);
    }

    void OnPetalHit(CPetal*, ERarity, CEntity*, float&) override {}
};

class CPollenBehavior : public CPetalBehavior
{
  public:
    bool IsOpen() const override { return true; }

    SFlowerStats GetStats(ERarity) const override { return EmptyFlowerStats(); }

    SPetalStats GetPetalStats(ERarity rarity) const override
    {
        int copy = PollenCopy(rarity);
        float scale = GetPetalRarityScale(rarity);

        SPetalStats stats;
        stats.damage = game_config::default_pollen_base_damage * scale;
        stats.health = game_config::default_pollen_base_health * scale;
        stats.reload = game_config::default_pollen_reload;
        stats.preload = game_config::default_pollen_reload;
        stats.copy = copy;
        stats.mass = game_config::default_pollen_mass;
        stats.radius = game_config::default_pollen_base_radius;
        return stats;
    }

    void OnTick(CPetal* owner, ERarity, CFlower* flower, float dt) override
    {
        auto* thrown = dynamic_cast<CThrownPetal*>(owner);
        if (thrown && thrown->m_thrown) return;

        MovePetalToOrbit(owner, flower, GetNeutralOrbitDistance(owner, flower), game_config::default_petal_orbit_k,
                       true);
        if (!owner || !flower || owner->m_lifetime < game_config::default_pollen_fire_cooldown) return;
        if (!flower->m_attacking && !flower->m_defending) return;

        float flower_speed =
            flower->GetFinalStats() ? flower->GetFinalStats()->max_velocity : game_config::default_max_velocity;
        float speed_mult = flower->m_attacking ? game_config::default_web_throw_attack_speed
                                               : game_config::default_web_throw_defend_speed;
        if (thrown)
        {
            thrown->BeginThrow(PetalThrowDirection(owner, flower), flower_speed * speed_mult,
                               game_config::default_web_throw_deceleration, false, true, true);
            owner->m_timer = std::max(0.f, game_config::default_pollen_lifetime);
            if (owner->m_timer <= 0.f) owner->Health() = 0.f;
            owner->m_detach_from_slot = true;
            owner->m_reload_override = game_config::default_pollen_reload;
        }
    }

    void OnFlowerTakeDamage(CPetal*, ERarity, CFlower*, float&, EDamageType, CEntity*) override {}
    void OnPetalSpawned(CPetal*, ERarity, CFlower*) override {}
    void OnPetalDestroyed(CPetal*, ERarity, CFlower*) override {}

    void OnPetalHit(CPetal* owner, ERarity, CEntity*, float&) override
    {
        if (auto* thrown = dynamic_cast<CThrownPetal*>(owner)) thrown->StopThrow(false);
    }
};

class CHoneyBehavior : public CPetalBehavior
{
  public:
    bool IsOpen() const override { return true; }

    SFlowerStats GetStats(ERarity) const override { return EmptyFlowerStats(); }

    SPetalStats GetPetalStats(ERarity rarity) const override
    {
        SPetalStats stats;
        stats.damage = 0.f;
        stats.health = game_config::default_honey_base_health * GetPetalRarityScale(rarity);
        stats.reload = game_config::default_honey_reload;
        stats.preload = game_config::default_honey_reload;
        stats.copy = static_cast<int>(game_config::default_honey_copy);
        stats.stack = false;
        stats.mass = game_config::default_honey_mass;
        stats.radius = game_config::default_honey_base_radius;
        return stats;
    }

    void OnTick(CPetal* owner, ERarity, CFlower* flower, float) override
    {
        auto* thrown = dynamic_cast<CThrownPetal*>(owner);
        if (thrown && thrown->m_thrown) return;

        MovePetalToOrbit(owner, flower, GetNeutralOrbitDistance(owner, flower), game_config::default_petal_orbit_k,
                       true);
        if (!owner || !flower || owner->m_lifetime < game_config::default_honey_fire_cooldown) return;
        if (!flower->m_attacking && !flower->m_defending) return;

        float flower_speed =
            flower->GetFinalStats() ? flower->GetFinalStats()->max_velocity : game_config::default_max_velocity;
        float speed_mult = flower->m_attacking ? game_config::default_web_throw_attack_speed
                                               : game_config::default_web_throw_defend_speed;
        if (thrown)
            thrown->BeginThrow(PetalThrowDirection(owner, flower), flower_speed * speed_mult,
                               game_config::default_web_throw_deceleration, false, false, true);
    }

    void OnFlowerTakeDamage(CPetal*, ERarity, CFlower*, float&, EDamageType, CEntity*) override {}
    void OnPetalSpawned(CPetal*, ERarity, CFlower*) override {}
    void OnPetalDestroyed(CPetal*, ERarity, CFlower*) override {}

    void OnPetalHit(CPetal* owner, ERarity, CEntity*, float& damage) override
    {
        damage = 0.f;
        if (auto* thrown = dynamic_cast<CThrownPetal*>(owner)) thrown->StopThrow(false);
    }
};

// Eou pheromone petal: same throwing behavior as honey, but collides with
// nothing (keeps flying through everything) and, once a throw fully stops,
// the petal reports its position to the bound pheromone group (stamped in
// OnPetalSpawned: team = flower team, group = slot index). Multiple slots of
// the same type stack so every pheromone group gets its own petal.
class CEouPheromoneBehavior : public CHoneyBehavior
{
  public:
    SPetalStats GetPetalStats(ERarity rarity) const override
    {
        SPetalStats stats = CHoneyBehavior::GetPetalStats(rarity);
        stats.stack = true;
        stats.copy = 1;
        return stats;
    }

    // Release modes: an attack throw guides the bound pheromone group (the
    // resting point becomes the group target); a defend throw builds/destroys
    // a cell at the resting point (see the ant-kingdom controller).
    void OnTick(CPetal* owner, ERarity, CFlower* flower, float dt) override
    {
        auto* thrown = dynamic_cast<CEouPheromonePetal*>(owner);
        if (!thrown || !flower) return;
        const bool pressed = flower->m_attacking || flower->m_defending;
        if (!pressed) thrown->m_input_latched = false;
        if (thrown->m_thrown) return;

        MovePetalToOrbit(owner, flower, GetNeutralOrbitDistance(owner, flower), game_config::default_petal_orbit_k,
                       true);
        if (!pressed || thrown->m_input_latched || owner->m_lifetime < game_config::default_web_fire_cooldown) return;
        thrown->m_input_latched = true;
        const bool defend = !flower->m_attacking && flower->m_defending;
        // Building is one action per press, not one toggle per petal/copy.
        if (defend && (thrown->m_group_id != 1 || owner->m_copy_index != 0)) return;
        const float flower_speed =
            flower->GetFinalStats() ? flower->GetFinalStats()->max_velocity : game_config::default_max_velocity;
        const float speed_mult = defend ? game_config::default_web_throw_defend_speed
                                        : game_config::default_web_throw_attack_speed;
        thrown->m_throw_was_defend = defend;
        thrown->BeginThrow(PetalThrowDirection(owner, flower), flower_speed * speed_mult,
                           game_config::default_web_throw_deceleration, false, false, true);
    }

    void OnPetalHit(CPetal*, ERarity, CEntity*, float& damage) override
    {
        // No collision: pass through everything without stopping.
        damage = 0.f;
    }

    void OnPetalSpawned(CPetal* owner, ERarity, CFlower* flower) override
    {
        auto* pheromone = dynamic_cast<CEouPheromonePetal*>(owner);
        if (!pheromone || !flower) return;
        pheromone->m_team_id = flower->Team();
        pheromone->m_group_id = pheromone->m_slot_index;
    }
};

class CWaxBehavior : public CPetalBehavior
{
  public:
    bool IsOpen() const override { return true; }

    SFlowerStats GetStats(ERarity) const override { return EmptyFlowerStats(); }

    SPetalStats GetPetalStats(ERarity rarity) const override
    {
        SPetalStats stats;
        stats.damage = 0.f;
        stats.health = game_config::default_wax_base_health * GetPetalRarityScale(rarity);
        stats.reload = game_config::default_wax_reload;
        stats.preload = game_config::default_wax_reload;
        stats.copy = static_cast<int>(game_config::default_wax_copy);
        stats.stack = false;
        stats.mass = game_config::default_wax_mass * game_config::GetMobMassScaleForLevel(GetLevel(rarity));
        stats.radius = MoonVisibleRadius(rarity) * 2.f;
        return stats;
    }

    void OnTick(CPetal* owner, ERarity, CFlower*, float) override
    {
        if (!owner) return;
        if (Length(owner->m_vel) <= game_config::default_moon_stop_velocity_epsilon)
        {
            owner->m_vel = { 0.f, 0.f };
            return;
        }
        owner->m_vel *= game_config::mob_stop_damping;
    }

    void OnFlowerTakeDamage(CPetal*, ERarity, CFlower*, float&, EDamageType, CEntity*) override {}
    void OnPetalSpawned(CPetal* owner, ERarity, CFlower* flower) override
    {
        if (!owner) return;
        owner->FacingAngle() = GetLimitedRng(-game_config::pi, game_config::pi);
        owner->HasFacing() = true;
        // Give the initial slide the slot's own direction, not the flower's
        // travel direction. Subsequent active/player pushing keeps its rules.
        const auto target = GetGlobalOrbitPosition(owner, flower, GetOrbitDistance(owner, flower), true);
        if (target)
        {
            const auto delta = *target - owner->Pos();
            const float distance = Length(delta);
            if (distance > game_config::entity_collision_epsilon)
            {
                const float slide = std::min(distance, std::max(0.f, game_config::default_wax_spawn_slide_distance));
                // A small initial nudge keeps the first flower contact from
                // selecting the travel direction before the slot slide starts.
                const auto nudge = delta / distance * (slide * 0.25f);
                const auto target_pos = owner->Pos() + nudge;
                if (owner->GameWorld() && !owner->GameWorld()->SweptCircleBlockedByWall(owner->Pos(), target_pos, owner->Radius()))
                    owner->Pos() = target_pos;
                owner->m_vel = florrbt::ComputeWaxSpawnVelocity(delta, slide * 0.75f,
                    game_config::mob_stop_damping, game_config::server_fixed_dt);
            }
        }
    }
    void OnPetalDestroyed(CPetal*, ERarity, CFlower*) override {}

    void OnPetalHit(CPetal*, ERarity, CEntity*, float& damage) override { damage = 0.f; }
};

inline float ShovelPreload(ERarity rarity)
{
    switch (rarity)
    {
    case ERarity::Common:
        return game_config::default_shovel_preload_common;
    case ERarity::Unusual:
        return game_config::default_shovel_preload_unusual;
    case ERarity::Rare:
        return game_config::default_shovel_preload_rare;
    case ERarity::Epic:
        return game_config::default_shovel_preload_epic;
    case ERarity::Legendary:
        return game_config::default_shovel_preload_legendary;
    case ERarity::Mythic:
        return game_config::default_shovel_preload_mythic;
    case ERarity::Ultra:
        return game_config::default_shovel_preload_ultra;
    case ERarity::Exotic:
        return game_config::default_shovel_preload_common;
    case ERarity::Super:
        return game_config::default_shovel_preload_super;
    case ERarity::Eternal:
        return game_config::default_shovel_preload_eternal;
    case ERarity::Unique:
        return game_config::default_shovel_preload_unique;
    case ERarity::Primordial:
        return game_config::default_shovel_preload_primordial;
    default:
        return game_config::default_shovel_preload_common;
    }
}

class CShovelBehavior : public CPetalBehavior
{
  public:
    bool IsOpen() const override { return true; }

    SFlowerStats GetStats(ERarity) const override { return EmptyFlowerStats(); }

    SPetalStats GetPetalStats(ERarity rarity) const override
    {
        SPetalStats stats;
        stats.damage = 0.f;
        stats.health = 1.f;
        stats.reload = game_config::default_shovel_dig_duration;
        stats.preload = ShovelPreload(rarity);
        stats.copy = static_cast<int>(game_config::default_shovel_copy);
        stats.stack = false;
        stats.mass = 0.f;
        stats.radius = game_config::default_shovel_base_radius;
        return stats;
    }

    void OnTick(CPetal* owner, ERarity, CFlower* flower, float) override
    {
        if (!owner || !flower) return;
        ClearPetalTarget(owner);
        MovePetalToOrbit(owner, flower, GetOrbitDistance(owner, flower), game_config::default_petal_orbit_k, true);
    }

    void OnFlowerTakeDamage(CPetal*, ERarity, CFlower*, float&, EDamageType, CEntity*) override {}
    void OnPetalSpawned(CPetal*, ERarity, CFlower*) override {}
    void OnPetalDestroyed(CPetal*, ERarity, CFlower*) override {}
    void OnPetalHit(CPetal*, ERarity, CEntity*, float& damage) override { damage = 0.f; }
};

class COrangeBehavior : public CPetalBehavior
{
  public:
    bool IsOpen() const override { return false; }

    SFlowerStats GetStats(ERarity) const override { return EmptyFlowerStats(); }

    SPetalStats GetPetalStats(ERarity rarity) const override
    {
        SPetalStats stats;
        stats.damage = game_config::default_orange_base_damage * GetPetalRarityScale(rarity);
        stats.health = game_config::default_orange_base_health * GetPetalRarityScale(rarity);
        stats.reload = game_config::default_orange_reload;
        stats.preload = game_config::default_orange_reload;
        stats.copy = std::max(1, static_cast<int>(std::round(game_config::default_orange_copy)));
        stats.mass = game_config::default_orange_mass;
        stats.radius = game_config::default_orange_base_radius;
        return stats;
    }

    void OnTick(CPetal* owner, ERarity, CFlower* flower, float) override
    {
        if (!owner || !flower || !flower->GetFinalStats()) return;

        int total_copies = flower->m_total_copies;
        int start_index = flower->GetStartCopyIndex(owner->m_slot_index);
        if (start_index < 0 || total_copies <= 0) return;

        int copies = owner->m_max_slot_num > 0 ? owner->m_max_slot_num : owner->m_base_petal_stats.copy;
        copies = std::max(1, copies);

        float reach = game_config::default_petal_neutral_reach;
        if (flower->m_attacking) reach += flower->GetFinalStats()->reach + game_config::default_petal_attack_offset;
        if (flower->m_defending) reach += game_config::default_petal_defend_offset;

        const float reach_distance =
            GetBaseOrbitRadius(owner, flower) + game_config::default_petal_orbit_radius + reach;
        const float circle_radius = game_config::default_orange_circle_radius;
        float group_angle =
            flower->GetPetalRotationAngle() +
            (2.f * game_config::pi * static_cast<float>(start_index)) / static_cast<float>(total_copies);
        florrbt::Vector2f direction = { std::cos(group_angle), std::sin(group_angle) };
        florrbt::Vector2f circle_center = GetOrbitCenter(owner, flower) +
                                           direction * (reach_distance + circle_radius);
        float base_angle = std::atan2(-direction.y, -direction.x);
        float angle =
            base_angle + 2.f * game_config::pi * static_cast<float>(owner->m_copy_index) / static_cast<float>(copies);
        florrbt::Vector2f target = circle_center + florrbt::Vector2f(std::cos(angle), std::sin(angle)) * circle_radius;

        florrbt::Vector2f delta = target - owner->Pos();
        float effective_k = game_config::default_petal_orbit_k * game_config::default_petal_follow_k_multiplier *
                            std::max(0.f, game_config::default_petal_follow_multiplier);
        owner->m_vel = delta * effective_k;
    }

    void OnFlowerTakeDamage(CPetal*, ERarity, CFlower*, float&, EDamageType, CEntity*) override {}
    void OnPetalSpawned(CPetal*, ERarity, CFlower*) override {}
    void OnPetalDestroyed(CPetal*, ERarity, CFlower*) override {}
};

inline bool CompassCanPoint(ERarity rarity) { return GetLevel(rarity) >= game_config::default_compass_point_min_level; }

inline float CompassMobWait(ERarity rarity)
{
    if (rarity == ERarity::Primordial) return 0.f;
    if (rarity == ERarity::Unique || rarity == ERarity::Eternal) return game_config::default_compass_wait_unique;
    if (rarity == ERarity::Super) return game_config::default_compass_wait_super;
    if (rarity == ERarity::Ultra) return game_config::default_compass_wait_ultra;
    return std::numeric_limits<float>::max();
}

inline int CompassMobPriority(ERarity rarity)
{
    if (rarity == ERarity::Primordial) return game_config::default_compass_priority_primordial;
    if (rarity == ERarity::Unique) return game_config::default_compass_priority_unique;
    if (rarity == ERarity::Eternal) return game_config::default_compass_priority_eternal;
    return 0;
}

inline bool IsCompassMagnetPetal(const CEntity* entity, const CCompassPetal* owner, const CFlower* flower)
{
    auto* petal = dynamic_cast<const CPetal*>(entity);
    if (!petal || petal == owner || !flower) return false;
    if (petal->GetPetalType() != EPetalType::Compass) return false;
    if (petal->IsMarkedForDes() || petal->IsDead() || !petal->CanCollide()) return false;
    if (CheckTeam(petal->Team(), flower->Team())) return false;
    if ((petal->Team() == 0 || flower->Team() == 0) && ShareRootOwner(owner, petal)) return false;
    return true;
}

inline CEntity* FindCompassMagnet(CCompassPetal* owner, CFlower* flower)
{
    if (!owner || !flower || !flower->GameWorld()) return nullptr;
    auto filter = [owner, flower](const CEntity* entity) -> bool {
        return IsCompassMagnetPetal(entity, owner, flower);
    };
    return flower->GameWorld()->FindClosestEntityByEdge(owner->Pos(), game_config::default_compass_magnet_range,
                                                        filter);
}

inline CEntity* FindCompassMob(CCompassPetal* owner, CFlower* flower)
{
    if (!owner || !flower || !flower->GameWorld()) return nullptr;

    CEntity* best = nullptr;
    int best_priority = 0;
    float best_dist_sq = std::numeric_limits<float>::max();
    flower->GameWorld()->ForEachEntity([&](CEntity* entity) {
        if (!IsValidPetalEnemyTarget(owner, flower, entity)) return;
        auto* mob = dynamic_cast<CMobBase*>(entity);
        if (!mob) return;

        int priority = CompassMobPriority(mob->GetRarity());
        if (priority <= 0) return;

        float dist_sq = DistanceSq(owner->Pos(), entity->Pos());
        if (priority > best_priority || (priority == best_priority && dist_sq < best_dist_sq))
        {
            best = entity;
            best_priority = priority;
            best_dist_sq = dist_sq;
        }
    });
    return best;
}

inline void CompassPointAt(CCompassPetal* owner, const CEntity* target)
{
    if (!owner || !target) return;
    florrbt::Vector2f delta = target->Pos() - owner->Pos();
    if (LengthSq(delta) <= game_config::entity_collision_epsilon * game_config::entity_collision_epsilon) return;
    owner->FacingAngle() = std::atan2(delta.y, delta.x);
    owner->HasFacing() = true;
}

class CCompassBehavior : public CPetalBehavior
{
  public:
    bool IsOpen() const override { return true; }

    SFlowerStats GetStats(ERarity) const override { return EmptyFlowerStats(); }

    SPetalStats GetPetalStats(ERarity rarity) const override
    {
        SPetalStats stats;
        stats.damage = game_config::default_compass_base_damage * GetPetalRarityScale(rarity);
        stats.health = game_config::default_compass_base_health * GetPetalRarityScale(rarity);
        stats.reload = game_config::default_compass_reload;
        stats.preload = game_config::default_compass_reload;
        stats.copy = static_cast<int>(game_config::default_compass_copy);
        stats.mass = game_config::default_compass_mass;
        stats.radius = game_config::default_compass_base_radius;
        return stats;
    }

    void OnTick(CPetal* raw_owner, ERarity rarity, CFlower* flower, float dt) override
    {
        auto* owner = dynamic_cast<CCompassPetal*>(raw_owner);
        if (!owner || !flower) return;

        MovePetalToOrbit(owner, flower, GetOrbitDistance(owner, flower), game_config::default_petal_orbit_k, true);
        if (!CompassCanPoint(rarity)) return;

        if (rarity != ERarity::Super)
        {
            if (CEntity* magnet = FindCompassMagnet(owner, flower))
            {
                owner->m_compass_target_id = magnet->Id();
                owner->m_compass_target_generation = magnet->Generation();
                owner->m_compass_wait_timer = 0.f;
                CompassPointAt(owner, magnet);
                return;
            }
        }

        CEntity* target = FindCompassMob(owner, flower);
        if (!target)
        {
            owner->m_compass_target_id = -1;
            owner->m_compass_target_generation = 0;
            owner->m_compass_wait_timer = 0.f;
            return;
        }

        if (owner->m_compass_target_id != target->Id() || owner->m_compass_target_generation != target->Generation())
        {
            owner->m_compass_target_id = target->Id();
            owner->m_compass_target_generation = target->Generation();
            owner->m_compass_wait_timer = 0.f;
        }
        owner->m_compass_wait_timer += dt;

        if (owner->m_compass_wait_timer >= CompassMobWait(rarity)) CompassPointAt(owner, target);
    }

    void OnFlowerTakeDamage(CPetal*, ERarity, CFlower*, float&, EDamageType, CEntity*) override {}
    void OnPetalSpawned(CPetal*, ERarity, CFlower*) override {}
    void OnPetalDestroyed(CPetal*, ERarity, CFlower*) override {}
};

inline float RelicHealthBonus(ERarity rarity)
{
    switch (rarity)
    {
    case ERarity::Common:
        return game_config::default_relic_health_bonus_common;
    case ERarity::Unusual:
        return game_config::default_relic_health_bonus_unusual;
    case ERarity::Rare:
        return game_config::default_relic_health_bonus_rare;
    case ERarity::Epic:
        return game_config::default_relic_health_bonus_epic;
    case ERarity::Legendary:
        return game_config::default_relic_health_bonus_legendary;
    case ERarity::Mythic:
        return game_config::default_relic_health_bonus_mythic;
    case ERarity::Ultra:
        return game_config::default_relic_health_bonus_ultra;
    case ERarity::Exotic:
        return game_config::default_relic_health_bonus_common;
    case ERarity::Super:
        return game_config::default_relic_health_bonus_super;
    case ERarity::Eternal:
        return game_config::default_relic_health_bonus_eternal;
    case ERarity::Unique:
        return game_config::default_relic_health_bonus_unique;
    case ERarity::Primordial:
        return game_config::default_relic_health_bonus_primordial;
    default:
        return 0.f;
    }
}

inline float RelicPsionicZoneRadius(ERarity rarity)
{
    if (rarity == ERarity::Primordial) return game_config::default_relic_psionic_zone_radius_primordial;
    if (rarity == ERarity::Unique || rarity == ERarity::Eternal) return game_config::default_relic_psionic_zone_radius;
    return 0.f;
}

class CRelicBehavior : public CPetalBehavior
{
  public:
    bool IsOpen() const override { return true; }

    SFlowerStats GetStats(ERarity rarity) const override
    {
        SFlowerStats stats = EmptyFlowerStats();
        stats.max_health_multiplier = 1.f + RelicHealthBonus(rarity);
        return stats;
    }

    SPetalStats GetPetalStats(ERarity rarity) const override
    {
        SPetalStats stats;
        stats.damage = game_config::default_relic_base_damage * GetPetalRarityScale(rarity);
        stats.health = game_config::default_relic_base_health * GetPetalRarityScale(rarity);
        stats.reload = game_config::default_relic_reload;
        stats.preload = game_config::default_relic_reload;
        stats.copy = static_cast<int>(game_config::default_relic_copy);
        stats.stack = false;
        stats.mass = game_config::default_relic_mass;
        stats.radius = game_config::default_relic_base_radius;
        return stats;
    }

    void OnTick(CPetal* owner, ERarity rarity, CFlower* flower, float dt) override
    {
        MovePetalToOrbitAndAttract(owner, flower, GetOrbitDistance(owner, flower), game_config::default_petal_orbit_k,
                                 true, dt);
        RefreshPsionicZone(owner, rarity, flower);
    }

    void OnFlowerTakeDamage(CPetal*, ERarity, CFlower*, float&, EDamageType, CEntity*) override {}
    void OnPetalSpawned(CPetal* owner, ERarity rarity, CFlower* flower) override
    {
        RefreshPsionicZone(owner, rarity, flower);
    }
    void OnPetalCleared(CPetal* owner, ERarity, CFlower* flower) override { DestroyPsionicZone(owner, flower); }
    void OnPetalDestroyed(CPetal* owner, ERarity, CFlower* flower) override { DestroyPsionicZone(owner, flower); }

  private:
    void RefreshPsionicZone(CPetal* owner, ERarity rarity, CFlower* flower) const
    {
        auto* relic = dynamic_cast<CRelicPetal*>(owner);
        if (!relic || !flower || !flower->GameWorld()) return;

        float radius = RelicPsionicZoneRadius(rarity);
        if (radius <= 0.f)
        {
            DestroyPsionicZone(owner, flower);
            return;
        }

        CEntity* raw_zone = relic->m_state_zone_id >= 0
                                ? flower->GameWorld()->GetEntity(relic->m_state_zone_id, relic->m_state_zone_generation)
                                : nullptr;
        auto* zone = dynamic_cast<CStateZone*>(raw_zone);
        if (!zone)
        {
            int team = flower->Team();
            auto new_zone = std::make_unique<CStateZone>(flower->GameWorld(), flower->Pos(), radius,
                                                         CStateZone::MakeStateFactory<CPsionicConnectionState>(
                                                             game_config::default_relic_psionic_zone_refresh, rarity),
                                                         [team](CEntity* entity) -> bool {
                                                             auto* mob = dynamic_cast<CMobBase*>(entity);
                                                             return mob && !mob->HasState<CDiggingState>() &&
                                                                    CheckTeam(mob->Team(), team);
                                                         });

            zone = dynamic_cast<CStateZone*>(flower->GameWorld()->InsertEntity(std::move(new_zone)));
            if (!zone)
            {
                relic->m_state_zone_id = -1;
                relic->m_state_zone_generation = 0;
                return;
            }
            relic->m_state_zone_id = zone->Id();
            relic->m_state_zone_generation = zone->Generation();
        }

        zone->SetCenter(flower->Pos());
        zone->Radius() = radius;
        zone->m_timer = game_config::default_relic_psionic_zone_refresh *
                        game_config::default_relic_psionic_zone_lifetime_multiplier;
        zone->m_state = CStateZone::MakeStateFactory<CPsionicConnectionState>(
            game_config::default_relic_psionic_zone_refresh, rarity);
        int team = flower->Team();
        zone->m_filter = [team](CEntity* entity) -> bool {
            auto* mob = dynamic_cast<CMobBase*>(entity);
            return mob && !mob->HasState<CDiggingState>() && CheckTeam(mob->Team(), team);
        };
    }

    void DestroyPsionicZone(CPetal* owner, CFlower* flower) const
    {
        auto* relic = dynamic_cast<CRelicPetal*>(owner);
        if (!relic || !flower || !flower->GameWorld()) return;

        CEntity* zone = relic->m_state_zone_id >= 0
                            ? flower->GameWorld()->GetEntity(relic->m_state_zone_id, relic->m_state_zone_generation)
                            : nullptr;
        if (zone) zone->MarkForDestroy(EEntityRemovalReason::OwnerRemoved);
        relic->m_state_zone_id = -1;
        relic->m_state_zone_generation = 0;
    }
};

class CAmuletBehavior final : public CPetalBehavior
{
  public:
    bool IsOpen() const override { return true; }

    SFlowerStats GetStats(ERarity rarity) const override
    {
        SFlowerStats stats = EmptyFlowerStats();
        stats.overheal_to_shield =
            game_config::default_amulet_overheal_conversion_per_level * GetSpecialPetalLevel(rarity);
        return stats;
    }

    SPetalStats GetPetalStats(ERarity rarity) const override
    {
        SPetalStats stats;
        stats.damage = game_config::default_amulet_base_damage * GetPetalRarityScale(rarity);
        stats.health = game_config::default_amulet_base_health * GetPetalRarityScale(rarity);
        stats.reload = game_config::default_amulet_reload;
        stats.preload = game_config::default_amulet_reload;
        stats.copy = static_cast<int>(game_config::default_amulet_copy);
        stats.mass = game_config::default_amulet_mass;
        stats.radius = game_config::default_amulet_base_radius;
        return stats;
    }

    void OnTick(CPetal* owner, ERarity, CFlower* flower, float) override
    {
        ClearPetalTarget(owner);
        MovePetalToOrbit(owner, flower, GetOrbitDistance(owner, flower), game_config::default_petal_orbit_k, true);
    }

    void OnFlowerTakeDamage(CPetal*, ERarity, CFlower*, float&, EDamageType, CEntity*) override {}
    void OnPetalSpawned(CPetal*, ERarity, CFlower*) override {}
    void OnPetalDestroyed(CPetal*, ERarity, CFlower*) override {}
};

class CPlankBehavior final : public CPetalBehavior
{
  public:
    bool IsOpen() const override { return true; }

    SFlowerStats GetStats(ERarity) const override { return EmptyFlowerStats(); }

    SPetalStats GetPetalStats(ERarity rarity) const override
    {
        SPetalStats stats;
        stats.damage = game_config::default_plank_base_damage * GetPetalRarityScale(rarity);
        stats.health = game_config::default_plank_base_health * GetPetalRarityScale(rarity);
        stats.reload = game_config::default_plank_reload;
        stats.preload = game_config::default_plank_reload;
        stats.copy = static_cast<int>(game_config::default_plank_copy);
        stats.mass = game_config::default_plank_mass;
        stats.radius = game_config::default_plank_base_radius;
        return stats;
    }

    void OnTick(CPetal* owner, ERarity, CFlower* flower, float) override
    {
        ClearPetalTarget(owner);
        float orbit_distance = GetNeutralOrbitDistance(owner, flower);
        if (flower && flower->m_defending)
            orbit_distance += game_config::default_petal_defend_offset;
        MovePetalToOrbit(owner, flower, orbit_distance, game_config::default_petal_orbit_k, true);
    }

    void OnPetalHit(CPetal*, ERarity, CEntity* target, float& damage) override
    {
        if (target && target->IsEntityType(EEntityType::Projectile))
            damage *= std::max(0.f, game_config::default_plank_projectile_damage_multiplier);
    }

    void OnFlowerTakeDamage(CPetal*, ERarity, CFlower*, float&, EDamageType, CEntity*) override {}
    void OnPetalSpawned(CPetal*, ERarity, CFlower*) override {}
    void OnPetalDestroyed(CPetal*, ERarity, CFlower*) override {}
};

class CTomatoBehavior final : public CPetalBehavior
{
  public:
    bool IsOpen() const override { return true; }

    SFlowerStats GetStats(ERarity) const override { return EmptyFlowerStats(); }

    SPetalStats GetPetalStats(ERarity rarity) const override
    {
        SPetalStats stats;
        stats.damage = game_config::default_tomato_base_damage * GetPetalRarityScale(rarity);
        stats.health = game_config::default_tomato_base_health * GetPetalRarityScale(rarity);
        stats.reload = game_config::default_tomato_reload;
        stats.preload = game_config::default_tomato_reload;
        stats.copy = static_cast<int>(game_config::default_tomato_copy);
        stats.mass = game_config::default_tomato_mass;
        stats.radius = game_config::default_tomato_base_radius;
        return stats;
    }

    void OnTick(CPetal* owner, ERarity, CFlower* flower, float dt) override
    {
        if (!owner || !flower) return;

        const std::uint64_t stats_revision = flower->GetFinalStatsRevision();
        if (owner->m_growth_stats_revision != stats_revision)
        {
            owner->m_growth_base_damage = owner->m_final_petal_stats.damage;
            owner->m_growth_base_health = owner->m_final_petal_stats.health;
            owner->m_growth_base_radius = owner->m_final_petal_stats.radius;
            owner->m_growth_stats_revision = stats_revision;
        }

        const float growth_delay = std::max(0.f, game_config::default_tomato_growth_delay);
        const float growth_duration = std::max(0.f, game_config::default_tomato_growth_duration);
        const float growth_age = std::max(0.f, owner->m_lifetime - growth_delay);
        const float progress = owner->m_lifetime < growth_delay
                                   ? 0.f
                                   : (growth_duration <= game_config::entity_collision_epsilon
                                          ? 1.f
                                          : std::clamp(growth_age / growth_duration, 0.f, 1.f));
        const float damage_multiplier =
            1.f + (std::max(1.f, game_config::default_tomato_max_damage_multiplier) - 1.f) * progress;
        const float health_multiplier =
            1.f + (std::max(1.f, game_config::default_tomato_max_health_multiplier) - 1.f) * progress;
        const float radius_multiplier =
            1.f + (std::max(1.f, game_config::default_tomato_max_radius_multiplier) - 1.f) * progress;

        const float old_max_health = owner->m_final_petal_stats.health;
        const float new_max_health = owner->m_growth_base_health * health_multiplier;
        owner->m_final_petal_stats.damage = owner->m_growth_base_damage * damage_multiplier;
        owner->m_final_petal_stats.health = new_max_health;
        owner->m_final_petal_stats.radius = owner->m_growth_base_radius * radius_multiplier;
        if (old_max_health > game_config::entity_collision_epsilon)
            owner->Health() *= new_max_health / old_max_health;
        owner->Health() = std::clamp(owner->Health(), 0.f, new_max_health);
        owner->Radius() = owner->m_final_petal_stats.radius;

        MovePetalToOrbitAndAttract(owner, flower, GetOrbitDistance(owner, flower), game_config::default_petal_orbit_k,
                                 true, dt);
    }

    void OnFlowerTakeDamage(CPetal*, ERarity, CFlower*, float&, EDamageType, CEntity*) override {}
    void OnPetalSpawned(CPetal* owner, ERarity, CFlower*) override
    {
        if (owner) owner->m_growth_stats_revision = std::numeric_limits<std::uint64_t>::max();
    }
    void OnPetalDestroyed(CPetal*, ERarity, CFlower*) override {}
};

class CHallucinogenicMushroomBehavior final : public CPetalBehavior
{
  public:
    bool IsOpen() const override { return true; }

    SFlowerStats GetStats(ERarity) const override { return EmptyFlowerStats(); }

    SPetalStats GetPetalStats(ERarity rarity) const override
    {
        SPetalStats stats;
        stats.damage = game_config::default_hallucinogenic_mushroom_base_damage * GetPetalRarityScale(rarity);
        stats.health = game_config::default_hallucinogenic_mushroom_base_health * GetPetalRarityScale(rarity);
        stats.reload = game_config::default_hallucinogenic_mushroom_reload;
        stats.preload = game_config::default_hallucinogenic_mushroom_reload;
        stats.copy = static_cast<int>(game_config::default_hallucinogenic_mushroom_copy);
        stats.mass = game_config::default_hallucinogenic_mushroom_mass;
        stats.radius = game_config::default_hallucinogenic_mushroom_base_radius;
        return stats;
    }

    void OnTick(CPetal* owner, ERarity, CFlower* flower, float dt) override
    {
        MovePetalToOrbitAndAttract(owner, flower, GetOrbitDistance(owner, flower), game_config::default_petal_orbit_k,
                                 true, dt);
    }

    void OnFlowerTakeDamage(CPetal*, ERarity, CFlower*, float&, EDamageType, CEntity*) override {}
    void OnPetalSpawned(CPetal*, ERarity, CFlower*) override {}
    void OnPetalDestroyed(CPetal*, ERarity, CFlower*) override {}

    void OnPetalHit(CPetal* owner, ERarity rarity, CEntity* target, float&) override
    {
        auto* mob = dynamic_cast<CMobBase*>(target);
        if (!owner || !mob) return;

        ApplyHallucinogenicExposure(mob, rarity, std::max(0.f, game_config::hallucinogenic_evolution_per_hit));
    }
};

class CYinYangBehavior : public CPetalBehavior
{
  public:
    bool IsOpen() const override { return true; }
    EPetalBonusMode GetBonusMode() const override { return EPetalBonusMode::PreloadKeepsBonus; }

    SFlowerStats GetStats(ERarity) const override
    {
        SFlowerStats stats = EmptyFlowerStats();
        stats.petal_rotation_mode = EPetalRotationMode::YinYang;
        return stats;
    }

    SPetalStats GetPetalStats(ERarity rarity) const override
    {
        SPetalStats stats;
        stats.damage = game_config::default_yinyang_base_damage * GetPetalRarityScale(rarity);
        stats.health = game_config::default_yinyang_base_health * GetPetalRarityScale(rarity);
        stats.reload = game_config::default_yinyang_reload;
        stats.preload = game_config::default_yinyang_reload;
        stats.copy = static_cast<int>(game_config::default_yinyang_copy);
        stats.mass = game_config::default_yinyang_mass;
        stats.radius = game_config::default_yinyang_base_radius;
        return stats;
    }

    void OnTick(CPetal* owner, ERarity, CFlower* flower, float dt) override
    {
        float orbit_distance = GetOrbitDistance(owner, flower);
        MovePetalToOrbitAndAttract(owner, flower, orbit_distance, game_config::default_petal_orbit_k, true, dt);
    }

    void OnFlowerTakeDamage(CPetal*, ERarity, CFlower*, float&, EDamageType, CEntity*) override {}
    void OnPetalSpawned(CPetal*, ERarity, CFlower*) override {}
    void OnPetalDestroyed(CPetal*, ERarity, CFlower*) override {}
};

// ---- Native magic petal family -------------------------------------------------

inline float MagicAttributeScale(ERarity rarity) { return GetPetalRarityScale(rarity); }

inline float MagicDoublingScale(float common_value, ERarity rarity)
{
    return common_value * std::pow(2.f, static_cast<float>(std::max(0, GetPetalLevel(rarity) - 1)));
}

inline float MagicManaCost(float common_cost, ERarity rarity) { return MagicDoublingScale(common_cost, rarity); }

inline float MagicBaseManaCapacity(ERarity rarity) { return MagicDoublingScale(57.8f * 2.f, rarity); }

inline float OrbManaRestore(ERarity rarity) { return 4.1f * 0.8f * MagicAttributeScale(rarity); }

inline bool IsBaseManaProvider(EPetalType type)
{
    return type == EPetalType::Orb || type == EPetalType::MagicLeaf;
}

// Orb and Magic Leaf establish the same base mana pool. Equipping more of
// either type must not add another copy of that pool; when rarities differ,
// the strongest equipped provider wins. Magic Cactus is deliberately not in
// this group and therefore remains fully stackable.
inline bool IsSelectedBaseManaProviderSlot(const CPetalSlot* slot, const CFlower* flower)
{
    if (!slot || !flower) return true;

    const CPetalSlot* selected = nullptr;
    int selected_level = 0;
    for (const CPetalSlot& candidate : flower->GetSlots())
    {
        if (!candidate.m_available || candidate.m_banned || !candidate.m_p_proto ||
            !IsBaseManaProvider(candidate.m_p_proto->m_type))
            continue;

        const int level = std::max(1, GetPetalLevel(candidate.m_stored_rarity));
        if (!selected || level > selected_level ||
            (level == selected_level && candidate.m_slot_index < selected->m_slot_index))
        {
            selected = &candidate;
            selected_level = level;
        }
    }
    return !selected || selected == slot;
}

inline float MagicEyeDamagePerMana(ERarity rarity)
{
    static constexpr float values[] = { 0.f, 3.3f, 4.9f, 7.4f, 11.1f, 16.7f,
                                        25.1f, 37.6f, 56.4f, 84.6f, 84.6f, 127.3f };
    const int index = static_cast<int>(rarity);
    if (index == static_cast<int>(ERarity::Exotic)) return values[static_cast<int>(ERarity::Common)];
    return index > 0 && index < static_cast<int>(std::size(values)) ? values[index] : values[1];
}

class COrbBehavior final : public CPetalBehavior
{
  public:
    bool IsOpen() const override { return true; }
    EPetalBonusMode GetBonusMode() const override { return EPetalBonusMode::PreloadKeepsBonus; }

    SFlowerStats GetStats(ERarity rarity) const override
    {
        SFlowerStats stats = EmptyFlowerStats();
        stats.max_mana = MagicBaseManaCapacity(rarity);
        return stats;
    }

    SFlowerStats GetStatsForSlot(ERarity rarity, const CPetalSlot* slot, const CFlower* flower) const override
    {
        SFlowerStats stats = GetStats(rarity);
        if (!IsSelectedBaseManaProviderSlot(slot, flower)) stats.max_mana = 0.f;
        return stats;
    }

    SPetalStats GetPetalStats(ERarity rarity) const override
    {
        SPetalStats stats;
        stats.damage = game_config::default_rose_base_damage * MagicAttributeScale(rarity);
        stats.health = 25.f * MagicAttributeScale(rarity);
        stats.reload = 1.5f;
        stats.preload = 2.5f;
        stats.copy = 1;
        stats.mass = game_config::default_rose_mass;
        stats.radius = game_config::default_rose_base_radius;
        return stats;
    }

    void OnTick(CPetal* raw_owner, ERarity rarity, CFlower* flower, float dt) override
    {
        auto* owner = dynamic_cast<CRosePetal*>(raw_owner);
        if (!owner || !flower) return;

        CFlower* target = nullptr;
        if (owner->m_lifetime >= game_config::default_healing_petal_target_delay && flower->GetMaxMana() > 0.f &&
            flower->Mana() + game_config::entity_collision_epsilon < flower->GetMaxMana())
            target = flower;

        if (!target)
        {
            ResetAttractionMotion(owner);
            ClearPetalTarget(owner);
            MovePetalToOrbit(owner, flower, GetOrbitDistance(owner, flower), game_config::default_petal_orbit_k, true);
            return;
        }

        const bool target_changed = owner->m_target_entity_id != target->Id() ||
                                    owner->m_target_entity_generation != target->Generation();
        SetPetalTarget(owner, target);
        AdvanceAttractionMotion(owner, target, game_config::default_healing_petal_target_move_time, dt,
                                target_changed);
        const float contact = owner->Radius() + target->Radius();
        if (DistanceSq(owner->Pos(), target->Pos()) <= contact * contact)
        {
            target->RestoreMana(OrbManaRestore(rarity));
            owner->Health() = 0.f;
        }
    }

    void OnPetalHit(CPetal* owner, ERarity rarity, CEntity* target, float& damage) override
    {
        auto* flower = dynamic_cast<CFlower*>(target);
        if (!owner || !flower || !CheckTeam(GetPreCorruptionTeam(owner), GetPreCorruptionTeam(target))) return;
        if (owner->m_lifetime < game_config::default_healing_petal_target_delay ||
            owner->m_target_entity_id != target->Id() ||
            owner->m_target_entity_generation != target->Generation() || flower->GetMaxMana() <= 0.f ||
            flower->Mana() >= flower->GetMaxMana())
        {
            damage = 0.f;
            return;
        }
        flower->RestoreMana(OrbManaRestore(rarity));
        damage = 0.f;
        owner->Health() = 0.f;
    }

    void OnFlowerTakeDamage(CPetal*, ERarity, CFlower*, float&, EDamageType, CEntity*) override {}
    void OnPetalSpawned(CPetal* owner, ERarity, CFlower*) override
    {
        ResetAttractionMotion(owner);
    }
    void OnPetalDestroyed(CPetal* owner, ERarity, CFlower*) override
    {
        ResetAttractionMotion(owner);
    }
};

class CMagicLeafBehavior final : public CPetalBehavior
{
  public:
    bool IsOpen() const override { return true; }
    EPetalBonusMode GetBonusMode() const override { return EPetalBonusMode::PreloadKeepsBonus; }
    SFlowerStats GetStats(ERarity rarity) const override
    {
        SFlowerStats stats = EmptyFlowerStats();
        const float scale = MagicAttributeScale(rarity);
        stats.max_mana = MagicBaseManaCapacity(rarity);
        stats.mana_regen = 2.5f * 0.8f * scale;
        return stats;
    }
    SFlowerStats GetStatsForSlot(ERarity rarity, const CPetalSlot* slot, const CFlower* flower) const override
    {
        SFlowerStats stats = GetStats(rarity);
        if (!IsSelectedBaseManaProviderSlot(slot, flower)) stats.max_mana = 0.f;
        return stats;
    }
    SPetalStats GetPetalStats(ERarity rarity) const override
    {
        const float scale = MagicAttributeScale(rarity);
        SPetalStats stats;
        stats.damage = 16.f * scale;
        stats.health = 12.f * scale;
        stats.reload = 1.8f;
        stats.preload = 1.8f;
        stats.copy = 1;
        stats.mass = game_config::default_leaf_mass;
        stats.radius = game_config::default_leaf_base_radius;
        return stats;
    }
    void OnTick(CPetal* owner, ERarity, CFlower* flower, float dt) override
    {
        MovePetalToOrbitAndAttract(owner, flower, GetOrbitDistance(owner, flower), game_config::default_petal_orbit_k,
                                  true, dt);
    }
    void OnFlowerTakeDamage(CPetal*, ERarity, CFlower*, float&, EDamageType, CEntity*) override {}
    void OnPetalSpawned(CPetal*, ERarity, CFlower*) override {}
    void OnPetalDestroyed(CPetal*, ERarity, CFlower*) override {}
};

class CMagicStingerBehavior final : public CPetalBehavior
{
  public:
    bool IsOpen() const override { return false; }
    float GetSpawnManaCost(ERarity rarity) const override { return MagicManaCost(10.f, rarity); }
    SFlowerStats GetStats(ERarity) const override { return EmptyFlowerStats(); }
    SPetalStats GetPetalStats(ERarity rarity) const override
    {
        SPetalStats stats;
        stats.damage = 100.f * MagicAttributeScale(rarity);
        stats.health = 1.f;
        stats.reload = 0.1f;
        stats.preload = 0.1f;
        stats.copy = 1;
        stats.mass = game_config::default_stinger_mass;
        stats.radius = game_config::default_stinger_base_radius;
        return stats;
    }
    void OnTick(CPetal* owner, ERarity, CFlower* flower, float dt) override
    {
        MovePetalToOrbitAndAttract(owner, flower, GetOrbitDistance(owner, flower), game_config::default_petal_orbit_k,
                                  false, dt);
    }
    void OnFlowerTakeDamage(CPetal*, ERarity, CFlower*, float&, EDamageType, CEntity*) override {}
    void OnPetalSpawned(CPetal*, ERarity, CFlower*) override {}
    void OnPetalDestroyed(CPetal*, ERarity, CFlower*) override {}
};

class CMagicMissileBehavior final : public CMissileBehavior
{
  public:
    float GetSpawnManaCost(ERarity rarity) const override { return MagicManaCost(5.4f, rarity); }
    SPetalStats GetPetalStats(ERarity rarity) const override
    {
        const float scale = MagicAttributeScale(rarity);
        SPetalStats stats;
        stats.damage = 35.f * scale;
        stats.health = 2.f * scale;
        stats.reload = 0.1f;
        stats.preload = 0.1f;
        stats.copy = 1;
        stats.mass = game_config::default_missile_mass;
        stats.radius = game_config::default_missile_base_radius;
        return stats;
    }
};

class CMagicEyeBehavior final : public CPetalBehavior
{
  public:
    bool IsOpen() const override { return true; }
    bool CountsForPetalLayout(const CPetal*) const override { return false; }
    SFlowerStats GetStats(ERarity) const override { return EmptyFlowerStats(); }
    SPetalStats GetPetalStats(ERarity) const override
    {
        SPetalStats stats;
        stats.damage = 0.f;
        stats.health = 1.f;
        // Magic Eye is a persistent face decoration. Its one-second trigger
        // cooldown is behavior state, not a reload affected by reload buffs.
        stats.reload = 0.f;
        stats.preload = 0.f;
        stats.copy = 1;
        stats.stack = false;
        stats.mass = 0.f;
        stats.radius = 0.f;
        return stats;
    }
    void OnTick(CPetal* raw_owner, ERarity rarity, CFlower* flower, float dt) override
    {
        auto* owner = dynamic_cast<CMagicEyePetal*>(raw_owner);
        if (!owner || !flower) return;

        // Keep one invisible slot-owned driver for the active effect. It does
        // not consume an orbit position, collide, or enter client snapshots.
        owner->m_hidden = true;
        owner->Pos() = flower->Pos();
        owner->PrevPos() = flower->Pos();
        owner->m_vel = {};
        owner->m_trigger_cooldown =
            std::max(0.f, owner->m_trigger_cooldown - std::max(0.f, std::isfinite(dt) ? dt : 0.f));

        if (!flower->m_defending || flower->IsDead() ||
            owner->m_trigger_cooldown > game_config::entity_collision_epsilon ||
            flower->Mana() <= game_config::entity_collision_epsilon)
            return;

        const float spent = flower->Mana() * 0.5f;
        if (!flower->TryConsumeMana(spent)) return;
        if (CGameWorld* world = flower->GameWorld())
            world->ChainLightning(*flower, spent * MagicEyeDamagePerMana(rarity), 7, flower);
        owner->m_trigger_cooldown = 1.f;
    }
    void OnFlowerTakeDamage(CPetal*, ERarity, CFlower*, float&, EDamageType, CEntity*) override {}
    void OnPetalSpawned(CPetal* raw_owner, ERarity, CFlower* flower) override
    {
        auto* owner = dynamic_cast<CMagicEyePetal*>(raw_owner);
        if (!owner) return;
        owner->m_hidden = true;
        owner->m_trigger_cooldown = 0.f;
        if (flower)
        {
            owner->Pos() = flower->Pos();
            owner->PrevPos() = flower->Pos();
        }
        owner->m_vel = {};
    }
    void OnPetalDestroyed(CPetal*, ERarity, CFlower*) override {}
};

class CMagicBubbleBehavior final : public CPetalBehavior
{
  public:
    bool IsOpen() const override { return true; }
    SFlowerStats GetStats(ERarity) const override { return EmptyFlowerStats(); }
    SPetalStats GetPetalStats(ERarity rarity) const override
    {
        SPetalStats stats;
        stats.damage = 0.f;
        stats.health = game_config::default_bubble_base_health * MagicAttributeScale(rarity);
        stats.reload = BubbleReload(rarity);
        stats.preload = game_config::default_bubble_reload_common;
        stats.copy = 1;
        stats.mass = game_config::default_bubble_mass;
        stats.radius = game_config::default_bubble_base_radius;
        return stats;
    }
    void OnTick(CPetal* owner, ERarity rarity, CFlower* flower, float) override
    {
        if (!owner || !flower) return;
        MovePetalToOrbit(owner, flower, GetNeutralOrbitDistance(owner, flower), game_config::default_petal_orbit_k,
                         true);
        if (!flower->m_defending || owner->m_lifetime < BubbleReload(rarity)) return;
        if (!flower->TryConsumeMana(MagicManaCost(5.f, rarity))) return;

        const float facing = std::isfinite(flower->FacingAngle()) ? flower->FacingAngle() : 0.f;
        const florrbt::Vector2f direction{ std::cos(facing), std::sin(facing) };
        flower->m_vel += direction * game_config::default_bubble_boost_speed;
        owner->Health() = 0.f;
    }
    void OnFlowerTakeDamage(CPetal*, ERarity, CFlower*, float&, EDamageType, CEntity*) override {}
    void OnPetalSpawned(CPetal*, ERarity, CFlower*) override {}
    void OnPetalDestroyed(CPetal*, ERarity, CFlower*) override {}
};

class CMagicLightningBehavior final : public CPetalBehavior
{
  public:
    bool IsOpen() const override { return true; }
    float GetSpawnManaCost(ERarity rarity) const override { return MagicManaCost(20.f, rarity); }
    SFlowerStats GetStats(ERarity) const override { return EmptyFlowerStats(); }
    SPetalStats GetPetalStats(ERarity rarity) const override
    {
        const float scale = MagicAttributeScale(rarity);
        SPetalStats stats;
        stats.damage = 15.f * scale;
        stats.health = 20.f * scale;
        stats.reload = 0.12f;
        stats.preload = 0.12f;
        stats.copy = 1;
        stats.mass = game_config::default_lightning_mass;
        stats.radius = game_config::default_lightning_base_radius;
        return stats;
    }
    void OnTick(CPetal* owner, ERarity rarity, CFlower* flower, float dt) override
    {
        if (!owner || !flower) return;
        MovePetalToOrbitAndAttract(owner, flower, GetOrbitDistance(owner, flower), game_config::default_petal_orbit_k,
                                  true, dt);
        if (!flower->m_attacking) return;
        if (CGameWorld* world = owner->GameWorld())
            world->MagicChainLightning(*owner, 15.f * MagicAttributeScale(rarity),
                                       2 + std::max(0, GetPetalLevel(rarity) - 1), 3, flower);
        owner->Health() = 0.f;
    }
    void OnFlowerTakeDamage(CPetal*, ERarity, CFlower*, float&, EDamageType, CEntity*) override {}
    void OnPetalSpawned(CPetal*, ERarity, CFlower*) override {}
    void OnPetalDestroyed(CPetal*, ERarity, CFlower*) override {}
};

class CMagicCactusBehavior final : public CPetalBehavior
{
  public:
    bool IsOpen() const override { return true; }
    EPetalBonusMode GetBonusMode() const override { return EPetalBonusMode::PreloadKeepsBonus; }
    SFlowerStats GetStats(ERarity rarity) const override
    {
        SFlowerStats stats = EmptyFlowerStats();
        stats.max_mana = MagicManaCost(231.f, rarity);
        return stats;
    }
    SPetalStats GetPetalStats(ERarity rarity) const override
    {
        const float scale = MagicAttributeScale(rarity);
        SPetalStats stats;
        stats.damage = 7.f * scale;
        stats.health = 15.f * scale;
        stats.reload = 1.f;
        stats.preload = 1.f;
        stats.copy = 1;
        stats.mass = game_config::default_cactus_mass;
        stats.radius = game_config::default_cactus_base_radius;
        return stats;
    }
    void OnTick(CPetal* owner, ERarity, CFlower* flower, float dt) override
    {
        MovePetalToOrbitAndAttract(owner, flower, GetOrbitDistance(owner, flower), game_config::default_petal_orbit_k,
                                  true, dt);
    }
    void OnFlowerTakeDamage(CPetal*, ERarity, CFlower*, float&, EDamageType, CEntity*) override {}
    void OnPetalSpawned(CPetal*, ERarity, CFlower*) override {}
    void OnPetalDestroyed(CPetal*, ERarity, CFlower*) override {}
};
