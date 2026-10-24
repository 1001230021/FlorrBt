#pragma once

class CGameWorld;
class CMobBase;

class COpenRewardService
{
  public:
    static void OnMobSpawned(CGameWorld& world, CMobBase& mob);
    static void OnMobDefeated(CGameWorld& world, CMobBase& mob);
};
