#pragma once
#include "../entity.h"
#include <string>
#include <utility>

class CPlayer;

class CPortal : public CEntity
{
  public:
    CPortal(CGameWorld* world, sf::Vector2f pos, float radius, std::string target_world);

    void Tick(float dt) override;
    void TakeDamage(float, CEntity*, EDamageType) override {}
    bool CanCollide() const override { return false; }
    bool IsDead() const override { return false; }

    const std::string& GetTargetWorldName() const { return m_target_world_name; }
    const std::string& GetFromPointName() const { return m_from_point_name; }
    void SetFromPointName(std::string from_point_name) { m_from_point_name = std::move(from_point_name); }

  private:
    void AttractAndTransferPlayer(CPlayer& player, float dt);

    std::string m_target_world_name;
    std::string m_from_point_name;
};
