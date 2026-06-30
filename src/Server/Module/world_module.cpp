#include "world_module.h"

bool IWorldModule::Init()
{
    return true;
}
void IWorldModule::Tick(float dt)
{
    for (const auto& worldPtr : m_Worlds)
    {
        if (worldPtr)
            worldPtr->Tick(dt);
    }
}
void IWorldModule::ShutDown() {}