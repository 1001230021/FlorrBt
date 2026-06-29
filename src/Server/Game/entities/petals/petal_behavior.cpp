#include <mutex>
#include "petals_behavior.h"

std::once_flag g_AirRegistered;
void RegisterAir()
{
    std::call_once(g_AirRegistered, []() {
        CPetalPrototype proto;
        proto.m_Type = EPetalType::Air;
        proto.m_Name = "Air";
        proto.m_BaseRadius = 0;
        proto.m_pBehavior = std::make_unique<CAirBehavior>();
        REGISTER_PETAL(EPetalType::Air, CPetal, proto);
        });
}

std::once_flag g_DustRegistered;
void RegisterDust()
{
    std::call_once(g_DustRegistered, []() {
        CPetalPrototype proto;
        proto.m_Type = EPetalType::Dust; 
        proto.m_Name = "Dust";
        proto.m_BaseRadius = 7.5f;
        proto.m_pBehavior = std::make_unique<CDustBehavior>();
        REGISTER_PETAL(EPetalType::Dust, CPetal, proto);
        });
}

std::once_flag g_GoldenLeafRegistered;
void RegisterGoldenLeaf()
{
    std::call_once(g_GoldenLeafRegistered, []() {
        CPetalPrototype proto;
        proto.m_Type = EPetalType::GoldenLeaf;
        proto.m_Name = "GoldenLeaf";
        proto.m_BaseRadius = 15.0f;
        proto.m_pBehavior = std::make_unique<CGoldenLeafBehavior>();
        REGISTER_PETAL(EPetalType::GoldenLeaf, CPetal, proto);
        });
}