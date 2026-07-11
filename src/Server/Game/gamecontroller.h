#pragma once
#include <vector>

class CGameWorld; 
class CEntity;
class CPlayer;

class IGameController
{
  public:
    virtual ~IGameController() = default; 
    virtual void OnTick(CGameWorld& world, float dt) = 0;
    virtual void OnPlayerConnect(CGameWorld& world, CPlayer* player) = 0;
    virtual void OnEntityDie(CGameWorld& world, CEntity* entity) = 0;
};
