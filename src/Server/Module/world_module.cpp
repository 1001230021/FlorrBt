#include "world_module.h"
#include "../Game/gamecontrollers/opencontroller.h"
#include "../../Shared/game_config.h"

IWorldModule::IWorldModule()
{
    auto world = std::make_unique<CGameWorld>(game_config::lobby_map_path);
    auto controller = std::make_unique<COpenController>();
    world->SetController(std::move(controller));
    m_worlds.emplace_back(std::move(world));
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
