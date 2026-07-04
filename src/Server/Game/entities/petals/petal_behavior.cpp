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

std::once_flag g_basic_registered;
void RegisterBasic()
{
    std::call_once(g_basic_registered, []()
    {
        CPetalPrototype proto;
        proto.m_type = EPetalType::Basic;
        proto.m_name = "Basic";
        proto.m_base_radius = game_config::default_basic_base_radius;
        proto.m_p_behavior = std::make_unique<CBasicBehavior>();
        REGISTER_PETAL(EPetalType::Basic, CPetal, proto);
    });
}

std::once_flag g_beetle_egg_registered;
void RegisterBeetleEgg()
{
    std::call_once(g_beetle_egg_registered, []()
    {
        CPetalPrototype proto;
        proto.m_type = EPetalType::BeetleEgg;
        proto.m_name = "BeetleEgg";
        proto.m_base_radius = game_config::default_beetleegg_base_radius;
        proto.m_p_behavior = std::make_unique<CBeetleEggBehavior>();
        REGISTER_PETAL(EPetalType::BeetleEgg, CBeetleEggPetal, proto);
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

std::once_flag g_yin_yang_registered;
void RegisterYinYang()
{
    std::call_once(g_yin_yang_registered, []()
    {
        CPetalPrototype proto;
        proto.m_type = EPetalType::YinYang;
        proto.m_name = "YinYang";
        proto.m_base_radius = game_config::default_yinyang_base_radius;
        proto.m_p_behavior = std::make_unique<CYinYangBehavior>();
        REGISTER_PETAL(EPetalType::YinYang, CPetal, proto);
    });
}

void RegisterPetals()
{
    RegisterAir();
    RegisterBasic();
    RegisterBeetleEgg();
    RegisterDust();
    RegisterGoldenLeaf();
    RegisterYinYang();
}
