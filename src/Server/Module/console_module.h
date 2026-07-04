#pragma once
#include "../../Engine/console.h"
#include "module.h"
#include <string>

class IConsoleModule : public IModule
{
  public:
    explicit IConsoleModule(CConsole& console) : m_console(console) {}

    bool Init() override;
    void Tick(float dt) override;
    void ShutDown() override;
    CConsole& GetConsole() { return m_console; }

  private:
    bool TryReadLine(std::string& line);

    CConsole& m_console;
    std::string m_pending_line;
};
