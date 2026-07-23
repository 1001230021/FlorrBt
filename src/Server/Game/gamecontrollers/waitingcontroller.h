#pragma once
#include "../gamecontroller.h"
#include <cstddef>
#include <functional>
#include <vector>

class CWaitingController : public IGameController
{
  public:
    using release_predicate = std::function<bool(CGameWorld& waiting_world, size_t player_count)>;

    CWaitingController(release_predicate predicate, CGameWorld* destination_world);
    CWaitingController(std::function<bool(size_t player_count)> predicate, CGameWorld* destination_world);

    void OnTick(CGameWorld& world, float dt) override;
    void OnPlayerConnect(CGameWorld& world, CPlayer* player) override;
    void OnPlayerSpawn(CGameWorld& world, CPlayer* player, CEntity* entity) override;
    void OnEntityDie(CGameWorld& world, CEntity* entity) override;

  private:
    size_t CountWaitingPlayers(CGameWorld& world) const;
    std::vector<CPlayer*> CollectWaitingPlayers(CGameWorld& world) const;
    void SetPlayerPetalsBanned(CPlayer* player, bool banned) const;
    void MaintainPetalBans(CGameWorld& world) const;
    void ReleasePlayers(CGameWorld& world);

    release_predicate m_release_predicate;
    CGameWorld* m_p_destination_world = nullptr;
};
