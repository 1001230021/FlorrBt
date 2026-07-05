#pragma once
#include "../../../../Shared/game_config.h"
#include "../../controllers/melee_controller.h"
#include "../../gameworld.h"
#include "petal.h"
#include "petal_slot.h"
#include <algorithm>
#include <cmath>

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
    stats.reach = 0.f;
    stats.petal_attraction_range = 0.f;
    stats.petal_dmg_multiplier = 1.f;
    stats.petal_reload_multiplier = 1.f;
    stats.petal_health_multiplier = 1.f;
    stats.petal_medicine_multiplier = 1.f;
    stats.petal_rotation_speed = 0.f;
    stats.petal_rotation_mode = EPetalRotationMode::Orbit;
    return stats;
}


inline float PetalRarityScale(ERarity rarity)
{
    return std::pow(game_config::default_petal_pow, static_cast<float>(GetLevel(rarity) - 1));
}

inline void PetalOrbitMove(CPetal* owner, CFlower* flower, float orbit_distance, float k, bool is_open)
{
    if (!owner || !flower) return;

    int total_copies = flower->m_total_copies;
    int start_index = flower->GetStartCopyIndex(owner->m_slot_index);
    if (start_index < 0 || total_copies <= 0) return;

    sf::Vector2f global;
    if // yinyang旋轉模式
        (flower->GetFinalStats()->petal_rotation_mode == EPetalRotationMode::YinYang && flower->GetYinYangCount() > 0)
    {
        int columns = flower->GetYinYangColumnCount();
        if (columns <= 0) return;

        int layout_index = start_index;
        if (is_open) layout_index += owner->m_copy_index;

        int column_index = layout_index % columns;
        int layer_index = layout_index / columns;
        float angle = flower->GetPetalRotationAngle() + (2.0f * game_config::pi * column_index) / static_cast<float>(columns);
        float layer_distance = static_cast<float>(layer_index) * game_config::default_petal_orbit_radius;
        global = flower->m_pos + sf::Vector2f(std::cos(angle), std::sin(angle)) * (orbit_distance + layer_distance);
        if (!is_open)
        {
            int copies_in_group = owner->m_base_petal_stats.copy;
            if (copies_in_group <= 0) return;

            float spread_radius = 0.0f;
            if (copies_in_group > 1) spread_radius = owner->m_radius / std::sin(game_config::pi / copies_in_group);
            float sub_angle = (2.0f * game_config::pi * owner->m_copy_index) / static_cast<float>(copies_in_group);
            global += sf::Vector2f(std::cos(sub_angle), std::sin(sub_angle)) * spread_radius;
        }

    } else if (is_open) { // 非yinyang
        float angle = flower->GetPetalRotationAngle() + (2.0f * game_config::pi * (start_index + owner->m_copy_index)) / static_cast<float>(total_copies);
        global = flower->m_pos + sf::Vector2f(std::cos(angle), std::sin(angle)) * orbit_distance;
    } else {
        float group_angle = flower->GetPetalRotationAngle() + (2.0f * game_config::pi * start_index) / static_cast<float>(total_copies);
        sf::Vector2f group_global = flower->m_pos + sf::Vector2f(std::cos(group_angle), std::sin(group_angle)) * orbit_distance;
        int copies_in_group = owner->m_base_petal_stats.copy;
        if (copies_in_group <= 0) return;

        float spread_radius = 0.0f;
        if (copies_in_group > 1) spread_radius = owner->m_radius / std::sin(game_config::pi / copies_in_group);
        float sub_angle = (2.0f * game_config::pi * owner->m_copy_index) / static_cast<float>(copies_in_group);
        global = group_global + sf::Vector2f(std::cos(sub_angle), std::sin(sub_angle)) * spread_radius;
    }

    owner->m_vel = (global - owner->m_pos) * k;
}

inline CMobBase* PetalFindTarget(CPetal* owner, CFlower* flower)
{
    if (!owner || !flower) return nullptr;

    CGameWorld* world = flower->GameWorld();
    if (!world) return nullptr;

    float horizon = flower->GetFinalStats()->petal_attraction_range + owner->m_radius;
    if (horizon <= 0.0f) return nullptr;

    CEntity* raw = world->FindClosestEntity(owner->m_pos, horizon,
                                            [owner](const CEntity* entity) -> bool
                                            {
                                                if (!entity || entity->m_is_marked_for_des) return false;
                                                if (entity == owner || entity == owner->GetOwner()) return false;
                                                return dynamic_cast<const CMobBase*>(entity) != nullptr;
                                            });
    return static_cast<CMobBase*>(raw);
}

void RegisterAir();
void RegisterBasic();
void RegisterBeetleEgg();
void RegisterDust();
void RegisterGoldenLeaf();
void RegisterYinYang();
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
        stats.mass = game_config::default_air_base_mass * level;
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

    SPetalStats GetPetalStats(ERarity) const override
    {
        SPetalStats stats;
        stats.damage = game_config::default_basic_base_damage;
        stats.health = game_config::default_basic_base_health;
        stats.reload = game_config::default_basic_reload;
        stats.preload = game_config::default_basic_reload;
        stats.copy = static_cast<int>(game_config::default_basic_copy);
        stats.mass = game_config::default_basic_mass;
        stats.radius = game_config::default_basic_base_radius;
        return stats;
    }

    void OnTick(CPetal* owner, ERarity, CFlower* flower, float) override
    {
        float orbit_distance = flower->GetFinalStats()->radius + owner->m_radius;
        if (flower->m_attacking) orbit_distance += game_config::default_petal_attack_offset;
        else if (flower->m_defending) orbit_distance += game_config::default_petal_defend_offset;
        PetalOrbitMove(owner, flower, orbit_distance, game_config::default_petal_orbit_k, true);

        CMobBase* target = PetalFindTarget(owner, flower);
        if (!target || target->m_is_marked_for_des) return;

        sf::Vector2f diff = target->m_pos - owner->m_pos;
        float dist_sq = LengthSq(diff);
        float radius_sum = owner->m_radius + target->m_radius;
        if (dist_sq <= radius_sum * radius_sum)
        {
            target->TakeDamage(owner->m_final_petal_stats.damage, flower, EDamageType::Normal);
            owner->OnCollision(target);
        }
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

class CBeetleEggBehavior : public CPetalBehavior
{
  public:
    bool IsOpen() const override { return true; }

    SFlowerStats GetStats(ERarity) const override { return EmptyFlowerStats(); }

    SPetalStats GetPetalStats(ERarity rarity) const override
    {
        SPetalStats stats;
        stats.health = game_config::default_beetleegg_base_health;
        stats.reload = BeetleEggReload(rarity);
        stats.preload = BeetleEggReload(rarity);
        stats.copy = static_cast<int>(game_config::default_beetleegg_copy);
        stats.mass = game_config::default_beetleegg_mass;
        stats.radius = game_config::default_beetleegg_base_radius;
        return stats;
    }

    void OnTick(CPetal* owner, ERarity, CFlower* flower, float) override
    {
        auto* egg = dynamic_cast<CBeetleEggPetal*>(owner);
        if (!egg || !flower || !flower->GameWorld()) return;

        if (!egg->m_has_spawned_summon)
        {
            PetalOrbitMove(owner, flower, flower->GetFinalStats()->radius + owner->m_radius,
                           game_config::default_petal_orbit_k, true);
            owner->m_health = 1.f;
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
        auto summon = CreateMob(EMobType::SummonedBeetle, flower->GameWorld(), spawn_pos, summon_rarity);
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
    static ERarity LowerSummonRarity(ERarity rarity)
    {
        int value = static_cast<int>(rarity);
        int common = static_cast<int>(ERarity::Common);
        return static_cast<ERarity>(std::max(common, value - 1));
    }
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

    void OnTick(CPetal* owner, ERarity, CFlower* flower, float) override
    {
        float orbit_distance = flower->GetFinalStats()->radius + owner->m_radius;
        if (flower->m_attacking)
        {
            orbit_distance += game_config::default_petal_attack_offset + flower->GetFinalStats()->reach;
            m_is_open = true;
        } else {
            if (flower->m_defending) orbit_distance += game_config::default_petal_defend_offset;
            m_is_open = false;
        }
        PetalOrbitMove(owner, flower, orbit_distance, game_config::default_petal_orbit_k, m_is_open);

        CMobBase* target = PetalFindTarget(owner, flower);
        if (!target || target->m_is_marked_for_des) return;

        sf::Vector2f diff = target->m_pos - owner->m_pos;
        float dist_sq = LengthSq(diff);
        float radius_sum = owner->m_radius + target->m_radius;
        if (dist_sq <= radius_sum * radius_sum)
        {
            target->TakeDamage(owner->m_final_petal_stats.damage, flower, EDamageType::Normal);
            owner->OnCollision(target);
        }
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

    void OnTick(CPetal* owner, ERarity, CFlower* flower, float) override
    {
        float orbit_distance = flower->GetFinalStats()->radius + owner->m_radius;
        if (flower->m_attacking) orbit_distance += game_config::default_petal_attack_offset;
        else if (flower->m_defending) orbit_distance += game_config::default_petal_defend_offset;
        PetalOrbitMove(owner, flower, orbit_distance, game_config::default_petal_orbit_k, true);

        CMobBase* target = PetalFindTarget(owner, flower);
        if (!target || target->m_is_marked_for_des) return;

        sf::Vector2f diff = target->m_pos - owner->m_pos;
        float dist_sq = LengthSq(diff);
        float radius_sum = owner->m_radius + target->m_radius;
        if (dist_sq <= radius_sum * radius_sum)
        {
            target->TakeDamage(owner->m_final_petal_stats.damage, flower, EDamageType::Normal);
            owner->OnCollision(target);
        }
    }

    void OnFlowerTakeDamage(CPetal*, ERarity, CFlower*, float&, EDamageType, CEntity*) override {}
    void OnPetalSpawned(CPetal*, ERarity, CFlower*) override {}
    void OnPetalDestroyed(CPetal*, ERarity, CFlower*) override {}
};
class CYinYangBehavior : public CPetalBehavior
{
  public:
    bool IsOpen() const override { return true; }
    EPetalBonusMode GetBonusMode() const override { return EPetalBonusMode::ReloadKeepsBonus; }

    SFlowerStats GetStats(ERarity) const override
    {
        SFlowerStats stats = EmptyFlowerStats();
        stats.petal_rotation_mode = EPetalRotationMode::YinYang;
        return stats;
    }

    SPetalStats GetPetalStats(ERarity) const override
    {
        SPetalStats stats;
        stats.damage = game_config::default_yinyang_base_damage;
        stats.health = game_config::default_yinyang_base_health;
        stats.reload = game_config::default_yinyang_reload;
        stats.preload = game_config::default_yinyang_reload;
        stats.copy = static_cast<int>(game_config::default_yinyang_copy);
        stats.mass = game_config::default_yinyang_mass;
        stats.radius = game_config::default_yinyang_base_radius;
        return stats;
    }

    void OnTick(CPetal* owner, ERarity, CFlower* flower, float) override
    {
        float orbit_distance = flower->GetFinalStats()->radius + owner->m_radius;
        if (flower->m_attacking) orbit_distance += game_config::default_petal_attack_offset;
        else if (flower->m_defending) orbit_distance += game_config::default_petal_defend_offset;
        PetalOrbitMove(owner, flower, orbit_distance, game_config::default_petal_orbit_k, true);

        CMobBase* target = PetalFindTarget(owner, flower);
        if (!target || target->m_is_marked_for_des) return;

        sf::Vector2f diff = target->m_pos - owner->m_pos;
        float dist_sq = LengthSq(diff);
        float radius_sum = owner->m_radius + target->m_radius;
        if (dist_sq <= radius_sum * radius_sum)
        {
            target->TakeDamage(owner->m_final_petal_stats.damage, flower, EDamageType::Normal);
            owner->OnCollision(target);
        }
    }

    void OnFlowerTakeDamage(CPetal*, ERarity, CFlower*, float&, EDamageType, CEntity*) override {}
    void OnPetalSpawned(CPetal*, ERarity, CFlower*) override {}
    void OnPetalDestroyed(CPetal*, ERarity, CFlower*) override {}
};
