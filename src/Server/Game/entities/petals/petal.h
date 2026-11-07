#pragma once
#include "../../../../Shared/game_config.h"
#include "../../../../Shared/math_types.h"
#include "../../../../Shared/shared.h"
#include "../../chain.h"
#include "../../prototype_registry.h"
#include "../flower.h"
#include "../projectile.h"
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <string>
#include <type_traits>
#include <unordered_map>

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

// 注册属性描述整组基础副本，各副本均分。
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
    // 已释放或隐藏的辅助花瓣保留重载归属，但不占轨道位置。
    virtual bool CountsForPetalLayout(const CPetal*) const { return true; }
    virtual EPetalBonusMode GetBonusMode() const { return EPetalBonusMode::AliveOnly; }
    // 缺失副本推进预加载或重载前只扣费一次；支付失败则停在周期起点。
    virtual float GetSpawnManaCost(ERarity) const { return 0.f; }

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
    void OnTickActions(float dt) override;
    void NotifyCleared();
    void OnBeforeRemoved(EEntityRemovalReason reason) override;
    void RefreshOrbitAnchor(CFlower* flower);
    CEntity* GetOrbitAnchor() const;

    void OnTakeDamage(float dmg, CEntity* attacker, EDamageType damage_type) override;
    const SEntityStats& GetEntityStats() const override { return m_final_petal_stats; }
    EPetalType GetPetalType() const { return m_petal_type; }
    void SetPetalType(EPetalType type)
    {
        m_petal_type = type;
        // 信息素使用 Honey 的协议视觉代理，客户端无需识别扩展类型。
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
    SEntityHandle m_target_ref;
    // 治疗和吸收花瓣沿短 Hermite 曲线靠近目标，起点速度保留原轨道切线。
    florrbt::Vector2f m_attraction_start_pos = {};
    florrbt::Vector2f m_attraction_start_velocity = {};
    float m_attraction_elapsed = 0.f;
    bool m_attraction_active = false;
    // 短暂屏蔽刚命中的目标，同时保留其身份供 Heavy 等行为读取最后接触。
    SEntityHandle m_target_release_ref;
    float m_target_release_timer = 0.f;
    float m_lifetime = 0.f;
    float m_timer = 0.f;
    float m_reload_override = -1.f;
    bool m_reload_ignore_multiplier = false;
    bool m_hidden = false;
    bool m_clear_notified = false;
    SEntityHandle m_orbit_anchor_ref;
    bool m_detach_from_slot = false;
    bool m_spawn_flight_boost = false;
    float m_growth_base_damage = 0.f;
    float m_growth_base_health = 0.f;
    float m_growth_base_radius = 0.f;
    std::uint64_t m_growth_stats_revision = std::numeric_limits<std::uint64_t>::max();
    std::uint64_t m_applied_flower_stats_revision = std::numeric_limits<std::uint64_t>::max();
    // Lightning 按连续接触累计命中次数，接触结束后按伤害乘次数释放一次连锁。
    SEntityHandle m_chain_target_ref;
    int m_chain_hit_count = 0;
    std::unordered_map<SEntityHandle, float, SEntityHandleHash> m_hit_credits;
};

class CMoonPetal : public CPetal
{
  public:
    using CPetal::CPetal;
    void OnTickMotion(float dt) override;
    florrbt::Vector2f m_orbit_center{};
    florrbt::Vector2f m_orbit_velocity{};
    float m_orbit_phase = game_config::pi;
    bool m_orbit_initialized = false;
};

class CBeetleEggPetal : public CPetal
{
  public:
    using CPetal::CPetal;

    void OnTakeDamage(float dmg, CEntity* attacker, EDamageType damage_type) override;

    SEntityHandle m_summon_ref;
    bool m_has_spawned_summon = false;
};

class CLinkerPetal final : public CPetal, public IChainLease
{
  public:
    using CPetal::CPetal;

    void OnTakeDamage(float, CEntity*, EDamageType) override {}
    void OnChainBroken(CChainEntity& chain) override;
    void ResetMissingChain();

    SEntityHandle m_first_target_ref;
    SEntityHandle m_chain_ref;
    float m_saved_chain_health = 0.f;
    // 释放后的 Linker 不再占槽位，单独保留相对轨道角。
    float m_released_orbit_angle_offset = 0.f;
    int m_charges_remaining = 2;
    bool m_released_from_slot = false;
    bool m_chain_finalized = false;
    bool m_has_released_orbit_angle = false;
};

class CRelicPetal : public CPetal
{
  public:
    using CPetal::CPetal;

    SEntityHandle m_state_zone_ref;
};

class CThrownPetal : public CPetal
{
  public:
    using CPetal::CPetal;

    void OnTickMotion(float dt) override;
    void OnTickEffects(float dt) override;
    void BeginThrow(florrbt::Vector2f direction, float speed, float deceleration_time, bool destroy_when_stopped,
                    bool tick_from_world, bool decelerates = true);
    void StopThrow(bool destroy);

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

    void OnTickActions(float dt) override;
    bool CollidesWithWalls() const override { return m_fired || CPetal::CollidesWithWalls(); }

    bool m_fired = false;
    bool m_has_fired_angle = false;
    float m_fired_angle = 0.f;
    float m_fired_lifetime = 0.f;
};

// 信息素不参与碰撞，投掷减速停止后向绑定分组报告位置；协议视觉使用 Honey。
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

    SEntityHandle m_compass_target_ref;
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

    void OnTakeDamage(float dmg, CEntity* attacker, EDamageType damage_type) override;

    std::unordered_map<SEntityHandle, float, SEntityHandleHash> m_hit_cooldowns;
};

class CBrokenEggPetal : public CPetal
{
  public:
    using CPetal::CPetal;

    bool CanCollide() const override { return false; }
};

class CMagicNoCollisionPetal : public CPetal
{
  public:
    using CPetal::CPetal;
    bool CanCollide() const override { return false; }
};

class CMagicEyePetal final : public CMagicNoCollisionPetal
{
  public:
    using CMagicNoCollisionPetal::CMagicNoCollisionPetal;

    float m_trigger_cooldown = 0.f;
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
};

class CDahliaPetal : public CPetal
{
  public:
    using CPetal::CPetal;
};

class CYggdrasilPetal : public CPetal
{
  public:
    using CPetal::CPetal;
    float m_revive_timer = 0.f;
};

// 注册表覆盖基础及扩展花瓣；预留区可选，固定编号 71 的 Magnet 仍无条件注册。
using CPetalRegistry = TPrototypeRegistry<EPetalType, CPetalPrototype, static_cast<std::size_t>(kPetalIdCapacity),
                                          static_cast<std::size_t>(kModPetalTypeBase)>;
namespace petal_registry_detail
{
inline CPetalRegistry& MutableRegistry()
{
    static CPetalRegistry registry("petal", static_cast<CPetalRegistry::name_function>(GetPetalTypeName));
    return registry;
}
}

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
        petal->SetTeam(flower->Team());
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

// 扩展花瓣使用客户端可见的固定编号，生成器须将它们同步到客户端表。
template <EModPetalType Type, class TBehavior, class TPetal = CPetal> bool RegisterModPetalDefinition()
{
    static_assert(IsModPetalTypeId(static_cast<int>(Type)), "mod petals must live in the reserved mod id range");
    static_assert(!GetModPetalTypeName(static_cast<int>(Type)).empty(),
                  "declare the mod petal in the mod_petal_type.h controller manifest first");
    return RegisterPetalDefinition<ToPetalType(Type), TBehavior, TPetal>();
}

const CPetalPrototype* FindPetalPrototype(EPetalType type);
