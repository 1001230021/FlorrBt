#pragma once

struct SMobStats
{
    float max_health = 1;
    float armor = 0;
    float damage = 0;
    float radius = 10.0f;
    float mass = 0.f;
    float search_range = 1024.f;
    float detection_multiplier = 1.f;
    float max_velocity = 50.f;
    float acceleration = 100.f;

    void ActedOn(const SMobStats& other)
    {
        max_health += other.max_health;
        armor += other.armor;
        damage += other.damage;
        radius += other.radius;
        mass += other.mass;
        detection_multiplier *= other.detection_multiplier;
    }
};

struct SFlowerStats : public SMobStats
{
    float reach = 0.0f;
    float petal_attraction_range = 5.f;

    float petal_dmg_multiplier = 1.f;
    float petal_reload_multiplier = 1.f;
    float petal_health_multiplier = 1.f;
    float petal_medicine_multiplier = 1.f;

    void ActedOn(const SFlowerStats& other)
    {
        SMobStats::ActedOn(other);

        reach += other.reach;
        petal_attraction_range += other.petal_attraction_range;

        petal_dmg_multiplier *= other.petal_dmg_multiplier;
        petal_reload_multiplier *= other.petal_reload_multiplier;
        petal_health_multiplier *= other.petal_health_multiplier;
        petal_medicine_multiplier *= other.petal_medicine_multiplier;
    }
};

struct SPetalStats
{ // 原則：不全初始化，但要用的一定要初始化！
    float health = 10;
    float damage = 10;
    float armor = 0;
    float medicine = 0;
    float reload = 2.5f;
    float preload = 2.5f;
    float mass = 0.f;
    float radius = 5.f;
    float angle = 0.f;
    int copy = 1;
    bool stack = true;

    void ActedOn(const SFlowerStats& other)
    {
        health = health * other.petal_health_multiplier;
        damage = damage * other.petal_dmg_multiplier;
        medicine = medicine * other.petal_medicine_multiplier;
    }
};
