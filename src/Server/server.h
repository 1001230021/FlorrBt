#pragma once
#include <memory>
#include <vector>
#include "Module/module.h"

class CServer {
public:
    static CServer* GetInstance() { return s_pInstance; }
    CServer();
    ~CServer();
    void Init();
    void Run();
    void ShutDown();
    bool IsRunning() const { return m_Running; }

private:
    static CServer* s_pInstance;
    std::vector<std::unique_ptr<IModule>> m_Modules;
    bool m_Running = true;
};