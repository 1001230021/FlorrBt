#pragma once
#include "../entity.h"
#include "../../../Shared/mob_type.h"
#include "../../../Shared/rarity.h"
#include <SFML/System/Vector2.hpp>

class CBloodSacrificeRitual : public CEntity
{
  public:
    CBloodSacrificeRitual(CGameWorld* world, sf::Vector2f pos, EMobType mob_type, ERarity rarity, float timer);

    void Tick(float dt) override;
    bool CanCollide() const override { return false; }
    bool IsVisible() const override { return !IsDead(); }

    ERarity GetRarity() const { return m_rarity; }
    float EffectProgress() const;

  private:
    EMobType m_mob_type = EMobType::None;
    ERarity m_rarity = ERarity::Null;
    float m_draw_duration = 10.f;
    float m_fade_duration = 60.f;
    float m_age = 0.f;
    bool m_spawned = false;
};
