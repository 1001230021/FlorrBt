#pragma once
#include "../Game/gameworld.h"
#include "module.h"
#include <memory>
#include <vector>

class IWorldModule : public IModule
{
  public:
    IWorldModule();
    bool Init() override;
    void Tick(float dt) override;
    void ShutDown() override;
    const std::vector<std::unique_ptr<CGameWorld>>& GetWorlds() const { return m_worlds; }

  private:
    std::vector<std::unique_ptr<CGameWorld>> m_worlds;
};