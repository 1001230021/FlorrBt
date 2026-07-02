#include "world_module.h"

IWorldModule::IWorldModule()
{
    m_worlds.emplace_back(std::make_unique<CGameWorld>());
}

bool IWorldModule::Init()
{
    return true;
}

void IWorldModule::Tick(float dt)
{
    for (const auto& world : m_worlds)
    {
        if (world) world->Tick(dt);
    }
}

void IWorldModule::ShutDown() {}