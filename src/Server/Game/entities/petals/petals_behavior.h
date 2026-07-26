#pragma once
#include "../../../../Shared/game_config.h"
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
    stats.max_velocity = 0.f;
    stats.acceleration = 0.f;
    stats.extra_hit_num = 0;
    stats.max_health_multiplier = 1.f;
    stats.health_regen = 0.f;
    stats.defense_health_regen = 0.f;
    stats.tridmgbonus = 0.f;
    stats.reach = 0.f;
    stats.petal_attraction_range = 0.f;
    stats.petal_dmg_multiplier = 1.f;
    stats.petal_reload_multiplier = 1.f;
    stats.petal_health_multiplier = 1.f;
    stats.petal_medicine_multiplier = 1.f;
    stats.healing_received_multiplier = 1.f;
    stats.mult_summoned_health = 1.f;
    stats.mult_summoned_damage = 1.f;
    stats.poison_damage_multiplier = 1.f;
    stats.poison_duration_multiplier = 1.f;
    stats.body_poison_damage_multiplier = 0.f;
    stats.body_poison_duration = 0.f;
    stats.petal_extra_hit_num = 0;
    stats.petal_swap_min_reload = game_config::default_petal_swap_min_reload;
    stats.petal_rotation_speed = 0.f;
    stats.petal_rotation_quantized = false;
    stats.petal_rotation_mode = EPetalRotationMode::Orbit;
    return stats;
}

inline float PetalRarityScale(ERarity rarity)
{
    if (rarity == ERarity::Exotic)
    {
        float ultra = std::pow(game_config::default_petal_pow, static_cast<float>(GetLevel(ERarity::Ultra) - 1));
        float super = std::pow(game_config::default_petal_pow, static_cast<float>(GetLevel(ERarity::Super) - 1));
        return BlendUltraSuper(ultra, super);
    }
    return std::pow(game_config::default_petal_pow, static_cast<float>(GetLevel(rarity) - 1));
}

inline float RoseMedicineScale(ERarity rarity);

inline int PetalLevel(ERarity rarity) { return GetLevel(rarity); }

inline float PetalValueLevel(ERarity rarity) { return GetRarityValueLevel(rarity); }

inline float PetalSpecialValueLevel(ERarity rarity)
{
    if (rarity == ERarity::Exotic)
        return BlendUltraSuper(static_cast<float>(GetLevel(ERarity::Ultra)),
                               static_cast<float>(GetLevel(ERarity::Super)),
                               game_config::rarity_exotic_special_super_weight);
    return static_cast<float>(GetLevel(rarity));
}

inline float TriBonusDamage(ERarity rarity)
{
    return game_config::default_triangle_bonus_damage * PetalRarityScale(rarity);
}

inline float PetalOrbitBaseRadius(const CPetal* owner, const CFlower* flower)
{
    if (!owner || !flower || !flower->GetFinalStats()) return 0.f;
    if (owner->m_type == EPetalType::Moon) return flower->GetFinalStats()->radius;

    CPetal* moon = flower->GetMoonPetal();
    if (moon && moon != owner) return moon->m_radius;
    return flower->GetFinalStats()->radius;
}

inline float PetalOrbitReach(const CPetal* owner, const CFlower* flower, bool include_mode_offset)
{
    if (!flower || !flower->GetFinalStats()) return 0.f;

    float reach = game_config::default_petal_neutral_reach;
    if (owner && PetalIgnoresReachBonus(owner->m_type)) return reach;

    if (flower->m_attacking) reach += flower->GetFinalStats()->reach;
    if (!include_mode_offset) return reach;

    if (flower->m_attacking) reach += game_config::default_petal_attack_offset;
    else if (flower->m_defending) reach += game_config::default_petal_defend_offset;
    return reach;
}

inline float PetalOrbitReach(const CFlower* flower, bool include_mode_offset)
{
    return PetalOrbitReach(nullptr, flower, include_mode_offset);
}

inline float PetalOrbitNeutralDistance(const CPetal* owner, const CFlower* flower)
{
    if (!owner || !flower || !flower->GetFinalStats()) return 0.f;
    return PetalOrbitBaseRadius(owner, flower) + game_config::default_petal_orbit_radius +
           game_config::default_petal_neutral_reach;
}

inline float PetalOrbitDistance(const CPetal* owner, const CFlower* flower)
{
    if (!owner || !flower || !flower->GetFinalStats()) return 0.f;

    float distance = PetalOrbitBaseRadius(owner, flower) + game_config::default_petal_orbit_radius +
                     PetalOrbitReach(owner, flower, true);
    return distance;
}

inline sf::Vector2f PetalOrbitCenter(const CPetal* owner, CFlower* flower)
{
    if (!owner || !flower) return {};
    if (owner->m_type == EPetalType::Moon) return flower->m_pos;

    CPetal* moon = flower->GetMoonPetal();
    if (!moon || moon == owner) return flower->m_pos;
    return moon->m_pos;
}

inline float PetalClosedClusterSpacing(const CPetal* owner)
{
    if (!owner) return game_config::default_petal_closed_cluster_spacing;

    switch (owner->m_type)
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

inline float PetalClosedClusterRadius(const CPetal* owner, int copies_in_group)
{
    if (!owner || copies_in_group <= 1) return 0.f;

    const float sin_step = std::max(game_config::default_petal_closed_cluster_sin_min,
                                    std::sin(game_config::pi / static_cast<float>(copies_in_group)));
    const float adjacent_center_distance = owner->m_radius * PetalClosedClusterSpacing(owner);
    return adjacent_center_distance / (2.0f * sin_step);
}

inline std::optional<sf::Vector2f> PetalOrbitGlobal(CPetal* owner, CFlower* flower, float orbit_distance, bool is_open)
{
    if (!owner || !flower) return std::nullopt;

    int total_copies = flower->m_total_copies;
    int start_index = flower->GetStartCopyIndex(owner->m_slot_index);
    if (start_index < 0 || total_copies <= 0) return std::nullopt;

    sf::Vector2f global;
    sf::Vector2f orbit_center = PetalOrbitCenter(owner, flower);
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
        global = orbit_center + sf::Vector2f(std::cos(angle), std::sin(angle)) * (orbit_distance + layer_distance);
        if (!is_open)
        {
            int copies_in_group = owner->m_max_slot_num;
            if (copies_in_group <= 0) copies_in_group = owner->m_base_petal_stats.copy;
            if (copies_in_group <= 0) return std::nullopt;

            float spread_radius = PetalClosedClusterRadius(owner, copies_in_group);
            float sub_angle = (2.0f * game_config::pi * owner->m_copy_index) / static_cast<float>(copies_in_group);
            global += sf::Vector2f(std::cos(sub_angle), std::sin(sub_angle)) * spread_radius;
        }

    } else if (is_open)
    { // 非yinyang
        float angle = flower->GetPetalRotationAngle() +
                      (2.0f * game_config::pi * (start_index + owner->m_copy_index)) / static_cast<float>(total_copies);
        global = orbit_center + sf::Vector2f(std::cos(angle), std::sin(angle)) * orbit_distance;
    } else
    {
        float group_angle =
            flower->GetPetalRotationAngle() + (2.0f * game_config::pi * start_index) / static_cast<float>(total_copies);
        sf::Vector2f group_global =
            orbit_center + sf::Vector2f(std::cos(group_angle), std::sin(group_angle)) * orbit_distance;
        int copies_in_group = owner->m_max_slot_num;
        if (copies_in_group <= 0) copies_in_group = owner->m_base_petal_stats.copy;
        if (copies_in_group <= 0) return std::nullopt;

        float spread_radius = PetalClosedClusterRadius(owner, copies_in_group);
        float sub_angle = (2.0f * game_config::pi * owner->m_copy_index) / static_cast<float>(copies_in_group);
        global = group_global + sf::Vector2f(std::cos(sub_angle), std::sin(sub_angle)) * spread_radius;
    }

    return global;
}

inline void PetalOrbitMove(CPetal* owner, CFlower* flower, float orbit_distance, float k, bool is_open)
{
    if (!owner || !flower) return;
    std::optional<sf::Vector2f> global = PetalOrbitGlobal(owner, flower, orbit_distance, is_open);
    if (!global) return;

    sf::Vector2f delta = *global - owner->m_pos;
    float effective_k = k * game_config::default_petal_follow_k_multiplier *
                        std::max(0.f, game_config::default_petal_follow_multiplier);
    if (owner->m_spawn_flight_boost)
    {
        if (LengthSq(delta) <= (owner->m_radius * owner->m_radius)) owner->m_spawn_flight_boost = false;
    }
    owner->m_vel = delta * effective_k;
}

inline bool IsValidPetalEnemyTarget(const CPetal* owner, const CFlower* flower, const CEntity* entity)
{
    if (!owner || !flower || !entity) return false;
    if (entity->m_is_marked_for_des || entity->IsDead() || !entity->CanCollide()) return false;
    if (entity == owner || entity == flower) return false;
    if (IsDiggingEntity(entity)) return false;
    if (dynamic_cast<const CPetal*>(entity)) return false;
    if (CheckTeam(entity->m_team, flower->m_team)) return false;
    if ((entity->m_team == 0 || flower->m_team == 0) && ShareRootOwner(owner, entity)) return false;
    if (BlocksNullifiedInteraction(owner, entity)) return false;
    return dynamic_cast<const CMobBase*>(entity) != nullptr;
}

inline float PetalEdgeDistance(const sf::Vector2f& center, const CEntity* entity)
{
    if (!entity) return std::numeric_limits<float>::max();
    return std::max(0.f, Distance(center, entity->m_pos) - entity->m_radius);
}

inline bool PetalTargetInRange(const sf::Vector2f& center, float range, const CEntity* entity)
{
    return entity && PetalEdgeDistance(center, entity) <= range;
}

inline void PetalSetTarget(CPetal* owner, const CEntity* target)
{
    if (!owner) return;
    owner->m_target_entity_id = target ? target->m_id : -1;
    owner->m_target_entity_generation = target ? target->m_generation : 0;
}

inline void PetalClearTarget(CPetal* owner) { PetalSetTarget(owner, nullptr); }

inline CEntity* PetalGetCachedTarget(CPetal* owner, CFlower* flower, const sf::Vector2f& center, float range,
                                     const std::function<bool(const CEntity*)>& filter)
{
    if (!owner || !flower || !flower->GameWorld() || owner->m_target_entity_id < 0) return nullptr;

    CEntity* target = owner->m_target_entity_generation != 0
                          ? flower->GameWorld()->GetEntity(owner->m_target_entity_id, owner->m_target_entity_generation)
                          : flower->GameWorld()->GetEntity(owner->m_target_entity_id);
    if (!target || !filter(target) || !PetalTargetInRange(center, range, target))
    {
        PetalClearTarget(owner);
        return nullptr;
    }
    return target;
}

inline CEntity* PetalFindClosestTarget(CPetal* owner, CFlower* flower, const sf::Vector2f& center, float range,
                                       const std::function<bool(const CEntity*)>& filter)
{
    if (!owner || !flower) return nullptr;

    CGameWorld* world = flower->GameWorld();
    if (!world) return nullptr;
    if (range <= 0.0f) return nullptr;

    return world->FindClosestEntityByEdge(center, range, filter);
}

inline float PetalEnemyTargetRange(const CPetal* owner, const CFlower* flower, bool include_final_bonus)
{
    if (!owner) return 0.f;

    float range = owner->m_radius + game_config::default_petal_attraction_range;
    if (include_final_bonus && flower && flower->GetFinalStats())
        range += flower->GetFinalStats()->petal_attraction_range;
    return range;
}

inline CEntity* PetalFindTarget(CPetal* owner, CFlower* flower)
{
    if (!owner || !flower || !flower->GetFinalStats()) return nullptr;
    if (owner->m_type == EPetalType::Glass)
    {
        PetalClearTarget(owner);
        return nullptr;
    }

    float range = PetalEnemyTargetRange(owner, flower, true);
    auto filter = [owner, flower](const CEntity* entity) -> bool {
        return IsValidPetalEnemyTarget(owner, flower, entity);
    };

    return PetalFindClosestTarget(owner, flower, owner->m_pos, range, filter);
}

inline void PetalAttractToTarget(CPetal* owner, const CEntity* target, float dt, float acceleration)
{
    if (!owner || !target || acceleration <= 0.f) return;
    (void)dt;

    sf::Vector2f delta = target->m_pos - owner->m_pos;
    if (LengthSq(delta) <= game_config::entity_collision_epsilon * game_config::entity_collision_epsilon) return;

    float len = Length(delta);
    if (len <= game_config::entity_collision_epsilon) return;

    owner->m_vel += delta / len * acceleration;
}

inline float PetalTargetAcceleration(const CFlower* flower,
                                     float multiplier = game_config::default_petal_target_acceleration_multiplier,
                                     bool include_range_scale = true)
{
    float acceleration = game_config::default_acceleration;
    float range_scale = 1.f;
    if (flower && flower->GetFinalStats())
    {
        acceleration = std::max(acceleration, flower->GetFinalStats()->acceleration);
        if (include_range_scale)
        {
            float base_range =
                std::max(game_config::entity_collision_epsilon, game_config::default_lentil_petal_attraction_range);
            range_scale = std::max(1.f, flower->GetFinalStats()->petal_attraction_range / base_range);
        }
    }
    return acceleration * multiplier * range_scale * game_config::default_petal_target_acceleration_scale;
}

inline float PetalLockedTargetAcceleration(const CFlower* flower,
                                           float multiplier = game_config::default_petal_target_acceleration_multiplier,
                                           bool include_range_scale = true)
{
    return PetalTargetAcceleration(flower, multiplier, include_range_scale) *
           game_config::default_petal_locked_target_acceleration_scale;
}

inline void PetalOrbitMoveAndAttract(CPetal* owner, CFlower* flower, float orbit_distance, float k, bool is_open,
                                     float dt, float acceleration_multiplier = 1.f)
{
    CEntity* target = PetalFindTarget(owner, flower);
    if (!owner || !target)
    {
        PetalClearTarget(owner);
        PetalOrbitMove(owner, flower, orbit_distance, k, is_open);
        return;
    }

    PetalSetTarget(owner, target);
    owner->m_vel = { 0.f, 0.f };
    PetalAttractToTarget(owner, target, dt, PetalLockedTargetAcceleration(flower, acceleration_multiplier, true));

    std::optional<sf::Vector2f> global = PetalOrbitGlobal(owner, flower, orbit_distance, is_open);
    if (global)
    {
        owner->m_vel += (*global - owner->m_pos) * k * game_config::default_petal_target_orbit_tether;
    }
}

inline void PetalTetherWhileTargeting(CPetal* owner, CFlower* flower, float orbit_distance, float k, bool is_open)
{
    if (!owner || !flower) return;

    std::optional<sf::Vector2f> global = PetalOrbitGlobal(owner, flower, orbit_distance, is_open);
    if (!global) return;

    owner->m_vel += (*global - owner->m_pos) * k * game_config::default_petal_target_orbit_tether;
}

void RegisterAir();
void RegisterAntEgg();
void RegisterAntennae();
void RegisterBasic();
void RegisterBeetleEgg();
void RegisterBrokenEgg();
void RegisterBubble();
void RegisterCompass();
void RegisterCogwheel();
void RegisterDust();
void RegisterGoldenLeaf();
void RegisterHeavy();
void RegisterIris();
void RegisterFaster();
void RegisterLeaf();
void RegisterLentil();
void RegisterLight();
void RegisterCorn();
void RegisterRice();
void RegisterMoon();
void RegisterNullification();
void RegisterPincer();
void RegisterBasil();
void RegisterRelic();
void RegisterRose();
void RegisterSoil();
void RegisterYinYang();
void RegisterYggdrasil();
void RegisterMissile();
void RegisterBloodSacrifice();
void RegisterCorruption();
void RegisterBandage();
void RegisterBone();
void RegisterCoin();
void RegisterDahlia();
void RegisterDandelionPetal();
void RegisterWing();
void RegisterTriangle();
void RegisterSawblade();
void RegisterFragment();
void RegisterMimic();
void RegisterGlass();
void RegisterStinger();
void RegisterRockPetal();
void RegisterWeb();
void RegisterCactus();
void RegisterPollen();
void RegisterHoney();
void RegisterWax();
void RegisterOrange();
void RegisterShovel();
void RegisterThirdEye();
void RegisterYucca();
void RegisterPetals();

class CAirBehavior : public CPetalBehavior
{
  public:
    bool IsOpen() const override { return true; }

    SFlowerStats GetStats(ERarity rarity) const override
    {
        SFlowerStats stats = EmptyFlowerStats();
        float level = PetalValueLevel(rarity);
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
        owner->m_facing_angle = GetLimitedRng(-game_config::pi, game_config::pi);
        owner->m_has_facing = true;
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
        return BlendUltraSuper(game_config::default_antennae_horizon_ultra, game_config::default_antennae_horizon_super,
                               game_config::rarity_exotic_special_super_weight);
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
        return BlendUltraSuper(game_config::default_antegg_reload_ultra, game_config::default_antegg_reload_super,
                               game_config::rarity_exotic_special_super_weight);
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
        return BlendUltraSuper(game_config::default_third_eye_reach_ultra, game_config::default_third_eye_reach_super,
                               game_config::rarity_exotic_special_super_weight);
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
        stats.damage = game_config::default_basic_base_damage * PetalRarityScale(rarity);
        stats.health = game_config::default_basic_base_health * PetalRarityScale(rarity);
        stats.reload = game_config::default_basic_reload;
        stats.preload = game_config::default_basic_reload;
        stats.copy = static_cast<int>(game_config::default_basic_copy);
        stats.mass = game_config::default_basic_mass;
        stats.radius = game_config::default_basic_base_radius;
        return stats;
    }

    void OnTick(CPetal* owner, ERarity, CFlower* flower, float dt) override
    {
        float orbit_distance = PetalOrbitDistance(owner, flower);
        PetalOrbitMoveAndAttract(owner, flower, orbit_distance, game_config::default_petal_orbit_k, true, dt);
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
        float scale = PetalRarityScale(rarity);

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
        PetalOrbitMoveAndAttract(owner, flower, PetalOrbitDistance(owner, flower), game_config::default_petal_orbit_k,
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
        stats.damage = game_config::default_corn_base_damage * PetalRarityScale(rarity);
        stats.health = game_config::default_corn_base_health * PetalRarityScale(rarity);
        stats.reload = game_config::default_corn_reload;
        stats.preload = game_config::default_corn_reload;
        stats.copy = static_cast<int>(game_config::default_corn_copy);
        stats.mass = game_config::default_corn_mass;
        stats.radius = game_config::default_corn_base_radius;
        return stats;
    }

    void OnTick(CPetal* owner, ERarity, CFlower* flower, float dt) override
    {
        PetalOrbitMoveAndAttract(owner, flower, PetalOrbitDistance(owner, flower), game_config::default_petal_orbit_k,
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
        stats.damage = game_config::default_rice_base_damage * PetalRarityScale(rarity);
        stats.health = game_config::default_rice_base_health * PetalRarityScale(rarity);
        stats.reload = game_config::default_rice_reload;
        stats.preload = game_config::default_rice_reload;
        stats.copy = static_cast<int>(game_config::default_rice_copy);
        stats.mass = game_config::default_rice_mass;
        stats.radius = game_config::default_rice_base_radius;
        return stats;
    }

    void OnTick(CPetal* owner, ERarity, CFlower* flower, float dt) override
    {
        PetalOrbitMoveAndAttract(owner, flower, PetalOrbitDistance(owner, flower), game_config::default_petal_orbit_k,
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
        stats.damage = game_config::default_rock_petal_base_damage * PetalRarityScale(rarity);
        stats.health = game_config::default_rock_petal_base_health * PetalRarityScale(rarity);
        stats.reload = game_config::default_rock_petal_reload;
        stats.preload = game_config::default_rock_petal_reload;
        stats.copy = static_cast<int>(game_config::default_rock_petal_copy);
        stats.mass = game_config::default_rock_petal_mass;
        stats.radius = game_config::default_rock_petal_base_radius;
        return stats;
    }

    void OnTick(CPetal* owner, ERarity, CFlower* flower, float dt) override
    {
        PetalOrbitMoveAndAttract(owner, flower, PetalOrbitDistance(owner, flower), game_config::default_petal_orbit_k,
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
        stats.max_health = game_config::default_cactus_flower_health * PetalRarityScale(rarity);
        return stats;
    }

    SPetalStats GetPetalStats(ERarity rarity) const override
    {
        SPetalStats stats;
        stats.damage = game_config::default_cactus_base_damage * PetalRarityScale(rarity);
        stats.health = game_config::default_cactus_base_health * PetalRarityScale(rarity);
        stats.reload = game_config::default_cactus_reload;
        stats.preload = game_config::default_cactus_reload;
        stats.copy = static_cast<int>(game_config::default_cactus_copy);
        stats.mass = game_config::default_cactus_mass;
        stats.radius = game_config::default_cactus_base_radius;
        return stats;
    }

    void OnTick(CPetal* owner, ERarity, CFlower* flower, float dt) override
    {
        PetalOrbitMoveAndAttract(owner, flower, PetalOrbitDistance(owner, flower), game_config::default_petal_orbit_k,
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
        stats.max_health = game_config::default_soil_flower_health * PetalRarityScale(rarity);
        stats.radius = game_config::default_soil_flower_radius;
        return stats;
    }

    SPetalStats GetPetalStats(ERarity rarity) const override
    {
        SPetalStats stats;
        stats.damage = game_config::default_soil_base_damage * PetalRarityScale(rarity);
        stats.health = game_config::default_soil_base_health * PetalRarityScale(rarity);
        stats.reload = game_config::default_soil_reload;
        stats.preload = game_config::default_soil_reload;
        stats.copy = static_cast<int>(game_config::default_soil_copy);
        stats.mass = game_config::default_soil_mass;
        stats.radius = game_config::default_soil_base_radius;
        return stats;
    }

    void OnTick(CPetal* owner, ERarity, CFlower* flower, float dt) override
    {
        PetalOrbitMoveAndAttract(owner, flower, PetalOrbitDistance(owner, flower), game_config::default_petal_orbit_k,
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
        return BlendUltraSuper(game_config::default_basil_healing_received_ultra,
                               game_config::default_basil_healing_received_super,
                               game_config::rarity_exotic_special_super_weight);
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
        stats.health = game_config::default_basil_base_health * PetalRarityScale(rarity);
        stats.reload = game_config::default_basil_reload;
        stats.preload = game_config::default_basil_reload;
        stats.copy = static_cast<int>(game_config::default_basil_copy);
        stats.mass = game_config::default_basil_mass;
        stats.radius = game_config::default_basil_base_radius;
        return stats;
    }

    void OnTick(CPetal* owner, ERarity, CFlower* flower, float) override
    {
        PetalOrbitMove(owner, flower, PetalOrbitNeutralDistance(owner, flower), game_config::default_petal_orbit_k,
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
        stats.damage = game_config::default_bone_base_damage * PetalRarityScale(rarity);
        stats.health = game_config::default_bone_base_health * PetalRarityScale(rarity);
        stats.armor = game_config::default_bone_base_armor * PetalRarityScale(rarity);
        stats.reload = game_config::default_bone_reload;
        stats.preload = game_config::default_bone_reload;
        stats.copy = static_cast<int>(game_config::default_bone_copy);
        stats.mass = game_config::default_bone_mass;
        stats.radius = game_config::default_bone_base_radius;
        return stats;
    }

    void OnTick(CPetal* owner, ERarity, CFlower* flower, float dt) override
    {
        PetalOrbitMoveAndAttract(owner, flower, PetalOrbitDistance(owner, flower), game_config::default_petal_orbit_k,
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
        stats.damage = game_config::default_coin_base_damage * PetalRarityScale(rarity);
        stats.health = game_config::default_coin_base_health * PetalRarityScale(rarity);
        stats.reload = game_config::default_coin_reload;
        stats.preload = game_config::default_coin_reload;
        stats.copy = static_cast<int>(game_config::default_coin_copy);
        stats.mass = game_config::default_coin_mass;
        stats.radius = game_config::default_coin_base_radius;
        return stats;
    }

    void OnTick(CPetal* owner, ERarity, CFlower* flower, float dt) override
    {
        PetalOrbitMoveAndAttract(owner, flower, PetalOrbitDistance(owner, flower), game_config::default_petal_orbit_k,
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
        float level = PetalValueLevel(rarity);
        SPetalStats stats;
        stats.damage = game_config::default_heavy_base_damage * PetalRarityScale(rarity);
        stats.health = game_config::default_heavy_base_health * PetalRarityScale(rarity);
        stats.reload = game_config::default_heavy_reload;
        stats.preload = game_config::default_heavy_reload;
        stats.copy = static_cast<int>(game_config::default_heavy_copy);
        float mass_level_sq =
            rarity == ERarity::Exotic
                ? BlendUltraSuper(static_cast<float>(GetLevel(ERarity::Ultra) * GetLevel(ERarity::Ultra)),
                                  static_cast<float>(GetLevel(ERarity::Super) * GetLevel(ERarity::Super)))
                : level * level;
        stats.mass = mass_level_sq * game_config::default_heavy_mass_multiplier;
        stats.radius = game_config::default_heavy_base_radius;
        return stats;
    }

    void OnTick(CPetal* owner, ERarity, CFlower* flower, float dt) override
    {
        if (flower && flower->m_defending)
        {
            float orbit_distance = PetalOrbitBaseRadius(owner, flower) + game_config::default_petal_orbit_radius +
                                   PetalOrbitReach(owner, flower, false);
            PetalOrbitMoveAndAttract(owner, flower, orbit_distance, game_config::default_petal_orbit_k, true, dt);
            return;
        }

        PetalOrbitMoveAndAttract(owner, flower, PetalOrbitDistance(owner, flower), game_config::default_petal_orbit_k,
                                 true, dt);
    }

    void OnFlowerTakeDamage(CPetal*, ERarity, CFlower*, float&, EDamageType, CEntity*) override {}
    void OnPetalSpawned(CPetal*, ERarity, CFlower*) override {}
    void OnPetalDestroyed(CPetal*, ERarity, CFlower*) override {}
};

class CFasterBehavior : public CPetalBehavior
{
  public:
    bool IsOpen() const override { return true; }

    SFlowerStats GetStats(ERarity rarity) const override
    {
        SFlowerStats stats = EmptyFlowerStats();
        stats.petal_rotation_speed = game_config::default_faster_rotation_speed_base +
                                     game_config::default_faster_rotation_speed_level * PetalValueLevel(rarity);
        return stats;
    }

    SPetalStats GetPetalStats(ERarity rarity) const override
    {
        SPetalStats stats;
        stats.damage = game_config::default_faster_base_damage * PetalRarityScale(rarity);
        stats.health = game_config::default_faster_base_health * PetalRarityScale(rarity);
        stats.reload = game_config::default_faster_reload;
        stats.preload = game_config::default_faster_reload;
        stats.copy = static_cast<int>(game_config::default_faster_copy);
        stats.mass = game_config::default_faster_mass;
        stats.radius = game_config::default_faster_base_radius;
        return stats;
    }

    void OnTick(CPetal* owner, ERarity, CFlower* flower, float dt) override
    {
        PetalOrbitMoveAndAttract(owner, flower, PetalOrbitDistance(owner, flower), game_config::default_petal_orbit_k,
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
        stats.damage = game_config::default_glass_base_damage * PetalRarityScale(rarity);
        stats.health = game_config::default_glass_base_health * PetalRarityScale(rarity);
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

        PetalOrbitMoveAndAttract(owner, flower, PetalOrbitDistance(owner, flower), game_config::default_petal_orbit_k,
                                 true, dt);
    }

    void OnPetalHit(CPetal* owner, ERarity, CEntity* target, float& damage) override
    {
        auto* glass = dynamic_cast<CGlassPetal*>(owner);
        if (!glass || !target) return;
        auto it = glass->m_hit_cooldowns.find(target->m_id);
        if (it != glass->m_hit_cooldowns.end() && it->second > 0.f)
        {
            damage = 0.f;
            return;
        }

        damage = glass->m_final_petal_stats.damage;
        glass->m_hit_cooldowns[target->m_id] = game_config::default_glass_hit_cooldown;
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
        stats.damage = game_config::default_stinger_base_damage * PetalRarityScale(rarity);
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
        PetalOrbitMoveAndAttract(owner, flower, PetalOrbitDistance(owner, flower), game_config::default_petal_orbit_k,
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
        return BlendUltraSuper(game_config::default_beetleegg_reload_ultra, game_config::default_beetleegg_reload_super,
                               game_config::rarity_exotic_special_super_weight);
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
    float level = std::clamp(PetalSpecialValueLevel(rarity), first_level, last_level);
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
    float level = std::clamp(PetalSpecialValueLevel(rarity), first_level, last_level);
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

inline bool FlowerHasSummonStatMultiplier(const CFlower* flower)
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

    summon->m_health = std::max(1.f, summon->m_health * health_multiplier);
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
        return BlendUltraSuper(game_config::default_bubble_reload_ultra, game_config::default_bubble_reload_super,
                               game_config::rarity_exotic_special_super_weight);
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

    SFlowerStats GetStats(ERarity) const override { return EmptyFlowerStats(); }

    SPetalStats GetPetalStats(ERarity rarity) const override = 0;

    void OnTick(CPetal* owner, ERarity, CFlower* flower, float dt) override
    {
        auto* egg = dynamic_cast<CBeetleEggPetal*>(owner);
        if (!egg || !flower || !flower->GameWorld()) return;

        if (!egg->m_has_spawned_summon)
        {
            PetalOrbitMoveAndAttract(owner, flower, PetalOrbitDistance(owner, flower),
                                     game_config::default_petal_orbit_k, true, dt);
            if (FlowerHasSummonStatMultiplier(flower) && owner->m_lifetime >= 1.f) owner->m_health = 0.f;
            return;
        }

        CEntity* summon = egg->m_summon_id >= 0
                              ? flower->GameWorld()->GetEntity(egg->m_summon_id, egg->m_summon_generation)
                              : nullptr;
        if (summon && !summon->m_is_marked_for_des)
        {
            owner->m_health = std::max(1.f, summon->m_health);
            return;
        }

        egg->m_summon_id = -1;
        egg->m_summon_generation = 0;
        owner->m_health = 0.f;
        owner->MarkForDestroy();
    }

    void OnFlowerTakeDamage(CPetal*, ERarity, CFlower*, float&, EDamageType, CEntity*) override {}

    void OnPetalSpawned(CPetal* owner, ERarity rarity, CFlower* flower) override
    {
        auto* egg = dynamic_cast<CBeetleEggPetal*>(owner);
        if (!egg) return;

        egg->m_summon_id = -1;
        egg->m_summon_generation = 0;
        egg->m_has_spawned_summon = false;
        owner->m_health = 1.f;
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
        if (summon) summon->MarkForDestroy();
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
        sf::Vector2f spawn_pos = owner ? owner->m_pos : flower->m_pos;
        EMobType summon_type = ResolveSummonType(flower);
        auto summon = CreateMob(summon_type, flower->GameWorld(), spawn_pos, summon_rarity);
        if (!summon)
        {
            owner->MarkForDestroy();
            return;
        }

        summon->m_team = flower->m_team;
        summon->SetController(std::make_unique<CSummonedMeleeController>(flower));
        CMobBase* raw_summon = dynamic_cast<CMobBase*>(flower->GameWorld()->InsertEntity(std::move(summon)));
        if (!raw_summon)
        {
            owner->MarkForDestroy();
            return;
        }

        ApplySummonedStatMultipliers(raw_summon, flower);
        egg->m_summon_id = raw_summon->m_id;
        egg->m_summon_generation = raw_summon->m_generation;
        egg->m_has_spawned_summon = true;
        owner->m_health = std::max(1.f, raw_summon->m_health);
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
        if (rarity == ERarity::Exotic) return ERarity::Ultra;

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
        PetalOrbitMove(owner, flower, PetalOrbitNeutralDistance(owner, flower), game_config::default_petal_orbit_k,
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

    void OnTick(CPetal* owner, ERarity, CFlower* flower, float dt) override
    {
        if (!owner || !flower) return;

        float orbit_distance = PetalOrbitNeutralDistance(owner, flower);
        (void)dt;
        PetalOrbitMove(owner, flower, orbit_distance, game_config::default_petal_orbit_k, true);

        if (flower->m_defending)
        {
            owner->m_health = 0.f;
            return;
        }
    }

    void OnFlowerTakeDamage(CPetal*, ERarity, CFlower*, float&, EDamageType, CEntity*) override {}
    void OnPetalSpawned(CPetal*, ERarity, CFlower*) override {}

    void OnPetalDestroyed(CPetal* owner, ERarity, CFlower* flower) override
    {
        if (!owner || !flower) return;

        CPetal* moon = flower->GetMoonPetal();
        sf::Vector2f delta = moon ? moon->m_pos - owner->m_pos : flower->m_pos - owner->m_pos;
        float len = Length(delta);
        if (len <= game_config::entity_collision_epsilon)
        {
            delta = { -owner->m_vel.x, -owner->m_vel.y };
            len = Length(delta);
        }
        if (len <= game_config::entity_collision_epsilon) return;

        if (moon) moon->m_vel += delta / len * game_config::default_bubble_boost_speed;
        else flower->m_vel += delta / len * game_config::default_bubble_boost_speed;
    }
};

inline float SignedAngleBetween(sf::Vector2f base, sf::Vector2f target)
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

inline CEntity* FindMissileLaunchTarget(CMissilePetal* missile, CFlower* flower, sf::Vector2f launch_direction)
{
    if (!missile || !flower || !flower->GameWorld()) return nullptr;

    float max_range = game_config::default_missile_lock_range;
    float max_angle = game_config::default_missile_lock_angle_degrees * game_config::pi / 180.f;
    auto filter = [missile, flower, launch_direction, max_angle](const CEntity* entity) -> bool {
        if (!IsValidPetalEnemyTarget(missile, flower, entity)) return false;
        sf::Vector2f to_target = entity->m_pos - missile->m_pos;
        return std::abs(SignedAngleBetween(launch_direction, to_target)) <= max_angle;
    };

    return flower->GameWorld()->FindClosestEntityByEdge(missile->m_pos, max_range, filter);
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
        stats.damage = game_config::default_cogwheel_base_damage * PetalRarityScale(rarity);
        stats.health = game_config::default_cogwheel_base_health * PetalRarityScale(rarity);
        stats.reload = game_config::default_cogwheel_reload;
        stats.preload = game_config::default_cogwheel_reload;
        stats.copy = static_cast<int>(game_config::default_cogwheel_copy);
        stats.mass = game_config::default_cogwheel_mass;
        stats.radius = game_config::default_cogwheel_base_radius;
        return stats;
    }

    void OnTick(CPetal* owner, ERarity, CFlower* flower, float dt) override
    {
        PetalOrbitMoveAndAttract(owner, flower, PetalOrbitDistance(owner, flower), game_config::default_petal_orbit_k,
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
        float level = PetalSpecialValueLevel(rarity);
        stats.petal_reload_multiplier = std::max(game_config::default_petal_stat_reload_multiplier_min,
                                                 1.0f - level * game_config::default_dust_reload_reduction);
        return stats;
    }

    SPetalStats GetPetalStats(ERarity rarity) const override
    {
        SPetalStats stats;
        stats.damage = game_config::default_dust_base_damage * PetalRarityScale(rarity);
        stats.health = game_config::default_dust_base_health * PetalRarityScale(rarity);
        stats.copy = static_cast<int>(game_config::default_dust_copy);
        stats.mass = game_config::default_dust_mass;
        stats.radius = game_config::default_dust_base_radius;
        return stats;
    }

    void OnTick(CPetal* owner, ERarity, CFlower* flower, float dt) override
    {
        float orbit_distance = PetalOrbitDistance(owner, flower);
        m_is_open = flower->m_attacking;
        PetalOrbitMoveAndAttract(owner, flower, orbit_distance, game_config::default_petal_orbit_k, m_is_open, dt);
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
        float level = PetalSpecialValueLevel(rarity);
        stats.petal_reload_multiplier = std::max(game_config::default_petal_stat_reload_multiplier_min,
                                                 1.0f - level * game_config::default_goldenleaf_base_reload_reduction);
        return stats;
    }

    SPetalStats GetPetalStats(ERarity rarity) const override
    {
        SPetalStats stats;
        stats.damage = game_config::default_goldenleaf_base_damage * PetalRarityScale(rarity);
        stats.health = game_config::default_goldenleaf_base_health * PetalRarityScale(rarity);
        stats.copy = static_cast<int>(game_config::default_goldenleaf_copy);
        stats.mass = game_config::default_goldenleaf_mass;
        stats.radius = game_config::default_goldenleaf_base_radius;
        return stats;
    }

    void OnTick(CPetal* owner, ERarity, CFlower* flower, float dt) override
    {
        float orbit_distance = PetalOrbitDistance(owner, flower);
        PetalOrbitMoveAndAttract(owner, flower, orbit_distance, game_config::default_petal_orbit_k, true, dt);
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
        stats.damage = game_config::default_leaf_base_damage * PetalRarityScale(rarity);
        stats.health = game_config::default_leaf_base_health * PetalRarityScale(rarity);
        stats.reload = game_config::default_leaf_reload;
        stats.preload = game_config::default_leaf_reload;
        stats.copy = static_cast<int>(game_config::default_leaf_copy);
        stats.mass = game_config::default_leaf_mass;
        stats.radius = game_config::default_leaf_base_radius;
        return stats;
    }

    void OnTick(CPetal* owner, ERarity, CFlower* flower, float dt) override
    {
        PetalOrbitMoveAndAttract(owner, flower, PetalOrbitDistance(owner, flower), game_config::default_petal_orbit_k,
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
        stats.damage = game_config::default_yucca_base_damage * PetalRarityScale(rarity);
        stats.health = game_config::default_yucca_base_health * PetalRarityScale(rarity);
        stats.reload = game_config::default_yucca_reload;
        stats.preload = game_config::default_yucca_reload;
        stats.copy = static_cast<int>(game_config::default_yucca_copy);
        stats.mass = game_config::default_yucca_mass;
        stats.radius = game_config::default_yucca_base_radius;
        return stats;
    }

    void OnTick(CPetal* owner, ERarity, CFlower* flower, float dt) override
    {
        PetalOrbitMoveAndAttract(owner, flower, PetalOrbitDistance(owner, flower), game_config::default_petal_orbit_k,
                                 true, dt);
    }

    void OnFlowerTakeDamage(CPetal*, ERarity, CFlower*, float&, EDamageType, CEntity*) override {}
    void OnPetalSpawned(CPetal*, ERarity, CFlower*) override {}
    void OnPetalDestroyed(CPetal*, ERarity, CFlower*) override {}
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
        return BlendUltraSuper(game_config::default_rose_medicine_scale_ultra,
                               game_config::default_rose_medicine_scale_super);
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

inline bool MobNeedsHealing(const CMobBase* mob)
{
    float max_health = MobMaxHealth(mob);
    return max_health > 0.f && mob->m_health < max_health - game_config::entity_collision_epsilon;
}

inline void HealMob(CMobBase* mob, float amount)
{
    if (!mob || amount <= 0.f) return;
    float max_health = MobMaxHealth(mob);
    if (max_health <= 0.f) return;
    if (auto* flower = dynamic_cast<CFlower*>(mob))
        amount *= std::max(0.f, flower->m_final_stats.healing_received_multiplier);
    amount *= std::max(0.f, GetMedicMultiplier(mob));
    mob->m_health = std::min(max_health, mob->m_health + amount);
}

inline bool RoseCanHealTarget(const CPetal* owner, const CFlower* flower, const CEntity* entity)
{
    if (!owner || !flower || !entity || entity == owner) return false;
    if (entity->m_is_marked_for_des || entity->IsDead() || !entity->CanCollide()) return false;
    if (!CheckTeam(entity->m_team, flower->m_team)) return false;
    if (BlocksNullifiedInteraction(owner, entity)) return false;

    const auto* mob = dynamic_cast<const CMobBase*>(entity);
    return mob && MobNeedsHealing(mob);
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
    case ERarity::Super:
    case ERarity::Exotic:
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
        stats.health = game_config::default_yggdrasil_base_health * PetalRarityScale(rarity);
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
        return owner && flower && corpse && corpse->m_is_dead && !corpse->m_is_marked_for_des &&
               CheckTeam(corpse->m_team, flower->m_team) && !BlocksNullifiedInteraction(owner, corpse);
    }

    static CEntity* FindCorpse(CPetal* owner, CFlower* flower)
    {
        if (!owner || !flower || !flower->GameWorld() ||
            owner->m_lifetime < game_config::default_healing_petal_target_delay)
            return nullptr;

        const float range = owner->m_radius * game_config::default_healing_petal_target_range_multiplier;
        if (range <= 0.f) return nullptr;

        const auto can_revive = [owner, flower](const CEntity* entity) { return IsValidCorpse(owner, flower, entity); };
        CEntity* target = PetalGetCachedTarget(owner, flower, owner->m_pos, range, can_revive);
        return target ? target : PetalFindClosestTarget(owner, flower, owner->m_pos, range, can_revive);
    }

    void OnTick(CPetal* raw_owner, ERarity rarity, CFlower* flower, float dt) override
    {
        auto* owner = dynamic_cast<CYggdrasilPetal*>(raw_owner);
        CEntity* target = FindCorpse(owner, flower);
        if (!owner || !flower) return;
        if (!target)
        {
            owner->m_revive_timer = 0.f;
            PetalClearTarget(owner);
            PetalOrbitMove(owner, flower, PetalOrbitDistance(owner, flower), game_config::default_petal_orbit_k, true);
            return;
        }

        const bool target_changed =
            owner->m_target_entity_id != target->m_id || owner->m_target_entity_generation != target->m_generation;
        PetalSetTarget(owner, target);
        if (target_changed) owner->m_revive_timer = 0.f;
        const sf::Vector2f delta = target->m_pos - owner->m_pos;
        const float distance = Length(delta);
        const float stop_distance = std::max(game_config::entity_collision_epsilon,
                                             owner->m_radius * game_config::default_yggdrasil_stop_distance_multiplier);
        if (distance <= stop_distance)
        {
            owner->m_pos = target->m_pos;
            owner->m_prev_pos = owner->m_pos;
            owner->m_vel = { 0.f, 0.f };
        } else
        {
            const float acceleration =
                PetalLockedTargetAcceleration(flower, game_config::default_petal_target_acceleration_multiplier, false);
            const float desired_speed = std::sqrt(std::max(0.f, 2.f * acceleration * (distance - stop_distance)));
            owner->m_vel = delta / distance * desired_speed;
        }

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
            PetalClearTarget(owner);
            return;
        }
        owner->m_reload_override = YggdrasilChannelTime(rarity);
        owner->m_reload_ignore_multiplier = true;
        owner->m_health = 0.f;
    }

    void OnFlowerTakeDamage(CPetal*, ERarity, CFlower*, float&, EDamageType, CEntity*) override {}
    void OnPetalSpawned(CPetal*, ERarity, CFlower*) override {}
    void OnPetalDestroyed(CPetal* owner, ERarity, CFlower*) override
    {
        if (!owner) return;
        owner->m_reload_override =
            owner->m_reload_override >= 0.f ? owner->m_reload_override : game_config::default_yggdrasil_preload;
        auto* ygg = dynamic_cast<CYggdrasilPetal*>(owner);
        if (ygg) ygg->m_revive_timer = 0.f;
    }
};

inline CEntity* RoseFindTarget(CPetal* owner, CFlower* flower)
{
    if (!owner || !flower || !flower->GameWorld()) return nullptr;
    if (owner->m_lifetime < game_config::default_healing_petal_target_delay) return nullptr;

    auto is_valid_heal_target = [owner, flower](const CEntity* entity) -> bool {
        return RoseCanHealTarget(owner, flower, entity);
    };

    if (RoseCanHealTarget(owner, flower, flower)) return flower;

    float range = owner->m_radius * game_config::default_healing_petal_target_range_multiplier;
    if (range <= 0.f)
    {
        PetalClearTarget(owner);
        return nullptr;
    }

    CEntity* raw = PetalGetCachedTarget(owner, flower, owner->m_pos, range, is_valid_heal_target);
    if (!raw) raw = PetalFindClosestTarget(owner, flower, owner->m_pos, range, is_valid_heal_target);
    return raw;
}

class CRoseBehavior : public CPetalBehavior
{
  public:
    bool IsOpen() const override { return true; }

    SFlowerStats GetStats(ERarity) const override { return EmptyFlowerStats(); }

    SPetalStats GetPetalStats(ERarity rarity) const override
    {
        SPetalStats stats;
        stats.damage = game_config::default_rose_base_damage * PetalRarityScale(rarity);
        stats.health = game_config::default_rose_base_health * PetalRarityScale(rarity);
        stats.medicine = game_config::default_rose_base_medicine * RoseMedicineScale(rarity);
        stats.reload = game_config::default_rose_reload;
        stats.preload = game_config::default_rose_preload;
        stats.copy = static_cast<int>(game_config::default_rose_copy);
        stats.mass = game_config::default_rose_mass;
        stats.radius = game_config::default_rose_base_radius;
        return stats;
    }

    void OnTick(CPetal* owner, ERarity, CFlower* flower, float dt) override
    {
        CEntity* target = RoseFindTarget(owner, flower);
        if (owner && target)
        {
            PetalSetTarget(owner, target);
            owner->m_vel = { 0.f, 0.f };
            PetalAttractToTarget(owner, target, dt,
                                 PetalLockedTargetAcceleration(
                                     flower, game_config::default_petal_target_acceleration_multiplier, false));
            PetalTetherWhileTargeting(owner, flower, PetalOrbitDistance(owner, flower),
                                      game_config::default_petal_orbit_k, true);
            return;
        }

        PetalClearTarget(owner);
        PetalOrbitMove(owner, flower, PetalOrbitDistance(owner, flower), game_config::default_petal_orbit_k, true);
    }

    void OnFlowerTakeDamage(CPetal*, ERarity, CFlower*, float&, EDamageType, CEntity*) override {}
    void OnPetalSpawned(CPetal*, ERarity, CFlower*) override {}
    void OnPetalDestroyed(CPetal*, ERarity, CFlower*) override {}

    void OnPetalHit(CPetal* owner, ERarity, CEntity* target, float& damage) override
    {
        auto* mob = dynamic_cast<CMobBase*>(target);
        CEntity* flower = owner ? owner->GetOwner() : nullptr;
        if (!owner || !flower || !mob) return;
        if (owner->m_lifetime < game_config::default_healing_petal_target_delay ||
            owner->m_target_entity_id != target->m_id || owner->m_target_entity_generation != target->m_generation)
        {
            damage = 0.f;
            return;
        }

        if (CheckTeam(owner->m_team, target->m_team))
        {
            if (!MobNeedsHealing(mob))
            {
                damage = 0.f;
                return;
            }
            HealMob(mob, owner->m_final_petal_stats.medicine);
            damage = 0.f;
            owner->m_health = 0.f;
            return;
        }

        damage = owner->m_final_petal_stats.damage;
        owner->m_health = 0.f;
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
        stats.damage = game_config::default_dahlia_base_damage * PetalRarityScale(rarity);
        stats.health = game_config::default_dahlia_base_health * PetalRarityScale(rarity);
        stats.medicine = game_config::default_dahlia_base_medicine * RoseMedicineScale(rarity);
        stats.reload = game_config::default_dahlia_reload;
        stats.preload = game_config::default_dahlia_reload;
        stats.copy = static_cast<int>(game_config::default_dahlia_copy);
        stats.mass = game_config::default_dahlia_mass;
        stats.radius = game_config::default_dahlia_base_radius;
        return stats;
    }

    void OnTick(CPetal* owner, ERarity, CFlower* flower, float dt) override
    {
        CEntity* target = RoseFindTarget(owner, flower);
        if (owner && target)
        {
            PetalSetTarget(owner, target);
            owner->m_vel = { 0.f, 0.f };
            PetalAttractToTarget(owner, target, dt, PetalLockedTargetAcceleration(flower));
            PetalTetherWhileTargeting(owner, flower, PetalOrbitDistance(owner, flower),
                                      game_config::default_petal_orbit_k, false);
            return;
        }

        PetalClearTarget(owner);
        PetalOrbitMove(owner, flower, PetalOrbitDistance(owner, flower), game_config::default_petal_orbit_k, false);
    }

    void OnFlowerTakeDamage(CPetal*, ERarity, CFlower*, float&, EDamageType, CEntity*) override {}
    void OnPetalSpawned(CPetal*, ERarity, CFlower*) override {}
    void OnPetalDestroyed(CPetal*, ERarity, CFlower*) override {}

    void OnPetalHit(CPetal* owner, ERarity, CEntity* target, float& damage) override
    {
        auto* mob = dynamic_cast<CMobBase*>(target);
        if (!owner || !mob) return;
        if (owner->m_lifetime < game_config::default_healing_petal_target_delay ||
            owner->m_target_entity_id != target->m_id || owner->m_target_entity_generation != target->m_generation)
        {
            damage = 0.f;
            return;
        }

        if (CheckTeam(owner->m_team, target->m_team))
        {
            if (!MobNeedsHealing(mob))
            {
                damage = 0.f;
                return;
            }
            HealMob(mob, owner->m_final_petal_stats.medicine);
            damage = 0.f;
            owner->m_health = 0.f;
            return;
        }

        damage = owner->m_final_petal_stats.damage;
        owner->m_health = 0.f;
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
        stats.damage = game_config::default_wing_base_damage * PetalRarityScale(rarity);
        stats.health = game_config::default_wing_base_health * PetalRarityScale(rarity);
        stats.reload = game_config::default_wing_reload;
        stats.preload = game_config::default_wing_reload;
        stats.copy = static_cast<int>(game_config::default_wing_copy);
        stats.mass = game_config::default_wing_mass;
        stats.radius = game_config::default_wing_base_radius;
        return stats;
    }

    void OnTick(CPetal* owner, ERarity, CFlower* flower, float dt) override
    {
        float distance = PetalOrbitDistance(owner, flower);
        if (owner && flower && flower->m_attacking)
        {
            float wave = std::sin(owner->m_lifetime * game_config::pi * 2.f) + game_config::default_wing_wave_offset;
            distance = PetalOrbitBaseRadius(owner, flower) + game_config::default_petal_orbit_radius +
                       PetalOrbitReach(owner, flower, true) * wave;
        }
        PetalOrbitMoveAndAttract(owner, flower, distance, game_config::default_petal_orbit_k, true, dt);
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
        stats.damage = game_config::default_triangle_base_damage * PetalRarityScale(rarity);
        stats.health = game_config::default_triangle_base_health * PetalRarityScale(rarity);
        stats.reload = game_config::default_triangle_reload;
        stats.preload = game_config::default_triangle_reload;
        stats.copy = static_cast<int>(game_config::default_triangle_copy);
        stats.mass = game_config::default_triangle_mass;
        stats.radius = game_config::default_triangle_base_radius;
        return stats;
    }

    void OnTick(CPetal* owner, ERarity, CFlower* flower, float dt) override
    {
        PetalOrbitMoveAndAttract(owner, flower, PetalOrbitDistance(owner, flower), game_config::default_petal_orbit_k,
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
        stats.damage = game_config::default_sawblade_base_damage * PetalRarityScale(rarity);
        stats.health = game_config::default_sawblade_base_health * PetalRarityScale(rarity);
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
        if (flower && flower->m_defending) reach += game_config::default_petal_defend_offset;
        float distance = PetalOrbitBaseRadius(owner, flower) + game_config::default_petal_orbit_radius + reach;
        PetalOrbitMoveAndAttract(owner, flower, distance, game_config::default_petal_orbit_k, true, dt);
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
        return BlendUltraSuper(game_config::default_fragment_value_ultra, game_config::default_fragment_value_super);
    case ERarity::Super:
        return game_config::default_fragment_value_super;
    case ERarity::Eternal:
        return game_config::default_fragment_value_eternal;
    case ERarity::Unique:
        return game_config::default_fragment_value_unique;
    case ERarity::Primordial:
        return game_config::default_fragment_value_primordial;
    default:
        return PetalRarityScale(rarity);
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
        PetalOrbitMoveAndAttract(owner, flower, PetalOrbitDistance(owner, flower), game_config::default_petal_orbit_k,
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

    SPetalStats GetPetalStatsForSlot(ERarity rarity, const CPetalSlot* slot, const CFlower* flower) const override
    {
        const CPetalPrototype* proto = MimicTargetPrototype(slot, flower);
        if (!proto || !proto->m_p_behavior) return GetPetalStats(rarity);
        return proto->m_p_behavior->GetPetalStats(rarity);
    }

    const CPetalPrototype* GetRuntimePrototypeForSlot(ERarity, const CPetalSlot* slot,
                                                      const CFlower* flower) const override
    {
        return MimicTargetPrototype(slot, flower);
    }

    EPetalBonusMode GetBonusModeForSlot(ERarity, const CPetalSlot* slot, const CFlower* flower) const override
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
            PetalOrbitMove(owner, flower, PetalOrbitDistance(owner, flower), game_config::default_petal_orbit_k, true);
            return;
        }

        if (owner && owner->m_type != proto->m_type) ApplyMimicStats(owner, rarity, proto, flower);
        proto->m_p_behavior->OnTick(owner, rarity, flower, dt);
    }

    void OnFlowerTakeDamage(CPetal* owner, ERarity rarity, CFlower* flower, float& dmg, EDamageType damage_type,
                            CEntity* attacker) override
    {
        const CPetalPrototype* proto = owner ? FindPetalPrototype(owner->m_type) : MimicTargetPrototype(owner, flower);
        if (proto && proto->m_type != EPetalType::Mimic && proto->m_p_behavior)
            proto->m_p_behavior->OnFlowerTakeDamage(owner, rarity, flower, dmg, damage_type, attacker);
    }

    void OnPetalHit(CPetal* owner, ERarity rarity, CEntity* target, float& damage) override
    {
        const CPetalPrototype* proto = owner ? FindPetalPrototype(owner->m_type) : nullptr;
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
        const CPetalPrototype* proto = owner ? FindPetalPrototype(owner->m_type) : MimicTargetPrototype(owner, flower);
        if (proto && proto->m_type != EPetalType::Mimic && proto->m_p_behavior)
            proto->m_p_behavior->OnPetalCleared(owner, rarity, flower);
    }

    void OnPetalDestroyed(CPetal* owner, ERarity rarity, CFlower* flower) override
    {
        const CPetalPrototype* proto = owner ? FindPetalPrototype(owner->m_type) : MimicTargetPrototype(owner, flower);
        if (proto && proto->m_type != EPetalType::Mimic && proto->m_p_behavior)
            proto->m_p_behavior->OnPetalDestroyed(owner, rarity, flower);
    }

    bool ShouldReloadAfterPetalDestroyed(CPetal* owner) const override
    {
        const CPetalPrototype* proto = owner ? FindPetalPrototype(owner->m_type) : nullptr;
        if (!proto || proto->m_type == EPetalType::Mimic || !proto->m_p_behavior) return true;
        return proto->m_p_behavior->ShouldReloadAfterPetalDestroyed(owner);
    }

  private:
    static void ApplyMimicStats(CPetal* owner, ERarity rarity, const CPetalPrototype* proto, CFlower* flower)
    {
        if (!owner || !proto || !proto->m_p_behavior) return;

        SPetalStats stats = proto->m_p_behavior->GetPetalStats(rarity);
        if (proto->m_extra_hit_num) stats.extra_hit_num = *proto->m_extra_hit_num;
        NormalizePetalStatsPerCopy(stats, owner->m_max_slot_num);
        stats.radius *= game_config::default_mimic_radius_multiplier;
        owner->m_type = proto->m_type;
        owner->m_base_petal_stats = stats;
        owner->m_final_petal_stats = stats;
        if (flower && flower->GetFinalStats())
        {
            const SFlowerStats& flower_stats = *flower->GetFinalStats();
            const float damage_bonus = owner->m_type == EPetalType::Triangle ? flower_stats.tridmgbonus : 0.f;
            owner->m_final_petal_stats.ActedOn(flower_stats, damage_bonus);
        }
        owner->m_radius = stats.radius;
        owner->m_mass = stats.mass;
        owner->m_health = std::min(owner->m_health, owner->m_final_petal_stats.health);
        if (owner->m_health <= 0.f) owner->m_health = owner->m_final_petal_stats.health;
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
        stats.damage = game_config::default_iris_base_damage * PetalRarityScale(rarity);
        stats.health = game_config::default_iris_base_health * PetalRarityScale(rarity);
        stats.reload = game_config::default_iris_reload;
        stats.preload = game_config::default_iris_reload;
        stats.copy = static_cast<int>(game_config::default_iris_copy);
        stats.mass = game_config::default_iris_mass;
        stats.radius = game_config::default_iris_base_radius;
        return stats;
    }

    void OnTick(CPetal* owner, ERarity, CFlower* flower, float dt) override
    {
        PetalOrbitMoveAndAttract(owner, flower, PetalOrbitDistance(owner, flower), game_config::default_petal_orbit_k,
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
        stats.petal_attraction_range = game_config::default_lentil_petal_attraction_range * PetalValueLevel(rarity);
        return stats;
    }

    SPetalStats GetPetalStats(ERarity rarity) const override
    {
        SPetalStats stats;
        stats.damage = game_config::default_lentil_base_damage * PetalRarityScale(rarity);
        stats.health = game_config::default_lentil_base_health * PetalRarityScale(rarity);
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

        const float range = PetalEnemyTargetRange(owner, flower, true);
        if (range <= 0.f) return nullptr;

        auto filter = [owner, flower](const CEntity* entity) -> bool {
            return IsValidPetalEnemyTarget(owner, flower, entity);
        };

        CEntity* raw = PetalGetCachedTarget(owner, flower, owner->m_pos, range, filter);
        if (!raw) raw = PetalFindClosestTarget(owner, flower, owner->m_pos, range, filter);
        return raw;
    }

    void OnTick(CPetal* owner, ERarity, CFlower* flower, float dt) override
    {
        CEntity* target = LentilFindTarget(owner, flower);
        PetalOrbitMove(owner, flower, PetalOrbitDistance(owner, flower), game_config::default_petal_orbit_k, true);
        if (owner && target)
        {
            PetalSetTarget(owner, target);
            owner->m_vel = { 0.f, 0.f };
            PetalAttractToTarget(owner, target, dt, PetalLockedTargetAcceleration(flower));
            PetalTetherWhileTargeting(owner, flower, PetalOrbitDistance(owner, flower),
                                      game_config::default_petal_orbit_k, true);
            return;
        }

        PetalClearTarget(owner);
    }

    void OnFlowerTakeDamage(CPetal*, ERarity, CFlower*, float&, EDamageType, CEntity*) override {}
    void OnPetalSpawned(CPetal*, ERarity, CFlower*) override {}
    void OnPetalDestroyed(CPetal*, ERarity, CFlower*) override {}
};
inline float MoonVisibleRadius(ERarity rarity)
{
    float level = PetalValueLevel(rarity);
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
        stats.damage = game_config::default_moon_base_damage * PetalRarityScale(rarity);
        stats.health = game_config::default_moon_base_health * PetalRarityScale(rarity);
        stats.reload = game_config::default_moon_reload;
        stats.preload = game_config::default_moon_reload;
        stats.copy = static_cast<int>(game_config::default_moon_copy);
        stats.stack = false;
        stats.mass = game_config::default_moon_base_mass;
        stats.radius = visible_radius * 2.f;
        return stats;
    }

    void OnTick(CPetal* owner, ERarity, CFlower*, float dt) override
    {
        if (!owner) return;

        float speed = Length(owner->m_vel);
        if (speed <= game_config::default_moon_stop_velocity_epsilon)
        {
            owner->m_vel = { 0.f, 0.f };
            return;
        }

        owner->m_vel *= game_config::mob_stop_damping;
    }

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
        stats.damage = game_config::default_pincer_base_damage * PetalRarityScale(rarity);
        stats.health = game_config::default_pincer_base_health * PetalRarityScale(rarity);
        stats.reload = game_config::default_pincer_reload;
        stats.preload = game_config::default_pincer_reload;
        stats.copy = static_cast<int>(game_config::default_pincer_copy);
        stats.mass = game_config::default_pincer_mass;
        stats.radius = game_config::default_pincer_base_radius;
        return stats;
    }

    void OnTick(CPetal* owner, ERarity, CFlower* flower, float dt) override
    {
        PetalOrbitMoveAndAttract(owner, flower, PetalOrbitDistance(owner, flower), game_config::default_petal_orbit_k,
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
        float poison_total = game_config::default_pincer_poison_total_damage * PetalRarityScale(rarity);
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
        stats.damage = game_config::default_dandelion_base_damage * PetalRarityScale(rarity);
        stats.health = game_config::default_dandelion_base_health * PetalRarityScale(rarity);
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

        float distance = PetalOrbitDistance(owner, flower);
        if (flower->m_attacking) distance += game_config::default_dandelion_attack_extra_reach;
        PetalOrbitMoveAndAttract(owner, flower, distance, game_config::default_petal_orbit_k, true, dt);

        sf::Vector2f direction = owner->m_pos - PetalOrbitCenter(owner, flower);
        if (LengthSq(direction) <= game_config::entity_collision_epsilon * game_config::entity_collision_epsilon)
            direction = { std::cos(flower->m_facing_angle), std::sin(flower->m_facing_angle) };
        if (LengthSq(direction) <= game_config::entity_collision_epsilon * game_config::entity_collision_epsilon)
            direction = { 1.f, 0.f };
        owner->m_facing_angle = std::atan2(direction.y, direction.x);
        owner->m_has_facing = true;
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
        stats.damage = game_config::default_missile_base_damage * PetalRarityScale(rarity);
        stats.health = game_config::default_missile_base_health * PetalRarityScale(rarity);
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

        PetalOrbitMoveAndAttract(owner, flower, PetalOrbitDistance(owner, flower), game_config::default_petal_orbit_k,
                                 true, dt);
        sf::Vector2f radial = owner->m_pos - PetalOrbitCenter(owner, flower);
        if (LengthSq(radial) <= game_config::entity_collision_epsilon * game_config::entity_collision_epsilon)
            radial = { std::cos(flower->m_facing_angle), std::sin(flower->m_facing_angle) };
        if (LengthSq(radial) <= game_config::entity_collision_epsilon * game_config::entity_collision_epsilon)
            radial = { 1.f, 0.f };
        owner->m_facing_angle = std::atan2(radial.y, radial.x);
        owner->m_has_facing = true;

        if (!flower->m_attacking || owner->m_lifetime < game_config::default_missile_arm_time) return;

        sf::Vector2f direction = owner->m_pos - flower->m_pos;
        float len = Length(direction);
        if (len <= game_config::entity_collision_epsilon)
        {
            direction = { std::cos(flower->m_facing_angle), std::sin(flower->m_facing_angle) };
            len = Length(direction);
        }
        if (len <= game_config::entity_collision_epsilon) direction = { 1.f, 0.f };
        else direction /= len;

        if (CEntity* target = FindMissileLaunchTarget(missile, flower, direction))
        {
            sf::Vector2f to_target = target->m_pos - owner->m_pos;
            float target_len = Length(to_target);
            if (target_len > game_config::entity_collision_epsilon) direction = to_target / target_len;
        }

        float flower_speed =
            flower->GetFinalStats() ? flower->GetFinalStats()->max_velocity : game_config::default_max_velocity;
        missile->BeginThrow(direction, flower_speed * game_config::default_missile_speed_multiplier, 0.f, false, true,
                            false);
        float fired_angle = std::atan2(direction.y, direction.x);
        owner->m_facing_angle = fired_angle;
        owner->m_has_facing = true;
        owner->m_detach_from_slot = true;
        if (owner->m_type == EPetalType::Missile)
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

class CCarrotBehavior : public CMissileBehavior
{
  public:
    SPetalStats GetPetalStats(ERarity rarity) const override
    {
        SPetalStats stats;
        stats.damage = game_config::default_carrot_base_damage * PetalRarityScale(rarity);
        stats.health = game_config::default_carrot_base_health * PetalRarityScale(rarity);
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

inline sf::Vector2f PetalThrowDirection(CPetal* owner, CFlower* flower)
{
    sf::Vector2f direction = owner && flower ? owner->m_pos - flower->m_pos : sf::Vector2f{ 0.f, 0.f };
    float len = Length(direction);
    if (len <= game_config::entity_collision_epsilon && flower)
        direction = { std::cos(flower->m_facing_angle), std::sin(flower->m_facing_angle) };
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
        radius_units = BlendUltraSuper(game_config::default_web_radius_ultra, game_config::default_web_radius_super,
                                       game_config::rarity_exotic_special_super_weight);
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
    auto mass_scale_for_level = [](int level) {
        return std::pow(game_config::mob_mass_scale_base,
                        static_cast<float>(level - 1) * game_config::mob_mass_scale_exp_multiplier);
    };

    float mass_scale = 1.f;
    if (rarity == ERarity::Exotic)
    {
        mass_scale = BlendUltraSuper(mass_scale_for_level(GetLevel(ERarity::Ultra)),
                                     mass_scale_for_level(GetLevel(ERarity::Super)));
    } else
    {
        mass_scale = mass_scale_for_level(GetLevel(rarity));
    }

    return game_config::mob_soldier_ant_mass * mass_scale;
}

inline float WebDesiredSpeedMultiplier(ERarity rarity)
{
    const float first_level = static_cast<float>(GetLevel(ERarity::Common));
    const float last_level = static_cast<float>(GetLevel(ERarity::Primordial));
    float t = std::clamp(PetalSpecialValueLevel(rarity) - first_level, 0.f, last_level - first_level) /
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

    auto zone = std::make_unique<CSpiderWebZone>(world, owner->m_pos, WebRadius(rarity),
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
        stats.damage = game_config::default_web_base_damage * PetalRarityScale(rarity);
        stats.health = game_config::default_web_base_health * PetalRarityScale(rarity);
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

        PetalOrbitMove(owner, flower, PetalOrbitNeutralDistance(owner, flower), game_config::default_petal_orbit_k,
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
        float scale = PetalRarityScale(rarity);

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

        PetalOrbitMove(owner, flower, PetalOrbitNeutralDistance(owner, flower), game_config::default_petal_orbit_k,
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
            if (owner->m_timer <= 0.f) owner->m_health = 0.f;
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
        stats.health = game_config::default_honey_base_health * PetalRarityScale(rarity);
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

        PetalOrbitMove(owner, flower, PetalOrbitNeutralDistance(owner, flower), game_config::default_petal_orbit_k,
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

class CWaxBehavior : public CPetalBehavior
{
  public:
    bool IsOpen() const override { return true; }

    SFlowerStats GetStats(ERarity) const override { return EmptyFlowerStats(); }

    SPetalStats GetPetalStats(ERarity rarity) const override
    {
        SPetalStats stats;
        stats.damage = 0.f;
        stats.health = game_config::default_wax_base_health * PetalRarityScale(rarity);
        stats.reload = game_config::default_wax_reload;
        stats.preload = game_config::default_wax_reload;
        stats.copy = static_cast<int>(game_config::default_wax_copy);
        stats.stack = false;
        stats.mass = game_config::default_wax_mass;
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
    void OnPetalSpawned(CPetal*, ERarity, CFlower*) override {}
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
        return BlendUltraSuper(game_config::default_shovel_preload_ultra, game_config::default_shovel_preload_super);
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
        PetalClearTarget(owner);
        PetalOrbitMove(owner, flower, PetalOrbitDistance(owner, flower), game_config::default_petal_orbit_k, true);
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
        stats.damage = game_config::default_orange_base_damage * PetalRarityScale(rarity);
        stats.health = game_config::default_orange_base_health * PetalRarityScale(rarity);
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
        else if (flower->m_defending) reach += game_config::default_petal_defend_offset;

        const float reach_distance =
            PetalOrbitBaseRadius(owner, flower) + game_config::default_petal_orbit_radius + reach;
        const float circle_radius = game_config::default_orange_circle_radius;
        float group_angle =
            flower->GetPetalRotationAngle() +
            (2.f * game_config::pi * static_cast<float>(start_index)) / static_cast<float>(total_copies);
        sf::Vector2f direction = { std::cos(group_angle), std::sin(group_angle) };
        sf::Vector2f circle_center = flower->m_pos + direction * (reach_distance + circle_radius);
        float base_angle = std::atan2(-direction.y, -direction.x);
        float angle =
            base_angle + 2.f * game_config::pi * static_cast<float>(owner->m_copy_index) / static_cast<float>(copies);
        sf::Vector2f target = circle_center + sf::Vector2f(std::cos(angle), std::sin(angle)) * circle_radius;

        sf::Vector2f delta = target - owner->m_pos;
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
    if (rarity == ERarity::Unique || rarity == ERarity::Eternal) return game_config::default_compass_priority_eternal;
    if (rarity == ERarity::Super) return game_config::default_compass_priority_super;
    return 0;
}

inline bool IsCompassMagnetPetal(const CEntity* entity, const CCompassPetal* owner, const CFlower* flower)
{
    auto* petal = dynamic_cast<const CPetal*>(entity);
    if (!petal || petal == owner || !flower) return false;
    if (petal->m_type != EPetalType::Compass) return false;
    if (petal->m_is_marked_for_des || petal->IsDead() || !petal->CanCollide()) return false;
    if (CheckTeam(petal->m_team, flower->m_team)) return false;
    if ((petal->m_team == 0 || flower->m_team == 0) && ShareRootOwner(owner, petal)) return false;
    return true;
}

inline CEntity* FindCompassMagnet(CCompassPetal* owner, CFlower* flower)
{
    if (!owner || !flower || !flower->GameWorld()) return nullptr;
    auto filter = [owner, flower](const CEntity* entity) -> bool {
        return IsCompassMagnetPetal(entity, owner, flower);
    };
    return flower->GameWorld()->FindClosestEntityByEdge(owner->m_pos, game_config::default_compass_magnet_range,
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

        float dist_sq = DistanceSq(owner->m_pos, entity->m_pos);
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
    sf::Vector2f delta = target->m_pos - owner->m_pos;
    if (LengthSq(delta) <= game_config::entity_collision_epsilon * game_config::entity_collision_epsilon) return;
    owner->m_facing_angle = std::atan2(delta.y, delta.x);
    owner->m_has_facing = true;
}

class CCompassBehavior : public CPetalBehavior
{
  public:
    bool IsOpen() const override { return true; }

    SFlowerStats GetStats(ERarity) const override { return EmptyFlowerStats(); }

    SPetalStats GetPetalStats(ERarity rarity) const override
    {
        SPetalStats stats;
        stats.damage = game_config::default_compass_base_damage * PetalRarityScale(rarity);
        stats.health = game_config::default_compass_base_health * PetalRarityScale(rarity);
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

        PetalOrbitMove(owner, flower, PetalOrbitDistance(owner, flower), game_config::default_petal_orbit_k, true);
        if (!CompassCanPoint(rarity)) return;

        if (rarity != ERarity::Super)
        {
            if (CEntity* magnet = FindCompassMagnet(owner, flower))
            {
                owner->m_compass_target_id = magnet->m_id;
                owner->m_compass_target_generation = magnet->m_generation;
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

        if (owner->m_compass_target_id != target->m_id || owner->m_compass_target_generation != target->m_generation)
        {
            owner->m_compass_target_id = target->m_id;
            owner->m_compass_target_generation = target->m_generation;
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
        return BlendUltraSuper(game_config::default_relic_health_bonus_ultra,
                               game_config::default_relic_health_bonus_super,
                               game_config::rarity_exotic_special_super_weight);
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
        stats.damage = game_config::default_relic_base_damage * PetalRarityScale(rarity);
        stats.health = game_config::default_relic_base_health * PetalRarityScale(rarity);
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
        PetalOrbitMoveAndAttract(owner, flower, PetalOrbitDistance(owner, flower), game_config::default_petal_orbit_k,
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
            int team = flower->m_team;
            auto new_zone = std::make_unique<CStateZone>(flower->GameWorld(), flower->m_pos, radius,
                                                         CStateZone::MakeStateFactory<CPsionicConnectionState>(
                                                             game_config::default_relic_psionic_zone_refresh, rarity),
                                                         [team](CEntity* entity) -> bool {
                                                             auto* mob = dynamic_cast<CMobBase*>(entity);
                                                             return mob && !mob->HasState<CDiggingState>() &&
                                                                    CheckTeam(mob->m_team, team);
                                                         });

            zone = dynamic_cast<CStateZone*>(flower->GameWorld()->InsertEntity(std::move(new_zone)));
            if (!zone)
            {
                relic->m_state_zone_id = -1;
                relic->m_state_zone_generation = 0;
                return;
            }
            relic->m_state_zone_id = zone->m_id;
            relic->m_state_zone_generation = zone->m_generation;
        }

        zone->SetCenter(flower->m_pos);
        zone->m_radius = radius;
        zone->m_timer = game_config::default_relic_psionic_zone_refresh *
                        game_config::default_relic_psionic_zone_lifetime_multiplier;
        zone->m_state = CStateZone::MakeStateFactory<CPsionicConnectionState>(
            game_config::default_relic_psionic_zone_refresh, rarity);
        int team = flower->m_team;
        zone->m_filter = [team](CEntity* entity) -> bool {
            auto* mob = dynamic_cast<CMobBase*>(entity);
            return mob && !mob->HasState<CDiggingState>() && CheckTeam(mob->m_team, team);
        };
    }

    void DestroyPsionicZone(CPetal* owner, CFlower* flower) const
    {
        auto* relic = dynamic_cast<CRelicPetal*>(owner);
        if (!relic || !flower || !flower->GameWorld()) return;

        CEntity* zone = relic->m_state_zone_id >= 0
                            ? flower->GameWorld()->GetEntity(relic->m_state_zone_id, relic->m_state_zone_generation)
                            : nullptr;
        if (zone) zone->MarkForDestroy();
        relic->m_state_zone_id = -1;
        relic->m_state_zone_generation = 0;
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
        stats.damage = game_config::default_yinyang_base_damage * PetalRarityScale(rarity);
        stats.health = game_config::default_yinyang_base_health * PetalRarityScale(rarity);
        stats.reload = game_config::default_yinyang_reload;
        stats.preload = game_config::default_yinyang_reload;
        stats.copy = static_cast<int>(game_config::default_yinyang_copy);
        stats.mass = game_config::default_yinyang_mass;
        stats.radius = game_config::default_yinyang_base_radius;
        return stats;
    }

    void OnTick(CPetal* owner, ERarity, CFlower* flower, float dt) override
    {
        float orbit_distance = PetalOrbitDistance(owner, flower);
        PetalOrbitMoveAndAttract(owner, flower, orbit_distance, game_config::default_petal_orbit_k, true, dt);
    }

    void OnFlowerTakeDamage(CPetal*, ERarity, CFlower*, float&, EDamageType, CEntity*) override {}
    void OnPetalSpawned(CPetal*, ERarity, CFlower*) override {}
    void OnPetalDestroyed(CPetal*, ERarity, CFlower*) override {}
};
