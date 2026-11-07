#pragma once
#include "../../../Shared/math_types.h"
#include <optional>

class CGameWorld;
class CMobBase;

// 由世界持有，不跨调用保留控制器、区域或实体指针。
class CEnvironmentSystem
{
  public:
    void Synchronize(CGameWorld& world, CMobBase& mob,
                     std::optional<florrbt::Vector2f> movement_start = std::nullopt) const;
};
