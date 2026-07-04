#include "server.h"
#include "Game/entities/mob.h"
#include "Game/entities/petals/petals_behavior.h"
#include "Module/console_module.h"
#include "Module/network_module.h"
#include "Module/world_module.h"
#include <stdexcept>
#include <SFML/System/Clock.hpp>
#include <SFML/System/Sleep.hpp>

CServer* CServer::s_p_instance = nullptr;

CServer::CServer()
{
    s_p_instance = this;
    m_modules.emplace_back(std::make_unique<IConsoleModule>());

    auto world_mod = std::make_unique<IWorldModule>();
    auto& worlds = world_mod->GetWorlds();
    if (worlds.empty())
    {
        throw std::runtime_error("IWorldModule returned no worlds");
    }

    CGameWorld& world = *worlds[0];
    m_modules.emplace_back(std::move(world_mod));
    m_modules.emplace_back(std::make_unique<INetworkModule>(world));
}

CServer::~CServer()
{
    s_p_instance = nullptr;
}

void CServer::Init()
{
    RegisterPetals();
    RegisterMobs();

    for (auto& module : m_modules)
    {
        if (!module->Init())
        {
            m_running = false;
            return;
        }
    }
}

void CServer::Run()
{
    const float dt = 0.016f;
    const sf::Time targetFrameTime = sf::seconds(dt);

    while (m_running)
    {
        sf::Clock frameClock;

        for (auto& module : m_modules)
            module->Tick(dt);

        sf::Time elapsed = frameClock.getElapsedTime();
        if (elapsed < targetFrameTime) sf::sleep(targetFrameTime - elapsed);
    }
}

void CServer::ShutDown()
{
    for (auto& module : m_modules)
    {
        module->ShutDown();
    }
    m_running = false;
}
