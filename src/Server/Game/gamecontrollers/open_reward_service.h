#pragma once

class CGameWorld;
class CMobBase;
class COpenController;

class COpenRewardService
{
  public:
    static void OnMobSpawned(CGameWorld& world, CMobBase& mob);
    static void OnMobDefeated(COpenController& controller, CGameWorld& world, CMobBase& mob);
};
