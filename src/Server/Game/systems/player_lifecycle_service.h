#pragma once

#include <memory>
#include <vector>

class CEntity;
class CGameWorld;
class CPlayer;

class IPlayerLifecycleNotifier
{
  public:
    virtual ~IPlayerLifecycleNotifier() = default;

    virtual bool QueueWelcome(CPlayer& player) = 0;
    virtual bool QueueOwnerStateUpdate(CPlayer& player) = 0;
    virtual bool QueueInventory(CPlayer& player) = 0;
};

class CPlayerLifecycleService
{
  public:
    void ProcessDropPickups(const std::vector<std::unique_ptr<CPlayer>>& players,
                            IPlayerLifecycleNotifier& notifier) const;
    CEntity* Respawn(CPlayer& player, CGameWorld& world) const;
    void RespawnDeadControlledEntities(const std::vector<std::unique_ptr<CPlayer>>& players,
                                       CGameWorld& respawn_world,
                                       IPlayerLifecycleNotifier& notifier) const;
    void NotifyPlayerLogin(CPlayer& player, IPlayerLifecycleNotifier& notifier) const;
    void NotifyPlayerWorldChanged(CPlayer& player, IPlayerLifecycleNotifier& notifier) const;
};
