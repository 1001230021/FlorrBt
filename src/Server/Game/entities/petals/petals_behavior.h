#pragma once
#include "../../../../Shared/game_config.h"
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
    stats.search_range = 0.f;
    stats.detection_multiplier = 1.f;
    stats.max_velocity = 0.f;
    stats.acceleration = 0.f;
    stats.reach = 0.f;
    stats.petal_attraction_range = 0.f;
    stats.petal_dmg_multiplier = 1.f;
    stats.petal_reload_multiplier = 1.f;
    stats.petal_health_multiplier = 1.f;
    stats.petal_medicine_multiplier = 1.f;
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
    if (is_open)
    {
        float angle = (2.0f * game_config::pi * (start_index + owner->m_copy_index)) / static_cast<float>(total_copies);
        global = flower->m_pos + sf::Vector2f(std::cos(angle), std::sin(angle)) * orbit_distance;
    } else {
        float group_angle = (2.0f * game_config::pi * start_index) / static_cast<float>(total_copies);
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

    float search_range = flower->GetFinalStats()->petal_attraction_range + owner->m_radius;
    if (search_range <= 0.0f) return nullptr;

    CEntity* raw = world->FindClosestEntity(owner->m_pos, search_range,
                                            [owner](const CEntity* entity) -> bool
                                            {
                                                if (!entity || entity->m_is_marked_for_des) return false;
                                                if (entity == owner || entity == owner->m_p_owner) return false;
                                                return dynamic_cast<const CMobBase*>(entity) != nullptr;
                                            });
    return static_cast<CMobBase*>(raw);
}

void RegisterAir();
void RegisterDust();
void RegisterGoldenLeaf();
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

class CDustBehavior : public CPetalBehavior
{
  public:
    bool IsOpen() const override { return m_is_open; }

    SFlowerStats GetStats(ERarity rarity) const override
    {
        SFlowerStats stats = EmptyFlowerStats();
        int level = GetLevel(rarity);
        stats.petal_reload_multiplier = std::max(0.05f, 1.0f - level * 0.03f);
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
        stats.petal_reload_multiplier = std::max(0.05f, 1.0f - level * game_config::default_goldenleaf_base_reload_reduction);
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