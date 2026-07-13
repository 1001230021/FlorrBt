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
    stats.detection_multiplier = 1.f;
    stats.max_velocity = 0.f;
    stats.acceleration = 0.f;
    stats.max_health_multiplier = 1.f;
    stats.reach = 0.f;
    stats.petal_attraction_range = 0.f;
    stats.petal_dmg_multiplier = 1.f;
    stats.petal_reload_multiplier = 1.f;
    stats.petal_health_multiplier = 1.f;
    stats.petal_medicine_multiplier = 1.f;
    stats.mult_summoned_health = 1.f;
    stats.petal_rotation_speed = 0.f;
    stats.petal_rotation_quantized = false;
    stats.petal_rotation_mode = EPetalRotationMode::Orbit;
    return stats;
}


inline float PetalRarityScale(ERarity rarity)
{
    return std::pow(game_config::default_petal_pow, static_cast<float>(GetLevel(rarity) - 1));
}

inline float RoseMedicineScale(ERarity rarity);

inline int PetalLevel(ERarity rarity)
{
    return GetLevel(rarity);
}

inline float TriBonusDamage(ERarity rarity)
{
    return 4.f * std::pow(3.f, static_cast<float>(PetalLevel(rarity) - 1));
}

inline float PetalOrbitBaseRadius(const CPetal* owner, const CFlower* flower)
{
    if (!owner || !flower || !flower->GetFinalStats()) return 0.f;
    if (owner->m_type == EPetalType::Moon) return flower->GetFinalStats()->radius;

    CPetal* moon = flower->GetMoonPetal();
    if (moon && moon != owner) return moon->m_radius;
    return flower->GetFinalStats()->radius;
}

inline float PetalOrbitReach(const CFlower* flower, bool include_mode_offset)
{
    if (!flower || !flower->GetFinalStats()) return 0.f;

    float reach = game_config::default_petal_neutral_reach + flower->GetFinalStats()->reach;
    if (!include_mode_offset) return reach;

    if (flower->m_attacking) reach += game_config::default_petal_attack_offset;
    else if (flower->m_defending) reach += game_config::default_petal_defend_offset;
    return reach;
}

inline float PetalOrbitDistance(const CPetal* owner, const CFlower* flower)
{
    if (!owner || !flower || !flower->GetFinalStats()) return 0.f;

    float distance = PetalOrbitBaseRadius(owner, flower) + game_config::default_petal_orbit_radius +
                     PetalOrbitReach(flower, true);
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
        float angle = flower->GetPetalRotationAngle() + (2.0f * game_config::pi * column_index) / static_cast<float>(columns);
        float layer_distance = static_cast<float>(layer_index) * game_config::default_petal_orbit_radius;
        global = orbit_center + sf::Vector2f(std::cos(angle), std::sin(angle)) * (orbit_distance + layer_distance);
        if (!is_open)
        {
            int copies_in_group = owner->m_base_petal_stats.copy;
            if (copies_in_group <= 0) return std::nullopt;

            float spread_radius = 0.0f;
            if (copies_in_group > 1) spread_radius = owner->m_radius / std::sin(game_config::pi / copies_in_group);
            float sub_angle = (2.0f * game_config::pi * owner->m_copy_index) / static_cast<float>(copies_in_group);
            global += sf::Vector2f(std::cos(sub_angle), std::sin(sub_angle)) * spread_radius;
        }

    } else if (is_open) { // 非yinyang
        float angle = flower->GetPetalRotationAngle() + (2.0f * game_config::pi * (start_index + owner->m_copy_index)) / static_cast<float>(total_copies);
        global = orbit_center + sf::Vector2f(std::cos(angle), std::sin(angle)) * orbit_distance;
    } else {
        float group_angle = flower->GetPetalRotationAngle() + (2.0f * game_config::pi * start_index) / static_cast<float>(total_copies);
        sf::Vector2f group_global = orbit_center + sf::Vector2f(std::cos(group_angle), std::sin(group_angle)) * orbit_distance;
        int copies_in_group = owner->m_base_petal_stats.copy;
        if (copies_in_group <= 0) return std::nullopt;

        float spread_radius = 0.0f;
        if (copies_in_group > 1) spread_radius = owner->m_radius / std::sin(game_config::pi / copies_in_group);
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
    float effective_k = k * 2.f;
    if (owner->m_spawn_flight_boost)
    {
        if (LengthSq(delta) <= (owner->m_radius * owner->m_radius))
            owner->m_spawn_flight_boost = false;
    }
    owner->m_vel = delta * effective_k;
}

inline bool IsValidPetalEnemyTarget(const CPetal* owner, const CFlower* flower, const CEntity* entity)
{
    if (!owner || !flower || !entity) return false;
    if (entity->m_is_marked_for_des || entity->IsDead() || !entity->CanCollide()) return false;
    if (entity == owner || entity == flower) return false;
    if (dynamic_cast<const CPetal*>(entity)) return false;
    if (CheckTeam(entity->m_team, flower->m_team)) return false;
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

inline CEntity* PetalGetCachedTarget(CPetal* owner, CFlower* flower, const sf::Vector2f& center, float range,
                                     const std::function<bool(const CEntity*)>& filter)
{
    if (!owner || !flower || !flower->GameWorld() || owner->m_target_entity_id < 0) return nullptr;

    CEntity* target = flower->GameWorld()->GetEntity(owner->m_target_entity_id);
    if (!target || !filter(target) || !PetalTargetInRange(center, range, target))
    {
        owner->m_target_entity_id = -1;
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

inline CEntity* PetalFindTarget(CPetal* owner, CFlower* flower)
{
    if (!owner || !flower || !flower->GetFinalStats()) return nullptr;

    float range = flower->GetFinalStats()->petal_attraction_range + owner->m_radius;
    auto filter = [owner, flower](const CEntity* entity) -> bool
    {
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

inline float PetalTargetAcceleration(const CFlower* flower, float multiplier = 4.f)
{
    float acceleration = game_config::default_acceleration;
    float range_scale = 1.f;
    if (flower && flower->GetFinalStats())
    {
        acceleration = std::max(acceleration, flower->GetFinalStats()->acceleration);
        float base_range = std::max(game_config::entity_collision_epsilon,
                                    game_config::default_lentil_petal_attraction_range);
        range_scale = std::max(1.f, flower->GetFinalStats()->petal_attraction_range / base_range);
    }
    return acceleration * multiplier * range_scale * 1.25f;
}

inline float PetalLockedTargetAcceleration(const CFlower* flower, float multiplier = 4.f)
{
    return PetalTargetAcceleration(flower, multiplier) * 0.8f;
}

inline void PetalClearTarget(CPetal* owner)
{
    if (!owner) return;
    owner->m_target_entity_id = -1;
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

    owner->m_target_entity_id = target->m_id;
    owner->m_vel = {0.f, 0.f};
    PetalAttractToTarget(owner, target, dt, PetalLockedTargetAcceleration(flower, acceleration_multiplier));

    std::optional<sf::Vector2f> global = PetalOrbitGlobal(owner, flower, orbit_distance, is_open);
    if (global)
    {
        constexpr float target_orbit_tether = 0.35f;
        owner->m_vel += (*global - owner->m_pos) * k * target_orbit_tether;
    }
}

inline void PetalTetherWhileTargeting(CPetal* owner, CFlower* flower, float orbit_distance, float k, bool is_open)
{
    if (!owner || !flower) return;

    std::optional<sf::Vector2f> global = PetalOrbitGlobal(owner, flower, orbit_distance, is_open);
    if (!global) return;

    constexpr float target_orbit_tether = 0.35f;
    owner->m_vel += (*global - owner->m_pos) * k * target_orbit_tether;
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
void RegisterLentil();
void RegisterMoon();
void RegisterNullification();
void RegisterPincer();
void RegisterRelic();
void RegisterRose();
void RegisterYinYang();
void RegisterYggdrasil();
void RegisterMissile();
void RegisterBloodSacrifice();
void RegisterCorruption();
void RegisterBandage();
void RegisterBone();
void RegisterCoin();
void RegisterDahlia();
void RegisterWing();
void RegisterTriangle();
void RegisterSawblade();
void RegisterFragment();
void RegisterMimic();
void RegisterGlass();
void RegisterStinger();
void RegisterPetals();

class CAirBehavior : public CPetalBehavior
{
  public:
    bool IsOpen() const override { return true; }

    SFlowerStats GetStats(ERarity rarity) const override
    {
        SFlowerStats stats = EmptyFlowerStats();
        int level = GetLevel(rarity);
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
    void OnPetalSpawned(CPetal*, ERarity, CFlower*) override {}
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

class CBoneBehavior : public CPetalBehavior
{
  public:
    bool IsOpen() const override { return true; }

    SFlowerStats GetStats(ERarity) const override { return EmptyFlowerStats(); }

    SPetalStats GetPetalStats(ERarity rarity) const override
    {
        SPetalStats stats;
        stats.damage = 14.f * PetalRarityScale(rarity);
        stats.health = 10.f * PetalRarityScale(rarity);
        stats.armor = 10.f * PetalRarityScale(rarity);
        stats.reload = 2.5f;
        stats.preload = 2.5f;
        stats.copy = 1;
        stats.mass = game_config::default_basic_mass;
        stats.radius = game_config::default_basic_base_radius;
        return stats;
    }

    void OnTick(CPetal* owner, ERarity, CFlower* flower, float dt) override
    {
        PetalOrbitMoveAndAttract(owner, flower, PetalOrbitDistance(owner, flower),
                                 game_config::default_petal_orbit_k, true, dt);
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
        stats.damage = 15.f * PetalRarityScale(rarity);
        stats.health = 10.f * PetalRarityScale(rarity);
        stats.reload = 2.5f;
        stats.preload = 2.5f;
        stats.copy = 1;
        stats.mass = game_config::default_basic_mass;
        stats.radius = game_config::default_basic_base_radius;
        return stats;
    }

    void OnTick(CPetal* owner, ERarity, CFlower* flower, float dt) override
    {
        PetalOrbitMoveAndAttract(owner, flower, PetalOrbitDistance(owner, flower),
                                 game_config::default_petal_orbit_k, true, dt);
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
        int level = GetLevel(rarity);
        SPetalStats stats;
        stats.damage = game_config::default_heavy_base_damage * PetalRarityScale(rarity);
        stats.health = game_config::default_heavy_base_health * PetalRarityScale(rarity);
        stats.reload = game_config::default_heavy_reload;
        stats.preload = game_config::default_heavy_reload;
        stats.copy = 1;
        stats.mass = static_cast<float>(level * level) * game_config::default_heavy_mass_multiplier;
        stats.radius = game_config::default_heavy_base_radius;
        return stats;
    }

    void OnTick(CPetal* owner, ERarity, CFlower* flower, float dt) override
    {
        if (flower && flower->m_defending)
        {
            float orbit_distance = PetalOrbitBaseRadius(owner, flower) + game_config::default_petal_orbit_radius +
                                   PetalOrbitReach(flower, false);
            PetalOrbitMoveAndAttract(owner, flower, orbit_distance, game_config::default_petal_orbit_k, true, dt);
            return;
        }

        PetalOrbitMoveAndAttract(owner, flower, PetalOrbitDistance(owner, flower),
                                 game_config::default_petal_orbit_k, true, dt);
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
                                     game_config::default_faster_rotation_speed_level * GetLevel(rarity);
        return stats;
    }

    SPetalStats GetPetalStats(ERarity rarity) const override
    {
        SPetalStats stats;
        stats.damage = game_config::default_faster_base_damage * PetalRarityScale(rarity);
        stats.health = game_config::default_faster_base_health * PetalRarityScale(rarity);
        stats.reload = game_config::default_faster_reload;
        stats.preload = game_config::default_faster_reload;
        stats.copy = 1;
        stats.mass = game_config::default_faster_mass;
        stats.radius = game_config::default_faster_base_radius;
        return stats;
    }

    void OnTick(CPetal* owner, ERarity, CFlower* flower, float dt) override
    {
        PetalOrbitMoveAndAttract(owner, flower, PetalOrbitDistance(owner, flower),
                                 game_config::default_petal_orbit_k, true, dt);
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
        stats.damage = 22.f * PetalRarityScale(rarity);
        stats.health = game_config::default_basic_base_health * PetalRarityScale(rarity);
        stats.reload = 2.5f;
        stats.preload = 2.5f;
        stats.copy = 1;
        stats.mass = game_config::default_basic_mass;
        stats.radius = game_config::default_basic_base_radius;
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
                if (it->second <= 0.f)
                    it = glass->m_hit_cooldowns.erase(it);
                else
                    ++it;
            }
        }

        PetalOrbitMoveAndAttract(owner, flower, PetalOrbitDistance(owner, flower),
                                 game_config::default_petal_orbit_k, true, dt);
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
        glass->m_hit_cooldowns[target->m_id] = 1.f;
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
        stats.damage = game_config::default_stinger_base_damage * PetalRarityScale(rarity) /
                       static_cast<float>(copy);
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
        PetalOrbitMoveAndAttract(owner, flower, PetalOrbitDistance(owner, flower),
                                 game_config::default_petal_orbit_k, false, dt);
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
    switch (rarity)
    {
    case ERarity::Common:
        return 1.10f;
    case ERarity::Unusual:
        return 1.20f;
    case ERarity::Rare:
        return 1.35f;
    case ERarity::Epic:
        return 1.50f;
    case ERarity::Legendary:
        return 1.75f;
    case ERarity::Mythic:
        return 2.10f;
    case ERarity::Ultra:
        return 2.50f;
    case ERarity::Super:
        return 2.80f;
    case ERarity::Eternal:
    case ERarity::Unique:
        return 3.25f;
    case ERarity::Primordial:
        return 4.00f;
    default:
        return 1.00f;
    }
}

inline bool FlowerHasSummonHealthMultiplier(const CFlower* flower)
{
    const SFlowerStats* stats = flower ? flower->GetFinalStats() : nullptr;
    return stats && stats->mult_summoned_health > 1.f + game_config::entity_collision_epsilon;
}

inline void ApplySummonedHealthMultiplier(CMobBase* summon, const CFlower* flower)
{
    if (!summon || !flower || !flower->GetFinalStats()) return;

    float multiplier = std::max(0.f, flower->GetFinalStats()->mult_summoned_health);
    if (std::abs(multiplier - 1.f) <= game_config::entity_collision_epsilon) return;

    if (auto* mob = dynamic_cast<CMob<SMobStats>*>(summon))
    {
        mob->m_base_stats.max_health *= multiplier;
        mob->m_final_stats.max_health *= multiplier;
    }

    summon->m_health = std::max(1.f, summon->m_health * multiplier);
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
            if (FlowerHasSummonHealthMultiplier(flower) && owner->m_lifetime >= 1.f)
                owner->m_health = 0.f;
            return;
        }

        CEntity* summon = flower->GameWorld()->GetEntity(egg->m_summon_id);
        if (summon && !summon->m_is_marked_for_des)
        {
            owner->m_health = std::max(1.f, summon->m_health);
            return;
        }

        egg->m_summon_id = -1;
        owner->m_health = 0.f;
        owner->m_is_marked_for_des = true;
    }

    void OnFlowerTakeDamage(CPetal*, ERarity, CFlower*, float&, EDamageType, CEntity*) override {}

    void OnPetalSpawned(CPetal* owner, ERarity rarity, CFlower* flower) override
    {
        auto* egg = dynamic_cast<CBeetleEggPetal*>(owner);
        if (!egg) return;

        egg->m_summon_id = -1;
        egg->m_has_spawned_summon = false;
        owner->m_health = 1.f;
        (void)rarity;
        (void)flower;
    }

    void OnPetalCleared(CPetal* owner, ERarity, CFlower* flower) override
    {
        auto* egg = dynamic_cast<CBeetleEggPetal*>(owner);
        if (!egg || !flower || !flower->GameWorld()) return;

        CEntity* summon = flower->GameWorld()->GetEntity(egg->m_summon_id);
        if (summon) summon->m_is_marked_for_des = true;
        egg->m_summon_id = -1;
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
            owner->m_is_marked_for_des = true;
            return;
        }

        summon->m_team = flower->m_team;
        summon->SetController(std::make_unique<CSummonedMeleeController>(flower->m_id));
        CMobBase* raw_summon = dynamic_cast<CMobBase*>(flower->GameWorld()->InsertEntity(std::move(summon)));
        if (!raw_summon)
        {
            owner->m_is_marked_for_des = true;
            return;
        }

        ApplySummonedHealthMultiplier(raw_summon, flower);
        egg->m_summon_id = raw_summon->m_id;
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
        if (m_summon_type == EMobType::SummonedSoldierAnt && flower &&
            !flower->FindStates<CPsionicConnectionState>().empty())
            return EMobType::SoldierTermite;
        return m_summon_type;
    }

    static ERarity LowerSummonRarity(ERarity rarity)
    {
        if (rarity == ERarity::Primordial) return ERarity::Eternal;
        if (rarity == ERarity::Unique || rarity == ERarity::Eternal) return ERarity::Super;

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
        return stats;
    }

    SPetalStats GetPetalStats(ERarity) const override
    {
        SPetalStats stats;
        stats.stack = false;
        stats.health = 1.f;
        stats.damage = 0.f;
        stats.reload = 0.f;
        stats.preload = 0.f;
        stats.copy = 1;
        stats.mass = 0.f;
        stats.radius = game_config::default_beetleegg_base_radius;
        return stats;
    }

    void OnTick(CPetal* owner, ERarity, CFlower* flower, float) override
    {
        PetalOrbitMove(owner, flower, PetalOrbitDistance(owner, flower),
                       game_config::default_petal_orbit_k, false);
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

        float orbit_distance = PetalOrbitBaseRadius(owner, flower) + game_config::default_petal_orbit_radius +
                               PetalOrbitReach(flower, false);
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
            delta = {-owner->m_vel.x, -owner->m_vel.y};
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
    auto filter = [missile, flower, launch_direction, max_angle](const CEntity* entity) -> bool
    {
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
        PetalOrbitMoveAndAttract(owner, flower, PetalOrbitDistance(owner, flower),
                                 game_config::default_petal_orbit_k, true, dt);
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
        int level = GetLevel(rarity);
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
        int level = GetLevel(rarity);
        stats.petal_reload_multiplier =
            std::max(game_config::default_petal_stat_reload_multiplier_min,
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

inline float RoseMedicineScale(ERarity rarity)
{
    const float sqrt3 = std::sqrt(3.0f);
    switch (rarity)
    {
    case ERarity::Common:
        return 1.0f;
    case ERarity::Unusual:
        return 3.0f;
    case ERarity::Rare:
        return 9.0f;
    case ERarity::Epic:
        return 27.0f;
    case ERarity::Legendary:
        return 81.0f;
    case ERarity::Mythic:
        return 243.0f;
    case ERarity::Ultra:
        return 243.0f * sqrt3;
    case ERarity::Super:
        return 243.0f * 3.0f;
    case ERarity::Eternal:
    case ERarity::Unique:
        return 243.0f * 3.0f * sqrt3;
    case ERarity::Primordial:
        return 243.0f * 9.0f * sqrt3;
    default:
        return 1.0f;
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
    mob->m_health = std::min(max_health, mob->m_health + amount);
}

inline float YggdrasilChannelTime(ERarity rarity)
{
    switch (rarity)
    {
    case ERarity::Super:
        return game_config::default_yggdrasil_channel_super;
    case ERarity::Eternal:
    case ERarity::Unique:
        return game_config::default_yggdrasil_channel_eternal;
    case ERarity::Primordial:
        return game_config::default_yggdrasil_channel_primordial;
    default:
    {
        int level = std::clamp(GetLevel(rarity), 1, GetLevel(ERarity::Ultra));
        float t = static_cast<float>(level - 1) / static_cast<float>(GetLevel(ERarity::Ultra) - 1);
        return game_config::default_yggdrasil_channel_common +
               (game_config::default_yggdrasil_channel_super - game_config::default_yggdrasil_channel_common) * t;
    }
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
        stats.copy = 1;
        stats.radius = game_config::default_yggdrasil_base_radius;
        return stats;
    }

    static CPlayerFlower* FindCorpse(CYggdrasilPetal* owner, CFlower* flower)
    {
        if (!owner || !flower || !flower->GameWorld()) return nullptr;

        float range = owner->m_radius * 5.f;
        if (range <= 0.f) return nullptr;

        auto filter = [owner, flower](const CEntity* entity) -> bool
        {
            if (!entity || entity->m_is_marked_for_des) return false;
            if (!CheckTeam(entity->m_team, flower->m_team)) return false;
            if (BlocksNullifiedInteraction(owner, entity)) return false;
            const auto* corpse = dynamic_cast<const CPlayerFlower*>(entity);
            return corpse && corpse->m_is_dead;
        };

        CEntity* raw = PetalGetCachedTarget(owner, flower, owner->m_pos, range, filter);
        if (!raw) raw = PetalFindClosestTarget(owner, flower, owner->m_pos, range, filter);
        return dynamic_cast<CPlayerFlower*>(raw);
    }

    void OnTick(CPetal* raw_owner, ERarity rarity, CFlower* flower, float dt) override
    {
        auto* owner = dynamic_cast<CYggdrasilPetal*>(raw_owner);
        if (!owner || !flower) return;

        CPlayerFlower* target = nullptr;
        if (owner->m_revive_target_id >= 0)
        {
            target = dynamic_cast<CPlayerFlower*>(flower->GameWorld()->GetEntity(owner->m_revive_target_id));
            const float range = owner->m_radius * 5.f;
            if (!target || !target->m_is_dead || !CheckTeam(target->m_team, flower->m_team) ||
                !PetalTargetInRange(owner->m_pos, range, target))
            {
                owner->m_revive_target_id = -1;
                owner->m_revive_timer = 0.f;
                PetalClearTarget(owner);
                target = nullptr;
            }
        }

        if (!target)
        {
            target = FindCorpse(owner, flower);
            if (target)
            {
                owner->m_revive_target_id = target->m_id;
                owner->m_revive_timer = 0.f;
            }
        }

        if (!target)
        {
            PetalClearTarget(owner);
            PetalOrbitMove(owner, flower, PetalOrbitDistance(owner, flower), game_config::default_petal_orbit_k, true);
            return;
        }

        owner->m_target_entity_id = target->m_id;
        owner->m_vel = {0.f, 0.f};
        PetalAttractToTarget(owner, target, dt, PetalLockedTargetAcceleration(flower));
        PetalTetherWhileTargeting(owner, flower, PetalOrbitDistance(owner, flower), game_config::default_petal_orbit_k, true);

        float revive_dist = owner->m_radius + target->m_radius;
        if (DistanceSq(owner->m_pos, target->m_pos) > revive_dist * revive_dist) return;
        owner->m_revive_timer += dt;
        if (owner->m_revive_timer < YggdrasilChannelTime(rarity)) return;

        target->ReviveFromYggdrasil(owner->m_final_petal_stats.medicine);
        owner->m_reload_override = YggdrasilChannelTime(rarity);
        owner->m_reload_ignore_multiplier = true;
        owner->m_health = 0.f;
    }

    void OnFlowerTakeDamage(CPetal*, ERarity, CFlower*, float&, EDamageType, CEntity*) override {}
    void OnPetalSpawned(CPetal*, ERarity, CFlower*) override {}
    void OnPetalDestroyed(CPetal* owner, ERarity rarity, CFlower*) override
    {
        if (!owner) return;
        owner->m_reload_override = owner->m_reload_override >= 0.f ? owner->m_reload_override : game_config::default_yggdrasil_preload;
        auto* ygg = dynamic_cast<CYggdrasilPetal*>(owner);
        if (ygg)
        {
            ygg->m_revive_target_id = -1;
            ygg->m_revive_timer = 0.f;
        }
    }
};

inline CEntity* RoseFindTarget(CPetal* owner, CFlower* flower)
{
    if (!owner || !flower || !flower->GameWorld()) return nullptr;
    if (owner->m_lifetime < 0.5f) return nullptr;

    float range = owner->m_radius * 5.f;
    if (range <= 0.f) return nullptr;

    auto is_valid_heal_target = [owner, flower](const CEntity* entity) -> bool
    {
        if (!entity) return false;
        if (entity == owner) return false;
        if (entity->m_is_marked_for_des || entity->IsDead() || !entity->CanCollide()) return false;
        if (!CheckTeam(entity->m_team, flower->m_team)) return false;
        if (BlocksNullifiedInteraction(owner, entity)) return false;
        const auto* mob = dynamic_cast<const CMobBase*>(entity);
        return mob && MobNeedsHealing(mob);
    };

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
            owner->m_target_entity_id = target->m_id;
            owner->m_vel = {0.f, 0.f};
            PetalAttractToTarget(owner, target, dt, PetalLockedTargetAcceleration(flower));
            PetalTetherWhileTargeting(owner, flower, PetalOrbitDistance(owner, flower), game_config::default_petal_orbit_k, true);
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
        if (owner->m_lifetime < 0.5f || owner->m_target_entity_id != target->m_id)
        {
            damage = 0.f;
            return;
        }

        if (CheckTeam(owner->m_team, target->m_team))
        {
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
        stats.damage = 1.7f * PetalRarityScale(rarity);
        stats.health = 1.7f * PetalRarityScale(rarity);
        stats.medicine = 1.2f * RoseMedicineScale(rarity);
        stats.reload = 1.5f;
        stats.preload = 1.5f;
        stats.copy = 3;
        stats.mass = game_config::default_rose_mass;
        stats.radius = game_config::default_rose_base_radius;
        return stats;
    }

    void OnTick(CPetal* owner, ERarity, CFlower* flower, float dt) override
    {
        CEntity* target = RoseFindTarget(owner, flower);
        if (owner && target)
        {
            owner->m_target_entity_id = target->m_id;
            owner->m_vel = {0.f, 0.f};
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
        if (owner->m_lifetime < 0.5f || owner->m_target_entity_id != target->m_id)
        {
            damage = 0.f;
            return;
        }

        if (CheckTeam(owner->m_team, target->m_team))
        {
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
        stats.damage = 20.f * PetalRarityScale(rarity);
        stats.health = 10.f * PetalRarityScale(rarity);
        stats.reload = 3.f;
        stats.preload = 3.f;
        stats.copy = 1;
        stats.mass = game_config::default_basic_mass;
        stats.radius = game_config::default_basic_base_radius;
        return stats;
    }

    void OnTick(CPetal* owner, ERarity, CFlower* flower, float dt) override
    {
        float distance = PetalOrbitDistance(owner, flower);
        if (owner && flower && flower->m_attacking)
        {
            float wave = std::sin(owner->m_lifetime * game_config::pi * 2.f) + 2.f;
            distance = PetalOrbitBaseRadius(owner, flower) + game_config::default_petal_orbit_radius +
                       PetalOrbitReach(flower, true) * wave;
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

    SFlowerStats GetStats(ERarity) const override { return EmptyFlowerStats(); }

    SPetalStats GetPetalStats(ERarity rarity) const override
    {
        SPetalStats stats;
        stats.damage = 5.f * PetalRarityScale(rarity) + TriBonusDamage(rarity);
        stats.health = 10.f * PetalRarityScale(rarity);
        stats.reload = 2.f;
        stats.preload = 2.f;
        stats.copy = 1;
        stats.mass = game_config::default_basic_mass;
        stats.radius = game_config::default_basic_base_radius;
        return stats;
    }

    void OnTick(CPetal* owner, ERarity, CFlower* flower, float dt) override
    {
        PetalOrbitMoveAndAttract(owner, flower, PetalOrbitDistance(owner, flower),
                                 game_config::default_petal_orbit_k, true, dt);
    }

    void OnPetalHit(CPetal* owner, ERarity rarity, CEntity*, float& damage) override
    {
        int triangle_count = 1;
        auto* flower = owner ? dynamic_cast<CFlower*>(owner->GetOwner()) : nullptr;
        if (flower)
        {
            triangle_count = 0;
            for (const auto& slot : flower->GetSlots())
            {
                if (!slot.m_p_proto) continue;
                bool is_triangle = slot.m_p_proto->m_type == EPetalType::Triangle;
                if (!is_triangle && slot.m_p_proto->m_type == EPetalType::Mimic)
                {
                    const CPetalPrototype* mimic_target = MimicTargetPrototype(&slot, flower);
                    is_triangle = mimic_target && mimic_target->m_type == EPetalType::Triangle;
                }
                if (!is_triangle) continue;
                triangle_count += std::max(0, slot.GetCurrentCopyCount(flower));
            }
            triangle_count = std::max(1, triangle_count);
        }
        damage = 5.f * PetalRarityScale(rarity) + TriBonusDamage(rarity) * static_cast<float>(triangle_count);
        if (owner) owner->m_final_petal_stats.damage = damage;
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
        stats.damage = 40.f * PetalRarityScale(rarity);
        stats.health = 25.f * PetalRarityScale(rarity);
        stats.reload = 2.5f;
        stats.preload = 2.5f;
        stats.copy = 1;
        stats.mass = game_config::default_basic_mass;
        stats.radius = game_config::default_basic_base_radius;
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
    case ERarity::Ultra: return 364.5f;
    case ERarity::Super: return 729.f;
    case ERarity::Eternal: return 2187.f;
    case ERarity::Unique: return 328000.f;
    case ERarity::Primordial: return 6561.f;
    default: return PetalRarityScale(rarity);
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
        stats.reload = rarity == ERarity::Unique ? 8.f : 10.f;
        stats.preload = stats.reload;
        stats.copy = rarity == ERarity::Unique ? 1 : (GetLevel(rarity) >= GetLevel(ERarity::Ultra) ? 3 : 1);
        stats.mass = game_config::default_basic_mass;
        stats.radius = game_config::default_basic_base_radius;
        return stats;
    }

    void OnTick(CPetal* owner, ERarity, CFlower* flower, float dt) override
    {
        PetalOrbitMoveAndAttract(owner, flower, PetalOrbitDistance(owner, flower),
                                 game_config::default_petal_orbit_k, true, dt);
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
        stats.damage = 0.f;
        stats.health = 10.f;
        stats.reload = 2.5f;
        stats.preload = 2.5f;
        stats.copy = 0;
        stats.mass = game_config::default_basic_mass;
        stats.radius = game_config::default_basic_base_radius;
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

    void OnFlowerTakeDamage(CPetal* owner, ERarity rarity, CFlower* flower, float& dmg,
                            EDamageType damage_type, CEntity* attacker) override
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
        stats.radius *= 0.5f;
        owner->m_type = proto->m_type;
        owner->m_base_petal_stats = stats;
        owner->m_final_petal_stats = stats;
        if (flower && flower->GetFinalStats()) owner->m_final_petal_stats.ActedOn(*flower->GetFinalStats());
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
        PetalOrbitMoveAndAttract(owner, flower, PetalOrbitDistance(owner, flower),
                                 game_config::default_petal_orbit_k, true, dt);
    }

    void OnFlowerTakeDamage(CPetal*, ERarity, CFlower*, float&, EDamageType, CEntity*) override {}

    void OnPetalHit(CPetal* owner, ERarity rarity, CEntity* target, float& damage) override
    {
        auto* mob = dynamic_cast<CMobBase*>(target);
        if (!owner || !mob) return;

        if (dynamic_cast<CFlower*>(target)) damage *= game_config::default_iris_flower_damage_multiplier;

        float duration = game_config::default_iris_poison_duration;
        float poison_per_second = duration > 0.f ? game_config::default_iris_poison_total_damage / duration : 0.f;
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
        stats.petal_attraction_range = game_config::default_lentil_petal_attraction_range * GetLevel(rarity);
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

        const float range = flower->GetFinalStats()->petal_attraction_range + owner->m_radius;
        if (range <= 0.f) return nullptr;

        auto filter = [owner, flower](const CEntity* entity) -> bool
        {
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
            owner->m_target_entity_id = target->m_id;
            owner->m_vel = {0.f, 0.f};
            PetalAttractToTarget(owner, target, dt, PetalLockedTargetAcceleration(flower));
            PetalTetherWhileTargeting(owner, flower, PetalOrbitDistance(owner, flower), game_config::default_petal_orbit_k, true);
            return;
        }

        PetalClearTarget(owner);
    }

    void OnFlowerTakeDamage(CPetal*, ERarity, CFlower*, float&, EDamageType, CEntity*) override {}
    void OnPetalSpawned(CPetal*, ERarity, CFlower*) override {}
    void OnPetalDestroyed(CPetal*, ERarity, CFlower*) override {}
};
class CMoonBehavior : public CPetalBehavior
{
  public:
    bool IsOpen() const override { return true; }

    SFlowerStats GetStats(ERarity) const override { return EmptyFlowerStats(); }

    SPetalStats GetPetalStats(ERarity rarity) const override
    {
        int level = GetLevel(rarity);
        float visible_radius = game_config::default_flower_radius +
                               static_cast<float>(std::max(0, level - 1)) * game_config::default_moon_radius_step;

        SPetalStats stats;
        stats.damage = game_config::default_moon_base_damage * PetalRarityScale(rarity);
        stats.health = game_config::default_moon_base_health * PetalRarityScale(rarity);
        stats.reload = game_config::default_moon_reload;
        stats.preload = game_config::default_moon_reload;
        stats.copy = 1;
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
            owner->m_vel = {0.f, 0.f};
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
        PetalOrbitMoveAndAttract(owner, flower, PetalOrbitDistance(owner, flower),
                                 game_config::default_petal_orbit_k, true, dt);
    }

    void OnFlowerTakeDamage(CPetal*, ERarity, CFlower*, float&, EDamageType, CEntity*) override {}

    void OnPetalHit(CPetal* owner, ERarity rarity, CEntity* target, float&) override
    {
        auto* mob = dynamic_cast<CMobBase*>(target);
        if (!owner || !mob) return;

        float poison_duration = game_config::default_pincer_poison_duration;
        float poison_total = game_config::default_pincer_poison_total_damage * PetalRarityScale(rarity);
        float poison_per_second = poison_duration > 0.f ? poison_total / poison_duration : 0.f;
        auto poison = std::make_unique<CPoisonState>(mob, poison_duration, poison_per_second, rarity, owner->GetOwner());
        if (poison->IsValid()) mob->AddState(std::move(poison));

        auto slow = std::make_unique<CPincerSpeedReduceState>(mob, game_config::default_pincer_slow_duration, rarity);
        if (slow->IsValid()) mob->AddState(std::move(slow));
    }

    void OnPetalSpawned(CPetal*, ERarity, CFlower*) override {}
    void OnPetalDestroyed(CPetal*, ERarity, CFlower*) override {}
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
        if (!missile || !flower || missile->m_fired)
            return;

        PetalOrbitMoveAndAttract(owner, flower, PetalOrbitDistance(owner, flower),
                                 game_config::default_petal_orbit_k, true, dt);
        if (!flower->m_attacking || owner->m_lifetime < game_config::default_missile_arm_time)
            return;

        sf::Vector2f direction = owner->m_pos - flower->m_pos;
        float len = Length(direction);
        if (len <= game_config::entity_collision_epsilon)
        {
            direction = {std::cos(flower->m_facing_angle), std::sin(flower->m_facing_angle)};
            len = Length(direction);
        }
        if (len <= game_config::entity_collision_epsilon)
            direction = {1.f, 0.f};
        else
            direction /= len;

        if (CEntity* target = FindMissileLaunchTarget(missile, flower, direction))
        {
            sf::Vector2f to_target = target->m_pos - owner->m_pos;
            float target_len = Length(to_target);
            if (target_len > game_config::entity_collision_epsilon) direction = to_target / target_len;
        }

        float flower_speed = flower->GetFinalStats() ? flower->GetFinalStats()->max_velocity : game_config::default_max_velocity;
        owner->m_vel = direction * (flower_speed * game_config::default_missile_speed_multiplier);
        float fired_angle = std::atan2(direction.y, direction.x);
        owner->m_facing_angle = fired_angle;
        owner->m_has_facing = true;
        owner->m_skip_world_tick = false;
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
        if (!owner || !missile)
            return;

        if (!missile->m_fired)
            damage *= game_config::default_missile_unfired_damage_multiplier;
        else
            owner->m_health = 0.f;
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
        if (!missile->m_fired)
            damage *= game_config::default_missile_unfired_damage_multiplier;
    }
};

inline bool CompassCanPoint(ERarity rarity)
{
    return GetLevel(rarity) >= GetLevel(ERarity::Ultra);
}

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
    if (rarity == ERarity::Primordial) return 3;
    if (rarity == ERarity::Unique || rarity == ERarity::Eternal) return 2;
    if (rarity == ERarity::Super) return 1;
    return 0;
}

inline bool IsCompassMagnetPetal(const CEntity* entity, const CCompassPetal* owner, const CFlower* flower)
{
    auto* petal = dynamic_cast<const CPetal*>(entity);
    if (!petal || petal == owner || !flower) return false;
    if (petal->m_type != EPetalType::Compass) return false;
    if (petal->m_is_marked_for_des || petal->IsDead() || !petal->CanCollide()) return false;
    if (CheckTeam(petal->m_team, flower->m_team)) return false;
    return true;
}

inline CEntity* FindCompassMagnet(CCompassPetal* owner, CFlower* flower)
{
    if (!owner || !flower || !flower->GameWorld()) return nullptr;
    auto filter = [owner, flower](const CEntity* entity) -> bool
    {
        return IsCompassMagnetPetal(entity, owner, flower);
    };
    return flower->GameWorld()->FindClosestEntityByEdge(owner->m_pos, game_config::default_compass_magnet_range, filter);
}

inline CEntity* FindCompassMob(CCompassPetal* owner, CFlower* flower)
{
    if (!owner || !flower || !flower->GameWorld()) return nullptr;

    CEntity* best = nullptr;
    int best_priority = 0;
    float best_dist_sq = std::numeric_limits<float>::max();
    for (CEntity* entity : flower->GameWorld()->GetAllEntities())
    {
        if (!IsValidPetalEnemyTarget(owner, flower, entity)) continue;
        auto* mob = dynamic_cast<CMobBase*>(entity);
        if (!mob) continue;

        int priority = CompassMobPriority(mob->GetRarity());
        if (priority <= 0) continue;

        float dist_sq = DistanceSq(owner->m_pos, entity->m_pos);
        if (priority > best_priority || (priority == best_priority && dist_sq < best_dist_sq))
        {
            best = entity;
            best_priority = priority;
            best_dist_sq = dist_sq;
        }
    }
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
                owner->m_compass_wait_timer = 0.f;
                CompassPointAt(owner, magnet);
                return;
            }
        }

        CEntity* target = FindCompassMob(owner, flower);
        if (!target)
        {
            owner->m_compass_target_id = -1;
            owner->m_compass_wait_timer = 0.f;
            return;
        }

        if (owner->m_compass_target_id != target->m_id)
        {
            owner->m_compass_target_id = target->m_id;
            owner->m_compass_wait_timer = 0.f;
        }
        owner->m_compass_wait_timer += dt;

        if (owner->m_compass_wait_timer >= CompassMobWait(rarity))
            CompassPointAt(owner, target);
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
        PetalOrbitMoveAndAttract(owner, flower, PetalOrbitDistance(owner, flower),
                                 game_config::default_petal_orbit_k, true, dt);
        RefreshPsionicZone(owner, rarity, flower);
    }

    void OnFlowerTakeDamage(CPetal*, ERarity, CFlower*, float&, EDamageType, CEntity*) override {}
    void OnPetalSpawned(CPetal* owner, ERarity rarity, CFlower* flower) override { RefreshPsionicZone(owner, rarity, flower); }
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

        CEntity* raw_zone = flower->GameWorld()->GetEntity(relic->m_state_zone_id);
        auto* zone = dynamic_cast<CStateZone*>(raw_zone);
        if (!zone)
        {
            int team = flower->m_team;
            auto new_zone = std::make_unique<CStateZone>(
                flower->GameWorld(), flower->m_pos, radius,
                CStateZone::MakeStateFactory<CPsionicConnectionState>(
                    game_config::default_relic_psionic_zone_refresh, rarity),
                [team](CEntity* entity) -> bool
                {
                    auto* mob = dynamic_cast<CMobBase*>(entity);
                    return mob && CheckTeam(mob->m_team, team);
                });

            zone = dynamic_cast<CStateZone*>(flower->GameWorld()->InsertEntity(std::move(new_zone)));
            if (!zone)
            {
                relic->m_state_zone_id = -1;
                return;
            }
            relic->m_state_zone_id = zone->m_id;
        }

        zone->SetCenter(flower->m_pos);
        zone->m_radius = radius;
        zone->m_timer = game_config::default_relic_psionic_zone_refresh * 2.f;
        zone->m_state = CStateZone::MakeStateFactory<CPsionicConnectionState>(
            game_config::default_relic_psionic_zone_refresh, rarity);
        int team = flower->m_team;
        zone->m_filter = [team](CEntity* entity) -> bool
        {
            auto* mob = dynamic_cast<CMobBase*>(entity);
            return mob && CheckTeam(mob->m_team, team);
        };
    }

    void DestroyPsionicZone(CPetal* owner, CFlower* flower) const
    {
        auto* relic = dynamic_cast<CRelicPetal*>(owner);
        if (!relic || !flower || !flower->GameWorld()) return;

        CEntity* zone = flower->GameWorld()->GetEntity(relic->m_state_zone_id);
        if (zone) zone->m_is_marked_for_des = true;
        relic->m_state_zone_id = -1;
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
