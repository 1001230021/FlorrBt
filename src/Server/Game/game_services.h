#pragma once

#include "../../Shared/rarity.h"
#include "../../Shared/math_types.h"
#include <cstdint>
#include <string_view>
#include <string>
#include <vector>
#include <memory>

class CGameWorld;
class CPlayer;
enum class ESkillRequestResult : std::uint8_t;
enum class EPetalType : int;
struct SDeferredWorldTransfer;

class IUniquePetalAccess
{
  public:
    virtual ~IUniquePetalAccess() = default;
    virtual bool IsUniquePetalOwned(EPetalType) const = 0;
    virtual bool ClaimUniquePetal(CPlayer&, EPetalType) = 0;
};

class IPlayerDirectory
{
  public:
    virtual ~IPlayerDirectory() = default;
    virtual const std::vector<std::unique_ptr<CPlayer>>& GetPlayers() const = 0;
    virtual CPlayer* FindPlayerById(std::uint32_t id) const = 0;
};

class IPlayerUpdates
{
  public:
    virtual ~IPlayerUpdates() = default;
    virtual bool QueueOwnerStateUpdate(CPlayer&) = 0;
    virtual bool QueueInventoryUpdate(CPlayer&) = 0;
    virtual void SendCodexDelta(CPlayer&, std::uint8_t mob_type, std::uint8_t rarity, std::uint32_t count) = 0;
    virtual bool QueueSkillResult(CPlayer&, ESkillRequestResult, std::uint8_t skill_index, std::uint8_t skill_count) = 0;
};

// 广播必须带来源世界，范围校验由投递方统一执行。
class IWorldEffects
{
  public:
    virtual ~IWorldEffects() = default;
    virtual void BroadcastLightningChain(CGameWorld&, const std::vector<florrbt::Vector2f>&, std::uint8_t style = 0) = 0;
    virtual void BroadcastTerrainDelta(CGameWorld&, std::uint8_t layer, std::uint16_t row, std::uint16_t col, std::uint32_t gid) = 0;
    virtual void BroadcastTerrainWallRemove(CGameWorld&, florrbt::Vector2f min, florrbt::Vector2f max) = 0;
};

class ISceneTransitions
{
  public:
    virtual ~ISceneTransitions() = default;
    virtual void NotifyPlayerWorldChanged(CPlayer&) = 0;
    virtual void NotifyPlayerStateChanged(CPlayer&) = 0;
    virtual void NotifyPlayerRulesChanged(CPlayer&) = 0;
};

// 网络在场景事务中绑定单一接收者，玩法不能借此向其他世界发送地图数据。
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
    virtual bool ReportPetal(std::string_view action, ERarity rarity, std::string_view petal_name, std::string_view player_name) = 0;
};

class IGameWorldResolver
{
  public:
    virtual ~IGameWorldResolver() = default;
    virtual CGameWorld* FindWorldById(std::uint32_t world_id) const = 0;
    virtual CGameWorld* GetMainWorld() const = 0;
    virtual std::vector<CGameWorld*> GetWorlds() const = 0;
    virtual std::vector<CGameWorld*> FindWorldsByMapName(const std::string&) const = 0;
    virtual CGameWorld* CreateRoomGameWorld(int room_code) = 0;
    virtual std::uint64_t GetTick() const = 0;
    virtual bool DeferWorldTransfer(const SDeferredWorldTransfer&) { return false; }
};
