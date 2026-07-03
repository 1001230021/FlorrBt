#pragma once
#include "rarity.h"
#include <SFML/System/Vector2.hpp>
#include <algorithm>
#include <cmath>
#include <random>

inline std::mt19937& GetRng()
{
    static std::mt19937 rng(std::random_device{}());
    return rng;
}

inline float GetLimitedRng(float start, float end)
{
    static std::mt19937 rng(std::random_device{}());

    std::uniform_real_distribution<float> dist(start, end);
    return dist(rng);
}

inline bool CheckChance(float chance)
{
    chance = std::clamp(chance, 0.0f, 1.0f);
    std::uniform_real_distribution<float> dist(0.0f, 1.0f);
    return dist(GetRng()) < chance;
}

inline bool CheckTeam(int t1, int t2)
{
    if (t1 == 0 || t2 == 0)
        return false;
    return t1 == t2;
}

inline int GetLevel(ERarity rarity)
{
    int level = static_cast<int>(rarity);
    if (level > static_cast<int>(ERarity::Unique))
        level--;
    if (level < 1)
        level = 1;
    return level;
}

inline float Distance(sf::Vector2f v1, sf::Vector2f v2)
{
    float dx = v1.x - v2.x;
    float dy = v1.y - v2.y;
    return sqrtf(dx*dx + dy*dy);
}

inline float DistanceSq(sf::Vector2f v1, sf::Vector2f v2)
{
    float dx = v1.x - v2.x;
    float dy = v1.y - v2.y;
    return dx * dx + dy * dy;
}

inline float Length(sf::Vector2f v)
{
    return sqrtf(v.x * v.x + v.y * v.y);
}

inline float LengthSq(sf::Vector2f v)
{
    return v.x * v.x + v.y * v.y;
}