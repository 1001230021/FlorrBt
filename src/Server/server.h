#pragma once
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
    bool IsRunning() const { return m_running; }

  private:
    static CServer* s_p_instance;
    std::vector<std::unique_ptr<IModule>> m_modules;
    bool m_running = true;
};