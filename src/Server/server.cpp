#include "server.h"
#include "Module/console_module.h"
#include "Module/world_module.h"

CServer* CServer::s_pInstance = nullptr;

CServer::CServer()
{
    s_pInstance = this;
    m_Modules.emplace_back(std::make_unique<IConsoleModule>());
    m_Modules.emplace_back(std::make_unique<IWorldModule>());
}

CServer::~CServer()
{
    s_pInstance = nullptr;
}

void CServer::Init()
{
    for (auto& m : m_Modules)
        if (!m->Init())
        {
            m_Running = false;
            return;
        }
}

void CServer::Run()
{
    const float dt = 0.016f;
    while (m_Running)
        for (auto& m : m_Modules)
            m->Tick(dt);
}

void CServer::ShutDown()
{
    for (auto& m : m_Modules)
        m->ShutDown();
    m_Running = false;
}