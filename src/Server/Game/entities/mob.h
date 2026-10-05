#pragma once
#include "../../../Shared/game_config.h"
#include "../../../Shared/math_types.h"
#include "../../../Shared/shared.h"
#include "../controller.h"
#include "../entity.h"
#include "../prototype_registry.h"
#include "../state.h"
#include <algorithm>
#include <functional>
#include <limits>
#include <memory>
#include <string>
#include <type_traits>
#include <vector>

class CPlayer;
class CGameContext;
class CSnapshotReader;
class CSnapshotWriter;

struct CDamageData
{
    std::string m_account_name;
    float m_total_dmg = 0.f;
    float m_reset_timer = 0.f;

    void ResetTimer() { m_reset_timer = 120.f; }
    CPlayer* ResolvePlayer(const CGameContext* context) const;
};

class CMobBase;
bool TrySharePsionicDamage(CMobBase* receiver, float dmg, CEntity* attacker, EDamageType dmg_type);
bool ShouldBlockDiggingDamage(CMobBase* receiver, CEntity* attacker, EDamageType dmg_type);

class CMobBase : public CEntity
{
  public:
    CMobBase(CGameWorld* pworld, florrbt::Vector2f pos, float r, EMobType mob_type)
        : CEntity(pworld, pos.x, pos.y, r, MakeMobEntityType(static_cast<std::uint8_t>(mob_type)))
    {
    }

    virtual ~CMobBase();
    CMobBase(const CMobBase&) = delete;
    CMobBase& operator=(const CMobBase&) = delete;

    void AddState(std::unique_ptr<CState> state);
    void TickStates(float dt);
    bool TickDropPickup(CPlayer* player);
    // Moves toward target_pos. When target_radius >= 0 the approach stop
    // distance is the mob's radius plus the target's radius, so melee mobs
    // close in until they actually overlap the target (contact damage range)
    // instead of braking one target-radius early.
    virtual void MoveTowards(const florrbt::Vector2f& target_pos, float dt, float target_radius = -1.f);
    // Analog intent: distance / full_speed_distance selects the requested
    // speed (0..1). A negative target_radius denotes a point, with no body
    // radius dead zone. AI callers keep the existing three-argument contract.
    virtual void MoveTowards(const florrbt::Vector2f& target_pos, float dt, float target_radius,
                             float full_speed_distance);
    void ApplyDamageDirect(float dmg, CEntity* attacker, EDamageType damage_type = EDamageType::Normal);

    virtual const SMobStats* GetBaseStats() const = 0;
    virtual const SMobStats* GetFinalStats() const = 0;
    EMobType GetMobType() const { return static_cast<EMobType>(GetNetworkType()); }
    const SEntityStats& GetEntityStats() const override
    {
        const SMobStats* stats = GetFinalStats();
        return stats ? static_cast<const SEntityStats&>(*stats) : CEntity::GetEntityStats();
    }
    float TickHorizon() const override
    {
        const SMobStats* stats = GetFinalStats();
        return stats ? std::max(0.f, stats->horizon) : CEntity::TickHorizon();
    }
    virtual ERarity GetRarity() const = 0;
    // Marks a mob created by an open-world natural spawn. This is deliberately
    // separate from rarity: console commands, precise map spawns and ability
    // summons may create Eternal/Unique mobs without consuming the natural
    // spawn uniqueness budget.
    bool IsNaturallySpawned() const { return m_naturally_spawned; }
    void SetNaturallySpawned(bool value) { m_naturally_spawned = value; }
    virtual bool IsFacingLocked() const { return false; }
    // Wall collision uses a circle of the mob's own radius, capped at the
    // player-flower radius for stability (a 0-radius "point" contact lets
    // mobs tunnel/embed into walls after a fast frame).
    float WallCollisionRadius() const override
    {
        return std::clamp(Radius(), 0.f, game_config::mob_player_flower_radius);
    }
    virtual std::uint32_t RuntimeSnapshotVersion() const { return 1; }
    virtual void CaptureRuntimeSnapshot(CSnapshotWriter&) const {}
    virtual bool RestoreRuntimeSnapshot(const CSnapshotReader&, std::uint32_t version, std::string& error)
    {
        if (version <= RuntimeSnapshotVersion()) return true;
        error = "Unsupported mob runtime snapshot version";
        return false;
    }

    std::vector<CDamageData>& GetDamageData() { return m_damage_data; }
    const std::vector<CDamageData>& GetDamageData() const { return m_damage_data; }
    float InactiveSeconds() const { return m_inactive_seconds; }
    void AddInactiveSeconds(float dt) { m_inactive_seconds += dt; }
    void ResetInactiveSeconds() { m_inactive_seconds = 0.f; }
    const std::vector<std::unique_ptr<CState>>& GetStates() const { return m_states; }
    void ClearStatesForRestore();
    void ClearTeamOverrideStates();

    template <typename TState> std::vector<TState*> FindStates()
    {
        std::vector<TState*> states;
        for (auto& state : m_states)
        {
            auto* casted = dynamic_cast<TState*>(state.get());
            if (casted) states.push_back(casted);
        }
        return states;
    }

    template <typename TState> TState* FindFirstState()
    {
        for (auto& state : m_states)
        {
            auto* casted = dynamic_cast<TState*>(state.get());
            if (casted) return casted;
        }
        return nullptr;
    }

    template <typename TState> const TState* FindFirstState() const
    {
        for (const auto& state : m_states)
        {
            auto* casted = dynamic_cast<const TState*>(state.get());
            if (casted) return casted;
        }
        return nullptr;
    }

    template <typename TState> bool HasState() const { return FindFirstState<TState>() != nullptr; }

    template <typename TState, typename TVisitor> void ForEachState(TVisitor visitor)
    {
        for (auto& state : m_states)
        {
            auto* casted = dynamic_cast<TState*>(state.get());
            if (casted) visitor(casted);
        }
    }

    template <typename TState, typename TVisitor> void ForEachState(TVisitor visitor) const
    {
        for (const auto& state : m_states)
        {
            auto* casted = dynamic_cast<const TState*>(state.get());
            if (casted) visitor(casted);
        }
    }

    bool RemoveState(CState* state);

    void SetController(std::unique_ptr<IController> controller) { m_p_controller = std::move(controller); }
    IController* GetController() { return m_p_controller.get(); }
    const IController* GetController() const { return m_p_controller.get(); }

    florrbt::Vector2f m_vel = { 0.f, 0.f };

  protected:
    florrbt::Vector2f ApplyMovementTargetStates(const florrbt::Vector2f& target_pos) const;
    void UnregisterMovementTargetState(CState* state);

    std::vector<CDamageData> m_damage_data;
    std::vector<std::unique_ptr<CState>> m_states;
    std::vector<CState*> m_movement_target_states;
    std::unique_ptr<IController> m_p_controller;
    float m_inactive_seconds = 0.f;
    bool m_naturally_spawned = false;
};

template <typename TStats = SMobStats> class CMob : public CMobBase
{
  public:
    using stats_type = TStats;

    CMob(CGameWorld* pworld, florrbt::Vector2f pos, float r, EMobType mob_type, ERarity rarity, const TStats& stats)
        : CMobBase(pworld, pos, r, mob_type), m_base_stats(stats), m_final_stats(stats), m_rarity(rarity)
    {
        if (rarity == ERarity::Eternal || rarity == ERarity::Unique || rarity == ERarity::Primordial)
            TickMode() = EEntityTickMode::Always;
        SetTickHorizon(std::max(0.f, stats.horizon));
        Health() = stats.max_health;
        Mass() = stats.mass;
        FacingAngle() = GetLimitedRng(-game_config::pi, game_config::pi);
        HasFacing() = true;
    }

    ~CMob() override;

    void Tick(float dt) override
    {
        if (m_p_controller) m_p_controller->OnTick(this, dt);
        Pos() += m_vel * dt;
        TickStates(dt);
    }

    const SMobStats* GetBaseStats() const override { return &m_base_stats; }
    const SMobStats* GetFinalStats() const override { return &m_final_stats; }
    ERarity GetRarity() const override { return m_rarity; }

    void TakeDamage(float dmg, CEntity* attacker, EDamageType dmg_type) override
    {
        if (ShouldBlockDiggingDamage(this, attacker, dmg_type)) return;
        if (dmg_type == EDamageType::Normal) dmg = std::max(0.f, dmg - m_base_stats.armor);
        if (dmg <= 0.f) return;
        if (TrySharePsionicDamage(this, dmg, attacker, dmg_type)) return;

        ApplyDamageDirect(dmg, attacker, dmg_type);
    }

    TStats m_base_stats;
    TStats m_final_stats;
    ERarity m_rarity;
};

class IAttackableMob
{
  public:
    virtual ~IAttackableMob() = default;

    virtual bool IsAttacking() const = 0;
    virtual bool IsDefending() const = 0;
    virtual void SetAttacking(bool attacking) = 0;
    virtual void SetDefending(bool defending) = 0;
    virtual bool TryAttack(CEntity* target) = 0;

    void ClearAttackState()
    {
        SetAttacking(false);
        SetDefending(false);
    }
};

class ISkillCasterMob
{
  public:
    virtual ~ISkillCasterMob() = default;

    virtual int GetSkillCount() const = 0;
    virtual bool CanCastSkill(int skill_index) const = 0;
    virtual bool TryCastSkill(int skill_index, CEntity* target) = 0;
    virtual bool IsSkillBusy() const = 0;
    virtual uint8_t GetWindupSkillId() const = 0;
};

class IHornetMob
{
  public:
    virtual ~IHornetMob() = default;
    virtual bool TryCastMissileInDirection(florrbt::Vector2f direction) = 0;
    virtual void SetMissileGenerationSuppressed(bool suppressed, bool discard_loaded) = 0;
};

template <typename TStats = SMobStats> class CAttackableMob : public CMob<TStats>, public IAttackableMob
{
  public:
    using stats_type = TStats;
    using CMob<TStats>::CMob;

    bool IsAttacking() const override { return m_attacking; }
    bool IsDefending() const override { return m_defending; }
    void SetAttacking(bool attacking) override { m_attacking = attacking; }
    void SetDefending(bool defending) override { m_defending = defending; }
    bool TryAttack(CEntity*) override { return false; }

    bool m_attacking = false;
    bool m_defending = false;
};

template <typename TStats = SMobStats> class CSkillCasterMob : public CAttackableMob<TStats>, public ISkillCasterMob
{
  public:
    using stats_type = TStats;
    using CAttackableMob<TStats>::CAttackableMob;

    int GetSkillCount() const override { return 0; }
    bool CanCastSkill(int) const override { return false; }
    bool TryCastSkill(int, CEntity*) override { return false; }
    bool IsSkillBusy() const override { return false; }
    uint8_t GetWindupSkillId() const override { return 0; }
};

class CMobPrototype
{
  public:
    using stats_factory = std::function<SMobStats(ERarity)>;
    using flower_stats_factory = std::function<SFlowerStats(ERarity)>;
    using controller_factory = std::function<std::unique_ptr<IController>(ERarity)>;
    using after_create = std::function<void(CMobBase&, ERarity)>;
    using rarity_resolver = std::function<ERarity(CGameWorld&, florrbt::Vector2f, ERarity)>;
    using mob_factory = std::function<std::unique_ptr<CMobBase>(CGameWorld*, florrbt::Vector2f, ERarity)>;

    CMobPrototype() = default;
    CMobPrototype(const CMobPrototype&) = delete;
    CMobPrototype& operator=(const CMobPrototype&) = delete;
    CMobPrototype(CMobPrototype&&) = default;
    CMobPrototype& operator=(CMobPrototype&&) = default;

    static void ApplyBaseMassMultiplier(SMobStats& stats)
    {
        const float multiplier = std::max(0.f, game_config::mob_base_mass_multiplier);
        const float max_mass = std::numeric_limits<float>::max();
        if (stats.mass >= max_mass / std::max(1.f, multiplier))
            stats.mass = max_mass;
        else
            stats.mass *= multiplier;
    }

    SMobStats BuildStats(ERarity rarity) const
    {
        SMobStats stats = m_stats_factory ? m_stats_factory(rarity) : m_base_stats;
        ApplyBaseMassMultiplier(stats);
        return stats;
    }
    SFlowerStats BuildFlowerStats(ERarity rarity) const
    {
        SFlowerStats stats = m_flower_stats_factory ? m_flower_stats_factory(rarity) : m_base_flower_stats;
        if (!m_flower_stats_factory && m_stats_factory)
            static_cast<SMobStats&>(stats) = m_stats_factory(rarity);
        ApplyBaseMassMultiplier(stats);
        return stats;
    }

    EMobType m_type = EMobType::None;
    std::string m_name;
    // When set, the snapshot wire type is disguised as this vanilla mob type
    // so the client renders the vanilla sprite and name without client-side
    // mod support.
    EMobType m_visual_proxy_type = EMobType::None;
    SMobStats m_base_stats;
    SFlowerStats m_base_flower_stats;
    int m_team = 2;
    stats_factory m_stats_factory;
    flower_stats_factory m_flower_stats_factory;
    controller_factory m_controller_factory;
    after_create m_after_create;
    rarity_resolver m_rarity_resolver;
    mob_factory m_factory;
    bool m_allow_skip_tick = true;

    template <typename TStats> TStats BuildTypedStats(ERarity rarity) const
    {
        TStats stats;
        static_cast<SMobStats&>(stats) = BuildStats(rarity);
        return stats;
    }
};

template <> inline SFlowerStats CMobPrototype::BuildTypedStats<SFlowerStats>(ERarity rarity) const
{
    return BuildFlowerStats(rarity);
}

// Registry slots cover the native range plus the reserved mod range
// (mod_mob_type.h); slots past the natives are optional so controllers can
// register content without every id being mandatory at Finalize.
using CMobRegistry =
    TPrototypeRegistry<EMobType, CMobPrototype, static_cast<std::size_t>(kMobIdCapacity),
                       static_cast<std::size_t>(EMobType::Jellyfish) + 1>;
namespace mob_registry_detail
{
inline CMobRegistry& MutableRegistry()
{
    static CMobRegistry registry("mob", static_cast<CMobRegistry::name_function>(GetMobTypeName));
    return registry;
}
} // namespace mob_registry_detail

inline const CMobRegistry& GetMobRegistry() { return mob_registry_detail::MutableRegistry(); }

template <EMobType Type, typename TMob> bool RegisterMobPrototype(CMobPrototype prototype)
{
    static_assert(std::is_base_of_v<CMobBase, TMob>, "TMob must derive from CMobBase");

    using stats_type = typename TMob::stats_type;
    if constexpr (std::is_same_v<stats_type, SFlowerStats>)
    {
        if (!prototype.m_flower_stats_factory)
        {
            mob_registry_detail::MutableRegistry().ReportError("missing flower stats factory for " +
                                                                std::string(GetMobTypeName(Type)));
            return false;
        }
    } else if (!prototype.m_stats_factory)
    {
        mob_registry_detail::MutableRegistry().ReportError("missing stats factory for " +
                                                            std::string(GetMobTypeName(Type)));
        return false;
    }

    prototype.m_type = Type;
    prototype.m_name = std::string(GetMobTypeName(Type));
    auto ptr = std::make_unique<CMobPrototype>(std::move(prototype));
    CMobPrototype* raw_ptr = ptr.get();
    raw_ptr->m_factory = [raw_ptr](CGameWorld* world, florrbt::Vector2f pos, ERarity rarity) -> std::unique_ptr<CMobBase> {
        if (!world) return nullptr;

        typename TMob::stats_type stats = raw_ptr->BuildTypedStats<typename TMob::stats_type>(rarity);
        auto mob = std::make_unique<TMob>(world, pos, stats.radius, Type, rarity, stats);
        mob->Team() = raw_ptr->m_team;
        mob->AllowSkipTick() = raw_ptr->m_allow_skip_tick;
        if (raw_ptr->m_after_create) raw_ptr->m_after_create(*mob, rarity);
        if (raw_ptr->m_controller_factory) mob->SetController(raw_ptr->m_controller_factory(rarity));
        return mob;
    };
    return mob_registry_detail::MutableRegistry().Add(Type, std::move(ptr));
}

const CMobPrototype* FindMobPrototype(EMobType type);

// Registers a controller-specific mob from the mod id space (mod_mob_type.h).
// A vanilla visual proxy is MANDATORY: the network snapshot always presents
// the mob as an existing type, so mod ids stay server-internal and never
// reach the wire or the client id tables.
template <EModMobType Type, typename TMob> bool RegisterModMob(CMobPrototype prototype, EMobType visual_proxy)
{
    static_assert(IsModMobTypeId(static_cast<int>(Type)), "mod mobs must live in the reserved mod id range");
    static_assert(!GetModMobTypeName(static_cast<int>(Type)).empty(),
                  "declare the mod mob in the mod_mob_type.h controller manifest first");
    if (visual_proxy == EMobType::None || static_cast<std::size_t>(visual_proxy) >= mob_type_names.size())
    {
        mob_registry_detail::MutableRegistry().ReportError(
            "mod mob " + std::string(GetModMobTypeName(static_cast<int>(Type))) +
            " requires a vanilla visual proxy");
        return false;
    }
    prototype.m_visual_proxy_type = visual_proxy;
    return RegisterMobPrototype<ToMobType(Type), TMob>(std::move(prototype));
}

std::unique_ptr<CMobBase> CreateMob(EMobType type, CGameWorld* world, florrbt::Vector2f pos, ERarity rarity,
                                    bool resolve_special_rarity = true, bool naturally_spawned = false);
inline std::unique_ptr<CMobBase> CreateMob(EModMobType type, CGameWorld* world, florrbt::Vector2f pos, ERarity rarity,
                                           bool resolve_special_rarity = true, bool naturally_spawned = false)
{
    return CreateMob(ToMobType(type), world, pos, rarity, resolve_special_rarity, naturally_spawned);
}
bool RegisterMobs(std::string& error);
