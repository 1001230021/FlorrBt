#pragma once
#include "game_config.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

enum class EPetalRotationMode
{
    Orbit,
    YinYang
};

struct SEntityStats
{
    int extra_hit_num = game_config::entity_default_extra_hit_num;
};

struct SMobStats : public SEntityStats
{
    float max_health = 1.f;
    float armor = 0.f;
    float damage = 0.f;
    float radius = game_config::stats_default_mob_radius;
    float mass = 0.f;
    float horizon = game_config::stats_default_mob_horizon;
    float max_absorb_range = 0.f;
    float detection_multiplier = 1.f;
    float max_velocity = game_config::stats_default_mob_max_velocity;
    float acceleration = game_config::stats_default_mob_acceleration;
    float turn_speed = 0.f;

    void ActedOn(const SMobStats& other)
    {
        extra_hit_num += other.extra_hit_num;
        max_health += other.max_health;
        armor += other.armor;
        damage += other.damage;
        radius += other.radius;
        mass += other.mass;
        horizon += other.horizon;
        max_absorb_range += other.max_absorb_range;
        detection_multiplier *= other.detection_multiplier;
        max_velocity += other.max_velocity;
        acceleration += other.acceleration;
        turn_speed += other.turn_speed;
    }
};

struct SFlowerStats : public SMobStats
{
    float max_health_multiplier = 1.f;
    float health_regen = 0.f;
    float defense_health_regen = 0.f;
    float tridmgbonus = 0.f;
    float reach = 0.0f;
    float petal_attraction_range = game_config::stats_default_flower_petal_attraction_range;

    float petal_dmg_multiplier = 1.f;
    float petal_reload_multiplier = 1.f;
    float petal_health_multiplier = 1.f;
    float petal_medicine_multiplier = 1.f;
    float healing_received_multiplier = 1.f;
    float overheal_to_shield = 0.f;
    float mult_summoned_health = 1.f;
    float mult_summoned_damage = 1.f;
    float poison_damage_multiplier = 1.f;
    float poison_duration_multiplier = 1.f;
    float body_poison_damage_multiplier = 0.f;
    float body_poison_duration = 0.f;
    int petal_extra_hit_num = 0;
    float petal_hit_compression_power = 0.f;
    float petal_swap_min_reload = game_config::default_petal_swap_min_reload;
    float petal_rotation_speed = game_config::stats_default_flower_petal_rotation_speed;
    bool petal_rotation_quantized = false;
    EPetalRotationMode petal_rotation_mode = EPetalRotationMode::Orbit;

    void ActedOn(const SFlowerStats& other)
    {
        SMobStats::ActedOn(other);

        max_health *= other.max_health_multiplier;
        max_health_multiplier *= other.max_health_multiplier;
        health_regen += other.health_regen;
        defense_health_regen += other.defense_health_regen;
        tridmgbonus += other.tridmgbonus;
        reach += other.reach;
        petal_attraction_range += other.petal_attraction_range;

        petal_dmg_multiplier *= other.petal_dmg_multiplier;
        petal_reload_multiplier *= other.petal_reload_multiplier;
        petal_health_multiplier *= other.petal_health_multiplier;
        petal_medicine_multiplier *= other.petal_medicine_multiplier;
        healing_received_multiplier *= other.healing_received_multiplier;
        overheal_to_shield += other.overheal_to_shield;
        mult_summoned_health *= other.mult_summoned_health;
        mult_summoned_damage *= other.mult_summoned_damage;
        poison_damage_multiplier *= other.poison_damage_multiplier;
        poison_duration_multiplier *= other.poison_duration_multiplier;
        body_poison_damage_multiplier = std::max(body_poison_damage_multiplier, other.body_poison_damage_multiplier);
        body_poison_duration = std::max(body_poison_duration, other.body_poison_duration);
        petal_extra_hit_num += other.petal_extra_hit_num;
        petal_hit_compression_power += other.petal_hit_compression_power;
        petal_swap_min_reload = std::min(petal_swap_min_reload, other.petal_swap_min_reload);
        petal_rotation_speed += other.petal_rotation_speed;
        petal_rotation_quantized = petal_rotation_quantized || other.petal_rotation_quantized;
        if (other.petal_rotation_mode != EPetalRotationMode::Orbit) petal_rotation_mode = other.petal_rotation_mode;
    }
};

struct SPetalStats : public SEntityStats
{
    SPetalStats() { extra_hit_num = game_config::petal_default_extra_hit_num; }

    float health = game_config::stats_default_petal_health;
    float damage = game_config::stats_default_petal_damage;
    float armor = 0.f;
    float medicine = 0.f;
    float reload = game_config::stats_default_petal_reload;
    float preload = game_config::stats_default_petal_preload;
    float mass = 0.f;
    float radius = game_config::stats_default_petal_radius;
    float angle = 0.f;
    int copy = game_config::stats_default_petal_copy;
    bool stack = true;

    void ActedOn(const SFlowerStats& other, float damage_bonus = 0.f)
    {
        // Each petal owns its baseline; the flower supplies only its explicit petal bonus.
        const std::int64_t original_hit_count =
            std::max<std::int64_t>(1, static_cast<std::int64_t>(extra_hit_num) + other.petal_extra_hit_num + 1);
        const double compression_power = std::max(0.0, static_cast<double>(other.petal_hit_compression_power));
        double compression_ratio = 0.0;
        if (compression_power > 0.0)
        {
            const double half_power =
                std::max(0.0, static_cast<double>(game_config::default_black_fungus_compression_half_power));
            compression_ratio = compression_power / (compression_power + half_power);
        }

        const double compressed_hit_count_value =
            std::clamp(std::round(static_cast<double>(original_hit_count) * (1.0 - compression_ratio)), 1.0,
                       static_cast<double>(std::numeric_limits<int>::max()));
        const std::int64_t compressed_hit_count = static_cast<std::int64_t>(compressed_hit_count_value);
        const double compression_damage_bonus =
            1.0 + std::max(0.0, static_cast<double>(game_config::default_black_fungus_compression_damage_bonus_max)) *
                      compression_ratio;
        const double compressed_damage_multiplier =
            static_cast<double>(original_hit_count) / static_cast<double>(compressed_hit_count) *
            compression_damage_bonus;

        extra_hit_num = static_cast<int>(compressed_hit_count) - 1;
        health *= other.petal_health_multiplier;
        damage = static_cast<float>(static_cast<double>(damage + damage_bonus) * other.petal_dmg_multiplier *
                                    compressed_damage_multiplier);
        medicine *= other.petal_medicine_multiplier;
        reload *= other.petal_reload_multiplier;
        preload *= other.petal_reload_multiplier;
    }
};
