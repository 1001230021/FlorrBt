#include "petals_behavior.h"
#include <mutex>

std::once_flag g_air_registered;
void RegisterAir()
{
    std::call_once(g_air_registered, []()
    {
        CPetalPrototype proto;
        proto.m_type = EPetalType::Air;
        proto.m_name = std::string(GetPetalTypeName(proto.m_type));
        proto.m_base_radius = 0.f;
        proto.m_p_behavior = std::make_unique<CAirBehavior>();
        REGISTER_PETAL(EPetalType::Air, CPetal, proto);
    });
}

std::once_flag g_ant_egg_registered;
void RegisterAntEgg()
{
    std::call_once(g_ant_egg_registered, []()
    {
        CPetalPrototype proto;
        proto.m_type = EPetalType::AntEgg;
        proto.m_name = std::string(GetPetalTypeName(proto.m_type));
        proto.m_base_radius = game_config::default_antegg_base_radius;
        proto.m_p_behavior = std::make_unique<CAntEggBehavior>();
        REGISTER_PETAL(EPetalType::AntEgg, CBeetleEggPetal, proto);
    });
}

std::once_flag g_antennae_registered;
void RegisterAntennae()
{
    std::call_once(g_antennae_registered, []()
    {
        CPetalPrototype proto;
        proto.m_type = EPetalType::Antennae;
        proto.m_name = std::string(GetPetalTypeName(proto.m_type));
        proto.m_base_radius = 0.f;
        proto.m_p_behavior = std::make_unique<CAntennaeBehavior>();
        REGISTER_PETAL(EPetalType::Antennae, CPetal, proto);
    });
}

std::once_flag g_blood_sacrifice_registered;
void RegisterBloodSacrifice()
{
    std::call_once(g_blood_sacrifice_registered, []()
    {
        CPetalPrototype proto;
        proto.m_type = EPetalType::BloodSacrifice;
        proto.m_name = std::string(GetPetalTypeName(proto.m_type));
        proto.m_base_radius = game_config::default_blood_sacrifice_base_radius;
        proto.m_p_behavior = std::make_unique<CBloodSacrificeBehavior>();
        REGISTER_PETAL(EPetalType::BloodSacrifice, CPetal, proto);
    });
}

std::once_flag g_basic_registered;
void RegisterBasic()
{
    std::call_once(g_basic_registered, []()
    {
        CPetalPrototype proto;
        proto.m_type = EPetalType::Basic;
        proto.m_name = std::string(GetPetalTypeName(proto.m_type));
        proto.m_base_radius = game_config::default_basic_base_radius;
        proto.m_p_behavior = std::make_unique<CBasicBehavior>();
        REGISTER_PETAL(EPetalType::Basic, CPetal, proto);
    });
}

std::once_flag g_bandage_registered;
void RegisterBandage()
{
    std::call_once(g_bandage_registered, []()
    {
        CPetalPrototype proto;
        proto.m_type = EPetalType::Bandage;
        proto.m_name = std::string(GetPetalTypeName(proto.m_type));
        proto.m_base_radius = game_config::default_bandage_base_radius;
        proto.m_p_behavior = std::make_unique<CBandageBehavior>();
        REGISTER_PETAL(EPetalType::Bandage, CPetal, proto);
    });
}

std::once_flag g_beetle_egg_registered;
void RegisterBeetleEgg()
{
    std::call_once(g_beetle_egg_registered, []()
    {
        CPetalPrototype proto;
        proto.m_type = EPetalType::BeetleEgg;
        proto.m_name = std::string(GetPetalTypeName(proto.m_type));
        proto.m_base_radius = game_config::default_beetleegg_base_radius;
        proto.m_p_behavior = std::make_unique<CBeetleEggBehavior>();
        REGISTER_PETAL(EPetalType::BeetleEgg, CBeetleEggPetal, proto);
    });
}

std::once_flag g_broken_egg_registered;
void RegisterBrokenEgg()
{
    std::call_once(g_broken_egg_registered, []()
    {
        CPetalPrototype proto;
        proto.m_type = EPetalType::BrokenEgg;
        proto.m_name = std::string(GetPetalTypeName(proto.m_type));
        proto.m_base_radius = game_config::default_beetleegg_base_radius;
        proto.m_p_behavior = std::make_unique<CBrokenEggBehavior>();
        REGISTER_PETAL(EPetalType::BrokenEgg, CBrokenEggPetal, proto);
    });
}

std::once_flag g_bubble_registered;
void RegisterBubble()
{
    std::call_once(g_bubble_registered, []()
    {
        CPetalPrototype proto;
        proto.m_type = EPetalType::Bubble;
        proto.m_name = std::string(GetPetalTypeName(proto.m_type));
        proto.m_base_radius = game_config::default_bubble_base_radius;
        proto.m_p_behavior = std::make_unique<CBubbleBehavior>();
        REGISTER_PETAL(EPetalType::Bubble, CPetal, proto);
    });
}

std::once_flag g_bone_registered;
void RegisterBone()
{
    std::call_once(g_bone_registered, []()
    {
        CPetalPrototype proto;
        proto.m_type = EPetalType::Bone;
        proto.m_name = std::string(GetPetalTypeName(proto.m_type));
        proto.m_base_radius = game_config::default_basic_base_radius;
        proto.m_p_behavior = std::make_unique<CBoneBehavior>();
        REGISTER_PETAL(EPetalType::Bone, CPetal, proto);
    });
}

std::once_flag g_coin_registered;
void RegisterCarrot()
{
    static std::once_flag g_carrot_registered;
    std::call_once(g_carrot_registered, []()
    {
        CPetalPrototype proto;
        proto.m_type = EPetalType::Carrot;
        proto.m_name = std::string(GetPetalTypeName(proto.m_type));
        proto.m_base_radius = game_config::default_carrot_base_radius;
        proto.m_p_behavior = std::make_unique<CCarrotBehavior>();
        REGISTER_PETAL(EPetalType::Carrot, CMissilePetal, proto);
    });
}

void RegisterCoin()
{
    std::call_once(g_coin_registered, []()
    {
        CPetalPrototype proto;
        proto.m_type = EPetalType::Coin;
        proto.m_name = std::string(GetPetalTypeName(proto.m_type));
        proto.m_base_radius = game_config::default_basic_base_radius;
        proto.m_p_behavior = std::make_unique<CCoinBehavior>();
        REGISTER_PETAL(EPetalType::Coin, CPetal, proto);
    });
}

std::once_flag g_compass_registered;
void RegisterCompass()
{
    std::call_once(g_compass_registered, []()
    {
        CPetalPrototype proto;
        proto.m_type = EPetalType::Compass;
        proto.m_name = std::string(GetPetalTypeName(proto.m_type));
        proto.m_base_radius = game_config::default_compass_base_radius;
        proto.m_p_behavior = std::make_unique<CCompassBehavior>();
        REGISTER_PETAL(EPetalType::Compass, CCompassPetal, proto);
    });
}

std::once_flag g_cogwheel_registered;
void RegisterCogwheel()
{
    std::call_once(g_cogwheel_registered, []()
    {
        CPetalPrototype proto;
        proto.m_type = EPetalType::Cogwheel;
        proto.m_name = std::string(GetPetalTypeName(proto.m_type));
        proto.m_base_radius = game_config::default_cogwheel_base_radius;
        proto.m_p_behavior = std::make_unique<CCogwheelBehavior>();
        REGISTER_PETAL(EPetalType::Cogwheel, CPetal, proto);
    });
}

std::once_flag g_corruption_registered;
void RegisterCorruption()
{
    std::call_once(g_corruption_registered, []()
    {
        CPetalPrototype proto;
        proto.m_type = EPetalType::Corruption;
        proto.m_name = std::string(GetPetalTypeName(proto.m_type));
        proto.m_base_radius = game_config::default_corruption_base_radius;
        proto.m_p_behavior = std::make_unique<CCorruptionBehavior>();
        REGISTER_PETAL(EPetalType::Corruption, CPetal, proto);
    });
}

std::once_flag g_dahlia_registered;
void RegisterDahlia()
{
    std::call_once(g_dahlia_registered, []()
    {
        CPetalPrototype proto;
        proto.m_type = EPetalType::Dahlia;
        proto.m_name = std::string(GetPetalTypeName(proto.m_type));
        proto.m_base_radius = game_config::default_rose_base_radius;
        proto.m_p_behavior = std::make_unique<CDahliaBehavior>();
        REGISTER_PETAL(EPetalType::Dahlia, CPetal, proto);
    });
}

std::once_flag g_dust_registered;
void RegisterDust()
{
    std::call_once(g_dust_registered, []()
    {
        CPetalPrototype proto;
        proto.m_type = EPetalType::Dust;
        proto.m_name = std::string(GetPetalTypeName(proto.m_type));
        proto.m_base_radius = game_config::default_dust_base_radius;
        proto.m_p_behavior = std::make_unique<CDustBehavior>();
        REGISTER_PETAL(EPetalType::Dust, CPetal, proto);
    });
}

std::once_flag g_fragment_registered;
void RegisterFragment()
{
    std::call_once(g_fragment_registered, []()
    {
        CPetalPrototype proto;
        proto.m_type = EPetalType::Fragment;
        proto.m_name = std::string(GetPetalTypeName(proto.m_type));
        proto.m_base_radius = game_config::default_basic_base_radius;
        proto.m_p_behavior = std::make_unique<CFragmentBehavior>();
        REGISTER_PETAL(EPetalType::Fragment, CPetal, proto);
    });
}

std::once_flag g_golden_leaf_registered;
void RegisterGoldenLeaf()
{
    std::call_once(g_golden_leaf_registered, []()
    {
        CPetalPrototype proto;
        proto.m_type = EPetalType::GoldenLeaf;
        proto.m_name = std::string(GetPetalTypeName(proto.m_type));
        proto.m_base_radius = game_config::default_goldenleaf_base_radius;
        proto.m_p_behavior = std::make_unique<CGoldenLeafBehavior>();
        REGISTER_PETAL(EPetalType::GoldenLeaf, CPetal, proto);
    });
}

std::once_flag g_glass_registered;
void RegisterGlass()
{
    std::call_once(g_glass_registered, []()
    {
        CPetalPrototype proto;
        proto.m_type = EPetalType::Glass;
        proto.m_name = std::string(GetPetalTypeName(proto.m_type));
        proto.m_base_radius = game_config::default_basic_base_radius;
        proto.m_p_behavior = std::make_unique<CGlassBehavior>();
        REGISTER_PETAL(EPetalType::Glass, CGlassPetal, proto);
    });
}

std::once_flag g_stinger_registered;
void RegisterStinger()
{
    std::call_once(g_stinger_registered, []()
    {
        CPetalPrototype proto;
        proto.m_type = EPetalType::Stinger;
        proto.m_name = std::string(GetPetalTypeName(proto.m_type));
        proto.m_base_radius = game_config::default_stinger_base_radius;
        proto.m_p_behavior = std::make_unique<CStingerBehavior>();
        REGISTER_PETAL(EPetalType::Stinger, CPetal, proto);
    });
}

std::once_flag g_heavy_registered;
void RegisterHeavy()
{
    std::call_once(g_heavy_registered, []()
    {
        CPetalPrototype proto;
        proto.m_type = EPetalType::Heavy;
        proto.m_name = std::string(GetPetalTypeName(proto.m_type));
        proto.m_base_radius = game_config::default_heavy_base_radius;
        proto.m_p_behavior = std::make_unique<CHeavyBehavior>();
        REGISTER_PETAL(EPetalType::Heavy, CPetal, proto);
    });
}

std::once_flag g_iris_registered;
void RegisterIris()
{
    std::call_once(g_iris_registered, []()
    {
        CPetalPrototype proto;
        proto.m_type = EPetalType::Iris;
        proto.m_name = std::string(GetPetalTypeName(proto.m_type));
        proto.m_base_radius = game_config::default_iris_base_radius;
        proto.m_p_behavior = std::make_unique<CIrisBehavior>();
        REGISTER_PETAL(EPetalType::Iris, CPetal, proto);
    });
}

std::once_flag g_faster_registered;
void RegisterFaster()
{
    std::call_once(g_faster_registered, []()
    {
        CPetalPrototype proto;
        proto.m_type = EPetalType::Faster;
        proto.m_name = std::string(GetPetalTypeName(proto.m_type));
        proto.m_base_radius = game_config::default_faster_base_radius;
        proto.m_p_behavior = std::make_unique<CFasterBehavior>();
        REGISTER_PETAL(EPetalType::Faster, CPetal, proto);
    });
}

std::once_flag g_leaf_registered;
void RegisterLeaf()
{
    std::call_once(g_leaf_registered, []()
    {
        CPetalPrototype proto;
        proto.m_type = EPetalType::Leaf;
        proto.m_name = std::string(GetPetalTypeName(proto.m_type));
        proto.m_base_radius = game_config::default_leaf_base_radius;
        proto.m_p_behavior = std::make_unique<CLeafBehavior>();
        REGISTER_PETAL(EPetalType::Leaf, CPetal, proto);
    });
}

std::once_flag g_lentil_registered;
void RegisterLentil()
{
    std::call_once(g_lentil_registered, []()
    {
        CPetalPrototype proto;
        proto.m_type = EPetalType::Lentil;
        proto.m_name = std::string(GetPetalTypeName(proto.m_type));
        proto.m_base_radius = game_config::default_lentil_base_radius;
        proto.m_p_behavior = std::make_unique<CLentilBehavior>();
        REGISTER_PETAL(EPetalType::Lentil, CPetal, proto);
    });
}

std::once_flag g_light_registered;
void RegisterLight()
{
    std::call_once(g_light_registered, []()
    {
        CPetalPrototype proto;
        proto.m_type = EPetalType::Light;
        proto.m_name = std::string(GetPetalTypeName(proto.m_type));
        proto.m_base_radius = game_config::default_light_base_radius;
        proto.m_p_behavior = std::make_unique<CLightBehavior>();
        REGISTER_PETAL(EPetalType::Light, CPetal, proto);
    });
}

std::once_flag g_corn_registered;
void RegisterCorn()
{
    std::call_once(g_corn_registered, []()
    {
        CPetalPrototype proto;
        proto.m_type = EPetalType::Corn;
        proto.m_name = std::string(GetPetalTypeName(proto.m_type));
        proto.m_base_radius = game_config::default_corn_base_radius;
        proto.m_p_behavior = std::make_unique<CCornBehavior>();
        REGISTER_PETAL(EPetalType::Corn, CPetal, proto);
    });
}

std::once_flag g_rice_registered;
void RegisterRice()
{
    std::call_once(g_rice_registered, []()
    {
        CPetalPrototype proto;
        proto.m_type = EPetalType::Rice;
        proto.m_name = std::string(GetPetalTypeName(proto.m_type));
        proto.m_base_radius = game_config::default_rice_base_radius;
        proto.m_p_behavior = std::make_unique<CRiceBehavior>();
        REGISTER_PETAL(EPetalType::Rice, CPetal, proto);
    });
}

std::once_flag g_pollen_registered;
void RegisterPollen()
{
    std::call_once(g_pollen_registered, []()
    {
        CPetalPrototype proto;
        proto.m_type = EPetalType::Pollen;
        proto.m_name = std::string(GetPetalTypeName(proto.m_type));
        proto.m_base_radius = game_config::default_pollen_base_radius;
        proto.m_p_behavior = std::make_unique<CPollenBehavior>();
        REGISTER_PETAL(EPetalType::Pollen, CThrownPetal, proto);
    });
}

std::once_flag g_honey_registered;
void RegisterHoney()
{
    std::call_once(g_honey_registered, []()
    {
        CPetalPrototype proto;
        proto.m_type = EPetalType::Honey;
        proto.m_name = std::string(GetPetalTypeName(proto.m_type));
        proto.m_base_radius = game_config::default_honey_base_radius;
        proto.m_p_behavior = std::make_unique<CHoneyBehavior>();
        REGISTER_PETAL(EPetalType::Honey, CThrownPetal, proto);
    });
}

std::once_flag g_mimic_registered;
void RegisterMimic()
{
    std::call_once(g_mimic_registered, []()
    {
        CPetalPrototype proto;
        proto.m_type = EPetalType::Mimic;
        proto.m_name = std::string(GetPetalTypeName(proto.m_type));
        proto.m_base_radius = game_config::default_basic_base_radius;
        proto.m_p_behavior = std::make_unique<CMimicBehavior>();
        REGISTER_PETAL(EPetalType::Mimic, CPetal, proto);
    });
}

std::once_flag g_moon_registered;
void RegisterMoon()
{
    std::call_once(g_moon_registered, []()
    {
        CPetalPrototype proto;
        proto.m_type = EPetalType::Moon;
        proto.m_name = std::string(GetPetalTypeName(proto.m_type));
        proto.m_base_radius = game_config::default_flower_radius;
        proto.m_p_behavior = std::make_unique<CMoonBehavior>();
        REGISTER_PETAL(EPetalType::Moon, CPetal, proto);
    });
}

std::once_flag g_nullification_registered;
void RegisterNullification()
{
    std::call_once(g_nullification_registered, []()
    {
        CPetalPrototype proto;
        proto.m_type = EPetalType::Nullification;
        proto.m_name = std::string(GetPetalTypeName(proto.m_type));
        proto.m_base_radius = 0.f;
        proto.m_p_behavior = std::make_unique<CNullificationBehavior>();
        REGISTER_PETAL(EPetalType::Nullification, CPetal, proto);
    });
}

std::once_flag g_pincer_registered;
void RegisterPincer()
{
    std::call_once(g_pincer_registered, []()
    {
        CPetalPrototype proto;
        proto.m_type = EPetalType::Pincer;
        proto.m_name = std::string(GetPetalTypeName(proto.m_type));
        proto.m_base_radius = game_config::default_pincer_base_radius;
        proto.m_p_behavior = std::make_unique<CPincerBehavior>();
        REGISTER_PETAL(EPetalType::Pincer, CPetal, proto);
    });
}

std::once_flag g_relic_registered;
void RegisterRelic()
{
    std::call_once(g_relic_registered, []()
    {
        CPetalPrototype proto;
        proto.m_type = EPetalType::Relic;
        proto.m_name = std::string(GetPetalTypeName(proto.m_type));
        proto.m_base_radius = game_config::default_relic_base_radius;
        proto.m_p_behavior = std::make_unique<CRelicBehavior>();
        REGISTER_PETAL(EPetalType::Relic, CRelicPetal, proto);
    });
}

std::once_flag g_basil_registered;
void RegisterBasil()
{
    std::call_once(g_basil_registered, []()
    {
        CPetalPrototype proto;
        proto.m_type = EPetalType::Basil;
        proto.m_name = std::string(GetPetalTypeName(proto.m_type));
        proto.m_base_radius = game_config::default_basil_base_radius;
        proto.m_p_behavior = std::make_unique<CBasilBehavior>();
        REGISTER_PETAL(EPetalType::Basil, CBasilPetal, proto);
    });
}

std::once_flag g_rock_petal_registered;
void RegisterRockPetal()
{
    std::call_once(g_rock_petal_registered, []()
    {
        CPetalPrototype proto;
        proto.m_type = EPetalType::Rock;
        proto.m_name = std::string(GetPetalTypeName(proto.m_type));
        proto.m_base_radius = game_config::default_rock_petal_base_radius;
        proto.m_p_behavior = std::make_unique<CRockPetalBehavior>();
        REGISTER_PETAL(EPetalType::Rock, CPetal, proto);
    });
}

std::once_flag g_rose_registered;
void RegisterRose()
{
    std::call_once(g_rose_registered, []()
    {
        CPetalPrototype proto;
        proto.m_type = EPetalType::Rose;
        proto.m_name = std::string(GetPetalTypeName(proto.m_type));
        proto.m_base_radius = game_config::default_rose_base_radius;
        proto.m_p_behavior = std::make_unique<CRoseBehavior>();
        REGISTER_PETAL(EPetalType::Rose, CPetal, proto);
    });
}

std::once_flag g_cactus_registered;
void RegisterCactus()
{
    std::call_once(g_cactus_registered, []()
    {
        CPetalPrototype proto;
        proto.m_type = EPetalType::Cactus;
        proto.m_name = std::string(GetPetalTypeName(proto.m_type));
        proto.m_base_radius = game_config::default_cactus_base_radius;
        proto.m_p_behavior = std::make_unique<CCactusBehavior>();
        REGISTER_PETAL(EPetalType::Cactus, CPetal, proto);
    });
}

std::once_flag g_soil_registered;
void RegisterSoil()
{
    std::call_once(g_soil_registered, []()
    {
        CPetalPrototype proto;
        proto.m_type = EPetalType::Soil;
        proto.m_name = std::string(GetPetalTypeName(proto.m_type));
        proto.m_base_radius = game_config::default_soil_base_radius;
        proto.m_p_behavior = std::make_unique<CSoilBehavior>();
        REGISTER_PETAL(EPetalType::Soil, CPetal, proto);
    });
}

std::once_flag g_web_registered;
void RegisterWeb()
{
    std::call_once(g_web_registered, []()
    {
        CPetalPrototype proto;
        proto.m_type = EPetalType::Web;
        proto.m_name = std::string(GetPetalTypeName(proto.m_type));
        proto.m_base_radius = game_config::default_web_base_radius;
        proto.m_p_behavior = std::make_unique<CWebBehavior>();
        REGISTER_PETAL(EPetalType::Web, CThrownPetal, proto);
    });
}

std::once_flag g_wax_registered;
void RegisterWax()
{
    std::call_once(g_wax_registered, []()
    {
        CPetalPrototype proto;
        proto.m_type = EPetalType::Wax;
        proto.m_name = std::string(GetPetalTypeName(proto.m_type));
        proto.m_base_radius = MoonVisibleRadius(ERarity::Common) * 2.f;
        proto.m_p_behavior = std::make_unique<CWaxBehavior>();
        REGISTER_PETAL(EPetalType::Wax, CPetal, proto);
    });
}

std::once_flag g_yin_yang_registered;
void RegisterYinYang()
{
    std::call_once(g_yin_yang_registered, []()
    {
        CPetalPrototype proto;
        proto.m_type = EPetalType::YinYang;
        proto.m_name = std::string(GetPetalTypeName(proto.m_type));
        proto.m_base_radius = game_config::default_yinyang_base_radius;
        proto.m_p_behavior = std::make_unique<CYinYangBehavior>();
        REGISTER_PETAL(EPetalType::YinYang, CPetal, proto);
    });
}

std::once_flag g_yggdrasil_registered;
void RegisterYggdrasil()
{
    std::call_once(g_yggdrasil_registered, []()
    {
        CPetalPrototype proto;
        proto.m_type = EPetalType::Yggdrasil;
        proto.m_name = std::string(GetPetalTypeName(proto.m_type));
        proto.m_base_radius = game_config::default_yggdrasil_base_radius;
        proto.m_p_behavior = std::make_unique<CYggdrasilBehavior>();
        REGISTER_PETAL(EPetalType::Yggdrasil, CYggdrasilPetal, proto);
    });
}

std::once_flag g_missile_registered;
void RegisterMissile()
{
    std::call_once(g_missile_registered, []()
    {
        CPetalPrototype proto;
        proto.m_type = EPetalType::Missile;
        proto.m_name = std::string(GetPetalTypeName(proto.m_type));
        proto.m_base_radius = game_config::default_missile_base_radius;
        proto.m_p_behavior = std::make_unique<CMissileBehavior>();
        REGISTER_PETAL(EPetalType::Missile, CMissilePetal, proto);
    });
}

std::once_flag g_sawblade_registered;
void RegisterSawblade()
{
    std::call_once(g_sawblade_registered, []()
    {
        CPetalPrototype proto;
        proto.m_type = EPetalType::Sawblade;
        proto.m_name = std::string(GetPetalTypeName(proto.m_type));
        proto.m_base_radius = game_config::default_basic_base_radius;
        proto.m_p_behavior = std::make_unique<CSawbladeBehavior>();
        REGISTER_PETAL(EPetalType::Sawblade, CPetal, proto);
    });
}

std::once_flag g_triangle_registered;
void RegisterTriangle()
{
    std::call_once(g_triangle_registered, []()
    {
        CPetalPrototype proto;
        proto.m_type = EPetalType::Triangle;
        proto.m_name = std::string(GetPetalTypeName(proto.m_type));
        proto.m_base_radius = game_config::default_basic_base_radius;
        proto.m_p_behavior = std::make_unique<CTriangleBehavior>();
        REGISTER_PETAL(EPetalType::Triangle, CPetal, proto);
    });
}

std::once_flag g_wing_registered;
void RegisterWing()
{
    std::call_once(g_wing_registered, []()
    {
        CPetalPrototype proto;
        proto.m_type = EPetalType::Wing;
        proto.m_name = std::string(GetPetalTypeName(proto.m_type));
        proto.m_base_radius = game_config::default_basic_base_radius;
        proto.m_p_behavior = std::make_unique<CWingBehavior>();
        REGISTER_PETAL(EPetalType::Wing, CPetal, proto);
    });
}

void RegisterPetals()
{
    RegisterAir();
    RegisterAntEgg();
    RegisterAntennae();
    RegisterBloodSacrifice();
    RegisterBandage();
    RegisterBasil();
    RegisterBasic();
    RegisterBeetleEgg();
    RegisterBrokenEgg();
    RegisterBone();
    RegisterBubble();
    RegisterCarrot();
    RegisterCoin();
    RegisterCompass();
    RegisterCogwheel();
    RegisterCorn();
    RegisterCorruption();
    RegisterDahlia();
    RegisterDust();
    RegisterFaster();
    RegisterFragment();
    RegisterGlass();
    RegisterGoldenLeaf();
    RegisterHeavy();
    RegisterHoney();
    RegisterIris();
    RegisterLeaf();
    RegisterLentil();
    RegisterLight();
    RegisterMimic();
    RegisterMoon();
    RegisterNullification();
    RegisterPincer();
    RegisterPollen();
    RegisterRelic();
    RegisterRice();
    RegisterRockPetal();
    RegisterRose();
    RegisterSawblade();
    RegisterSoil();
    RegisterStinger();
    RegisterTriangle();
    RegisterCactus();
    RegisterWeb();
    RegisterWax();
    RegisterWing();
    RegisterYinYang();
    RegisterYggdrasil();
    RegisterMissile();
}
