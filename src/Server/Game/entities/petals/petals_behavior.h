#pragma once
#include "petal.h"
#include "petal_slot.h"
#include "../../gameworld.h"
#include <cmath>

inline void PetalOrbitMove(CPetal* owner, CFlower* flower, float orbitDistance, float k, bool isOpen)
{
    int totalCopies = flower->m_TotalCopies;
    int startIndex = flower->GetStartCopyIndex(owner->m_SlotIndex);
    if (startIndex < 0 || totalCopies <= 0) return;

    sf::Vector2f global;
    if (isOpen)
    {
        float angle = (2.0f * 3.14159f * (startIndex + owner->m_CopyIndex)) / (float)totalCopies;
        global = flower->m_Pos + sf::Vector2f(cos(angle), sin(angle)) * orbitDistance;
    }
    else
    {
        float groupAngle = (2.0f * 3.14159f * startIndex) / (float)totalCopies;
        sf::Vector2f groupGlobal = flower->m_Pos + sf::Vector2f(cos(groupAngle), sin(groupAngle)) * orbitDistance;
        int copiesInGroup = owner->m_BasePetalStats.copy;
        float spreadRadius = 0.0f;
        if (copiesInGroup > 1) spreadRadius = owner->m_Radius / sin(3.14159f / copiesInGroup);
        float subAngle = (2.0f * 3.14159f * owner->m_CopyIndex) / (float)copiesInGroup;
        global = groupGlobal + sf::Vector2f(cos(subAngle), sin(subAngle)) * spreadRadius;
    }
    owner->m_Vel = (global - owner->m_Pos) * k;
}

inline CMobBase* PetalFindTarget(CPetal* owner, CFlower* flower)
{
    CGameWorld* world = flower->GameWorld();
    if (!world) return nullptr;
    float searchRange = flower->GetStats().petal_attraction_range + owner->m_Radius;
    if (searchRange <= 0.0f) return nullptr;

    CEntity* raw = world->FindClosestEntity(owner->m_Pos, searchRange,
        [owner](const CEntity* e) -> bool
        {
            if (!e || e->m_IsMarkedForDes) return false;
            if (e == owner || e == owner->m_pOwner) return false;
            if (dynamic_cast<const CMobBase*>(e) == nullptr) return false;
            return true;
        });
    return static_cast<CMobBase*>(raw);
}

// ----------------------------------------------------------------------

// ================ Air ================
void RegisterAir();
class CAirBehavior : public CPetalBehavior
{
public:
    bool IsOpen() const override { return true; }

    SFlowerStats GetStats(ERarity rarity) const override
    {
        SFlowerStats stats;
        int level = GetLevel(rarity);
        stats.radius = 8.f * level;
        stats.mass = 16.f * level;
        return stats;
    }

    SPetalStats GetPetalStats(ERarity rarity) const override
    {
        SPetalStats ps;
        ps.stack = false;
        ps.reload = 0;
        ps.preload = 0;
        ps.copy = 0;
        return ps;
    }

    void OnTick(CPetal* owner, ERarity rarity, CFlower* flower, float dt) override {}
    void OnFlowerTakeDamage(CPetal* owner, ERarity rarity, CFlower* flower, float& dmg, EDamageType damage_type, CEntity* attacker) override {}
    void OnPetalSpawned(CPetal* owner, ERarity rarity, CFlower* flower) {}
    void OnPetalDestroyed(CPetal* owner, ERarity rarity, CFlower* flower) {}
};

// ================ Dust ================
void RegisterDust();
class CDustBehavior : public CPetalBehavior
{
public:
    bool m_IsOpen = false;

    bool IsOpen() const override { return m_IsOpen; }

    SFlowerStats GetStats(ERarity rarity) const override
    {
        SFlowerStats stats;
        int level = static_cast<int>(rarity);
        if (level < 1) level = 1;
        stats.petal_reload_multiplier = 1.0f - level * 0.05f;
        if (stats.petal_reload_multiplier < 0.05f) stats.petal_reload_multiplier = 0.05f;
        return stats;
    }

    SPetalStats GetPetalStats(ERarity rarity) const override
    {
        SPetalStats ps;
        int level = static_cast<int>(rarity);
        if (level < 1) level = 1;
        ps.damage = 4.3f * pow(3, level - 1);
        ps.health = 1.7f * pow(3, level - 1);
        ps.copy = 3;
        ps.mass = 1;
        return ps;
    }

    void OnTick(CPetal* owner, ERarity rarity, CFlower* flower, float dt) override
    {
        float orbitDistance = flower->GetStats().radius + owner->m_Radius;
        if (flower->m_Attacking)
        {
            orbitDistance += 30.0f;
            m_IsOpen = true;
        } else {
            if (flower->m_Defending) orbitDistance -= 10.0f;
            m_IsOpen = false;
        }
        PetalOrbitMove(owner, flower, orbitDistance, 5.0f, m_IsOpen);

        CMobBase* target = PetalFindTarget(owner, flower);
        if (!target || target->m_IsMarkedForDes) return;

        sf::Vector2f diff = target->m_Pos - owner->m_Pos;
        float dist = sqrt(diff.x * diff.x + diff.y * diff.y);
        if (dist < owner->m_Radius + target->m_Radius)
        {
            target->TakeDamage(static_cast<int>(owner->m_FinalPetalStats.damage), flower, EDamageType::Normal);
            owner->OnCollision(target);
        }
    }

    void OnFlowerTakeDamage(CPetal* owner, ERarity rarity, CFlower* flower, float& dmg, EDamageType damage_type, CEntity* attacker) override {}
    void OnPetalSpawned(CPetal* owner, ERarity rarity, CFlower* flower) {}
    void OnPetalDestroyed(CPetal* owner, ERarity rarity, CFlower* flower) {}
};

// ================ Golden Leaf ================
void RegisterGoldenLeaf();
class CGoldenLeafBehavior : public CPetalBehavior
{
public:
    bool IsOpen() const override { return true; }

    SFlowerStats GetStats(ERarity rarity) const override
    {
        SFlowerStats stats;
        int level = static_cast<int>(rarity);
        if (level < 1) level = 1;
        stats.petal_reload_multiplier = 1.0f - level * 0.05f;
        if (stats.petal_reload_multiplier < 0.05f) stats.petal_reload_multiplier = 0.05f;
        return stats;
    }

    SPetalStats GetPetalStats(ERarity rarity) const override
    {
        SPetalStats ps;
        int level = static_cast<int>(rarity);
        if (level < 1) level = 1;
        ps.damage = 16 * pow(3, level - 1);
        ps.health = 12 * pow(3, level - 1);
        ps.copy = 1;
        ps.mass = 3;
        return ps;
    }

    void OnTick(CPetal* owner, ERarity rarity, CFlower* flower, float dt) override
    {
        float orbitDistance = flower->GetStats().radius + owner->m_Radius;
        if (flower->m_Attacking) orbitDistance += 30.0f;
        else if (flower->m_Defending) orbitDistance -= 10.0f;
        PetalOrbitMove(owner, flower, orbitDistance, 5.0f, true);

        CMobBase* target = PetalFindTarget(owner, flower);
        if (!target || target->m_IsMarkedForDes) return;

        sf::Vector2f diff = target->m_Pos - owner->m_Pos;
        float dist = sqrt(diff.x * diff.x + diff.y * diff.y);
        if (dist < owner->m_Radius + target->m_Radius)
        {
            target->TakeDamage(static_cast<int>(owner->m_FinalPetalStats.damage), flower, EDamageType::Normal);
            owner->OnCollision(target);
        }
    }

    void OnFlowerTakeDamage(CPetal* owner, ERarity rarity, CFlower* flower, float& dmg, EDamageType damage_type, CEntity* attacker) override {}
    void OnPetalSpawned(CPetal* owner, ERarity rarity, CFlower* flower) {}
    void OnPetalDestroyed(CPetal* owner, ERarity rarity, CFlower* flower) {}
};