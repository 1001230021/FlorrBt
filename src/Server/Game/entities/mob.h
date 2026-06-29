#pragma once
#include "../entity.h"
#include "../../../Shared/shared.h"
#include "../state.h"
#include <memory>
#include <vector>

class CMobBase : public CEntity {
public:
    CMobBase(CGameWorld* pworld, float x, float y, float r)
        : CEntity(pworld, x, y, r) {
    }

    virtual ~CMobBase();
    CMobBase(const CMobBase&) = delete;
    CMobBase& operator=(const CMobBase&) = delete;

    void AddState(std::unique_ptr<CState> state);
    void TickStates(float dt);

    template<typename TState>
    std::vector<TState*> FindStates()
    {
        std::vector<TState*> vt;
        for (auto& state : m_States)
        {
            auto* casted = dynamic_cast<TState*>(state.get());
            if (casted) vt.push_back(casted);
        }
        return vt;
    }

    bool RemoveState(CState* state);

    sf::Vector2f m_Vel = { 0.f, 0.f };

protected:
    std::vector<std::unique_ptr<CState>> m_States;
};

template <typename TStats = SMobStats>
class CMob : public CMobBase {
public:
    CMob(CGameWorld* pworld, float x, float y, float r, ERarity rarity, const TStats& stats)
        : CMobBase(pworld, x, y, r), m_Rarity(rarity), m_BaseStats(stats), m_FinalStats(stats)
    {
        m_Health = stats.max_health;
    }

    virtual ~CMob() override;

    void Tick(float dt) override {
        m_Pos += m_Vel * dt;
        TickStates(dt);
    }

    const TStats& GetStats() const { return m_BaseStats; }
    TStats& GetStats() { return m_BaseStats; }

    void TakeDamage(float dmg, CEntity* attacker, EDamageType dmg_type) override
    {
        if (dmg_type == EDamageType::Normal)
            dmg -= m_BaseStats.armor;
        m_Health -= dmg;
        if (m_Health <= 0)
        {
            m_Health = 0;
            m_IsMarkedForDes = true;
        }
    }

    TStats m_BaseStats;
    TStats m_FinalStats;
    ERarity m_Rarity;
};
