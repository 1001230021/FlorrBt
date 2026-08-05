#include "petals_behavior.h"

bool RegisterPetals(std::string& error)
{
    CPetalRegistry& registry = petal_registry_detail::MutableRegistry();
    if (registry.IsFrozen())
    {
        error.clear();
        return true;
    }

    RegisterPetalDefinition<EPetalType::Air, CAirBehavior>();
    RegisterPetalDefinition<EPetalType::AntEgg, CAntEggBehavior, CBeetleEggPetal>();
    RegisterPetalDefinition<EPetalType::Antennae, CAntennaeBehavior>();
    RegisterPetalDefinition<EPetalType::Basic, CBasicBehavior>();
    RegisterPetalDefinition<EPetalType::BeetleEgg, CBeetleEggBehavior, CBeetleEggPetal>();
    RegisterPetalDefinition<EPetalType::Bone, CBoneBehavior>();
    RegisterPetalDefinition<EPetalType::Bubble, CBubbleBehavior>();
    RegisterPetalDefinition<EPetalType::Carrot, CCarrotBehavior, CMissilePetal>();
    RegisterPetalDefinition<EPetalType::Coin, CCoinBehavior>();
    RegisterPetalDefinition<EPetalType::Compass, CCompassBehavior, CCompassPetal>();
    RegisterPetalDefinition<EPetalType::Cogwheel, CCogwheelBehavior>();
    registry.MarkUnsupported(EPetalType::Disc, "reserved petal type without an implementation");
    RegisterPetalDefinition<EPetalType::Dust, CDustBehavior>();
    RegisterPetalDefinition<EPetalType::GoldenLeaf, CGoldenLeafBehavior>();
    RegisterPetalDefinition<EPetalType::Iris, CIrisBehavior>();
    RegisterPetalDefinition<EPetalType::Lentil, CLentilBehavior>();
    RegisterPetalDefinition<EPetalType::Moon, CMoonBehavior>();
    RegisterPetalDefinition<EPetalType::Nullification, CNullificationBehavior>();
    RegisterPetalDefinition<EPetalType::Pincer, CPincerBehavior>();
    RegisterPetalDefinition<EPetalType::Relic, CRelicBehavior, CRelicPetal>();
    RegisterPetalDefinition<EPetalType::Rose, CRoseBehavior, CRosePetal>();
    RegisterPetalDefinition<EPetalType::YinYang, CYinYangBehavior>();
    RegisterPetalDefinition<EPetalType::Missile, CMissileBehavior, CMissilePetal>();
    RegisterPetalDefinition<EPetalType::BloodSacrifice, CBloodSacrificeBehavior>();
    RegisterPetalDefinition<EPetalType::Corruption, CCorruptionBehavior>();
    RegisterPetalDefinition<EPetalType::Bandage, CBandageBehavior>();
    RegisterPetalDefinition<EPetalType::Heavy, CHeavyBehavior>();
    RegisterPetalDefinition<EPetalType::Faster, CFasterBehavior>();
    RegisterPetalDefinition<EPetalType::Yggdrasil, CYggdrasilBehavior, CYggdrasilPetal>();
    RegisterPetalDefinition<EPetalType::Dahlia, CDahliaBehavior, CDahliaPetal>();
    RegisterPetalDefinition<EPetalType::Wing, CWingBehavior>();
    RegisterPetalDefinition<EPetalType::Triangle, CTriangleBehavior>();
    RegisterPetalDefinition<EPetalType::Sawblade, CSawbladeBehavior>();
    RegisterPetalDefinition<EPetalType::Fragment, CFragmentBehavior>();
    RegisterPetalDefinition<EPetalType::Mimic, CMimicBehavior>();
    RegisterPetalDefinition<EPetalType::Glass, CGlassBehavior, CGlassPetal>();
    RegisterPetalDefinition<EPetalType::Stinger, CStingerBehavior>();
    RegisterPetalDefinition<EPetalType::BrokenEgg, CBrokenEggBehavior, CBrokenEggPetal>();
    RegisterPetalDefinition<EPetalType::Light, CLightBehavior>();
    RegisterPetalDefinition<EPetalType::Leaf, CLeafBehavior>();
    RegisterPetalDefinition<EPetalType::Rock, CRockPetalBehavior>();
    RegisterPetalDefinition<EPetalType::Web, CWebBehavior, CThrownPetal>();
    RegisterPetalDefinition<EPetalType::Cactus, CCactusBehavior>();
    RegisterPetalDefinition<EPetalType::Pollen, CPollenBehavior, CThrownPetal>();
    RegisterPetalDefinition<EPetalType::Corn, CCornBehavior>();
    RegisterPetalDefinition<EPetalType::Rice, CRiceBehavior>();
    RegisterPetalDefinition<EPetalType::Basil, CBasilBehavior, CBasilPetal>();
    RegisterPetalDefinition<EPetalType::Soil, CSoilBehavior>();
    RegisterPetalDefinition<EPetalType::Honey, CHoneyBehavior, CThrownPetal>();
    RegisterPetalDefinition<EPetalType::Wax, CWaxBehavior>();
    RegisterPetalDefinition<EPetalType::ThirdEye, CThirdEyeBehavior>();
    RegisterPetalDefinition<EPetalType::Dandelion, CDandelionBehavior>();
    RegisterPetalDefinition<EPetalType::Orange, COrangeBehavior>();
    RegisterPetalDefinition<EPetalType::Shovel, CShovelBehavior, CShovelPetal>();
    RegisterPetalDefinition<EPetalType::Yucca, CYuccaBehavior>();
    RegisterPetalDefinition<EPetalType::WhiteFungus, CWhiteFungusBehavior>();
    RegisterPetalDefinition<EPetalType::BlackFungus, CBlackFungusBehavior>();
    RegisterPetalDefinition<EPetalType::Broccoli, CBroccoliBehavior>();
    RegisterPetalDefinition<EPetalType::Douli, CDouliBehavior>();
    RegisterPetalDefinition<EPetalType::Trapper, CTrapperBehavior, CTrapperPetal>();
    RegisterPetalDefinition<EPetalType::Amulet, CAmuletBehavior>();
    RegisterPetalDefinition<EPetalType::Plank, CPlankBehavior>();
    RegisterPetalDefinition<EPetalType::Tomato, CTomatoBehavior>();

    return registry.Finalize(error);
}
