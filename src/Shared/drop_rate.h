#pragma once
#include "mob_type.h"
#include "petal_type.h"
#include "rarity.h"
#include "tools.h"
#include <algorithm>
#include <cstddef>
#include <unordered_map>
#include <utility>
#include <vector>

struct SDropRate
{
    EPetalType type = EPetalType::None;
    ERarity rarity = ERarity::Null;
    double drop_rate = 0.0;
};

struct SDropRateKey
{
    EMobType mob_type = EMobType::None;
    ERarity rarity = ERarity::Null;

    bool operator==(const SDropRateKey& other) const
    {
        return mob_type == other.mob_type && rarity == other.rarity;
    }
};

struct SDropRateKeyHash
{
    size_t operator()(const SDropRateKey& key) const
    {
        return (static_cast<size_t>(key.mob_type) << 8) ^ static_cast<size_t>(key.rarity);
    }
};

inline std::unordered_map<SDropRateKey, std::vector<SDropRate>, SDropRateKeyHash>& GetDropRateTable()
{
    static std::unordered_map<SDropRateKey, std::vector<SDropRate>, SDropRateKeyHash> table;
    return table;
}

inline void ClearDropRateTable()
{
    GetDropRateTable().clear();
}

inline bool RegisterDropRate(EMobType mob_type, ERarity mob_rarity, EPetalType drop_type, ERarity drop_rarity,
                             double drop_rate)
{
    if (mob_type == EMobType::None || mob_rarity == ERarity::Null) return false;
    if (drop_type == EPetalType::None || drop_rarity == ERarity::Null) return false;
    if (drop_rate <= 0.0) return false;

    auto& drops = GetDropRateTable()[SDropRateKey{mob_type, mob_rarity}];
    drops.push_back({drop_type, drop_rarity, std::clamp(drop_rate, 0.0, 1.0)});
    std::sort(drops.begin(), drops.end(), [](const SDropRate& lhs, const SDropRate& rhs)
    {
        if (lhs.type != rhs.type) return static_cast<int>(lhs.type) < static_cast<int>(rhs.type);
        return GetLevel(lhs.rarity) > GetLevel(rhs.rarity);
    });
    return true;
}

inline void RegisterDropRates()
{
    ClearDropRateTable();

    RegisterDropRate(EMobType::SoldierAnt, ERarity::Common, EPetalType::Wing, ERarity::Common, 0.875);
    RegisterDropRate(EMobType::SoldierAnt, ERarity::Common, EPetalType::Wing, ERarity::Unusual, 0.125);
    RegisterDropRate(EMobType::SoldierAnt, ERarity::Common, EPetalType::Glass, ERarity::Common, 0.667);
    RegisterDropRate(EMobType::SoldierAnt, ERarity::Common, EPetalType::Glass, ERarity::Unusual, 0.333);
    RegisterDropRate(EMobType::SoldierAnt, ERarity::Unusual, EPetalType::Wing, ERarity::Unusual, 0.125);
    RegisterDropRate(EMobType::SoldierAnt, ERarity::Unusual, EPetalType::Wing, ERarity::Common, 0.875);
    RegisterDropRate(EMobType::SoldierAnt, ERarity::Unusual, EPetalType::Glass, ERarity::Unusual, 0.333);
    RegisterDropRate(EMobType::SoldierAnt, ERarity::Unusual, EPetalType::Glass, ERarity::Common, 0.667);
    RegisterDropRate(EMobType::SoldierAnt, ERarity::Rare, EPetalType::Wing, ERarity::Rare, 0.051);
    RegisterDropRate(EMobType::SoldierAnt, ERarity::Rare, EPetalType::Wing, ERarity::Unusual, 0.700);
    RegisterDropRate(EMobType::SoldierAnt, ERarity::Rare, EPetalType::Wing, ERarity::Common, 0.249);
    RegisterDropRate(EMobType::SoldierAnt, ERarity::Rare, EPetalType::Glass, ERarity::Rare, 0.051);
    RegisterDropRate(EMobType::SoldierAnt, ERarity::Rare, EPetalType::Glass, ERarity::Unusual, 0.492);
    RegisterDropRate(EMobType::SoldierAnt, ERarity::Rare, EPetalType::Glass, ERarity::Common, 0.457);
    RegisterDropRate(EMobType::SoldierAnt, ERarity::Epic, EPetalType::Wing, ERarity::Epic, 0.030);
    RegisterDropRate(EMobType::SoldierAnt, ERarity::Epic, EPetalType::Wing, ERarity::Rare, 0.695);
    RegisterDropRate(EMobType::SoldierAnt, ERarity::Epic, EPetalType::Wing, ERarity::Unusual, 0.275);
    RegisterDropRate(EMobType::SoldierAnt, ERarity::Epic, EPetalType::Glass, ERarity::Epic, 0.030);
    RegisterDropRate(EMobType::SoldierAnt, ERarity::Epic, EPetalType::Glass, ERarity::Rare, 0.487);
    RegisterDropRate(EMobType::SoldierAnt, ERarity::Epic, EPetalType::Glass, ERarity::Unusual, 0.483);
    RegisterDropRate(EMobType::SoldierAnt, ERarity::Legendary, EPetalType::Wing, ERarity::Legendary, 0.010);
    RegisterDropRate(EMobType::SoldierAnt, ERarity::Legendary, EPetalType::Wing, ERarity::Epic, 0.690);
    RegisterDropRate(EMobType::SoldierAnt, ERarity::Legendary, EPetalType::Wing, ERarity::Rare, 0.300);
    RegisterDropRate(EMobType::SoldierAnt, ERarity::Legendary, EPetalType::Glass, ERarity::Legendary, 0.010);
    RegisterDropRate(EMobType::SoldierAnt, ERarity::Legendary, EPetalType::Glass, ERarity::Epic, 0.482);
    RegisterDropRate(EMobType::SoldierAnt, ERarity::Legendary, EPetalType::Glass, ERarity::Rare, 0.508);
    RegisterDropRate(EMobType::SoldierAnt, ERarity::Mythic, EPetalType::Wing, ERarity::Mythic, 0.003);
    RegisterDropRate(EMobType::SoldierAnt, ERarity::Mythic, EPetalType::Wing, ERarity::Legendary, 0.687);
    RegisterDropRate(EMobType::SoldierAnt, ERarity::Mythic, EPetalType::Wing, ERarity::Epic, 0.310);
    RegisterDropRate(EMobType::SoldierAnt, ERarity::Mythic, EPetalType::Glass, ERarity::Mythic, 0.003);
    RegisterDropRate(EMobType::SoldierAnt, ERarity::Mythic, EPetalType::Glass, ERarity::Legendary, 0.479);
    RegisterDropRate(EMobType::SoldierAnt, ERarity::Mythic, EPetalType::Glass, ERarity::Epic, 0.518);
    RegisterDropRate(EMobType::SoldierAnt, ERarity::Ultra, EPetalType::Wing, ERarity::Ultra, 0.0005);
    RegisterDropRate(EMobType::SoldierAnt, ERarity::Ultra, EPetalType::Wing, ERarity::Mythic, 0.6735);
    RegisterDropRate(EMobType::SoldierAnt, ERarity::Ultra, EPetalType::Wing, ERarity::Legendary, 0.326);
    RegisterDropRate(EMobType::SoldierAnt, ERarity::Ultra, EPetalType::Glass, ERarity::Ultra, 0.0005);
    RegisterDropRate(EMobType::SoldierAnt, ERarity::Ultra, EPetalType::Glass, ERarity::Mythic, 0.4655);
    RegisterDropRate(EMobType::SoldierAnt, ERarity::Ultra, EPetalType::Glass, ERarity::Legendary, 0.534);
    RegisterDropRate(EMobType::SoldierAnt, ERarity::Super, EPetalType::Wing, ERarity::Super, 0.000008);
    RegisterDropRate(EMobType::SoldierAnt, ERarity::Super, EPetalType::Wing, ERarity::Ultra, 0.672992);
    RegisterDropRate(EMobType::SoldierAnt, ERarity::Super, EPetalType::Wing, ERarity::Mythic, 0.327);
    RegisterDropRate(EMobType::SoldierAnt, ERarity::Super, EPetalType::Glass, ERarity::Super, 0.000008);
    RegisterDropRate(EMobType::SoldierAnt, ERarity::Super, EPetalType::Glass, ERarity::Ultra, 0.464992);
    RegisterDropRate(EMobType::SoldierAnt, ERarity::Super, EPetalType::Glass, ERarity::Mythic, 0.535);

    RegisterDropRate(EMobType::NormalLadybug, ERarity::Common, EPetalType::Basic, ERarity::Common, 0.56);
    RegisterDropRate(EMobType::NormalLadybug, ERarity::Common, EPetalType::Basic, ERarity::Unusual, 0.44);
    RegisterDropRate(EMobType::NormalLadybug, ERarity::Common, EPetalType::Rose, ERarity::Common, 0.667);
    RegisterDropRate(EMobType::NormalLadybug, ERarity::Common, EPetalType::Rose, ERarity::Unusual, 0.333);
    RegisterDropRate(EMobType::NormalLadybug, ERarity::Unusual, EPetalType::Basic, ERarity::Unusual, 0.440);
    RegisterDropRate(EMobType::NormalLadybug, ERarity::Unusual, EPetalType::Basic, ERarity::Common, 0.560);
    RegisterDropRate(EMobType::NormalLadybug, ERarity::Unusual, EPetalType::Rose, ERarity::Unusual, 0.333);
    RegisterDropRate(EMobType::NormalLadybug, ERarity::Unusual, EPetalType::Rose, ERarity::Common, 0.667);
    RegisterDropRate(EMobType::NormalLadybug, ERarity::Rare, EPetalType::Basic, ERarity::Rare, 0.051);
    RegisterDropRate(EMobType::NormalLadybug, ERarity::Rare, EPetalType::Basic, ERarity::Unusual, 0.385);
    RegisterDropRate(EMobType::NormalLadybug, ERarity::Rare, EPetalType::Basic, ERarity::Common, 0.564);
    RegisterDropRate(EMobType::NormalLadybug, ERarity::Rare, EPetalType::Rose, ERarity::Rare, 0.051);
    RegisterDropRate(EMobType::NormalLadybug, ERarity::Rare, EPetalType::Rose, ERarity::Unusual, 0.492);
    RegisterDropRate(EMobType::NormalLadybug, ERarity::Rare, EPetalType::Rose, ERarity::Common, 0.457);
    RegisterDropRate(EMobType::NormalLadybug, ERarity::Epic, EPetalType::Basic, ERarity::Epic, 0.030);
    RegisterDropRate(EMobType::NormalLadybug, ERarity::Epic, EPetalType::Basic, ERarity::Rare, 0.380);
    RegisterDropRate(EMobType::NormalLadybug, ERarity::Epic, EPetalType::Basic, ERarity::Unusual, 0.590);
    RegisterDropRate(EMobType::NormalLadybug, ERarity::Epic, EPetalType::Rose, ERarity::Epic, 0.030);
    RegisterDropRate(EMobType::NormalLadybug, ERarity::Epic, EPetalType::Rose, ERarity::Rare, 0.487);
    RegisterDropRate(EMobType::NormalLadybug, ERarity::Epic, EPetalType::Rose, ERarity::Unusual, 0.483);
    RegisterDropRate(EMobType::NormalLadybug, ERarity::Legendary, EPetalType::Basic, ERarity::Legendary, 0.010);
    RegisterDropRate(EMobType::NormalLadybug, ERarity::Legendary, EPetalType::Basic, ERarity::Epic, 0.375);
    RegisterDropRate(EMobType::NormalLadybug, ERarity::Legendary, EPetalType::Basic, ERarity::Rare, 0.615);
    RegisterDropRate(EMobType::NormalLadybug, ERarity::Legendary, EPetalType::Rose, ERarity::Legendary, 0.010);
    RegisterDropRate(EMobType::NormalLadybug, ERarity::Legendary, EPetalType::Rose, ERarity::Epic, 0.482);
    RegisterDropRate(EMobType::NormalLadybug, ERarity::Legendary, EPetalType::Rose, ERarity::Rare, 0.508);
    RegisterDropRate(EMobType::NormalLadybug, ERarity::Mythic, EPetalType::Basic, ERarity::Mythic, 0.003);
    RegisterDropRate(EMobType::NormalLadybug, ERarity::Mythic, EPetalType::Basic, ERarity::Legendary, 0.372);
    RegisterDropRate(EMobType::NormalLadybug, ERarity::Mythic, EPetalType::Basic, ERarity::Epic, 0.625);
    RegisterDropRate(EMobType::NormalLadybug, ERarity::Mythic, EPetalType::Rose, ERarity::Mythic, 0.003);
    RegisterDropRate(EMobType::NormalLadybug, ERarity::Mythic, EPetalType::Rose, ERarity::Legendary, 0.479);
    RegisterDropRate(EMobType::NormalLadybug, ERarity::Mythic, EPetalType::Rose, ERarity::Epic, 0.518);
    RegisterDropRate(EMobType::NormalLadybug, ERarity::Ultra, EPetalType::Basic, ERarity::Ultra, 0.0005);
    RegisterDropRate(EMobType::NormalLadybug, ERarity::Ultra, EPetalType::Basic, ERarity::Mythic, 0.3585);
    RegisterDropRate(EMobType::NormalLadybug, ERarity::Ultra, EPetalType::Basic, ERarity::Legendary, 0.641);
    RegisterDropRate(EMobType::NormalLadybug, ERarity::Ultra, EPetalType::Rose, ERarity::Ultra, 0.0005);
    RegisterDropRate(EMobType::NormalLadybug, ERarity::Ultra, EPetalType::Rose, ERarity::Mythic, 0.4655);
    RegisterDropRate(EMobType::NormalLadybug, ERarity::Ultra, EPetalType::Rose, ERarity::Legendary, 0.534);
    RegisterDropRate(EMobType::NormalLadybug, ERarity::Super, EPetalType::Basic, ERarity::Super, 0.000008);
    RegisterDropRate(EMobType::NormalLadybug, ERarity::Super, EPetalType::Basic, ERarity::Ultra, 0.357992);
    RegisterDropRate(EMobType::NormalLadybug, ERarity::Super, EPetalType::Basic, ERarity::Mythic, 0.642);
    RegisterDropRate(EMobType::NormalLadybug, ERarity::Super, EPetalType::Rose, ERarity::Super, 0.000008);
    RegisterDropRate(EMobType::NormalLadybug, ERarity::Super, EPetalType::Rose, ERarity::Ultra, 0.464992);
    RegisterDropRate(EMobType::NormalLadybug, ERarity::Super, EPetalType::Rose, ERarity::Mythic, 0.535);

    RegisterDropRate(EMobType::Bee, ERarity::Common, EPetalType::Stinger, ERarity::Common, 0.793);
    RegisterDropRate(EMobType::Bee, ERarity::Common, EPetalType::Stinger, ERarity::Unusual, 0.207);
    RegisterDropRate(EMobType::Bee, ERarity::Unusual, EPetalType::Stinger, ERarity::Unusual, 0.207);
    RegisterDropRate(EMobType::Bee, ERarity::Unusual, EPetalType::Stinger, ERarity::Common, 0.793);
    RegisterDropRate(EMobType::Bee, ERarity::Rare, EPetalType::Stinger, ERarity::Rare, 0.051);
    RegisterDropRate(EMobType::Bee, ERarity::Rare, EPetalType::Stinger, ERarity::Unusual, 0.618);
    RegisterDropRate(EMobType::Bee, ERarity::Rare, EPetalType::Stinger, ERarity::Common, 0.331);
    RegisterDropRate(EMobType::Bee, ERarity::Epic, EPetalType::Stinger, ERarity::Epic, 0.030);
    RegisterDropRate(EMobType::Bee, ERarity::Epic, EPetalType::Stinger, ERarity::Rare, 0.613);
    RegisterDropRate(EMobType::Bee, ERarity::Epic, EPetalType::Stinger, ERarity::Unusual, 0.357);
    RegisterDropRate(EMobType::Bee, ERarity::Legendary, EPetalType::Stinger, ERarity::Legendary, 0.010);
    RegisterDropRate(EMobType::Bee, ERarity::Legendary, EPetalType::Stinger, ERarity::Epic, 0.608);
    RegisterDropRate(EMobType::Bee, ERarity::Legendary, EPetalType::Stinger, ERarity::Rare, 0.382);
    RegisterDropRate(EMobType::Bee, ERarity::Mythic, EPetalType::Stinger, ERarity::Mythic, 0.003);
    RegisterDropRate(EMobType::Bee, ERarity::Mythic, EPetalType::Stinger, ERarity::Legendary, 0.605);
    RegisterDropRate(EMobType::Bee, ERarity::Mythic, EPetalType::Stinger, ERarity::Epic, 0.392);
    RegisterDropRate(EMobType::Bee, ERarity::Ultra, EPetalType::Stinger, ERarity::Ultra, 0.0005);
    RegisterDropRate(EMobType::Bee, ERarity::Ultra, EPetalType::Stinger, ERarity::Mythic, 0.5915);
    RegisterDropRate(EMobType::Bee, ERarity::Ultra, EPetalType::Stinger, ERarity::Legendary, 0.408);
    RegisterDropRate(EMobType::Bee, ERarity::Super, EPetalType::Stinger, ERarity::Super, 0.000008);
    RegisterDropRate(EMobType::Bee, ERarity::Super, EPetalType::Stinger, ERarity::Ultra, 0.590992);
    RegisterDropRate(EMobType::Bee, ERarity::Super, EPetalType::Stinger, ERarity::Mythic, 0.409);

    RegisterDropRate(EMobType::Hornet, ERarity::Common, EPetalType::Missile, ERarity::Common, 0.870108);
    RegisterDropRate(EMobType::Hornet, ERarity::Common, EPetalType::Missile, ERarity::Unusual, 0.129892);
    RegisterDropRate(EMobType::Hornet, ERarity::Unusual, EPetalType::Missile, ERarity::Unusual, 0.129892);
    RegisterDropRate(EMobType::Hornet, ERarity::Unusual, EPetalType::Missile, ERarity::Common, 0.870108);
    RegisterDropRate(EMobType::Hornet, ERarity::Rare, EPetalType::Antennae, ERarity::Rare, 0.051);
    RegisterDropRate(EMobType::Hornet, ERarity::Rare, EPetalType::Missile, ERarity::Rare, 0.051);
    RegisterDropRate(EMobType::Hornet, ERarity::Rare, EPetalType::Missile, ERarity::Unusual, 0.695108);
    RegisterDropRate(EMobType::Hornet, ERarity::Rare, EPetalType::Missile, ERarity::Common, 0.253892);
    RegisterDropRate(EMobType::Hornet, ERarity::Epic, EPetalType::Antennae, ERarity::Epic, 0.030);
    RegisterDropRate(EMobType::Hornet, ERarity::Epic, EPetalType::Antennae, ERarity::Rare, 0.354008);
    RegisterDropRate(EMobType::Hornet, ERarity::Epic, EPetalType::Missile, ERarity::Epic, 0.030);
    RegisterDropRate(EMobType::Hornet, ERarity::Epic, EPetalType::Missile, ERarity::Rare, 0.690108);
    RegisterDropRate(EMobType::Hornet, ERarity::Epic, EPetalType::Missile, ERarity::Unusual, 0.279892);
    RegisterDropRate(EMobType::Hornet, ERarity::Legendary, EPetalType::Antennae, ERarity::Legendary, 0.010);
    RegisterDropRate(EMobType::Hornet, ERarity::Legendary, EPetalType::Antennae, ERarity::Epic, 0.349008);
    RegisterDropRate(EMobType::Hornet, ERarity::Legendary, EPetalType::Antennae, ERarity::Rare, 0.640992);
    RegisterDropRate(EMobType::Hornet, ERarity::Legendary, EPetalType::Missile, ERarity::Legendary, 0.010);
    RegisterDropRate(EMobType::Hornet, ERarity::Legendary, EPetalType::Missile, ERarity::Epic, 0.685108);
    RegisterDropRate(EMobType::Hornet, ERarity::Legendary, EPetalType::Missile, ERarity::Rare, 0.304892);
    RegisterDropRate(EMobType::Hornet, ERarity::Mythic, EPetalType::Antennae, ERarity::Mythic, 0.003);
    RegisterDropRate(EMobType::Hornet, ERarity::Mythic, EPetalType::Antennae, ERarity::Legendary, 0.346008);
    RegisterDropRate(EMobType::Hornet, ERarity::Mythic, EPetalType::Antennae, ERarity::Epic, 0.650992);
    RegisterDropRate(EMobType::Hornet, ERarity::Mythic, EPetalType::Missile, ERarity::Mythic, 0.003);
    RegisterDropRate(EMobType::Hornet, ERarity::Mythic, EPetalType::Missile, ERarity::Legendary, 0.682108);
    RegisterDropRate(EMobType::Hornet, ERarity::Mythic, EPetalType::Missile, ERarity::Epic, 0.314892);
    RegisterDropRate(EMobType::Hornet, ERarity::Ultra, EPetalType::Antennae, ERarity::Ultra, 0.0005);
    RegisterDropRate(EMobType::Hornet, ERarity::Ultra, EPetalType::Antennae, ERarity::Mythic, 0.332508);
    RegisterDropRate(EMobType::Hornet, ERarity::Ultra, EPetalType::Antennae, ERarity::Legendary, 0.666992);
    RegisterDropRate(EMobType::Hornet, ERarity::Ultra, EPetalType::Missile, ERarity::Ultra, 0.0005);
    RegisterDropRate(EMobType::Hornet, ERarity::Ultra, EPetalType::Missile, ERarity::Mythic, 0.668608);
    RegisterDropRate(EMobType::Hornet, ERarity::Ultra, EPetalType::Missile, ERarity::Legendary, 0.330892);
    RegisterDropRate(EMobType::Hornet, ERarity::Super, EPetalType::Antennae, ERarity::Super, 0.000002);
    RegisterDropRate(EMobType::Hornet, ERarity::Super, EPetalType::Antennae, ERarity::Ultra, 0.332);
    RegisterDropRate(EMobType::Hornet, ERarity::Super, EPetalType::Antennae, ERarity::Mythic, 0.667998);
    RegisterDropRate(EMobType::Hornet, ERarity::Super, EPetalType::Missile, ERarity::Super, 0.00001);
    RegisterDropRate(EMobType::Hornet, ERarity::Super, EPetalType::Missile, ERarity::Ultra, 0.6681);
    RegisterDropRate(EMobType::Hornet, ERarity::Super, EPetalType::Missile, ERarity::Mythic, 0.33189);

    RegisterDropRate(EMobType::BandageBeetle, ERarity::Common, EPetalType::Lentil, ERarity::Common, 0.453);
    RegisterDropRate(EMobType::BandageBeetle, ERarity::Common, EPetalType::BeetleEgg, ERarity::Common, 0.800);
    RegisterDropRate(EMobType::BandageBeetle, ERarity::Common, EPetalType::BeetleEgg, ERarity::Unusual, 0.200);

    RegisterDropRate(EMobType::Beetle, ERarity::Common, EPetalType::BeetleEgg, ERarity::Common, 0.667);
    RegisterDropRate(EMobType::Beetle, ERarity::Common, EPetalType::BeetleEgg, ERarity::Unusual, 0.333);

    RegisterDropRate(EMobType::BandageBeetle, ERarity::Unusual, EPetalType::Lentil, ERarity::Common, 0.608);
    RegisterDropRate(EMobType::BandageBeetle, ERarity::Unusual, EPetalType::Lentil, ERarity::Unusual, 0.392);
    RegisterDropRate(EMobType::BandageBeetle, ERarity::Unusual, EPetalType::BeetleEgg, ERarity::Common, 0.565);
    RegisterDropRate(EMobType::BandageBeetle, ERarity::Unusual, EPetalType::BeetleEgg, ERarity::Unusual, 0.435);
    RegisterDropRate(EMobType::BandageBeetle, ERarity::Rare, EPetalType::Lentil, ERarity::Rare, 0.051);
    RegisterDropRate(EMobType::BandageBeetle, ERarity::Rare, EPetalType::Lentil, ERarity::Unusual, 0.278);
    RegisterDropRate(EMobType::BandageBeetle, ERarity::Rare, EPetalType::Lentil, ERarity::Common, 0.671);
    RegisterDropRate(EMobType::BandageBeetle, ERarity::Rare, EPetalType::BeetleEgg, ERarity::Rare, 0.051);
    RegisterDropRate(EMobType::BandageBeetle, ERarity::Rare, EPetalType::BeetleEgg, ERarity::Unusual, 0.625);
    RegisterDropRate(EMobType::BandageBeetle, ERarity::Rare, EPetalType::BeetleEgg, ERarity::Common, 0.324);
    RegisterDropRate(EMobType::BandageBeetle, ERarity::Epic, EPetalType::Lentil, ERarity::Epic, 0.030);
    RegisterDropRate(EMobType::BandageBeetle, ERarity::Epic, EPetalType::Lentil, ERarity::Rare, 0.273);
    RegisterDropRate(EMobType::BandageBeetle, ERarity::Epic, EPetalType::Lentil, ERarity::Unusual, 0.697);
    RegisterDropRate(EMobType::BandageBeetle, ERarity::Epic, EPetalType::BeetleEgg, ERarity::Epic, 0.030);
    RegisterDropRate(EMobType::BandageBeetle, ERarity::Epic, EPetalType::BeetleEgg, ERarity::Rare, 0.620);
    RegisterDropRate(EMobType::BandageBeetle, ERarity::Epic, EPetalType::BeetleEgg, ERarity::Unusual, 0.350);
    RegisterDropRate(EMobType::BandageBeetle, ERarity::Legendary, EPetalType::Lentil, ERarity::Legendary, 0.010);
    RegisterDropRate(EMobType::BandageBeetle, ERarity::Legendary, EPetalType::Lentil, ERarity::Epic, 0.268);
    RegisterDropRate(EMobType::BandageBeetle, ERarity::Legendary, EPetalType::Lentil, ERarity::Rare, 0.722);
    RegisterDropRate(EMobType::BandageBeetle, ERarity::Legendary, EPetalType::BeetleEgg, ERarity::Legendary, 0.010);
    RegisterDropRate(EMobType::BandageBeetle, ERarity::Legendary, EPetalType::BeetleEgg, ERarity::Epic, 0.615);
    RegisterDropRate(EMobType::BandageBeetle, ERarity::Legendary, EPetalType::BeetleEgg, ERarity::Rare, 0.375);
    RegisterDropRate(EMobType::BandageBeetle, ERarity::Mythic, EPetalType::Lentil, ERarity::Mythic, 0.003);
    RegisterDropRate(EMobType::BandageBeetle, ERarity::Mythic, EPetalType::Lentil, ERarity::Legendary, 0.265);
    RegisterDropRate(EMobType::BandageBeetle, ERarity::Mythic, EPetalType::Lentil, ERarity::Epic, 0.732);
    RegisterDropRate(EMobType::BandageBeetle, ERarity::Mythic, EPetalType::BeetleEgg, ERarity::Mythic, 0.003);
    RegisterDropRate(EMobType::BandageBeetle, ERarity::Mythic, EPetalType::BeetleEgg, ERarity::Legendary, 0.612);
    RegisterDropRate(EMobType::BandageBeetle, ERarity::Mythic, EPetalType::BeetleEgg, ERarity::Epic, 0.385);
    RegisterDropRate(EMobType::BandageBeetle, ERarity::Ultra, EPetalType::Lentil, ERarity::Ultra, 0.0005);
    RegisterDropRate(EMobType::BandageBeetle, ERarity::Ultra, EPetalType::Lentil, ERarity::Mythic, 0.2515);
    RegisterDropRate(EMobType::BandageBeetle, ERarity::Ultra, EPetalType::Lentil, ERarity::Legendary, 0.748);
    RegisterDropRate(EMobType::BandageBeetle, ERarity::Ultra, EPetalType::BeetleEgg, ERarity::Ultra, 0.0005);
    RegisterDropRate(EMobType::BandageBeetle, ERarity::Ultra, EPetalType::BeetleEgg, ERarity::Mythic, 0.5985);
    RegisterDropRate(EMobType::BandageBeetle, ERarity::Ultra, EPetalType::BeetleEgg, ERarity::Legendary, 0.401);
    RegisterDropRate(EMobType::BandageBeetle, ERarity::Super, EPetalType::Lentil, ERarity::Super, 0.000008);
    RegisterDropRate(EMobType::BandageBeetle, ERarity::Super, EPetalType::Lentil, ERarity::Ultra, 0.250992);
    RegisterDropRate(EMobType::BandageBeetle, ERarity::Super, EPetalType::Lentil, ERarity::Mythic, 0.749);
    RegisterDropRate(EMobType::BandageBeetle, ERarity::Super, EPetalType::BeetleEgg, ERarity::Super, 0.000008);
    RegisterDropRate(EMobType::BandageBeetle, ERarity::Super, EPetalType::BeetleEgg, ERarity::Ultra, 0.597992);
    RegisterDropRate(EMobType::BandageBeetle, ERarity::Super, EPetalType::BeetleEgg, ERarity::Mythic, 0.402);

    RegisterDropRate(EMobType::Beetle, ERarity::Unusual, EPetalType::BeetleEgg, ERarity::Common, 0.547);
    RegisterDropRate(EMobType::Beetle, ERarity::Unusual, EPetalType::BeetleEgg, ERarity::Unusual, 0.453);
    RegisterDropRate(EMobType::Beetle, ERarity::Rare, EPetalType::BeetleEgg, ERarity::Rare, 0.051);
    RegisterDropRate(EMobType::Beetle, ERarity::Rare, EPetalType::BeetleEgg, ERarity::Unusual, 0.492);
    RegisterDropRate(EMobType::Beetle, ERarity::Rare, EPetalType::BeetleEgg, ERarity::Common, 0.457);
    RegisterDropRate(EMobType::Beetle, ERarity::Epic, EPetalType::BeetleEgg, ERarity::Epic, 0.030);
    RegisterDropRate(EMobType::Beetle, ERarity::Epic, EPetalType::BeetleEgg, ERarity::Rare, 0.487);
    RegisterDropRate(EMobType::Beetle, ERarity::Epic, EPetalType::BeetleEgg, ERarity::Unusual, 0.483);
    RegisterDropRate(EMobType::Beetle, ERarity::Legendary, EPetalType::BeetleEgg, ERarity::Legendary, 0.010);
    RegisterDropRate(EMobType::Beetle, ERarity::Legendary, EPetalType::BeetleEgg, ERarity::Epic, 0.482);
    RegisterDropRate(EMobType::Beetle, ERarity::Legendary, EPetalType::BeetleEgg, ERarity::Rare, 0.508);
    RegisterDropRate(EMobType::Beetle, ERarity::Mythic, EPetalType::BeetleEgg, ERarity::Mythic, 0.003);
    RegisterDropRate(EMobType::Beetle, ERarity::Mythic, EPetalType::BeetleEgg, ERarity::Legendary, 0.479);
    RegisterDropRate(EMobType::Beetle, ERarity::Mythic, EPetalType::BeetleEgg, ERarity::Epic, 0.518);
    RegisterDropRate(EMobType::Beetle, ERarity::Ultra, EPetalType::BeetleEgg, ERarity::Ultra, 0.0005);
    RegisterDropRate(EMobType::Beetle, ERarity::Ultra, EPetalType::BeetleEgg, ERarity::Mythic, 0.4655);
    RegisterDropRate(EMobType::Beetle, ERarity::Ultra, EPetalType::BeetleEgg, ERarity::Legendary, 0.534);
    RegisterDropRate(EMobType::Beetle, ERarity::Super, EPetalType::BeetleEgg, ERarity::Super, 0.000008);
    RegisterDropRate(EMobType::Beetle, ERarity::Super, EPetalType::BeetleEgg, ERarity::Ultra, 0.464992);
    RegisterDropRate(EMobType::Beetle, ERarity::Super, EPetalType::BeetleEgg, ERarity::Mythic, 0.535);
}

inline const std::vector<SDropRate>& QueryDropRates(EMobType mob_type, ERarity rarity)
{
    static const std::vector<SDropRate> empty;
    const auto& table = GetDropRateTable();
    auto it = table.find(SDropRateKey{mob_type, rarity});
    return it == table.end() ? empty : it->second;
}

inline std::vector<SDropRate> RollDrops(EMobType mob_type, ERarity rarity)
{
    std::vector<SDropRate> result;
    const std::vector<SDropRate>& rates = QueryDropRates(mob_type, rarity);

    EPetalType current_type = EPetalType::None;
    double remaining_rate = 1.0;

    for (const SDropRate& rate : rates)
    {
        if (rate.type == EPetalType::None || rate.rarity == ERarity::Null || rate.drop_rate <= 0.0) continue;

        if (rate.type != current_type)
        {
            current_type = rate.type;
            remaining_rate = 1.0;
        }

        if (remaining_rate <= 0.0) continue;

        double check_rate = std::clamp(rate.drop_rate / remaining_rate, 0.0, 1.0);
        if (CheckChance(check_rate))
        {
            result.push_back(rate);
            current_type = rate.type;
            remaining_rate = 0.0;
            continue;
        }

        remaining_rate = std::max(0.0, remaining_rate - rate.drop_rate);
    }

    return result;
}

