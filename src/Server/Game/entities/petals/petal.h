#pragma once
#include <string>
#include <memory>
#include <functional>
#include <unordered_map>
#include "../flower.h"
#include "../projectile.h"
#include "../../../../Shared/shared.h"

class CPetal;

class CPetalBehavior {
public:
    virtual ~CPetalBehavior() = default;

    virtual bool IsOpen() const { return false; }

    virtual SFlowerStats GetStats(ERarity rarity) const = 0;
    virtual SPetalStats GetPetalStats(ERarity rarity) const = 0;

    virtual void OnTick(CPetal* owner, ERarity rarity, CFlower* flower, float dt) = 0;
    virtual void OnFlowerTakeDamage(CPetal* owner, ERarity rarity, CFlower* flower, float& dmg, EDamageType damage_type, CEntity* attacker) = 0;
    virtual void OnPetalSpawned(CPetal* owner, ERarity rarity, CFlower* flower) = 0;
    virtual void OnPetalDestroyed(CPetal* owner, ERarity rarity, CFlower* flower) = 0;
};

class CPetalPrototype {
public:
    CPetalPrototype() = default;
    CPetalPrototype(const CPetalPrototype&) = delete;
    CPetalPrototype& operator=(const CPetalPrototype&) = delete;
    CPetalPrototype(CPetalPrototype&&) = default;
    CPetalPrototype& operator=(CPetalPrototype&&) = default;

    EPetalType m_Type;
    std::string m_Name;
    float m_BaseRadius;
    std::unique_ptr<CPetalBehavior> m_pBehavior;
    std::function<CPetal* (CFlower*, int, ERarity)> m_Factory;
};

class CPetal : public CProjectile {
public:
    CPetal(float r, CFlower* owner, int slot, SPetalStats petalStats);
    virtual void Tick(float dt) override;

    void TakeDamage(float dmg, CEntity* attacker, EDamageType damage_type) override;

    SPetalStats m_BasePetalStats;
    SPetalStats m_FinalPetalStats;
    int m_MaxSlotNum;
    int m_CopyIndex = 0;
    int m_SlotIndex = 0;
};

inline std::unordered_map<EPetalType, std::unique_ptr<CPetalPrototype>> g_PetalRegistry;

#define REGISTER_PETAL(type, petalClass, proto) \
    static bool _reg_##petalClass = [&]() -> bool { \
        auto ptr = std::make_unique<CPetalPrototype>(std::move(proto)); \
        CPetalPrototype* rawPtr = ptr.get(); \
        rawPtr->m_Factory = [rawPtr](CFlower* f, int slot, ERarity rarity) -> CPetal* { \
            SPetalStats petalStats = rawPtr->m_pBehavior->GetPetalStats(rarity); \
            auto* p = new petalClass(petalStats.radius, f, slot, petalStats); \
            p->m_SlotIndex = slot; \
            return p; \
        }; \
        g_PetalRegistry[type] = std::move(ptr); \
        return true; \
    }()
