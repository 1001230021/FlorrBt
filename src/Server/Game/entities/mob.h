#pragma once
#include "../../../Shared/game_config.h"
#include "../../../Shared/shared.h"
#include "../controller.h"
#include "../entity.h"
#include "../prototype_registry.h"
#include "../state.h"
#include <algorithm>
#include <functional>
#include <memory>
#include <string>
#include <type_traits>
#include <vector>

class CPlayer;
class CSnapshotReader;
class CSnapshotWriter;

struct CDamageData
{
    CPlayer* m_player = nullptr;
    float m_total_dmg = 0.f;
    float m_reset_timer = 0.f;

    void ResetTimer() { m_reset_timer = 120.f; }
};

class CMobBase;
bool TrySharePsionicDamage(CMobBase* receiver, float dmg, CEntity* attacker, EDamageType dmg_type);
bool ShouldBlockDiggingDamage(CMobBase* receiver, CEntity* attacker, EDamageType dmg_type);

class CMobBase : public CEntity
{
  public:
    CMobBase(CGameWorld* pworld, sf::Vector2f pos, float r, EMobType mob_type)
        : CEntity(pworld, pos.x, pos.y, r, MakeMobEntityType(static_cast<std::uint8_t>(mob_type)))
    {
    }

    virtual ~CMobBase();
    CMobBase(const CMobBase&) = delete;
    CMobBase& operator=(const CMobBase&) = delete;

    void AddState(std::unique_ptr<CState> state);
    void TickStates(float dt);
    bool TickDropPickup(CPlayer* player);
    virtual void MoveTowards(const sf::Vector2f& target_pos, float dt);
    void ApplyDamageDirect(float dmg, CEntity* attacker);

    virtual const SMobStats* GetBaseStats() const = 0;
    virtual const SMobStats* GetFinalStats() const = 0;
    EMobType GetMobType() const { return static_cast<EMobType>(GetNetworkType()); }
    const SEntityStats& GetEntityStats() const override
    {
        const SMobStats* stats = GetFinalStats();
        return stats ? static_cast<const SEntityStats&>(*stats) : CEntity::GetEntityStats();
    }
    virtual ERarity GetRarity() const = 0;
    virtual bool IsFacingLocked() const { return false; }
    float WallCollisionRadius() const override
    {
        return std::max(0.f, m_radius * game_config::mob_wall_collision_radius_multiplier);
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
    const std::vector<std::unique_ptr<CState>>& GetStates() const { return m_states; }
    void ClearStatesForRestore();

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

    sf::Vector2f m_vel = { 0.f, 0.f };

  protected:
    std::vector<CDamageData> m_damage_data;
    std::vector<std::unique_ptr<CState>> m_states;
    std::unique_ptr<IController> m_p_controller;
};

template <typename TStats = SMobStats> class CMob : public CMobBase
{
  public:
    using stats_type = TStats;

    CMob(CGameWorld* pworld, sf::Vector2f pos, float r, EMobType mob_type, ERarity rarity, const TStats& stats)
        : CMobBase(pworld, pos, r, mob_type), m_base_stats(stats), m_final_stats(stats), m_rarity(rarity)
    {
        m_health = stats.max_health;
        m_mass = stats.mass;
        m_facing_angle = GetLimitedRng(-game_config::pi, game_config::pi);
        m_has_facing = true;
    }

    ~CMob() override;

    void Tick(float dt) override
    {
        if (m_p_controller) m_p_controller->OnTick(this, dt);
        m_pos += m_vel * dt;
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

        ApplyDamageDirect(dmg, attacker);
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
    using rarity_resolver = std::function<ERarity(CGameWorld&, sf::Vector2f, ERarity)>;
    using mob_factory = std::function<std::unique_ptr<CMobBase>(CGameWorld*, sf::Vector2f, ERarity)>;

    CMobPrototype() = default;
    CMobPrototype(const CMobPrototype&) = delete;
    CMobPrototype& operator=(const CMobPrototype&) = delete;
    CMobPrototype(CMobPrototype&&) = default;
    CMobPrototype& operator=(CMobPrototype&&) = default;

    SMobStats BuildStats(ERarity rarity) const { return m_stats_factory ? m_stats_factory(rarity) : m_base_stats; }
    SFlowerStats BuildFlowerStats(ERarity rarity) const
    {
        if (m_flower_stats_factory) return m_flower_stats_factory(rarity);

        SFlowerStats stats = m_base_flower_stats;
        if (m_stats_factory) static_cast<SMobStats&>(stats) = BuildStats(rarity);
        return stats;
    }

    EMobType m_type = EMobType::None;
    std::string m_name;
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

using CMobRegistry = TPrototypeRegistry<EMobType, CMobPrototype, mob_type_names.size()>;
namespace mob_registry_detail
{
inline CMobRegistry& MutableRegistry()
{
    static CMobRegistry registry("mob", static_cast<CMobRegistry::name_function>(GetMobTypeName));
    return registry;
}
} // namespace mob_registry_detail

inline const CMobRegistry& MobRegistry() { return mob_registry_detail::MutableRegistry(); }

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
    raw_ptr->m_factory = [raw_ptr](CGameWorld* world, sf::Vector2f pos, ERarity rarity) -> std::unique_ptr<CMobBase> {
        if (!world) return nullptr;

        typename TMob::stats_type stats = raw_ptr->BuildTypedStats<typename TMob::stats_type>(rarity);
        auto mob = std::make_unique<TMob>(world, pos, stats.radius, Type, rarity, stats);
        mob->m_team = raw_ptr->m_team;
        mob->m_allow_skip_tick = raw_ptr->m_allow_skip_tick;
        if (raw_ptr->m_after_create) raw_ptr->m_after_create(*mob, rarity);
        if (raw_ptr->m_controller_factory) mob->SetController(raw_ptr->m_controller_factory(rarity));
        return mob;
    };
    return mob_registry_detail::MutableRegistry().Add(Type, std::move(ptr));
}

const CMobPrototype* FindMobPrototype(EMobType type);
std::unique_ptr<CMobBase> CreateMob(EMobType type, CGameWorld* world, sf::Vector2f pos, ERarity rarity,
                                    bool resolve_special_rarity = true);
bool RegisterMobs(std::string& error);
