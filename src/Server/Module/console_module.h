#pragma once
#include "module.h"
#include "../../Engine/console.h"

class IConsoleModule : public IModule {
  public:
    virtual bool Init() override;
    virtual void Tick(float dt) override;
    virtual void ShutDown() override;
    CConsole& GetConsole() { return m_Console; }

  private:
    CConsole m_Console;
};