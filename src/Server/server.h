#pragma once
#include "../Engine/console.h"
#include "Module/module.h"
#include <memory>
#include <vector>

class CServer
{
  public:
    static CServer* GetInstance() { return s_p_instance; }
    CServer();
    ~CServer();

    void Init();
    void Run();
    void ShutDown();
    void RequestStop() { m_running = false; }
    bool IsRunning() const { return m_running; }
    CConsole& GetConsole() { return m_console; }

  private:
    static CServer* s_p_instance;
    CConsole m_console;
    std::vector<std::unique_ptr<IModule>> m_modules;
    bool m_running = true;
};
