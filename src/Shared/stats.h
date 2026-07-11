#pragma once

enum class EPetalRotationMode
{
    Orbit,
    YinYang
};

struct SMobStats
{
    float max_health = 1.f;
    float armor = 0.f;
    float damage = 0.f;
    float radius = 10.0f;
    float mass = 0.f;
    float horizon = 1024.f;
    float max_absorb_range = 0.f;
    float detection_multiplier = 1.f;
    float max_velocity = 50.f;
    float acceleration = 100.f;
    float turn_speed = 0.f;

    void ActedOn(const SMobStats& other)
    {
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
    float reach = 0.0f;
    float petal_attraction_range = 5.f;

    float petal_dmg_multiplier = 1.f;
    float petal_reload_multiplier = 1.f;
    float petal_health_multiplier = 1.f;
    float petal_medicine_multiplier = 1.f;
    float petal_rotation_speed = 3.f;
    bool petal_rotation_quantized = false;
    EPetalRotationMode petal_rotation_mode = EPetalRotationMode::Orbit;

    void ActedOn(const SFlowerStats& other)
    {
        SMobStats::ActedOn(other);

        max_health *= other.max_health_multiplier;
        max_health_multiplier *= other.max_health_multiplier;
        reach += other.reach;
        petal_attraction_range += other.petal_attraction_range;

        petal_dmg_multiplier *= other.petal_dmg_multiplier;
        petal_reload_multiplier *= other.petal_reload_multiplier;
        petal_health_multiplier *= other.petal_health_multiplier;
        petal_medicine_multiplier *= other.petal_medicine_multiplier;
        petal_rotation_speed += other.petal_rotation_speed;
        petal_rotation_quantized = petal_rotation_quantized || other.petal_rotation_quantized;
        if (other.petal_rotation_mode != EPetalRotationMode::Orbit) petal_rotation_mode = other.petal_rotation_mode;
    }
};

struct SPetalStats
{
    float health = 10.f;
    float damage = 10.f;
    float armor = 0.f;
    float medicine = 0.f;
    float reload = 2.5f;
    float preload = 2.5f;
    float mass = 0.f;
    float radius = 5.f;
    float angle = 0.f;
    int copy = 1;
    bool stack = true;

    void ActedOn(const SFlowerStats& other)
    {
        health *= other.petal_health_multiplier;
        damage *= other.petal_dmg_multiplier;
        medicine *= other.petal_medicine_multiplier;
        reload *= other.petal_reload_multiplier;
        preload *= other.petal_reload_multiplier;
    }
};
