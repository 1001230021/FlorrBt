#pragma once
#include "../entity.h"
#include "../../../Shared/math_types.h"
#include <string>
#include <utility>

class CPlayer;

class CPortal : public CEntity
{
  public:
    CPortal(CGameWorld* world, florrbt::Vector2f pos, float radius, std::string target_world);

    void OnTickActions(float dt) override;
    void OnTickForces(float dt) override;
    void OnTickActionRequests(float dt) override;
    void OnTakeDamage(float, CEntity*, EDamageType) override {}
    bool CanCollide() const override { return false; }
    bool IsDead() const override { return false; }

    const std::string& GetTargetWorldName() const { return m_target_world_name; }
    const std::string& GetFromPointName() const { return m_from_point_name; }
    void SetFromPointName(std::string from_point_name) { m_from_point_name = std::move(from_point_name); }

  private:
    void UpdatePlayerContact(CPlayer& player, float dt, bool transfer);

    std::string m_target_world_name;
    std::string m_from_point_name;
};
