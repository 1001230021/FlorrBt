#pragma once
#include "../../../Shared/game_config.h"
#include "../../../Shared/shared.h"
#include "../controller.h"
#include "../entity.h"
#include "../state.h"
#include <algorithm>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

class CPlayer;

struct CDamageData
{
    CPlayer* m_player = nullptr;
    float m_total_dmg = 0.f;
    float m_reset_timer = 0.f;

    void ResetTimer() { m_reset_timer = 120.f; }
};

class CMobBase;
bool TrySharePsionicDamage(CMobBase* receiver, float dmg, CEntity* attacker, EDamageType dmg_type);

class CMobBase : public CEntity
{
  public:
    CMobBase(CGameWorld* pworld, sf::Vector2f pos, float r) : CEntity(pworld, pos.x, pos.y, r) {}

    virtual ~CMobBase();
    CMobBase(const CMobBase&) = delete;
    CMobBase& operator=(const CMobBase&) = delete;

    void AddState(std::unique_ptr<CState> state);
    void TickStates(float dt);
    void TickPetalContacts();
    bool TickDropPickup(CPlayer* player);
    virtual void MoveTowards(const sf::Vector2f& target_pos, float dt);
    void ApplyDamageDirect(float dmg, CEntity* attacker);

    virtual const SMobStats* GetBaseStats() const = 0;
    virtual const SMobStats* GetFinalStats() const = 0;
    virtual ERarity GetRarity() const = 0;

    std::vector<CDamageData>& GetDamageData() { return m_damage_data; }
    const std::vector<CDamageData>& GetDamageData() const { return m_damage_data; }

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

    bool RemoveState(CState* state);

    void SetController(std::unique_ptr<IController> controller) { m_p_controller = std::move(controller); }
    IController* GetController() { return m_p_controller.get(); }

    sf::Vector2f m_vel = {0.f, 0.f};
    EMobType m_mob_type = EMobType::None;

  protected:
    std::vector<CDamageData> m_damage_data;
    std::vector<std::unique_ptr<CState>> m_states;
    std::unique_ptr<IController> m_p_controller;
};

template <typename TStats = SMobStats> class CMob : public CMobBase
{
  public:
    using stats_type = TStats;

    CMob(CGameWorld* pworld, sf::Vector2f pos, float r, ERarity rarity, const TStats& stats)
        : CMobBase(pworld, pos, r), m_base_stats(stats), m_final_stats(stats), m_rarity(rarity)
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
        TickPetalContacts();
    }

    const SMobStats* GetBaseStats() const override { return &m_base_stats; }
    const SMobStats* GetFinalStats() const override { return &m_final_stats; }
    ERarity GetRarity() const override { return m_rarity; }

    void TakeDamage(float dmg, CEntity* attacker, EDamageType dmg_type) override
    {
        if (dmg_type == EDamageType::Normal) dmg = std::max(0.f, dmg - m_base_stats.armor);
        if (dmg <= 0.f) return;
        if (TrySharePsionicDamage(this, dmg, attacker, dmg_type)) return;

        ApplyDamageDirect(dmg, attacker);
    }

    TStats m_base_stats;
    TStats m_final_stats;
    ERarity m_rarity;
};

class CMobPrototype
{
  public:
    using stats_factory = std::function<SMobStats(ERarity)>;
    using flower_stats_factory = std::function<SFlowerStats(ERarity)>;
    using controller_factory = std::function<std::unique_ptr<IController>()>;
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

        SFlowerStats stats;
        static_cast<SMobStats&>(stats) = BuildStats(rarity);
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
    mob_factory m_factory;

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

inline std::unordered_map<EMobType, std::unique_ptr<CMobPrototype>> g_mob_registry;

template <typename TMob> bool RegisterMobPrototype(EMobType type, CMobPrototype prototype)
{
    auto ptr = std::make_unique<CMobPrototype>(std::move(prototype));
    CMobPrototype* raw_ptr = ptr.get();
    raw_ptr->m_factory = [raw_ptr](CGameWorld* world, sf::Vector2f pos, ERarity rarity) -> std::unique_ptr<CMobBase>
    {
        if (!world) return nullptr;

        typename TMob::stats_type stats = raw_ptr->BuildTypedStats<typename TMob::stats_type>(rarity);
        auto mob = std::make_unique<TMob>(world, pos, stats.radius, rarity, stats);
        mob->m_mob_type = raw_ptr->m_type;
        mob->m_team = raw_ptr->m_team;
        if (raw_ptr->m_controller_factory) mob->SetController(raw_ptr->m_controller_factory());
        return mob;
    };
    g_mob_registry[type] = std::move(ptr);
    return true;
}

const CMobPrototype* FindMobPrototype(EMobType type);
std::unique_ptr<CMobBase> CreateMob(EMobType type, CGameWorld* world, sf::Vector2f pos, ERarity rarity);
void RegisterBeetle();
void RegisterBandageBeetle();
void RegisterNormalLadybug();
void RegisterNormalFlower();
void RegisterPlayerFlower();
void RegisterSoldierAnt();
void RegisterSoldierFireAnt();
void RegisterSoldierTermite();
void RegisterSummonedBeetle();
void RegisterSummonedSoldierAnt();
void RegisterMobs();

#define REGISTER_MOB(type, mob_class, proto) RegisterMobPrototype<mob_class>(type, std::move(proto))
