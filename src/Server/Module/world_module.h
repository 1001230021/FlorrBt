#pragma once
#include "../Game/gameworld.h"
#include "module.h"
#include <memory>
#include <string>
#include <vector>

class IWorldModule : public IModule
{
  public:
    IWorldModule();
    bool Init() override;
    void Tick(float dt) override;
    void ShutDown() override;
    const std::vector<std::unique_ptr<CGameWorld>>& GetWorlds() const { return m_worlds; }
    std::vector<CGameWorld*> FindWorldsByMapName(const std::string& map_name) const;
    CGameWorld* FindRandomWorldByMapName(const std::string& map_name) const;

  private:
    std::vector<std::unique_ptr<CGameWorld>> m_worlds;
};
