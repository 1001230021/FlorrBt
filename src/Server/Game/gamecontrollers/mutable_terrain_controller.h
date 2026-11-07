#pragma once
#include "opencontroller.h"

// 模式只决定地形工具是否可用；实际地形及回放历史由世界管理。
class CMutableTerrainController : public COpenController
{
  public:
    bool ForcesPrimordialNullification() const override { return true; }
    bool PrepareActivation(CWorldPreparation& preparation, std::string& error) const override;
    void OnTick(CGameWorld& world, float dt) override;
    std::string_view SnapshotKey() const override { return "world_controller.mutable_terrain"; }
    std::uint32_t SnapshotVersion() const override { return 1; }
    int PaintTerrain(CGameWorld& world, florrbt::Vector2f center, float size_tiles = 2.f);
    int EraseTerrain(CGameWorld& world, florrbt::Vector2f center, float size_tiles = 2.f);
    int DeleteTile(CGameWorld& world, int row, int col);
};
