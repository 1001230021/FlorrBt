#pragma once
#include "entity.h"
#include "state.h"
#include <functional>
#include <memory>
#include <utility>
#include <SFML/System/Vector2.hpp>

class CMobBase;

class CStateZone : public CEntity
{
  public:
    using state_factory = std::function<std::unique_ptr<CState>(CMobBase*)>;
    using zone_filter = std::function<bool(CEntity*)>;

    CStateZone(CGameWorld* world, sf::Vector2f pos, float radius, state_factory state,
               zone_filter filter = nullptr);

    template <typename TState, typename... TArgs>
    static state_factory MakeStateFactory(TArgs... args)
    {
        return [args...](CMobBase* mob) mutable -> std::unique_ptr<CState>
        {
            auto state = std::make_unique<TState>(mob, args...);
            if constexpr (requires(const TState& checked_state) { checked_state.IsValid(); })
            {
                if (!state->IsValid()) return nullptr;
            }
            return state;
        };
    }

    void Tick(float dt) override;
    void SetCenter(sf::Vector2f pos) { m_pos = pos; }
    bool CanCollide() const override { return false; }
    bool IsVisible() const override { return false; }

    void Apply();

    state_factory m_state;
    zone_filter m_filter;
    float m_timer = endless;
};

class CSpiderWebZone : public CStateZone
{
  public:
    CSpiderWebZone(CGameWorld* world, sf::Vector2f pos, float radius, CEntity* owner,
                   float lifetime, float desired_speed_multiplier);

    bool IsVisible() const override { return !IsDead(); }
};
