#pragma once
#include "../../Shared/talent_type.h"
#include <vector>

class CGameWorld; 
class CEntity;
class CPlayer;
struct STalentContext;

class IGameController
{
  public:
    virtual ~IGameController() = default; 
    virtual void OnTick(CGameWorld& world, float dt) = 0;
    virtual void OnPlayerConnect(CGameWorld& world, CPlayer* player) = 0;
    virtual void OnPlayerSpawn(CGameWorld& world, CPlayer* player, CEntity* entity) = 0;
    virtual void OnEntityDie(CGameWorld& world, CEntity* entity) = 0;
    virtual void ModifyTalentContext(CGameWorld& world, CPlayer* player, ETalentEvent event, STalentContext& ctx)
    {
        (void)world;
        (void)player;
        (void)event;
        (void)ctx;
    }
};
