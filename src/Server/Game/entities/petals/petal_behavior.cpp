#include "petals_behavior.h"
#include <mutex>

std::once_flag g_air_registered;
void RegisterAir()
{
    std::call_once(g_air_registered, []()
    {
        CPetalPrototype proto;
        proto.m_type = EPetalType::Air;
        proto.m_name = "Air";
        proto.m_base_radius = 0.f;
        proto.m_p_behavior = std::make_unique<CAirBehavior>();
        REGISTER_PETAL(EPetalType::Air, CPetal, proto);
    });
}

std::once_flag g_dust_registered;
void RegisterDust()
{
    std::call_once(g_dust_registered, []()
    {
        CPetalPrototype proto;
        proto.m_type = EPetalType::Dust;
        proto.m_name = "Dust";
        proto.m_base_radius = game_config::default_dust_base_radius;
        proto.m_p_behavior = std::make_unique<CDustBehavior>();
        REGISTER_PETAL(EPetalType::Dust, CPetal, proto);
    });
}

std::once_flag g_golden_leaf_registered;
void RegisterGoldenLeaf()
{
    std::call_once(g_golden_leaf_registered, []()
    {
        CPetalPrototype proto;
        proto.m_type = EPetalType::GoldenLeaf;
        proto.m_name = "GoldenLeaf";
        proto.m_base_radius = game_config::default_goldenleaf_base_radius;
        proto.m_p_behavior = std::make_unique<CGoldenLeafBehavior>();
        REGISTER_PETAL(EPetalType::GoldenLeaf, CPetal, proto);
    });
}

void RegisterPetals()
{
    RegisterAir();
    RegisterDust();
    RegisterGoldenLeaf();
}
