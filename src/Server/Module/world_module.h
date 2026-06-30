#pragma once
#include "module.h"
#include "../Game/gameworld.h"
#include <memory>

class IWorldModule : public IModule {
  public:
    virtual bool Init() override;
    virtual void Tick(float dt) override;
    virtual void ShutDown() override;
    const std::vector<std::unique_ptr<CGameWorld>>& GetWorlds() const { return m_Worlds; }

  private:
    std::vector<std::unique_ptr<CGameWorld>> m_Worlds;
};
