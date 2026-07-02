#pragma once
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

class CMobBase : public CEntity
{
  public:
    CMobBase(CGameWorld* pworld, float x, float y, float r) : CEntity(pworld, x, y, r) {}

    virtual ~CMobBase();
    CMobBase(const CMobBase&) = delete;
    CMobBase& operator=(const CMobBase&) = delete;

    void AddState(std::unique_ptr<CState> state);
    void TickStates(float dt);
    virtual void MoveTowards(const sf::Vector2f& target_pos, float dt);

    virtual const SMobStats* GetBaseStats() const = 0;
    virtual const SMobStats* GetFinalStats() const = 0;

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

  protected:
    std::vector<std::unique_ptr<CState>> m_states;
    std::unique_ptr<IController> m_p_controller;
};

template <typename TStats = SMobStats> class CMob : public CMobBase
{
  public:
    CMob(CGameWorld* pworld, float x, float y, float r, ERarity rarity, const TStats& stats)
        : CMobBase(pworld, x, y, r), m_base_stats(stats), m_final_stats(stats), m_rarity(rarity)
    {
        m_health = stats.max_health;
        m_mass = stats.mass;
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

    void TakeDamage(float dmg, CEntity* attacker, EDamageType dmg_type) override
    {
        if (dmg_type == EDamageType::Normal) dmg = std::max(0.f, dmg - m_base_stats.armor);
        if (dmg <= 0.f) return;

        m_health -= dmg;
        if (m_health <= 0.f)
        {
            m_health = 0.f;
            m_is_marked_for_des = true;
        }
    }

    TStats m_base_stats;
    TStats m_final_stats;
    ERarity m_rarity;
};

class CMobPrototype
{
  public:
    using stats_factory = std::function<SMobStats(ERarity)>;
    using controller_factory = std::function<std::unique_ptr<IController>()>;
    using mob_factory = std::function<std::unique_ptr<CMobBase>(CGameWorld*, float, float, ERarity)>;

    CMobPrototype() = default;
    CMobPrototype(const CMobPrototype&) = delete;
    CMobPrototype& operator=(const CMobPrototype&) = delete;
    CMobPrototype(CMobPrototype&&) = default;
    CMobPrototype& operator=(CMobPrototype&&) = default;

    SMobStats BuildStats(ERarity rarity) const { return m_stats_factory ? m_stats_factory(rarity) : m_base_stats; }

    EMobType m_type = EMobType::None;
    std::string m_name;
    SMobStats m_base_stats;
    int m_team = 2;
    stats_factory m_stats_factory;
    controller_factory m_controller_factory;
    mob_factory m_factory;
};

inline std::unordered_map<EMobType, std::unique_ptr<CMobPrototype>> g_mob_registry;

template <typename TMob> bool RegisterMobPrototype(EMobType type, CMobPrototype prototype)
{
    auto ptr = std::make_unique<CMobPrototype>(std::move(prototype));
    CMobPrototype* raw_ptr = ptr.get();
    raw_ptr->m_factory = [raw_ptr](CGameWorld* world, float x, float y, ERarity rarity) -> std::unique_ptr<CMobBase>
    {
        if (!world) return nullptr;

        SMobStats stats = raw_ptr->BuildStats(rarity);
        auto mob = std::make_unique<TMob>(world, x, y, stats.radius, rarity, stats);
        mob->m_team = raw_ptr->m_team;
        if (raw_ptr->m_controller_factory) mob->SetController(raw_ptr->m_controller_factory());
        return mob;
    };
    g_mob_registry[type] = std::move(ptr);
    return true;
}

const CMobPrototype* FindMobPrototype(EMobType type);
std::unique_ptr<CMobBase> CreateMob(EMobType type, CGameWorld* world, float x, float y, ERarity rarity);
void RegisterNormalLadybug();
void RegisterMobs();

#define REGISTER_MOB(type, mob_class, proto) RegisterMobPrototype<mob_class>(type, std::move(proto))