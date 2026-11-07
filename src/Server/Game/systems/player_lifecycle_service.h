#pragma once

#include <cstdint>
#include <memory>
#include <vector>

class CEntity;
class CGameWorld;
class CPlayer;
enum class EPlayerSpawnReason : std::uint8_t;

class IPlayerLifecycleNotifier
{
  public:
    virtual ~IPlayerLifecycleNotifier() = default;

    // 原子初始化 Welcome、背包、拥有者和地形基线；失败保留重试并屏蔽普通快照。
    virtual bool QueueWelcome(CPlayer& player) = 0;
    // 同场景更换身体只更新玩家基线，不重放地图和地形。
    virtual bool QueuePlayerState(CPlayer& player) = 0;
    virtual bool QueueOwnerStateUpdate(CPlayer& player) = 0;
    virtual bool QueueInventory(CPlayer& player) = 0;
};

class CPlayerLifecycleService
{
  public:
    void ProcessDropPickups(const std::vector<std::unique_ptr<CPlayer>>& players,
                            IPlayerLifecycleNotifier& notifier) const;
    CEntity* SpawnPlayer(CPlayer& player, CGameWorld& world, EPlayerSpawnReason reason) const;
    bool RespawnPlayer(CPlayer& player, CGameWorld& world, IPlayerLifecycleNotifier& notifier) const;
    void RespawnDeadControlledEntities(const std::vector<std::unique_ptr<CPlayer>>& players, CGameWorld& respawn_world,
                                       IPlayerLifecycleNotifier& notifier) const;
    void NotifyPlayerLogin(CPlayer& player, IPlayerLifecycleNotifier& notifier) const;
    void NotifyPlayerWorldChanged(CPlayer& player, IPlayerLifecycleNotifier& notifier) const;
};
