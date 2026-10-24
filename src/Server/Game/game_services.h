#pragma once

#include "../../Shared/rarity.h"
#include "../../Shared/math_types.h"
#include <cstdint>
#include <string_view>

class CGameWorld;
struct SDeferredWorldTransfer;

// Bound to one recipient by the network during an atomic scene baseline.
// Gameplay code cannot select a player in another world through this API.
class IPlayerSceneWriter
{
  public:
    virtual ~IPlayerSceneWriter() = default;
    virtual bool TerrainTile(std::uint8_t layer, std::uint16_t row, std::uint16_t col, std::uint32_t gid) = 0;
    virtual bool RemoveWall(florrbt::Vector2f min, florrbt::Vector2f max) = 0;
};

class IGameEventSink
{
  public:
    virtual ~IGameEventSink() = default;

    virtual bool ShouldReportRarity(ERarity rarity, int minimum_rarity) const = 0;
    virtual bool ReportMob(std::string_view action, ERarity rarity, std::string_view mob_name) = 0;
    virtual bool ReportMobSpawn(CGameWorld& source_world, std::string_view action, ERarity rarity,
                                std::string_view mob_name) = 0;
};

class IGameWorldResolver
{
  public:
    virtual ~IGameWorldResolver() = default;
    virtual CGameWorld* FindWorldById(std::uint32_t world_id) const = 0;
    virtual CGameWorld* GetMainWorld() const = 0;
    virtual bool DeferWorldTransfer(const SDeferredWorldTransfer&) { return false; }
};
