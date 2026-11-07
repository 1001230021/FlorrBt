#include "mutable_terrain_controller.h"
#include "../gameworld.h"

bool CMutableTerrainController::PrepareActivation(CWorldPreparation& preparation, std::string& error) const
{
    return preparation.RequireTerrain(error);
}

void CMutableTerrainController::OnTick(CGameWorld& world, float dt)
{
    COpenController::OnTick(world, dt);
    world.Terrain().TickTools(dt);
}

int CMutableTerrainController::PaintTerrain(CGameWorld& world, florrbt::Vector2f center, float size_tiles)
{
    return world.Terrain().PaintTerrain(center, size_tiles);
}

int CMutableTerrainController::EraseTerrain(CGameWorld& world, florrbt::Vector2f center, float size_tiles)
{
    return world.Terrain().EraseTerrain(center, size_tiles);
}

int CMutableTerrainController::DeleteTile(CGameWorld& world, int row, int col)
{
    return world.Terrain().DeleteTile(row, col);
}
