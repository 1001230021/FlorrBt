#pragma once
#include "../../../../Shared/shared.h"
#include "../flower.h"
#include "../projectile.h"
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>

class CPetal;

enum class EPetalBonusMode
{
    AliveOnly,
    ReloadKeepsBonus,
    PreloadKeepsBonus
};

inline bool KeepsBonusDuringPreload(EPetalBonusMode mode) { return mode == EPetalBonusMode::PreloadKeepsBonus; }
inline bool KeepsBonusDuringReload(EPetalBonusMode mode)
{
    return mode == EPetalBonusMode::ReloadKeepsBonus || mode == EPetalBonusMode::PreloadKeepsBonus;
}
inline bool LosesBonusDuringReload(EPetalBonusMode mode) { return mode == EPetalBonusMode::AliveOnly; }

class CPetalBehavior
{
  public:
    virtual ~CPetalBehavior() = default;

    virtual bool IsOpen() const { return false; }
    virtual EPetalBonusMode GetBonusMode() const { return EPetalBonusMode::AliveOnly; }

    virtual SFlowerStats GetStats(ERarity rarity) const = 0;
    virtual SPetalStats GetPetalStats(ERarity rarity) const = 0;

    virtual void OnTick(CPetal* owner, ERarity rarity, CFlower* flower, float dt) = 0;
    virtual void OnFlowerTakeDamage(CPetal* owner, ERarity rarity, CFlower* flower, float& dmg,
                                    EDamageType damage_type, CEntity* attacker) = 0;
    virtual void OnPetalHit(CPetal*, ERarity, CEntity*, float&) {}
    virtual void OnPetalSpawned(CPetal* owner, ERarity rarity, CFlower* flower) = 0;
    virtual void OnPetalCleared(CPetal*, ERarity, CFlower*) {}
    virtual void OnPetalDestroyed(CPetal* owner, ERarity rarity, CFlower* flower) = 0;
    virtual bool ShouldReloadAfterPetalDestroyed(CPetal*) const { return true; }
};

class CPetalPrototype
{
  public:
    using petal_factory = std::function<std::unique_ptr<CPetal>(CFlower*, int, ERarity)>;

    CPetalPrototype() = default;
    CPetalPrototype(const CPetalPrototype&) = delete;
    CPetalPrototype& operator=(const CPetalPrototype&) = delete;
    CPetalPrototype(CPetalPrototype&&) = default;
    CPetalPrototype& operator=(CPetalPrototype&&) = default;

    EPetalType m_type = EPetalType::None;
    std::string m_name;
    float m_base_radius = 0.f;
    std::unique_ptr<CPetalBehavior> m_p_behavior;
    petal_factory m_factory;
};

class CPetal : public CProjectile
{
  public:
    CPetal(float r, CFlower* owner, int slot, SPetalStats petal_stats);
    void Tick(float dt) override;

    void TakeDamage(float dmg, CEntity* attacker, EDamageType damage_type) override;
    bool CanCollide() const override { return !m_hidden && CProjectile::CanCollide(); }
    bool IsVisible() const override { return !m_hidden && CProjectile::IsVisible(); }

    SPetalStats m_base_petal_stats;
    SPetalStats m_final_petal_stats;
    EPetalType m_type = EPetalType::None;
    ERarity m_rarity = ERarity::Null;
    int m_max_slot_num = 0;
    int m_copy_index = 0;
    int m_slot_index = 0;
    int m_target_entity_id = -1;
    float m_lifetime = 0.f;
    float m_reload_override = -1.f;
    bool m_reload_ignore_multiplier = false;
    bool m_hidden = false;
    bool m_detach_from_slot = false;
    bool m_spawn_flight_boost = false;
};

class CBeetleEggPetal : public CPetal
{
  public:
    using CPetal::CPetal;

    void TakeDamage(float dmg, CEntity* attacker, EDamageType damage_type) override;

    int m_summon_id = -1;
    bool m_has_spawned_summon = false;
};

class CRelicPetal : public CPetal
{
  public:
    using CPetal::CPetal;

    int m_state_zone_id = -1;
};

class CMissilePetal : public CPetal
{
  public:
    using CPetal::CPetal;

    void Tick(float dt) override;

    bool m_fired = false;
    float m_fired_lifetime = 0.f;
};

class CCompassPetal : public CPetal
{
  public:
    using CPetal::CPetal;

    int m_compass_target_id = -1;
    float m_compass_wait_timer = 0.f;
};

class CYggdrasilPetal : public CPetal
{
  public:
    using CPetal::CPetal;

    int m_revive_target_id = -1;
    float m_revive_timer = 0.f;
};

inline std::unordered_map<EPetalType, std::unique_ptr<CPetalPrototype>> g_petal_registry;

template <typename TPetal> bool RegisterPetalPrototype(EPetalType type, CPetalPrototype prototype)
{
    auto ptr = std::make_unique<CPetalPrototype>(std::move(prototype));
    CPetalPrototype* raw_ptr = ptr.get();
    raw_ptr->m_factory = [raw_ptr](CFlower* flower, int slot, ERarity rarity) -> std::unique_ptr<CPetal>
    {
        if (!flower || !raw_ptr->m_p_behavior) return nullptr;

        SPetalStats petal_stats = raw_ptr->m_p_behavior->GetPetalStats(rarity);
        petal_stats.radius *= 0.5f;
        auto petal = std::make_unique<TPetal>(petal_stats.radius, flower, slot, petal_stats);
        petal->m_type = raw_ptr->m_type;
        petal->m_rarity = rarity;
        petal->m_slot_index = slot;
        petal->m_team = flower->m_team;
        return petal;
    };
    g_petal_registry[type] = std::move(ptr);
    return true;
}

const CPetalPrototype* FindPetalPrototype(EPetalType type);

#define REGISTER_PETAL(type, petal_class, proto) RegisterPetalPrototype<petal_class>(type, std::move(proto))
