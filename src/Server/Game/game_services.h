#pragma once

#include "../../Shared/rarity.h"
#include <cstdint>
#include <string_view>

class CGameWorld;

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
};
