#pragma once
#include "../../../Shared/mob_type.h"
#include "../../../Shared/rarity.h"
#include "../entity.h"
#include <SFML/System/Vector2.hpp>

class CBloodSacrificeRitual : public CEntity
{
  public:
    CBloodSacrificeRitual(CGameWorld* world, sf::Vector2f pos, EMobType mob_type, ERarity rarity, float timer);

    void Tick(float dt) override;
    bool CanCollide() const override { return false; }
    bool IsVisible() const override { return !IsDead(); }

    ERarity GetRarity() const { return m_rarity; }
    EMobType GetMobType() const { return m_mob_type; }
    float GetDrawDuration() const { return m_draw_duration; }
    float GetFadeDuration() const { return m_fade_duration; }
    float GetAge() const { return m_age; }
    bool HasSpawned() const { return m_spawned; }
    void RestoreRuntime(float draw_duration, float fade_duration, float age, bool spawned)
    {
        m_draw_duration = draw_duration;
        m_fade_duration = fade_duration;
        m_age = age;
        m_spawned = spawned;
    }
    float EffectProgress() const;

  private:
    EMobType m_mob_type = EMobType::None;
    ERarity m_rarity = ERarity::Null;
    float m_draw_duration = 10.f;
    float m_fade_duration = 0.f;
    float m_age = 0.f;
    bool m_spawned = false;
};
