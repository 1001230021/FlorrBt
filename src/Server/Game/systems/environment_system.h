#pragma once
#include "../../../Shared/math_types.h"
#include <optional>

class CGameWorld;
class CMobBase;

// Owned by the world. No controller/region/entity pointers survive a call.
class CEnvironmentSystem
{
  public:
    void Synchronize(CGameWorld& world, CMobBase& mob,
                     std::optional<florrbt::Vector2f> movement_start = std::nullopt) const;
};
