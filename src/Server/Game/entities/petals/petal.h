#pragma once
#include "../../../../Shared/game_config.h"
#include "../../../../Shared/math_types.h"
#include "../../../../Shared/shared.h"
#include "../../prototype_registry.h"
#include "../flower.h"
#include "../projectile.h"
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <string>
#include <type_traits>

class CPetal;
class CPetalPrototype;
class CPetalSlot;

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

// Registered stats describe the whole base stack; each base copy receives an equal share.
inline void NormalizePetalStatsPerCopy(SPetalStats& stats)
{
    const int count = std::max(1, stats.copy);
    stats.health /= static_cast<float>(count);
    stats.damage /= static_cast<float>(count);
    stats.armor /= static_cast<float>(count);
    stats.medicine /= static_cast<float>(count);
}

class CPetalBehavior
{
  public:
    virtual ~CPetalBehavior() = default;

    virtual bool IsOpen() const { return false; }
    virtual EPetalBonusMode GetBonusMode() const { return EPetalBonusMode::AliveOnly; }

    virtual SFlowerStats GetStats(ERarity rarity) const = 0;
    virtual SPetalStats GetPetalStats(ERarity rarity) const = 0;
    virtual SFlowerStats GetStatsForSlot(ERarity rarity, const CPetalSlot*, const CFlower*) const
    {
        return GetStats(rarity);
    }
    virtual SPetalStats GetSlotPetalStats(ERarity rarity, const CPetalSlot*, const CFlower*) const
    {
        return GetPetalStats(rarity);
    }
    virtual const CPetalPrototype* GetRuntimeSlotPrototype(ERarity, const CPetalSlot*, const CFlower*) const
    {
        return nullptr;
    }
    virtual EPetalBonusMode GetSlotBonusMode(ERarity rarity, const CPetalSlot* slot, const CFlower* flower) const
    {
        (void)rarity;
        (void)slot;
        (void)flower;
        return GetBonusMode();
    }

    virtual void OnTick(CPetal* owner, ERarity rarity, CFlower* flower, float dt) = 0;
    virtual void OnFlowerTakeDamage(CPetal* owner, ERarity rarity, CFlower* flower, float& dmg, EDamageType damage_type,
                                    CEntity* attacker) = 0;
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
    std::unique_ptr<CPetalBehavior> m_p_behavior;
    petal_factory m_factory;
};

class CPetal : public CProjectile
{
  public:
    CPetal(float r, CFlower* owner, int slot, SPetalStats petal_stats, EPetalType type);
    void Tick(float dt) override;

    void TakeDamage(float dmg, CEntity* attacker, EDamageType damage_type) override;
    const SEntityStats& GetEntityStats() const override { return m_final_petal_stats; }
    EPetalType GetPetalType() const { return m_petal_type; }
    void SetPetalType(EPetalType type)
    {
        m_petal_type = type;
        // The eou pheromone petal disguises itself as Honey on the wire so
        // clients render the vanilla sprite without client-side mod support
        // (same idea as mod mob visual proxies).
        const EPetalType wire_type =
            type == ToPetalType(EModPetalType::EouPheromone) ? EPetalType::Honey : type;
        SetNetworkType(PetalNetworkType(static_cast<std::uint8_t>(wire_type)));
    }
    bool CanCollide() const override { return !m_hidden && CProjectile::CanCollide(); }
    bool CollidesWithWalls() const override;
    bool IsVisible() const override { return !m_hidden && CProjectile::IsVisible(); }

    SPetalStats m_base_petal_stats;
    SPetalStats m_final_petal_stats;
    ERarity m_rarity = ERarity::Null;
    EPetalType m_petal_type = EPetalType::None;
    int m_max_slot_num = 0;
    int m_copy_index = 0;
    int m_slot_index = 0;
    int m_target_entity_id = -1;
    std::uint64_t m_target_entity_generation = 0;
    float m_lifetime = 0.f;
    float m_timer = 0.f;
    float m_reload_override = -1.f;
    bool m_reload_ignore_multiplier = false;
    bool m_hidden = false;
    bool m_detach_from_slot = false;
    bool m_spawn_flight_boost = false;
    float m_growth_base_damage = 0.f;
    float m_growth_base_health = 0.f;
    float m_growth_base_radius = 0.f;
    std::uint64_t m_growth_stats_revision = std::numeric_limits<std::uint64_t>::max();
    std::uint64_t m_applied_flower_stats_revision = std::numeric_limits<std::uint64_t>::max();
    // Lightning petal: contact-session hit accumulator. While the petal
    // touches a target each damage event increments m_chain_hit_count; when
    // the contact ends the stored chain fires once with damage*count.
    int m_chain_target_id = -1;
    std::uint64_t m_chain_target_generation = 0;
    int m_chain_hit_count = 0;
    std::unordered_map<int, float> m_hit_credits;
};

class CBeetleEggPetal : public CPetal
{
  public:
    using CPetal::CPetal;

    void TakeDamage(float dmg, CEntity* attacker, EDamageType damage_type) override;

    int m_summon_id = -1;
    std::uint64_t m_summon_generation = 0;
    bool m_has_spawned_summon = false;
};

class CRelicPetal : public CPetal
{
  public:
    using CPetal::CPetal;

    int m_state_zone_id = -1;
    std::uint64_t m_state_zone_generation = 0;
};

class CThrownPetal : public CPetal
{
  public:
    using CPetal::CPetal;

    void Tick(float dt) override;
    void BeginThrow(florrbt::Vector2f direction, float speed, float deceleration_time, bool destroy_when_stopped,
                    bool tick_from_world, bool decelerates = true);
    void StopThrow(bool destroy);

    // Called once when a throw finishes decelerating to a full stop.
    virtual void OnThrowStopped() {}

    bool m_thrown = false;
    bool m_throw_was_defend = false;
    bool m_throw_decelerates = true;
    bool m_destroy_when_stopped = false;
    float m_throw_age = 0.f;
    float m_throw_deceleration_time = game_config::default_petal_throw_initial_deceleration;
    float m_throw_initial_speed = 0.f;
    florrbt::Vector2f m_throw_direction = { 1.f, 0.f };
};

class CMissilePetal : public CThrownPetal
{
  public:
    using CThrownPetal::CThrownPetal;

    void Tick(float dt) override;
    bool CollidesWithWalls() const override { return m_fired || CPetal::CollidesWithWalls(); }

    bool m_fired = false;
    bool m_has_fired_angle = false;
    float m_fired_angle = 0.f;
    float m_fired_lifetime = 0.f;
};

// Eou pheromone petal: a honey-like petal that collides with nothing. When a
// throw fully decelerates it reports its position to the bound pheromone
// group (team + group are stamped on spawn). Disguised as Honey on the wire.
class CEouPheromonePetal final : public CThrownPetal
{
  public:
    CEouPheromonePetal(float r, CFlower* owner, int slot, SPetalStats petal_stats,
                       EPetalType type = ToPetalType(EModPetalType::EouPheromone))
        : CThrownPetal(r, owner, slot, petal_stats, type)
    {
    }

    void OnThrowStopped() override;
    bool CanCollide() const override { return false; }
    bool CollidesWithWalls() const override { return false; }

    int m_team_id = 0;
    int m_group_id = 0;
    bool m_input_latched = false;
};

class CTrapperPetal final : public CPetal
{
  public:
    using CPetal::CPetal;

    bool IsCollisionPositionLocked() const override { return true; }

    float m_fire_reload_timer = game_config::default_trapper_fire_interval;
    float m_recoil_timer = 0.f;
};

class CCompassPetal : public CPetal
{
  public:
    using CPetal::CPetal;

    int m_compass_target_id = -1;
    std::uint64_t m_compass_target_generation = 0;
    float m_compass_wait_timer = 0.f;
};

class CBonePetal : public CPetal
{
  public:
    using CPetal::CPetal;
};

class CGlassPetal : public CPetal
{
  public:
    using CPetal::CPetal;

    void TakeDamage(float dmg, CEntity* attacker, EDamageType damage_type) override;

    std::unordered_map<int, float> m_hit_cooldowns;
};

class CBrokenEggPetal : public CPetal
{
  public:
    using CPetal::CPetal;

    bool CanCollide() const override { return false; }
};

class CShovelPetal : public CPetal
{
  public:
    using CPetal::CPetal;

    bool CanCollide() const override { return false; }
};

class CBasilPetal : public CPetal
{
  public:
    using CPetal::CPetal;

    bool CanCollide() const override { return false; }
};

class CBatteryPetal : public CPetal
{
  public:
    using CPetal::CPetal;

    bool CanCollide() const override { return false; }

    int m_charges_remaining = 0;
};

class CRosePetal : public CPetal
{
  public:
    using CPetal::CPetal;

    int m_move_timer = 0;
};

class CDahliaPetal : public CPetal
{
  public:
    using CPetal::CPetal;

    int m_move_timer = 0;
};

class CYggdrasilPetal : public CPetal
{
  public:
    using CPetal::CPetal;

    int m_move_timer = 0;
    float m_revive_timer = 0.f;
};

// Registry slots cover the natives plus the reserved mod petal range
// (mod_petal_type.h). Slots from kModPetalTypeBase up are optional at
// Finalize (mod content plus native Magnet, which sits inside the pinned
// mod window at id 71 and is still registered unconditionally below).
using CPetalRegistry = TPrototypeRegistry<EPetalType, CPetalPrototype, static_cast<std::size_t>(kPetalIdCapacity),
                                          static_cast<std::size_t>(kModPetalTypeBase)>;
namespace petal_registry_detail
{
inline CPetalRegistry& MutableRegistry()
{
    static CPetalRegistry registry("petal", static_cast<CPetalRegistry::name_function>(GetPetalTypeName));
    return registry;
}
} // namespace petal_registry_detail

inline const CPetalRegistry& GetPetalRegistry() { return petal_registry_detail::MutableRegistry(); }

template <typename TPetal> bool RegisterPetalPrototype(EPetalType type, CPetalPrototype prototype)
{
    if (prototype.m_type != EPetalType::None && prototype.m_type != type)
    {
        petal_registry_detail::MutableRegistry().ReportError(
            "prototype type does not match registration key " + std::string(GetPetalTypeName(type)));
        return false;
    }
    if (!prototype.m_p_behavior)
    {
        petal_registry_detail::MutableRegistry().ReportError("missing behavior for " +
                                                              std::string(GetPetalTypeName(type)));
        return false;
    }

    prototype.m_type = type;
    prototype.m_name = std::string(GetPetalTypeName(type));
    auto ptr = std::make_unique<CPetalPrototype>(std::move(prototype));
    CPetalPrototype* raw_ptr = ptr.get();
    raw_ptr->m_factory = [raw_ptr](CFlower* flower, int slot, ERarity rarity) -> std::unique_ptr<CPetal> {
        if (!flower || !raw_ptr->m_p_behavior) return nullptr;

        SPetalStats petal_stats = raw_ptr->m_p_behavior->GetPetalStats(rarity);
        NormalizePetalStatsPerCopy(petal_stats);
        petal_stats.radius *= game_config::default_petal_collision_radius_multiplier;
        auto petal = std::make_unique<TPetal>(petal_stats.radius, flower, slot, petal_stats, raw_ptr->m_type);
        petal->m_rarity = rarity;
        petal->m_slot_index = slot;
        petal->Team() = flower->Team();
        return petal;
    };
    return petal_registry_detail::MutableRegistry().Add(type, std::move(ptr));
}

template <EPetalType Type, class TBehavior, class TPetal = CPetal> bool RegisterPetalDefinition()
{
    static_assert(std::is_base_of_v<CPetalBehavior, TBehavior>, "petal behavior must derive from CPetalBehavior");
    static_assert(std::is_base_of_v<CPetal, TPetal>, "petal entity must derive from CPetal");

    CPetalPrototype prototype;
    prototype.m_p_behavior = std::make_unique<TBehavior>();
    return RegisterPetalPrototype<TPetal>(Type, std::move(prototype));
}

// Registers a controller-specific petal from the mod id space
// (mod_petal_type.h). Unlike mod mobs these ids are client-visible wire ids,
// so the manifest pins them forever and the id generator ships them to the
// client tables.
template <EModPetalType Type, class TBehavior, class TPetal = CPetal> bool RegisterModPetalDefinition()
{
    static_assert(IsModPetalTypeId(static_cast<int>(Type)), "mod petals must live in the reserved mod id range");
    static_assert(!GetModPetalTypeName(static_cast<int>(Type)).empty(),
                  "declare the mod petal in the mod_petal_type.h controller manifest first");
    return RegisterPetalDefinition<ToPetalType(Type), TBehavior, TPetal>();
}

const CPetalPrototype* FindPetalPrototype(EPetalType type);
