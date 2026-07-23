#pragma once
#include "mob_type.h"
#include "petal_type.h"
#include "rarity.h"
#include "tools.h"
#include <algorithm>
#include <cstddef>
#include <initializer_list>
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
        return GetRaritySortRank(lhs.rarity) > GetRaritySortRank(rhs.rarity);
    });
    return true;
}

struct SDropRateSpec
{
    ERarity mob_rarity = ERarity::Null;
    ERarity drop_rarity = ERarity::Null;
    double drop_rate = 0.0;
};

inline void RemoveDropRatesForMob(EMobType mob_type, std::initializer_list<EPetalType> preserved_types = {})
{
    for (ERarity rarity : {ERarity::Common, ERarity::Unusual, ERarity::Rare, ERarity::Epic,
                           ERarity::Legendary, ERarity::Mythic, ERarity::Ultra, ERarity::Super})
    {
        auto& table = GetDropRateTable();
        auto it = table.find(SDropRateKey{mob_type, rarity});
        if (it == table.end()) continue;

        auto& drops = it->second;
        drops.erase(std::remove_if(drops.begin(), drops.end(), [preserved_types](const SDropRate& drop)
        {
            return std::find(preserved_types.begin(), preserved_types.end(), drop.type) == preserved_types.end();
        }), drops.end());

        if (drops.empty()) table.erase(it);
    }
}

inline void RegisterDropRateTable(EMobType mob_type, EPetalType drop_type,
                                  std::initializer_list<SDropRateSpec> drops)
{
    for (const SDropRateSpec& drop : drops)
        RegisterDropRate(mob_type, drop.mob_rarity, drop_type, drop.drop_rarity, drop.drop_rate);
}

inline void RegisterWikiId6DropTable(EMobType mob_type, EPetalType drop_type)
{
    RegisterDropRateTable(mob_type, drop_type, {
        {ERarity::Common, ERarity::Common, 0.598}, {ERarity::Common, ERarity::Unusual, 0.164},
        {ERarity::Unusual, ERarity::Common, 0.485}, {ERarity::Unusual, ERarity::Unusual, 0.512},
        {ERarity::Rare, ERarity::Unusual, 0.834}, {ERarity::Rare, ERarity::Rare, 0.165},
        {ERarity::Epic, ERarity::Unusual, 0.067}, {ERarity::Epic, ERarity::Rare, 0.810},
        {ERarity::Epic, ERarity::Epic, 0.123},
        {ERarity::Legendary, ERarity::Rare, 0.072}, {ERarity::Legendary, ERarity::Epic, 0.877},
        {ERarity::Legendary, ERarity::Legendary, 0.050},
        {ERarity::Mythic, ERarity::Epic, 0.075}, {ERarity::Mythic, ERarity::Legendary, 0.917},
        {ERarity::Mythic, ERarity::Mythic, 0.008},
        {ERarity::Ultra, ERarity::Legendary, 0.317}, {ERarity::Ultra, ERarity::Mythic, 0.679},
        {ERarity::Ultra, ERarity::Ultra, 0.004},
        {ERarity::Super, ERarity::Mythic, 0.356}, {ERarity::Super, ERarity::Ultra, 0.644},
    });
}

inline void RegisterWikiId7UnusualDropTable(EMobType mob_type, EPetalType drop_type)
{
    RegisterDropRateTable(mob_type, drop_type, {
        {ERarity::Common, ERarity::Unusual, 0.101},
        {ERarity::Unusual, ERarity::Unusual, 0.348},
        {ERarity::Rare, ERarity::Unusual, 0.883}, {ERarity::Rare, ERarity::Rare, 0.103},
        {ERarity::Epic, ERarity::Unusual, 0.197}, {ERarity::Epic, ERarity::Rare, 0.727},
        {ERarity::Epic, ERarity::Epic, 0.076},
        {ERarity::Legendary, ERarity::Rare, 0.207}, {ERarity::Legendary, ERarity::Epic, 0.763},
        {ERarity::Legendary, ERarity::Legendary, 0.031},
        {ERarity::Mythic, ERarity::Epic, 0.212}, {ERarity::Mythic, ERarity::Legendary, 0.783},
        {ERarity::Mythic, ERarity::Mythic, 0.005},
        {ERarity::Ultra, ERarity::Legendary, 0.394}, {ERarity::Ultra, ERarity::Mythic, 0.603},
        {ERarity::Ultra, ERarity::Ultra, 0.003},
        {ERarity::Super, ERarity::Mythic, 0.538}, {ERarity::Super, ERarity::Ultra, 0.462},
    });
}

inline void RegisterWikiId4DropTable(EMobType mob_type, EPetalType drop_type)
{
    RegisterDropRateTable(mob_type, drop_type, {
        {ERarity::Common, ERarity::Common, 0.662}, {ERarity::Common, ERarity::Unusual, 0.306},
        {ERarity::Unusual, ERarity::Common, 0.232}, {ERarity::Unusual, ERarity::Unusual, 0.768},
        {ERarity::Rare, ERarity::Unusual, 0.697}, {ERarity::Rare, ERarity::Rare, 0.303},
        {ERarity::Epic, ERarity::Rare, 0.764}, {ERarity::Epic, ERarity::Epic, 0.231},
        {ERarity::Legendary, ERarity::Rare, 0.005}, {ERarity::Legendary, ERarity::Epic, 0.897},
        {ERarity::Legendary, ERarity::Legendary, 0.098},
        {ERarity::Mythic, ERarity::Epic, 0.006}, {ERarity::Mythic, ERarity::Legendary, 0.979},
        {ERarity::Mythic, ERarity::Mythic, 0.015},
        {ERarity::Ultra, ERarity::Legendary, 0.045}, {ERarity::Ultra, ERarity::Mythic, 0.945},
        {ERarity::Ultra, ERarity::Ultra, 0.010},
        {ERarity::Super, ERarity::Mythic, 0.127}, {ERarity::Super, ERarity::Ultra, 0.873},
    });
}

inline void RegisterWikiId5DropTable(EMobType mob_type, EPetalType drop_type)
{
    RegisterDropRateTable(mob_type, drop_type, {
        {ERarity::Common, ERarity::Common, 0.638}, {ERarity::Common, ERarity::Unusual, 0.194},
        {ERarity::Unusual, ERarity::Common, 0.420}, {ERarity::Unusual, ERarity::Unusual, 0.579},
        {ERarity::Rare, ERarity::Unusual, 0.805}, {ERarity::Rare, ERarity::Rare, 0.195},
        {ERarity::Epic, ERarity::Unusual, 0.039}, {ERarity::Epic, ERarity::Rare, 0.815},
        {ERarity::Epic, ERarity::Epic, 0.146},
        {ERarity::Legendary, ERarity::Rare, 0.043}, {ERarity::Legendary, ERarity::Epic, 0.897},
        {ERarity::Legendary, ERarity::Legendary, 0.060},
        {ERarity::Mythic, ERarity::Epic, 0.045}, {ERarity::Mythic, ERarity::Legendary, 0.946},
        {ERarity::Mythic, ERarity::Mythic, 0.009},
        {ERarity::Ultra, ERarity::Legendary, 0.252}, {ERarity::Ultra, ERarity::Mythic, 0.744},
        {ERarity::Ultra, ERarity::Ultra, 0.005},
        {ERarity::Super, ERarity::Mythic, 0.290}, {ERarity::Super, ERarity::Ultra, 0.710},
    });
}

inline void RegisterWikiId5MythicDropTable(EMobType mob_type, EPetalType drop_type)
{
    RegisterDropRateTable(mob_type, drop_type, {
        {ERarity::Mythic, ERarity::Mythic, 0.009},
        {ERarity::Ultra, ERarity::Mythic, 0.838}, {ERarity::Ultra, ERarity::Ultra, 0.006},
        {ERarity::Super, ERarity::Mythic, 0.290}, {ERarity::Super, ERarity::Ultra, 0.710},
    });
}

inline void RegisterWikiAntEggMobDropTable(EMobType mob_type)
{
    RegisterDropRateTable(mob_type, EPetalType::AntEgg, {
        {ERarity::Common, ERarity::Common, 0.598}, {ERarity::Common, ERarity::Unusual, 0.164},
        {ERarity::Unusual, ERarity::Common, 0.485}, {ERarity::Unusual, ERarity::Unusual, 0.512},
        {ERarity::Rare, ERarity::Unusual, 0.834}, {ERarity::Rare, ERarity::Rare, 0.165},
        {ERarity::Epic, ERarity::Unusual, 0.067}, {ERarity::Epic, ERarity::Rare, 0.810},
        {ERarity::Epic, ERarity::Epic, 0.123},
        {ERarity::Legendary, ERarity::Rare, 0.072}, {ERarity::Legendary, ERarity::Epic, 0.877},
        {ERarity::Legendary, ERarity::Legendary, 0.050},
        {ERarity::Mythic, ERarity::Epic, 0.075}, {ERarity::Mythic, ERarity::Legendary, 0.917},
        {ERarity::Mythic, ERarity::Mythic, 0.008},
        {ERarity::Ultra, ERarity::Legendary, 0.212}, {ERarity::Ultra, ERarity::Mythic, 0.783},
        {ERarity::Ultra, ERarity::Ultra, 0.005},
        {ERarity::Super, ERarity::Mythic, 0.356}, {ERarity::Super, ERarity::Ultra, 0.644},
    });
}

inline void RegisterWikiFireAntEggDropTable(EMobType mob_type, EPetalType drop_type = EPetalType::AntEgg)
{
    RegisterDropRateTable(mob_type, drop_type, {
        {ERarity::Common, ERarity::Common, 0.598}, {ERarity::Common, ERarity::Unusual, 0.164},
        {ERarity::Unusual, ERarity::Common, 0.485}, {ERarity::Unusual, ERarity::Unusual, 0.512},
        {ERarity::Rare, ERarity::Unusual, 0.834}, {ERarity::Rare, ERarity::Rare, 0.165},
        {ERarity::Epic, ERarity::Unusual, 0.067}, {ERarity::Epic, ERarity::Rare, 0.810},
        {ERarity::Epic, ERarity::Epic, 0.123},
        {ERarity::Legendary, ERarity::Rare, 0.072}, {ERarity::Legendary, ERarity::Epic, 0.877},
        {ERarity::Legendary, ERarity::Legendary, 0.050},
        {ERarity::Mythic, ERarity::Epic, 0.075}, {ERarity::Mythic, ERarity::Legendary, 0.917},
        {ERarity::Mythic, ERarity::Mythic, 0.008},
        {ERarity::Ultra, ERarity::Legendary, 0.856}, {ERarity::Ultra, ERarity::Mythic, 0.143},
        {ERarity::Ultra, ERarity::Ultra, 0.0005},
        {ERarity::Super, ERarity::Mythic, 0.773}, {ERarity::Super, ERarity::Ultra, 0.227},
    });
}

inline void RegisterWikiWorkerCornDropTable(EMobType mob_type)
{
    RegisterDropRateTable(mob_type, EPetalType::Corn, {
        {ERarity::Common, ERarity::Common, 0.455}, {ERarity::Common, ERarity::Unusual, 0.101},
        {ERarity::Unusual, ERarity::Common, 0.613}, {ERarity::Unusual, ERarity::Unusual, 0.348},
        {ERarity::Rare, ERarity::Common, 0.014}, {ERarity::Rare, ERarity::Unusual, 0.883},
        {ERarity::Rare, ERarity::Rare, 0.103},
        {ERarity::Epic, ERarity::Unusual, 0.197}, {ERarity::Epic, ERarity::Rare, 0.727},
        {ERarity::Epic, ERarity::Epic, 0.076},
        {ERarity::Legendary, ERarity::Rare, 0.207}, {ERarity::Legendary, ERarity::Epic, 0.763},
        {ERarity::Legendary, ERarity::Legendary, 0.031},
        {ERarity::Mythic, ERarity::Epic, 0.212}, {ERarity::Mythic, ERarity::Legendary, 0.783},
        {ERarity::Mythic, ERarity::Mythic, 0.005},
        {ERarity::Ultra, ERarity::Legendary, 0.394}, {ERarity::Ultra, ERarity::Mythic, 0.603},
        {ERarity::Ultra, ERarity::Ultra, 0.003},
        {ERarity::Super, ERarity::Mythic, 0.538}, {ERarity::Super, ERarity::Ultra, 0.462},
    });
}

inline void RegisterWikiFireWorkerCornDropTable(EMobType mob_type)
{
    RegisterDropRateTable(mob_type, EPetalType::Corn, {
        {ERarity::Common, ERarity::Common, 0.455}, {ERarity::Common, ERarity::Unusual, 0.101},
        {ERarity::Unusual, ERarity::Common, 0.613}, {ERarity::Unusual, ERarity::Unusual, 0.348},
        {ERarity::Rare, ERarity::Common, 0.014}, {ERarity::Rare, ERarity::Unusual, 0.883},
        {ERarity::Rare, ERarity::Rare, 0.103},
        {ERarity::Epic, ERarity::Unusual, 0.197}, {ERarity::Epic, ERarity::Rare, 0.727},
        {ERarity::Epic, ERarity::Epic, 0.076},
        {ERarity::Legendary, ERarity::Rare, 0.207}, {ERarity::Legendary, ERarity::Epic, 0.763},
        {ERarity::Legendary, ERarity::Legendary, 0.031},
        {ERarity::Mythic, ERarity::Epic, 0.212}, {ERarity::Mythic, ERarity::Legendary, 0.783},
        {ERarity::Mythic, ERarity::Mythic, 0.005},
        {ERarity::Ultra, ERarity::Legendary, 0.502}, {ERarity::Ultra, ERarity::Mythic, 0.496},
        {ERarity::Ultra, ERarity::Ultra, 0.002},
        {ERarity::Super, ERarity::Mythic, 0.538}, {ERarity::Super, ERarity::Ultra, 0.462},
    });
}

inline void RegisterWikiAntHoleDropTables()
{
    RegisterDropRateTable(EMobType::AntHole, EPetalType::Soil, {
        {ERarity::Common, ERarity::Common, 0.598}, {ERarity::Common, ERarity::Unusual, 0.164},
        {ERarity::Unusual, ERarity::Common, 0.485}, {ERarity::Unusual, ERarity::Unusual, 0.512},
        {ERarity::Rare, ERarity::Common, 0.013}, {ERarity::Rare, ERarity::Unusual, 0.884},
        {ERarity::Rare, ERarity::Rare, 0.103},
        {ERarity::Epic, ERarity::Unusual, 0.067}, {ERarity::Epic, ERarity::Rare, 0.810},
        {ERarity::Epic, ERarity::Epic, 0.123},
        {ERarity::Legendary, ERarity::Rare, 0.072}, {ERarity::Legendary, ERarity::Epic, 0.877},
        {ERarity::Legendary, ERarity::Legendary, 0.050},
        {ERarity::Mythic, ERarity::Epic, 0.075}, {ERarity::Mythic, ERarity::Legendary, 0.917},
        {ERarity::Mythic, ERarity::Mythic, 0.008},
        {ERarity::Ultra, ERarity::Legendary, 0.852}, {ERarity::Ultra, ERarity::Mythic, 0.143},
        {ERarity::Ultra, ERarity::Ultra, 0.005},
        {ERarity::Super, ERarity::Mythic, 0.773}, {ERarity::Super, ERarity::Ultra, 0.227},
    });
    RegisterDropRateTable(EMobType::AntHole, EPetalType::Shovel, {
        {ERarity::Common, ERarity::Common, 0.539}, {ERarity::Common, ERarity::Unusual, 0.133},
        {ERarity::Unusual, ERarity::Common, 0.553}, {ERarity::Unusual, ERarity::Unusual, 0.436},
        {ERarity::Rare, ERarity::Common, 0.003}, {ERarity::Rare, ERarity::Unusual, 0.862},
        {ERarity::Rare, ERarity::Rare, 0.135},
        {ERarity::Epic, ERarity::Unusual, 0.114}, {ERarity::Epic, ERarity::Rare, 0.786},
        {ERarity::Epic, ERarity::Epic, 0.100},
        {ERarity::Legendary, ERarity::Rare, 0.122}, {ERarity::Legendary, ERarity::Epic, 0.837},
        {ERarity::Legendary, ERarity::Legendary, 0.041},
        {ERarity::Mythic, ERarity::Epic, 0.126}, {ERarity::Mythic, ERarity::Legendary, 0.867},
        {ERarity::Mythic, ERarity::Mythic, 0.006},
        {ERarity::Ultra, ERarity::Legendary, 0.883}, {ERarity::Ultra, ERarity::Mythic, 0.116},
        {ERarity::Ultra, ERarity::Ultra, 0.0004},
        {ERarity::Super, ERarity::Mythic, 0.812}, {ERarity::Super, ERarity::Ultra, 0.186},
    });
}

inline void RegisterWikiQueenFireAntDropTables()
{
    RegisterDropRateTable(EMobType::FireQueenAnt, EPetalType::AntEgg, {
        {ERarity::Common, ERarity::Common, 0.662}, {ERarity::Common, ERarity::Unusual, 0.306},
        {ERarity::Unusual, ERarity::Common, 0.232}, {ERarity::Unusual, ERarity::Unusual, 0.768},
        {ERarity::Rare, ERarity::Unusual, 0.697}, {ERarity::Rare, ERarity::Rare, 0.303},
        {ERarity::Epic, ERarity::Rare, 0.764}, {ERarity::Epic, ERarity::Epic, 0.231},
        {ERarity::Legendary, ERarity::Rare, 0.005}, {ERarity::Legendary, ERarity::Epic, 0.897},
        {ERarity::Legendary, ERarity::Legendary, 0.098},
        {ERarity::Mythic, ERarity::Epic, 0.006}, {ERarity::Mythic, ERarity::Legendary, 0.979},
        {ERarity::Mythic, ERarity::Mythic, 0.015},
        {ERarity::Ultra, ERarity::Legendary, 0.733}, {ERarity::Ultra, ERarity::Mythic, 0.266},
        {ERarity::Ultra, ERarity::Ultra, 0.001},
        {ERarity::Super, ERarity::Mythic, 0.597}, {ERarity::Super, ERarity::Ultra, 0.403},
    });
    RegisterDropRateTable(EMobType::FireQueenAnt, EPetalType::Basil, {
        {ERarity::Mythic, ERarity::Mythic, 0.009},
        {ERarity::Ultra, ERarity::Mythic, 0.169}, {ERarity::Ultra, ERarity::Ultra, 0.0006},
        {ERarity::Super, ERarity::Mythic, 0.734}, {ERarity::Super, ERarity::Ultra, 0.266},
    });
    RegisterDropRate(EMobType::FireQueenAnt, ERarity::Ultra, EPetalType::BrokenEgg, ERarity::Ultra, 0.0057);
    RegisterDropRate(EMobType::FireQueenAnt, ERarity::Super, EPetalType::BrokenEgg, ERarity::Ultra, 0.6745);
}

inline void RegisterWikiTermiteRelicDropTable(EMobType mob_type)
{
    RegisterDropRateTable(mob_type, EPetalType::Relic, {
        {ERarity::Common, ERarity::Unusual, 0.035},
        {ERarity::Unusual, ERarity::Unusual, 0.132},
        {ERarity::Rare, ERarity::Unusual, 0.722}, {ERarity::Rare, ERarity::Rare, 0.035},
        {ERarity::Epic, ERarity::Unusual, 0.582}, {ERarity::Epic, ERarity::Rare, 0.392},
        {ERarity::Epic, ERarity::Epic, 0.026},
        {ERarity::Legendary, ERarity::Rare, 0.591}, {ERarity::Legendary, ERarity::Epic, 0.399},
        {ERarity::Legendary, ERarity::Legendary, 0.010},
        {ERarity::Mythic, ERarity::Epic, 0.596}, {ERarity::Mythic, ERarity::Legendary, 0.402},
        {ERarity::Mythic, ERarity::Mythic, 0.002},
        {ERarity::Ultra, ERarity::Legendary, 0.969}, {ERarity::Ultra, ERarity::Mythic, 0.030},
        {ERarity::Ultra, ERarity::Ultra, 0.0001},
        {ERarity::Super, ERarity::Mythic, 0.950}, {ERarity::Super, ERarity::Ultra, 0.050},
    });
}

inline void RegisterWikiTermiteOvermindRelicDropTable()
{
    RegisterDropRateTable(EMobType::TermiteOvermind, EPetalType::Relic, {
        {ERarity::Common, ERarity::Unusual, 0.306},
        {ERarity::Unusual, ERarity::Unusual, 0.768},
        {ERarity::Rare, ERarity::Unusual, 0.697}, {ERarity::Rare, ERarity::Rare, 0.303},
        {ERarity::Epic, ERarity::Rare, 0.764}, {ERarity::Epic, ERarity::Epic, 0.231},
        {ERarity::Legendary, ERarity::Rare, 0.005}, {ERarity::Legendary, ERarity::Epic, 0.897},
        {ERarity::Legendary, ERarity::Legendary, 0.098},
        {ERarity::Mythic, ERarity::Epic, 0.006}, {ERarity::Mythic, ERarity::Legendary, 0.979},
        {ERarity::Mythic, ERarity::Mythic, 0.015},
        {ERarity::Ultra, ERarity::Legendary, 0.733}, {ERarity::Ultra, ERarity::Mythic, 0.266},
        {ERarity::Ultra, ERarity::Ultra, 0.001},
        {ERarity::Super, ERarity::Mythic, 0.597}, {ERarity::Super, ERarity::Ultra, 0.403},
    });
}

inline void RegisterWikiTermiteOvermindCompassDropTable()
{
    RegisterDropRate(EMobType::TermiteOvermind, ERarity::Ultra, EPetalType::Compass, ERarity::Ultra, 0.020);
    RegisterDropRate(EMobType::TermiteOvermind, ERarity::Super, EPetalType::Compass, ERarity::Ultra, 0.984);
}

inline void RegisterWikiAntDropRates()
{
    RemoveDropRatesForMob(EMobType::AntEgg);
    RemoveDropRatesForMob(EMobType::BabyAnt);
    RemoveDropRatesForMob(EMobType::WorkerAnt);
    RemoveDropRatesForMob(EMobType::SoldierAnt, {EPetalType::Wing});
    RemoveDropRatesForMob(EMobType::QueenAnt, {EPetalType::BrokenEgg});
    RemoveDropRatesForMob(EMobType::AntHole);
    RemoveDropRatesForMob(EMobType::FireAntEgg);
    RemoveDropRatesForMob(EMobType::BabyFireAnt);
    RemoveDropRatesForMob(EMobType::WorkerFireAnt);
    RemoveDropRatesForMob(EMobType::SoldierFireAnt);
    RemoveDropRatesForMob(EMobType::FireQueenAnt);
    RemoveDropRatesForMob(EMobType::TermiteEgg);
    RemoveDropRatesForMob(EMobType::BabyTermite);
    RemoveDropRatesForMob(EMobType::WorkerTermite);
    RemoveDropRatesForMob(EMobType::SoldierTermite);
    RemoveDropRatesForMob(EMobType::TermiteOvermind);

    RegisterWikiAntEggMobDropTable(EMobType::AntEgg);
    RegisterWikiId6DropTable(EMobType::BabyAnt, EPetalType::Light);
    RegisterWikiId7UnusualDropTable(EMobType::BabyAnt, EPetalType::Leaf);
    RegisterWikiId6DropTable(EMobType::BabyAnt, EPetalType::Rice);
    RegisterWikiId7UnusualDropTable(EMobType::WorkerAnt, EPetalType::Leaf);
    RegisterWikiWorkerCornDropTable(EMobType::WorkerAnt);
    RegisterWikiId5DropTable(EMobType::SoldierAnt, EPetalType::Glass);
    RegisterWikiId4DropTable(EMobType::QueenAnt, EPetalType::AntEgg);
    RegisterWikiId5MythicDropTable(EMobType::QueenAnt, EPetalType::Basil);
    RegisterWikiAntHoleDropTables();

    RegisterWikiFireAntEggDropTable(EMobType::FireAntEgg);
    RegisterWikiId6DropTable(EMobType::BabyFireAnt, EPetalType::Light);
    RegisterWikiFireWorkerCornDropTable(EMobType::WorkerFireAnt);
    RegisterWikiId6DropTable(EMobType::SoldierFireAnt, EPetalType::Triangle);
    RegisterWikiQueenFireAntDropTables();

    RegisterWikiFireAntEggDropTable(EMobType::TermiteEgg);
    RegisterWikiTermiteRelicDropTable(EMobType::BabyTermite);
    RegisterWikiId6DropTable(EMobType::BabyTermite, EPetalType::Carrot);
    RegisterWikiTermiteRelicDropTable(EMobType::WorkerTermite);
    RegisterWikiTermiteRelicDropTable(EMobType::SoldierTermite);
    RegisterWikiFireAntEggDropTable(EMobType::SoldierTermite, EPetalType::Bone);
    RegisterWikiTermiteOvermindRelicDropTable();
    RegisterWikiTermiteOvermindCompassDropTable();
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

    RegisterDropRate(EMobType::Rock, ERarity::Common, EPetalType::Rock, ERarity::Common, 0.455);
    RegisterDropRate(EMobType::Rock, ERarity::Common, EPetalType::Rock, ERarity::Unusual, 0.101);
    RegisterDropRate(EMobType::Rock, ERarity::Unusual, EPetalType::Rock, ERarity::Common, 0.613);
    RegisterDropRate(EMobType::Rock, ERarity::Unusual, EPetalType::Rock, ERarity::Unusual, 0.348);
    RegisterDropRate(EMobType::Rock, ERarity::Rare, EPetalType::Rock, ERarity::Common, 0.014);
    RegisterDropRate(EMobType::Rock, ERarity::Rare, EPetalType::Rock, ERarity::Unusual, 0.883);
    RegisterDropRate(EMobType::Rock, ERarity::Rare, EPetalType::Rock, ERarity::Rare, 0.103);
    RegisterDropRate(EMobType::Rock, ERarity::Rare, EPetalType::Heavy, ERarity::Rare, 0.035);
    RegisterDropRate(EMobType::Rock, ERarity::Epic, EPetalType::Rock, ERarity::Unusual, 0.197);
    RegisterDropRate(EMobType::Rock, ERarity::Epic, EPetalType::Rock, ERarity::Rare, 0.727);
    RegisterDropRate(EMobType::Rock, ERarity::Epic, EPetalType::Rock, ERarity::Epic, 0.076);
    RegisterDropRate(EMobType::Rock, ERarity::Epic, EPetalType::Heavy, ERarity::Rare, 0.392);
    RegisterDropRate(EMobType::Rock, ERarity::Epic, EPetalType::Heavy, ERarity::Epic, 0.026);
    RegisterDropRate(EMobType::Rock, ERarity::Legendary, EPetalType::Rock, ERarity::Rare, 0.207);
    RegisterDropRate(EMobType::Rock, ERarity::Legendary, EPetalType::Rock, ERarity::Epic, 0.763);
    RegisterDropRate(EMobType::Rock, ERarity::Legendary, EPetalType::Rock, ERarity::Legendary, 0.031);
    RegisterDropRate(EMobType::Rock, ERarity::Legendary, EPetalType::Heavy, ERarity::Rare, 0.591);
    RegisterDropRate(EMobType::Rock, ERarity::Legendary, EPetalType::Heavy, ERarity::Epic, 0.399);
    RegisterDropRate(EMobType::Rock, ERarity::Legendary, EPetalType::Heavy, ERarity::Legendary, 0.010);
    RegisterDropRate(EMobType::Rock, ERarity::Mythic, EPetalType::Rock, ERarity::Epic, 0.212);
    RegisterDropRate(EMobType::Rock, ERarity::Mythic, EPetalType::Rock, ERarity::Legendary, 0.783);
    RegisterDropRate(EMobType::Rock, ERarity::Mythic, EPetalType::Rock, ERarity::Mythic, 0.005);
    RegisterDropRate(EMobType::Rock, ERarity::Mythic, EPetalType::Heavy, ERarity::Epic, 0.596);
    RegisterDropRate(EMobType::Rock, ERarity::Mythic, EPetalType::Heavy, ERarity::Legendary, 0.402);
    RegisterDropRate(EMobType::Rock, ERarity::Mythic, EPetalType::Heavy, ERarity::Mythic, 0.002);
    RegisterDropRate(EMobType::Rock, ERarity::Ultra, EPetalType::Rock, ERarity::Legendary, 0.918);
    RegisterDropRate(EMobType::Rock, ERarity::Ultra, EPetalType::Rock, ERarity::Mythic, 0.089);
    RegisterDropRate(EMobType::Rock, ERarity::Ultra, EPetalType::Rock, ERarity::Ultra, 0.0003);
    RegisterDropRate(EMobType::Rock, ERarity::Ultra, EPetalType::Heavy, ERarity::Legendary, 0.969);
    RegisterDropRate(EMobType::Rock, ERarity::Ultra, EPetalType::Heavy, ERarity::Mythic, 0.030);
    RegisterDropRate(EMobType::Rock, ERarity::Ultra, EPetalType::Heavy, ERarity::Ultra, 0.0001);
    RegisterDropRate(EMobType::Rock, ERarity::Ultra, EPetalType::Moon, ERarity::Ultra, 0.00001);
    RegisterDropRate(EMobType::Rock, ERarity::Super, EPetalType::Rock, ERarity::Mythic, 0.857);
    RegisterDropRate(EMobType::Rock, ERarity::Super, EPetalType::Rock, ERarity::Ultra, 0.143);
    RegisterDropRate(EMobType::Rock, ERarity::Super, EPetalType::Heavy, ERarity::Mythic, 0.950);
    RegisterDropRate(EMobType::Rock, ERarity::Super, EPetalType::Heavy, ERarity::Ultra, 0.050);
    RegisterDropRate(EMobType::Rock, ERarity::Super, EPetalType::Moon, ERarity::Ultra, 0.005);

    RegisterDropRate(EMobType::NormalLadybug, ERarity::Common, EPetalType::Light, ERarity::Common, 0.56);
    RegisterDropRate(EMobType::NormalLadybug, ERarity::Common, EPetalType::Light, ERarity::Unusual, 0.44);
    RegisterDropRate(EMobType::NormalLadybug, ERarity::Common, EPetalType::Rose, ERarity::Common, 0.667);
    RegisterDropRate(EMobType::NormalLadybug, ERarity::Common, EPetalType::Rose, ERarity::Unusual, 0.333);
    RegisterDropRate(EMobType::NormalLadybug, ERarity::Unusual, EPetalType::Light, ERarity::Unusual, 0.440);
    RegisterDropRate(EMobType::NormalLadybug, ERarity::Unusual, EPetalType::Light, ERarity::Common, 0.560);
    RegisterDropRate(EMobType::NormalLadybug, ERarity::Unusual, EPetalType::Rose, ERarity::Unusual, 0.333);
    RegisterDropRate(EMobType::NormalLadybug, ERarity::Unusual, EPetalType::Rose, ERarity::Common, 0.667);
    RegisterDropRate(EMobType::NormalLadybug, ERarity::Rare, EPetalType::Light, ERarity::Rare, 0.051);
    RegisterDropRate(EMobType::NormalLadybug, ERarity::Rare, EPetalType::Light, ERarity::Unusual, 0.385);
    RegisterDropRate(EMobType::NormalLadybug, ERarity::Rare, EPetalType::Light, ERarity::Common, 0.564);
    RegisterDropRate(EMobType::NormalLadybug, ERarity::Rare, EPetalType::Rose, ERarity::Rare, 0.051);
    RegisterDropRate(EMobType::NormalLadybug, ERarity::Rare, EPetalType::Rose, ERarity::Unusual, 0.492);
    RegisterDropRate(EMobType::NormalLadybug, ERarity::Rare, EPetalType::Rose, ERarity::Common, 0.457);
    RegisterDropRate(EMobType::NormalLadybug, ERarity::Epic, EPetalType::Light, ERarity::Epic, 0.030);
    RegisterDropRate(EMobType::NormalLadybug, ERarity::Epic, EPetalType::Light, ERarity::Rare, 0.380);
    RegisterDropRate(EMobType::NormalLadybug, ERarity::Epic, EPetalType::Light, ERarity::Unusual, 0.590);
    RegisterDropRate(EMobType::NormalLadybug, ERarity::Epic, EPetalType::Rose, ERarity::Epic, 0.030);
    RegisterDropRate(EMobType::NormalLadybug, ERarity::Epic, EPetalType::Rose, ERarity::Rare, 0.487);
    RegisterDropRate(EMobType::NormalLadybug, ERarity::Epic, EPetalType::Rose, ERarity::Unusual, 0.483);
    RegisterDropRate(EMobType::NormalLadybug, ERarity::Legendary, EPetalType::Light, ERarity::Legendary, 0.010);
    RegisterDropRate(EMobType::NormalLadybug, ERarity::Legendary, EPetalType::Light, ERarity::Epic, 0.375);
    RegisterDropRate(EMobType::NormalLadybug, ERarity::Legendary, EPetalType::Light, ERarity::Rare, 0.615);
    RegisterDropRate(EMobType::NormalLadybug, ERarity::Legendary, EPetalType::Rose, ERarity::Legendary, 0.010);
    RegisterDropRate(EMobType::NormalLadybug, ERarity::Legendary, EPetalType::Rose, ERarity::Epic, 0.482);
    RegisterDropRate(EMobType::NormalLadybug, ERarity::Legendary, EPetalType::Rose, ERarity::Rare, 0.508);
    RegisterDropRate(EMobType::NormalLadybug, ERarity::Mythic, EPetalType::Light, ERarity::Mythic, 0.003);
    RegisterDropRate(EMobType::NormalLadybug, ERarity::Mythic, EPetalType::Light, ERarity::Legendary, 0.372);
    RegisterDropRate(EMobType::NormalLadybug, ERarity::Mythic, EPetalType::Light, ERarity::Epic, 0.625);
    RegisterDropRate(EMobType::NormalLadybug, ERarity::Mythic, EPetalType::Rose, ERarity::Mythic, 0.003);
    RegisterDropRate(EMobType::NormalLadybug, ERarity::Mythic, EPetalType::Rose, ERarity::Legendary, 0.479);
    RegisterDropRate(EMobType::NormalLadybug, ERarity::Mythic, EPetalType::Rose, ERarity::Epic, 0.518);
    RegisterDropRate(EMobType::NormalLadybug, ERarity::Ultra, EPetalType::Light, ERarity::Ultra, 0.0005);
    RegisterDropRate(EMobType::NormalLadybug, ERarity::Ultra, EPetalType::Light, ERarity::Mythic, 0.3585);
    RegisterDropRate(EMobType::NormalLadybug, ERarity::Ultra, EPetalType::Light, ERarity::Legendary, 0.641);
    RegisterDropRate(EMobType::NormalLadybug, ERarity::Ultra, EPetalType::Rose, ERarity::Ultra, 0.0005);
    RegisterDropRate(EMobType::NormalLadybug, ERarity::Ultra, EPetalType::Rose, ERarity::Mythic, 0.4655);
    RegisterDropRate(EMobType::NormalLadybug, ERarity::Ultra, EPetalType::Rose, ERarity::Legendary, 0.534);
    RegisterDropRate(EMobType::NormalLadybug, ERarity::Super, EPetalType::Light, ERarity::Super, 0.000008);
    RegisterDropRate(EMobType::NormalLadybug, ERarity::Super, EPetalType::Light, ERarity::Ultra, 0.357992);
    RegisterDropRate(EMobType::NormalLadybug, ERarity::Super, EPetalType::Light, ERarity::Mythic, 0.642);
    RegisterDropRate(EMobType::NormalLadybug, ERarity::Super, EPetalType::Rose, ERarity::Super, 0.000008);
    RegisterDropRate(EMobType::NormalLadybug, ERarity::Super, EPetalType::Rose, ERarity::Ultra, 0.464992);
    RegisterDropRate(EMobType::NormalLadybug, ERarity::Super, EPetalType::Rose, ERarity::Mythic, 0.535);

    RegisterDropRate(EMobType::Bee, ERarity::Common, EPetalType::Stinger, ERarity::Common, 0.598);
    RegisterDropRate(EMobType::Bee, ERarity::Common, EPetalType::Stinger, ERarity::Unusual, 0.164);
    RegisterDropRate(EMobType::Bee, ERarity::Common, EPetalType::Pollen, ERarity::Unusual, 0.069);
    RegisterDropRate(EMobType::Bee, ERarity::Unusual, EPetalType::Stinger, ERarity::Common, 0.485);
    RegisterDropRate(EMobType::Bee, ERarity::Unusual, EPetalType::Stinger, ERarity::Unusual, 0.512);
    RegisterDropRate(EMobType::Bee, ERarity::Unusual, EPetalType::Pollen, ERarity::Unusual, 0.247);
    RegisterDropRate(EMobType::Bee, ERarity::Rare, EPetalType::Stinger, ERarity::Unusual, 0.834);
    RegisterDropRate(EMobType::Bee, ERarity::Rare, EPetalType::Stinger, ERarity::Rare, 0.165);
    RegisterDropRate(EMobType::Bee, ERarity::Rare, EPetalType::Honey, ERarity::Rare, 0.070);
    RegisterDropRate(EMobType::Bee, ERarity::Rare, EPetalType::Pollen, ERarity::Unusual, 0.872);
    RegisterDropRate(EMobType::Bee, ERarity::Rare, EPetalType::Pollen, ERarity::Rare, 0.070);
    RegisterDropRate(EMobType::Bee, ERarity::Epic, EPetalType::Stinger, ERarity::Unusual, 0.067);
    RegisterDropRate(EMobType::Bee, ERarity::Epic, EPetalType::Stinger, ERarity::Rare, 0.810);
    RegisterDropRate(EMobType::Bee, ERarity::Epic, EPetalType::Stinger, ERarity::Epic, 0.123);
    RegisterDropRate(EMobType::Bee, ERarity::Epic, EPetalType::Honey, ERarity::Rare, 0.610);
    RegisterDropRate(EMobType::Bee, ERarity::Epic, EPetalType::Honey, ERarity::Epic, 0.051);
    RegisterDropRate(EMobType::Bee, ERarity::Epic, EPetalType::Pollen, ERarity::Unusual, 0.338);
    RegisterDropRate(EMobType::Bee, ERarity::Epic, EPetalType::Pollen, ERarity::Rare, 0.610);
    RegisterDropRate(EMobType::Bee, ERarity::Epic, EPetalType::Pollen, ERarity::Epic, 0.051);
    RegisterDropRate(EMobType::Bee, ERarity::Legendary, EPetalType::Stinger, ERarity::Rare, 0.072);
    RegisterDropRate(EMobType::Bee, ERarity::Legendary, EPetalType::Stinger, ERarity::Epic, 0.877);
    RegisterDropRate(EMobType::Bee, ERarity::Legendary, EPetalType::Stinger, ERarity::Legendary, 0.050);
    RegisterDropRate(EMobType::Bee, ERarity::Legendary, EPetalType::Honey, ERarity::Rare, 0.349);
    RegisterDropRate(EMobType::Bee, ERarity::Legendary, EPetalType::Honey, ERarity::Epic, 0.630);
    RegisterDropRate(EMobType::Bee, ERarity::Legendary, EPetalType::Honey, ERarity::Legendary, 0.020);
    RegisterDropRate(EMobType::Bee, ERarity::Legendary, EPetalType::Pollen, ERarity::Rare, 0.349);
    RegisterDropRate(EMobType::Bee, ERarity::Legendary, EPetalType::Pollen, ERarity::Epic, 0.630);
    RegisterDropRate(EMobType::Bee, ERarity::Legendary, EPetalType::Pollen, ERarity::Legendary, 0.020);
    RegisterDropRate(EMobType::Bee, ERarity::Mythic, EPetalType::Stinger, ERarity::Epic, 0.075);
    RegisterDropRate(EMobType::Bee, ERarity::Mythic, EPetalType::Stinger, ERarity::Legendary, 0.917);
    RegisterDropRate(EMobType::Bee, ERarity::Mythic, EPetalType::Stinger, ERarity::Mythic, 0.008);
    RegisterDropRate(EMobType::Bee, ERarity::Mythic, EPetalType::Honey, ERarity::Epic, 0.356);
    RegisterDropRate(EMobType::Bee, ERarity::Mythic, EPetalType::Honey, ERarity::Legendary, 0.641);
    RegisterDropRate(EMobType::Bee, ERarity::Mythic, EPetalType::Honey, ERarity::Mythic, 0.003);
    RegisterDropRate(EMobType::Bee, ERarity::Mythic, EPetalType::Pollen, ERarity::Epic, 0.356);
    RegisterDropRate(EMobType::Bee, ERarity::Mythic, EPetalType::Pollen, ERarity::Legendary, 0.641);
    RegisterDropRate(EMobType::Bee, ERarity::Mythic, EPetalType::Pollen, ERarity::Mythic, 0.003);
    RegisterDropRate(EMobType::Bee, ERarity::Ultra, EPetalType::Stinger, ERarity::Legendary, 0.212);
    RegisterDropRate(EMobType::Bee, ERarity::Ultra, EPetalType::Stinger, ERarity::Mythic, 0.783);
    RegisterDropRate(EMobType::Bee, ERarity::Ultra, EPetalType::Stinger, ERarity::Ultra, 0.005);
    RegisterDropRate(EMobType::Bee, ERarity::Ultra, EPetalType::Honey, ERarity::Legendary, 0.538);
    RegisterDropRate(EMobType::Bee, ERarity::Ultra, EPetalType::Honey, ERarity::Mythic, 0.460);
    RegisterDropRate(EMobType::Bee, ERarity::Ultra, EPetalType::Honey, ERarity::Ultra, 0.002);
    RegisterDropRate(EMobType::Bee, ERarity::Ultra, EPetalType::Pollen, ERarity::Legendary, 0.538);
    RegisterDropRate(EMobType::Bee, ERarity::Ultra, EPetalType::Pollen, ERarity::Mythic, 0.460);
    RegisterDropRate(EMobType::Bee, ERarity::Ultra, EPetalType::Pollen, ERarity::Ultra, 0.002);
    RegisterDropRate(EMobType::Bee, ERarity::Super, EPetalType::Stinger, ERarity::Mythic, 0.356);
    RegisterDropRate(EMobType::Bee, ERarity::Super, EPetalType::Stinger, ERarity::Ultra, 0.644);
    RegisterDropRate(EMobType::Bee, ERarity::Super, EPetalType::Honey, ERarity::Mythic, 0.662);
    RegisterDropRate(EMobType::Bee, ERarity::Super, EPetalType::Honey, ERarity::Ultra, 0.338);
    RegisterDropRate(EMobType::Bee, ERarity::Super, EPetalType::Pollen, ERarity::Mythic, 0.662);
    RegisterDropRate(EMobType::Bee, ERarity::Super, EPetalType::Pollen, ERarity::Ultra, 0.338);

    RegisterDropRate(EMobType::WorkerAnt, ERarity::Common, EPetalType::Leaf, ERarity::Unusual, 0.101);
    RegisterDropRate(EMobType::WorkerAnt, ERarity::Common, EPetalType::Corn, ERarity::Common, 0.455);
    RegisterDropRate(EMobType::WorkerAnt, ERarity::Common, EPetalType::Corn, ERarity::Unusual, 0.101);
    RegisterDropRate(EMobType::WorkerAnt, ERarity::Unusual, EPetalType::Leaf, ERarity::Unusual, 0.348);
    RegisterDropRate(EMobType::WorkerAnt, ERarity::Unusual, EPetalType::Corn, ERarity::Common, 0.613);
    RegisterDropRate(EMobType::WorkerAnt, ERarity::Unusual, EPetalType::Corn, ERarity::Unusual, 0.348);
    RegisterDropRate(EMobType::WorkerAnt, ERarity::Rare, EPetalType::Leaf, ERarity::Unusual, 0.883);
    RegisterDropRate(EMobType::WorkerAnt, ERarity::Rare, EPetalType::Leaf, ERarity::Rare, 0.103);
    RegisterDropRate(EMobType::WorkerAnt, ERarity::Rare, EPetalType::Corn, ERarity::Common, 0.014);
    RegisterDropRate(EMobType::WorkerAnt, ERarity::Rare, EPetalType::Corn, ERarity::Unusual, 0.883);
    RegisterDropRate(EMobType::WorkerAnt, ERarity::Rare, EPetalType::Corn, ERarity::Rare, 0.103);
    RegisterDropRate(EMobType::WorkerAnt, ERarity::Epic, EPetalType::Leaf, ERarity::Unusual, 0.197);
    RegisterDropRate(EMobType::WorkerAnt, ERarity::Epic, EPetalType::Leaf, ERarity::Rare, 0.727);
    RegisterDropRate(EMobType::WorkerAnt, ERarity::Epic, EPetalType::Leaf, ERarity::Epic, 0.076);
    RegisterDropRate(EMobType::WorkerAnt, ERarity::Epic, EPetalType::Corn, ERarity::Unusual, 0.197);
    RegisterDropRate(EMobType::WorkerAnt, ERarity::Epic, EPetalType::Corn, ERarity::Rare, 0.727);
    RegisterDropRate(EMobType::WorkerAnt, ERarity::Epic, EPetalType::Corn, ERarity::Epic, 0.076);
    RegisterDropRate(EMobType::WorkerAnt, ERarity::Legendary, EPetalType::Leaf, ERarity::Rare, 0.207);
    RegisterDropRate(EMobType::WorkerAnt, ERarity::Legendary, EPetalType::Leaf, ERarity::Epic, 0.763);
    RegisterDropRate(EMobType::WorkerAnt, ERarity::Legendary, EPetalType::Leaf, ERarity::Legendary, 0.031);
    RegisterDropRate(EMobType::WorkerAnt, ERarity::Legendary, EPetalType::Corn, ERarity::Rare, 0.207);
    RegisterDropRate(EMobType::WorkerAnt, ERarity::Legendary, EPetalType::Corn, ERarity::Epic, 0.763);
    RegisterDropRate(EMobType::WorkerAnt, ERarity::Legendary, EPetalType::Corn, ERarity::Legendary, 0.031);
    RegisterDropRate(EMobType::WorkerAnt, ERarity::Mythic, EPetalType::Leaf, ERarity::Epic, 0.212);
    RegisterDropRate(EMobType::WorkerAnt, ERarity::Mythic, EPetalType::Leaf, ERarity::Legendary, 0.783);
    RegisterDropRate(EMobType::WorkerAnt, ERarity::Mythic, EPetalType::Leaf, ERarity::Mythic, 0.005);
    RegisterDropRate(EMobType::WorkerAnt, ERarity::Mythic, EPetalType::Corn, ERarity::Epic, 0.212);
    RegisterDropRate(EMobType::WorkerAnt, ERarity::Mythic, EPetalType::Corn, ERarity::Legendary, 0.783);
    RegisterDropRate(EMobType::WorkerAnt, ERarity::Mythic, EPetalType::Corn, ERarity::Mythic, 0.005);
    RegisterDropRate(EMobType::WorkerAnt, ERarity::Ultra, EPetalType::Leaf, ERarity::Legendary, 0.394);
    RegisterDropRate(EMobType::WorkerAnt, ERarity::Ultra, EPetalType::Leaf, ERarity::Mythic, 0.603);
    RegisterDropRate(EMobType::WorkerAnt, ERarity::Ultra, EPetalType::Leaf, ERarity::Ultra, 0.003);
    RegisterDropRate(EMobType::WorkerAnt, ERarity::Ultra, EPetalType::Corn, ERarity::Legendary, 0.394);
    RegisterDropRate(EMobType::WorkerAnt, ERarity::Ultra, EPetalType::Corn, ERarity::Mythic, 0.603);
    RegisterDropRate(EMobType::WorkerAnt, ERarity::Ultra, EPetalType::Corn, ERarity::Ultra, 0.003);
    RegisterDropRate(EMobType::WorkerAnt, ERarity::Super, EPetalType::Leaf, ERarity::Mythic, 0.538);
    RegisterDropRate(EMobType::WorkerAnt, ERarity::Super, EPetalType::Leaf, ERarity::Ultra, 0.462);
    RegisterDropRate(EMobType::WorkerAnt, ERarity::Super, EPetalType::Corn, ERarity::Mythic, 0.538);
    RegisterDropRate(EMobType::WorkerAnt, ERarity::Super, EPetalType::Corn, ERarity::Ultra, 0.462);

    RegisterDropRate(EMobType::LeafPiece, ERarity::Common, EPetalType::Leaf, ERarity::Common, 0.352);
    RegisterDropRate(EMobType::LeafPiece, ERarity::Common, EPetalType::Leaf, ERarity::Unusual, 0.048);
    RegisterDropRate(EMobType::LeafPiece, ERarity::Unusual, EPetalType::Leaf, ERarity::Common, 0.393143);
    RegisterDropRate(EMobType::LeafPiece, ERarity::Unusual, EPetalType::Leaf, ERarity::Unusual, 0.092571);
    RegisterDropRate(EMobType::LeafPiece, ERarity::Rare, EPetalType::Leaf, ERarity::Unusual, 0.434286);
    RegisterDropRate(EMobType::LeafPiece, ERarity::Rare, EPetalType::Leaf, ERarity::Rare, 0.137143);
    RegisterDropRate(EMobType::LeafPiece, ERarity::Epic, EPetalType::Leaf, ERarity::Rare, 0.475429);
    RegisterDropRate(EMobType::LeafPiece, ERarity::Epic, EPetalType::Leaf, ERarity::Epic, 0.181714);
    RegisterDropRate(EMobType::LeafPiece, ERarity::Legendary, EPetalType::Leaf, ERarity::Epic, 0.516571);
    RegisterDropRate(EMobType::LeafPiece, ERarity::Legendary, EPetalType::Leaf, ERarity::Legendary, 0.226286);
    RegisterDropRate(EMobType::LeafPiece, ERarity::Mythic, EPetalType::Leaf, ERarity::Legendary, 0.557714);
    RegisterDropRate(EMobType::LeafPiece, ERarity::Mythic, EPetalType::Leaf, ERarity::Mythic, 0.270857);
    RegisterDropRate(EMobType::LeafPiece, ERarity::Ultra, EPetalType::Leaf, ERarity::Mythic, 0.598857);
    RegisterDropRate(EMobType::LeafPiece, ERarity::Ultra, EPetalType::Leaf, ERarity::Ultra, 0.315429);
    RegisterDropRate(EMobType::LeafPiece, ERarity::Super, EPetalType::Leaf, ERarity::Mythic, 0.640);
    RegisterDropRate(EMobType::LeafPiece, ERarity::Super, EPetalType::Leaf, ERarity::Ultra, 0.360);

    RegisterDropRate(EMobType::BabyAnt, ERarity::Common, EPetalType::Rice, ERarity::Common, 0.662);
    RegisterDropRate(EMobType::BabyAnt, ERarity::Common, EPetalType::Rice, ERarity::Unusual, 0.306);
    RegisterDropRate(EMobType::BabyAnt, ERarity::Common, EPetalType::Light, ERarity::Common, 0.598);
    RegisterDropRate(EMobType::BabyAnt, ERarity::Common, EPetalType::Light, ERarity::Unusual, 0.164);
    RegisterDropRate(EMobType::BabyAnt, ERarity::Common, EPetalType::Leaf, ERarity::Unusual, 0.101);
    RegisterDropRate(EMobType::BabyAnt, ERarity::Unusual, EPetalType::Rice, ERarity::Common, 0.232);
    RegisterDropRate(EMobType::BabyAnt, ERarity::Unusual, EPetalType::Rice, ERarity::Unusual, 0.768);
    RegisterDropRate(EMobType::BabyAnt, ERarity::Unusual, EPetalType::Light, ERarity::Common, 0.485);
    RegisterDropRate(EMobType::BabyAnt, ERarity::Unusual, EPetalType::Light, ERarity::Unusual, 0.512);
    RegisterDropRate(EMobType::BabyAnt, ERarity::Unusual, EPetalType::Leaf, ERarity::Unusual, 0.348);
    RegisterDropRate(EMobType::BabyAnt, ERarity::Rare, EPetalType::Rice, ERarity::Unusual, 0.697);
    RegisterDropRate(EMobType::BabyAnt, ERarity::Rare, EPetalType::Rice, ERarity::Rare, 0.303);
    RegisterDropRate(EMobType::BabyAnt, ERarity::Rare, EPetalType::Light, ERarity::Unusual, 0.834);
    RegisterDropRate(EMobType::BabyAnt, ERarity::Rare, EPetalType::Light, ERarity::Rare, 0.165);
    RegisterDropRate(EMobType::BabyAnt, ERarity::Rare, EPetalType::Leaf, ERarity::Unusual, 0.883);
    RegisterDropRate(EMobType::BabyAnt, ERarity::Rare, EPetalType::Leaf, ERarity::Rare, 0.103);
    RegisterDropRate(EMobType::BabyAnt, ERarity::Epic, EPetalType::Rice, ERarity::Rare, 0.764);
    RegisterDropRate(EMobType::BabyAnt, ERarity::Epic, EPetalType::Rice, ERarity::Epic, 0.231);
    RegisterDropRate(EMobType::BabyAnt, ERarity::Epic, EPetalType::Light, ERarity::Unusual, 0.067);
    RegisterDropRate(EMobType::BabyAnt, ERarity::Epic, EPetalType::Light, ERarity::Rare, 0.810);
    RegisterDropRate(EMobType::BabyAnt, ERarity::Epic, EPetalType::Light, ERarity::Epic, 0.123);
    RegisterDropRate(EMobType::BabyAnt, ERarity::Epic, EPetalType::Leaf, ERarity::Unusual, 0.197);
    RegisterDropRate(EMobType::BabyAnt, ERarity::Epic, EPetalType::Leaf, ERarity::Rare, 0.727);
    RegisterDropRate(EMobType::BabyAnt, ERarity::Epic, EPetalType::Leaf, ERarity::Epic, 0.076);
    RegisterDropRate(EMobType::BabyAnt, ERarity::Legendary, EPetalType::Rice, ERarity::Rare, 0.005);
    RegisterDropRate(EMobType::BabyAnt, ERarity::Legendary, EPetalType::Rice, ERarity::Epic, 0.897);
    RegisterDropRate(EMobType::BabyAnt, ERarity::Legendary, EPetalType::Rice, ERarity::Legendary, 0.098);
    RegisterDropRate(EMobType::BabyAnt, ERarity::Legendary, EPetalType::Light, ERarity::Rare, 0.072);
    RegisterDropRate(EMobType::BabyAnt, ERarity::Legendary, EPetalType::Light, ERarity::Epic, 0.877);
    RegisterDropRate(EMobType::BabyAnt, ERarity::Legendary, EPetalType::Light, ERarity::Legendary, 0.050);
    RegisterDropRate(EMobType::BabyAnt, ERarity::Legendary, EPetalType::Leaf, ERarity::Rare, 0.207);
    RegisterDropRate(EMobType::BabyAnt, ERarity::Legendary, EPetalType::Leaf, ERarity::Epic, 0.763);
    RegisterDropRate(EMobType::BabyAnt, ERarity::Legendary, EPetalType::Leaf, ERarity::Legendary, 0.031);
    RegisterDropRate(EMobType::BabyAnt, ERarity::Mythic, EPetalType::Rice, ERarity::Epic, 0.006);
    RegisterDropRate(EMobType::BabyAnt, ERarity::Mythic, EPetalType::Rice, ERarity::Legendary, 0.979);
    RegisterDropRate(EMobType::BabyAnt, ERarity::Mythic, EPetalType::Rice, ERarity::Mythic, 0.015);
    RegisterDropRate(EMobType::BabyAnt, ERarity::Mythic, EPetalType::Light, ERarity::Epic, 0.075);
    RegisterDropRate(EMobType::BabyAnt, ERarity::Mythic, EPetalType::Light, ERarity::Legendary, 0.917);
    RegisterDropRate(EMobType::BabyAnt, ERarity::Mythic, EPetalType::Light, ERarity::Mythic, 0.008);
    RegisterDropRate(EMobType::BabyAnt, ERarity::Mythic, EPetalType::Leaf, ERarity::Epic, 0.212);
    RegisterDropRate(EMobType::BabyAnt, ERarity::Mythic, EPetalType::Leaf, ERarity::Legendary, 0.783);
    RegisterDropRate(EMobType::BabyAnt, ERarity::Mythic, EPetalType::Leaf, ERarity::Mythic, 0.005);
    RegisterDropRate(EMobType::BabyAnt, ERarity::Ultra, EPetalType::Rice, ERarity::Legendary, 0.045);
    RegisterDropRate(EMobType::BabyAnt, ERarity::Ultra, EPetalType::Rice, ERarity::Mythic, 0.945);
    RegisterDropRate(EMobType::BabyAnt, ERarity::Ultra, EPetalType::Rice, ERarity::Ultra, 0.010);
    RegisterDropRate(EMobType::BabyAnt, ERarity::Ultra, EPetalType::Light, ERarity::Legendary, 0.212);
    RegisterDropRate(EMobType::BabyAnt, ERarity::Ultra, EPetalType::Light, ERarity::Mythic, 0.783);
    RegisterDropRate(EMobType::BabyAnt, ERarity::Ultra, EPetalType::Light, ERarity::Ultra, 0.005);
    RegisterDropRate(EMobType::BabyAnt, ERarity::Ultra, EPetalType::Leaf, ERarity::Legendary, 0.394);
    RegisterDropRate(EMobType::BabyAnt, ERarity::Ultra, EPetalType::Leaf, ERarity::Mythic, 0.603);
    RegisterDropRate(EMobType::BabyAnt, ERarity::Ultra, EPetalType::Leaf, ERarity::Ultra, 0.003);
    RegisterDropRate(EMobType::BabyAnt, ERarity::Super, EPetalType::Rice, ERarity::Mythic, 0.127);
    RegisterDropRate(EMobType::BabyAnt, ERarity::Super, EPetalType::Rice, ERarity::Ultra, 0.873);
    RegisterDropRate(EMobType::BabyAnt, ERarity::Super, EPetalType::Light, ERarity::Mythic, 0.356);
    RegisterDropRate(EMobType::BabyAnt, ERarity::Super, EPetalType::Light, ERarity::Ultra, 0.644);
    RegisterDropRate(EMobType::BabyAnt, ERarity::Super, EPetalType::Leaf, ERarity::Mythic, 0.538);
    RegisterDropRate(EMobType::BabyAnt, ERarity::Super, EPetalType::Leaf, ERarity::Ultra, 0.462);

    RegisterDropRate(EMobType::QueenAnt, ERarity::Common, EPetalType::AntEgg, ERarity::Common, 0.662);
    RegisterDropRate(EMobType::QueenAnt, ERarity::Common, EPetalType::AntEgg, ERarity::Unusual, 0.306);
    RegisterDropRate(EMobType::QueenAnt, ERarity::Unusual, EPetalType::AntEgg, ERarity::Common, 0.232);
    RegisterDropRate(EMobType::QueenAnt, ERarity::Unusual, EPetalType::AntEgg, ERarity::Unusual, 0.768);
    RegisterDropRate(EMobType::QueenAnt, ERarity::Rare, EPetalType::AntEgg, ERarity::Unusual, 0.697);
    RegisterDropRate(EMobType::QueenAnt, ERarity::Rare, EPetalType::AntEgg, ERarity::Rare, 0.303);
    RegisterDropRate(EMobType::QueenAnt, ERarity::Epic, EPetalType::AntEgg, ERarity::Rare, 0.764);
    RegisterDropRate(EMobType::QueenAnt, ERarity::Epic, EPetalType::AntEgg, ERarity::Epic, 0.231);
    RegisterDropRate(EMobType::QueenAnt, ERarity::Legendary, EPetalType::AntEgg, ERarity::Rare, 0.005);
    RegisterDropRate(EMobType::QueenAnt, ERarity::Legendary, EPetalType::AntEgg, ERarity::Epic, 0.897);
    RegisterDropRate(EMobType::QueenAnt, ERarity::Legendary, EPetalType::AntEgg, ERarity::Legendary, 0.098);
    RegisterDropRate(EMobType::QueenAnt, ERarity::Mythic, EPetalType::AntEgg, ERarity::Epic, 0.006);
    RegisterDropRate(EMobType::QueenAnt, ERarity::Mythic, EPetalType::AntEgg, ERarity::Legendary, 0.979);
    RegisterDropRate(EMobType::QueenAnt, ERarity::Mythic, EPetalType::AntEgg, ERarity::Mythic, 0.015);
    RegisterDropRate(EMobType::QueenAnt, ERarity::Mythic, EPetalType::Basil, ERarity::Mythic, 0.009);
    RegisterDropRate(EMobType::QueenAnt, ERarity::Ultra, EPetalType::AntEgg, ERarity::Legendary, 0.045);
    RegisterDropRate(EMobType::QueenAnt, ERarity::Ultra, EPetalType::AntEgg, ERarity::Mythic, 0.945);
    RegisterDropRate(EMobType::QueenAnt, ERarity::Ultra, EPetalType::AntEgg, ERarity::Ultra, 0.010);
    RegisterDropRate(EMobType::QueenAnt, ERarity::Ultra, EPetalType::Basil, ERarity::Mythic, 0.838);
    RegisterDropRate(EMobType::QueenAnt, ERarity::Ultra, EPetalType::Basil, ERarity::Ultra, 0.006);
    RegisterDropRate(EMobType::QueenAnt, ERarity::Ultra, EPetalType::BrokenEgg, ERarity::Ultra, 0.0057);
    RegisterDropRate(EMobType::QueenAnt, ERarity::Super, EPetalType::AntEgg, ERarity::Mythic, 0.127);
    RegisterDropRate(EMobType::QueenAnt, ERarity::Super, EPetalType::AntEgg, ERarity::Ultra, 0.873);
    RegisterDropRate(EMobType::QueenAnt, ERarity::Super, EPetalType::Basil, ERarity::Mythic, 0.290);
    RegisterDropRate(EMobType::QueenAnt, ERarity::Super, EPetalType::Basil, ERarity::Ultra, 0.710);
    RegisterDropRate(EMobType::QueenAnt, ERarity::Super, EPetalType::BrokenEgg, ERarity::Ultra, 0.6745);

    RegisterDropRate(EMobType::AntHole, ERarity::Common, EPetalType::Soil, ERarity::Common, 0.598);
    RegisterDropRate(EMobType::AntHole, ERarity::Common, EPetalType::Soil, ERarity::Unusual, 0.164);
    RegisterDropRate(EMobType::AntHole, ERarity::Unusual, EPetalType::Soil, ERarity::Common, 0.485);
    RegisterDropRate(EMobType::AntHole, ERarity::Unusual, EPetalType::Soil, ERarity::Unusual, 0.512);
    RegisterDropRate(EMobType::AntHole, ERarity::Rare, EPetalType::Soil, ERarity::Unusual, 0.834);
    RegisterDropRate(EMobType::AntHole, ERarity::Rare, EPetalType::Soil, ERarity::Rare, 0.165);
    RegisterDropRate(EMobType::AntHole, ERarity::Epic, EPetalType::Soil, ERarity::Unusual, 0.067);
    RegisterDropRate(EMobType::AntHole, ERarity::Epic, EPetalType::Soil, ERarity::Rare, 0.810);
    RegisterDropRate(EMobType::AntHole, ERarity::Epic, EPetalType::Soil, ERarity::Epic, 0.123);
    RegisterDropRate(EMobType::AntHole, ERarity::Legendary, EPetalType::Soil, ERarity::Rare, 0.072);
    RegisterDropRate(EMobType::AntHole, ERarity::Legendary, EPetalType::Soil, ERarity::Epic, 0.877);
    RegisterDropRate(EMobType::AntHole, ERarity::Legendary, EPetalType::Soil, ERarity::Legendary, 0.050);
    RegisterDropRate(EMobType::AntHole, ERarity::Mythic, EPetalType::Soil, ERarity::Epic, 0.075);
    RegisterDropRate(EMobType::AntHole, ERarity::Mythic, EPetalType::Soil, ERarity::Legendary, 0.917);
    RegisterDropRate(EMobType::AntHole, ERarity::Mythic, EPetalType::Soil, ERarity::Mythic, 0.008);
    RegisterDropRate(EMobType::AntHole, ERarity::Ultra, EPetalType::Soil, ERarity::Legendary, 0.212);
    RegisterDropRate(EMobType::AntHole, ERarity::Ultra, EPetalType::Soil, ERarity::Mythic, 0.783);
    RegisterDropRate(EMobType::AntHole, ERarity::Ultra, EPetalType::Soil, ERarity::Ultra, 0.005);
    RegisterDropRate(EMobType::AntHole, ERarity::Super, EPetalType::Soil, ERarity::Mythic, 0.356);
    RegisterDropRate(EMobType::AntHole, ERarity::Super, EPetalType::Soil, ERarity::Ultra, 0.644);
    RegisterDropRate(EMobType::AntHole, ERarity::Common, EPetalType::Shovel, ERarity::Common, 0.539);
    RegisterDropRate(EMobType::AntHole, ERarity::Common, EPetalType::Shovel, ERarity::Unusual, 0.133);
    RegisterDropRate(EMobType::AntHole, ERarity::Unusual, EPetalType::Shovel, ERarity::Common, 0.553);
    RegisterDropRate(EMobType::AntHole, ERarity::Unusual, EPetalType::Shovel, ERarity::Unusual, 0.436);
    RegisterDropRate(EMobType::AntHole, ERarity::Rare, EPetalType::Shovel, ERarity::Common, 0.003);
    RegisterDropRate(EMobType::AntHole, ERarity::Rare, EPetalType::Shovel, ERarity::Unusual, 0.862);
    RegisterDropRate(EMobType::AntHole, ERarity::Rare, EPetalType::Shovel, ERarity::Rare, 0.135);
    RegisterDropRate(EMobType::AntHole, ERarity::Epic, EPetalType::Shovel, ERarity::Unusual, 0.114);
    RegisterDropRate(EMobType::AntHole, ERarity::Epic, EPetalType::Shovel, ERarity::Rare, 0.786);
    RegisterDropRate(EMobType::AntHole, ERarity::Epic, EPetalType::Shovel, ERarity::Epic, 0.100);
    RegisterDropRate(EMobType::AntHole, ERarity::Legendary, EPetalType::Shovel, ERarity::Rare, 0.122);
    RegisterDropRate(EMobType::AntHole, ERarity::Legendary, EPetalType::Shovel, ERarity::Epic, 0.837);
    RegisterDropRate(EMobType::AntHole, ERarity::Legendary, EPetalType::Shovel, ERarity::Legendary, 0.041);
    RegisterDropRate(EMobType::AntHole, ERarity::Mythic, EPetalType::Shovel, ERarity::Epic, 0.126);
    RegisterDropRate(EMobType::AntHole, ERarity::Mythic, EPetalType::Shovel, ERarity::Legendary, 0.867);
    RegisterDropRate(EMobType::AntHole, ERarity::Mythic, EPetalType::Shovel, ERarity::Mythic, 0.006);
    RegisterDropRate(EMobType::AntHole, ERarity::Ultra, EPetalType::Shovel, ERarity::Legendary, 0.883);
    RegisterDropRate(EMobType::AntHole, ERarity::Ultra, EPetalType::Shovel, ERarity::Mythic, 0.116);
    RegisterDropRate(EMobType::AntHole, ERarity::Ultra, EPetalType::Shovel, ERarity::Ultra, 0.0004);
    RegisterDropRate(EMobType::AntHole, ERarity::Super, EPetalType::Shovel, ERarity::Mythic, 0.812);
    RegisterDropRate(EMobType::AntHole, ERarity::Super, EPetalType::Shovel, ERarity::Ultra, 0.186);

    RegisterDropRate(EMobType::BumbleBee, ERarity::Common, EPetalType::Pollen, ERarity::Unusual, 0.472);
    RegisterDropRate(EMobType::BumbleBee, ERarity::Unusual, EPetalType::Pollen, ERarity::Unusual, 0.922);
    RegisterDropRate(EMobType::BumbleBee, ERarity::Rare, EPetalType::Pollen, ERarity::Unusual, 0.541);
    RegisterDropRate(EMobType::BumbleBee, ERarity::Rare, EPetalType::Pollen, ERarity::Rare, 0.459);
    RegisterDropRate(EMobType::BumbleBee, ERarity::Rare, EPetalType::Wax, ERarity::Rare, 0.328);
    RegisterDropRate(EMobType::BumbleBee, ERarity::Rare, EPetalType::Honey, ERarity::Rare, 0.135);
    RegisterDropRate(EMobType::BumbleBee, ERarity::Epic, EPetalType::Pollen, ERarity::Rare, 0.640);
    RegisterDropRate(EMobType::BumbleBee, ERarity::Epic, EPetalType::Pollen, ERarity::Epic, 0.360);
    RegisterDropRate(EMobType::BumbleBee, ERarity::Epic, EPetalType::Wax, ERarity::Rare, 0.746);
    RegisterDropRate(EMobType::BumbleBee, ERarity::Epic, EPetalType::Wax, ERarity::Epic, 0.251);
    RegisterDropRate(EMobType::BumbleBee, ERarity::Epic, EPetalType::Honey, ERarity::Rare, 0.786);
    RegisterDropRate(EMobType::BumbleBee, ERarity::Epic, EPetalType::Honey, ERarity::Epic, 0.100);
    RegisterDropRate(EMobType::BumbleBee, ERarity::Legendary, EPetalType::Pollen, ERarity::Epic, 0.839);
    RegisterDropRate(EMobType::BumbleBee, ERarity::Legendary, EPetalType::Pollen, ERarity::Legendary, 0.161);
    RegisterDropRate(EMobType::BumbleBee, ERarity::Legendary, EPetalType::Wax, ERarity::Epic, 0.889);
    RegisterDropRate(EMobType::BumbleBee, ERarity::Legendary, EPetalType::Wax, ERarity::Legendary, 0.107);
    RegisterDropRate(EMobType::BumbleBee, ERarity::Legendary, EPetalType::Honey, ERarity::Rare, 0.122);
    RegisterDropRate(EMobType::BumbleBee, ERarity::Legendary, EPetalType::Honey, ERarity::Epic, 0.837);
    RegisterDropRate(EMobType::BumbleBee, ERarity::Legendary, EPetalType::Honey, ERarity::Legendary, 0.041);
    RegisterDropRate(EMobType::BumbleBee, ERarity::Mythic, EPetalType::Pollen, ERarity::Legendary, 0.974);
    RegisterDropRate(EMobType::BumbleBee, ERarity::Mythic, EPetalType::Pollen, ERarity::Mythic, 0.026);
    RegisterDropRate(EMobType::BumbleBee, ERarity::Mythic, EPetalType::Wax, ERarity::Legendary, 0.980);
    RegisterDropRate(EMobType::BumbleBee, ERarity::Mythic, EPetalType::Wax, ERarity::Mythic, 0.017);
    RegisterDropRate(EMobType::BumbleBee, ERarity::Mythic, EPetalType::Honey, ERarity::Epic, 0.126);
    RegisterDropRate(EMobType::BumbleBee, ERarity::Mythic, EPetalType::Honey, ERarity::Legendary, 0.867);
    RegisterDropRate(EMobType::BumbleBee, ERarity::Mythic, EPetalType::Honey, ERarity::Mythic, 0.006);
    RegisterDropRate(EMobType::BumbleBee, ERarity::Ultra, EPetalType::Pollen, ERarity::Mythic, 0.977);
    RegisterDropRate(EMobType::BumbleBee, ERarity::Ultra, EPetalType::Pollen, ERarity::Ultra, 0.017);
    RegisterDropRate(EMobType::BumbleBee, ERarity::Ultra, EPetalType::Wax, ERarity::Legendary, 0.033);
    RegisterDropRate(EMobType::BumbleBee, ERarity::Ultra, EPetalType::Wax, ERarity::Mythic, 0.956);
    RegisterDropRate(EMobType::BumbleBee, ERarity::Ultra, EPetalType::Wax, ERarity::Ultra, 0.011);
    RegisterDropRate(EMobType::BumbleBee, ERarity::Ultra, EPetalType::Honey, ERarity::Legendary, 0.289);
    RegisterDropRate(EMobType::BumbleBee, ERarity::Ultra, EPetalType::Honey, ERarity::Mythic, 0.707);
    RegisterDropRate(EMobType::BumbleBee, ERarity::Ultra, EPetalType::Honey, ERarity::Ultra, 0.004);
    RegisterDropRate(EMobType::BumbleBee, ERarity::Super, EPetalType::Pollen, ERarity::Mythic, 0.030);
    RegisterDropRate(EMobType::BumbleBee, ERarity::Super, EPetalType::Pollen, ERarity::Ultra, 0.970);
    RegisterDropRate(EMobType::BumbleBee, ERarity::Super, EPetalType::Wax, ERarity::Mythic, 0.103);
    RegisterDropRate(EMobType::BumbleBee, ERarity::Super, EPetalType::Wax, ERarity::Ultra, 0.897);
    RegisterDropRate(EMobType::BumbleBee, ERarity::Super, EPetalType::Honey, ERarity::Mythic, 0.438);
    RegisterDropRate(EMobType::BumbleBee, ERarity::Super, EPetalType::Honey, ERarity::Ultra, 0.562);

    RegisterDropRate(EMobType::Spider, ERarity::Common, EPetalType::Faster, ERarity::Common, 0.598);
    RegisterDropRate(EMobType::Spider, ERarity::Common, EPetalType::Faster, ERarity::Unusual, 0.164);
    RegisterDropRate(EMobType::Spider, ERarity::Common, EPetalType::Web, ERarity::Unusual, 0.164);
    RegisterDropRate(EMobType::Spider, ERarity::Unusual, EPetalType::Faster, ERarity::Common, 0.485);
    RegisterDropRate(EMobType::Spider, ERarity::Unusual, EPetalType::Faster, ERarity::Unusual, 0.512);
    RegisterDropRate(EMobType::Spider, ERarity::Unusual, EPetalType::Web, ERarity::Unusual, 0.512);
    RegisterDropRate(EMobType::Spider, ERarity::Rare, EPetalType::Faster, ERarity::Unusual, 0.834);
    RegisterDropRate(EMobType::Spider, ERarity::Rare, EPetalType::Faster, ERarity::Rare, 0.165);
    RegisterDropRate(EMobType::Spider, ERarity::Rare, EPetalType::Web, ERarity::Unusual, 0.834);
    RegisterDropRate(EMobType::Spider, ERarity::Rare, EPetalType::Web, ERarity::Rare, 0.165);
    RegisterDropRate(EMobType::Spider, ERarity::Epic, EPetalType::Faster, ERarity::Unusual, 0.067);
    RegisterDropRate(EMobType::Spider, ERarity::Epic, EPetalType::Faster, ERarity::Rare, 0.810);
    RegisterDropRate(EMobType::Spider, ERarity::Epic, EPetalType::Faster, ERarity::Epic, 0.123);
    RegisterDropRate(EMobType::Spider, ERarity::Epic, EPetalType::Web, ERarity::Unusual, 0.067);
    RegisterDropRate(EMobType::Spider, ERarity::Epic, EPetalType::Web, ERarity::Rare, 0.810);
    RegisterDropRate(EMobType::Spider, ERarity::Epic, EPetalType::Web, ERarity::Epic, 0.123);
    RegisterDropRate(EMobType::Spider, ERarity::Legendary, EPetalType::Faster, ERarity::Rare, 0.072);
    RegisterDropRate(EMobType::Spider, ERarity::Legendary, EPetalType::Faster, ERarity::Epic, 0.877);
    RegisterDropRate(EMobType::Spider, ERarity::Legendary, EPetalType::Faster, ERarity::Legendary, 0.050);
    RegisterDropRate(EMobType::Spider, ERarity::Legendary, EPetalType::Web, ERarity::Rare, 0.072);
    RegisterDropRate(EMobType::Spider, ERarity::Legendary, EPetalType::Web, ERarity::Epic, 0.877);
    RegisterDropRate(EMobType::Spider, ERarity::Legendary, EPetalType::Web, ERarity::Legendary, 0.050);
    RegisterDropRate(EMobType::Spider, ERarity::Mythic, EPetalType::Faster, ERarity::Epic, 0.075);
    RegisterDropRate(EMobType::Spider, ERarity::Mythic, EPetalType::Faster, ERarity::Legendary, 0.917);
    RegisterDropRate(EMobType::Spider, ERarity::Mythic, EPetalType::Faster, ERarity::Mythic, 0.008);
    RegisterDropRate(EMobType::Spider, ERarity::Mythic, EPetalType::Web, ERarity::Epic, 0.075);
    RegisterDropRate(EMobType::Spider, ERarity::Mythic, EPetalType::Web, ERarity::Legendary, 0.917);
    RegisterDropRate(EMobType::Spider, ERarity::Mythic, EPetalType::Web, ERarity::Mythic, 0.008);
    RegisterDropRate(EMobType::Spider, ERarity::Mythic, EPetalType::ThirdEye, ERarity::Mythic, 0.0003);
    RegisterDropRate(EMobType::Spider, ERarity::Ultra, EPetalType::Faster, ERarity::Legendary, 0.212);
    RegisterDropRate(EMobType::Spider, ERarity::Ultra, EPetalType::Faster, ERarity::Mythic, 0.783);
    RegisterDropRate(EMobType::Spider, ERarity::Ultra, EPetalType::Faster, ERarity::Ultra, 0.005);
    RegisterDropRate(EMobType::Spider, ERarity::Ultra, EPetalType::Web, ERarity::Legendary, 0.212);
    RegisterDropRate(EMobType::Spider, ERarity::Ultra, EPetalType::Web, ERarity::Mythic, 0.783);
    RegisterDropRate(EMobType::Spider, ERarity::Ultra, EPetalType::Web, ERarity::Ultra, 0.005);
    RegisterDropRate(EMobType::Spider, ERarity::Ultra, EPetalType::ThirdEye, ERarity::Mythic, 0.060);
    RegisterDropRate(EMobType::Spider, ERarity::Ultra, EPetalType::ThirdEye, ERarity::Ultra, 0.0002);
    RegisterDropRate(EMobType::Spider, ERarity::Super, EPetalType::Faster, ERarity::Mythic, 0.356);
    RegisterDropRate(EMobType::Spider, ERarity::Super, EPetalType::Faster, ERarity::Ultra, 0.644);
    RegisterDropRate(EMobType::Spider, ERarity::Super, EPetalType::Web, ERarity::Mythic, 0.356);
    RegisterDropRate(EMobType::Spider, ERarity::Super, EPetalType::Web, ERarity::Ultra, 0.644);
    RegisterDropRate(EMobType::Spider, ERarity::Super, EPetalType::ThirdEye, ERarity::Mythic, 0.960);
    RegisterDropRate(EMobType::Spider, ERarity::Super, EPetalType::ThirdEye, ERarity::Ultra, 0.040);

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
    RegisterDropRate(EMobType::Hornet, ERarity::Common, EPetalType::Orange, ERarity::Unusual, 0.164);
    RegisterDropRate(EMobType::Hornet, ERarity::Unusual, EPetalType::Orange, ERarity::Unusual, 0.512);
    RegisterDropRate(EMobType::Hornet, ERarity::Rare, EPetalType::Orange, ERarity::Unusual, 0.834);
    RegisterDropRate(EMobType::Hornet, ERarity::Rare, EPetalType::Orange, ERarity::Rare, 0.165);
    RegisterDropRate(EMobType::Hornet, ERarity::Epic, EPetalType::Orange, ERarity::Unusual, 0.067);
    RegisterDropRate(EMobType::Hornet, ERarity::Epic, EPetalType::Orange, ERarity::Rare, 0.810);
    RegisterDropRate(EMobType::Hornet, ERarity::Epic, EPetalType::Orange, ERarity::Epic, 0.123);
    RegisterDropRate(EMobType::Hornet, ERarity::Legendary, EPetalType::Orange, ERarity::Rare, 0.072);
    RegisterDropRate(EMobType::Hornet, ERarity::Legendary, EPetalType::Orange, ERarity::Epic, 0.877);
    RegisterDropRate(EMobType::Hornet, ERarity::Legendary, EPetalType::Orange, ERarity::Legendary, 0.050);
    RegisterDropRate(EMobType::Hornet, ERarity::Mythic, EPetalType::Orange, ERarity::Epic, 0.075);
    RegisterDropRate(EMobType::Hornet, ERarity::Mythic, EPetalType::Orange, ERarity::Legendary, 0.917);
    RegisterDropRate(EMobType::Hornet, ERarity::Mythic, EPetalType::Orange, ERarity::Mythic, 0.008);
    RegisterDropRate(EMobType::Hornet, ERarity::Ultra, EPetalType::Orange, ERarity::Legendary, 0.856);
    RegisterDropRate(EMobType::Hornet, ERarity::Ultra, EPetalType::Orange, ERarity::Mythic, 0.143);
    RegisterDropRate(EMobType::Hornet, ERarity::Ultra, EPetalType::Orange, ERarity::Ultra, 0.0005);
    RegisterDropRate(EMobType::Hornet, ERarity::Super, EPetalType::Orange, ERarity::Mythic, 0.773);
    RegisterDropRate(EMobType::Hornet, ERarity::Super, EPetalType::Orange, ERarity::Ultra, 0.227);

    RegisterDropRate(EMobType::Dandelion, ERarity::Common, EPetalType::Dandelion, ERarity::Common, 0.870108);
    RegisterDropRate(EMobType::Dandelion, ERarity::Common, EPetalType::Dandelion, ERarity::Unusual, 0.129892);
    RegisterDropRate(EMobType::Dandelion, ERarity::Unusual, EPetalType::Dandelion, ERarity::Unusual, 0.129892);
    RegisterDropRate(EMobType::Dandelion, ERarity::Unusual, EPetalType::Dandelion, ERarity::Common, 0.870108);
    RegisterDropRate(EMobType::Dandelion, ERarity::Rare, EPetalType::Dandelion, ERarity::Rare, 0.051);
    RegisterDropRate(EMobType::Dandelion, ERarity::Rare, EPetalType::Dandelion, ERarity::Unusual, 0.695108);
    RegisterDropRate(EMobType::Dandelion, ERarity::Rare, EPetalType::Dandelion, ERarity::Common, 0.253892);
    RegisterDropRate(EMobType::Dandelion, ERarity::Epic, EPetalType::Dandelion, ERarity::Epic, 0.030);
    RegisterDropRate(EMobType::Dandelion, ERarity::Epic, EPetalType::Dandelion, ERarity::Rare, 0.690108);
    RegisterDropRate(EMobType::Dandelion, ERarity::Epic, EPetalType::Dandelion, ERarity::Unusual, 0.279892);
    RegisterDropRate(EMobType::Dandelion, ERarity::Legendary, EPetalType::Dandelion, ERarity::Legendary, 0.010);
    RegisterDropRate(EMobType::Dandelion, ERarity::Legendary, EPetalType::Dandelion, ERarity::Epic, 0.685108);
    RegisterDropRate(EMobType::Dandelion, ERarity::Legendary, EPetalType::Dandelion, ERarity::Rare, 0.304892);
    RegisterDropRate(EMobType::Dandelion, ERarity::Mythic, EPetalType::Dandelion, ERarity::Mythic, 0.003);
    RegisterDropRate(EMobType::Dandelion, ERarity::Mythic, EPetalType::Dandelion, ERarity::Legendary, 0.682108);
    RegisterDropRate(EMobType::Dandelion, ERarity::Mythic, EPetalType::Dandelion, ERarity::Epic, 0.314892);
    RegisterDropRate(EMobType::Dandelion, ERarity::Ultra, EPetalType::Dandelion, ERarity::Ultra, 0.0005);
    RegisterDropRate(EMobType::Dandelion, ERarity::Ultra, EPetalType::Dandelion, ERarity::Mythic, 0.668608);
    RegisterDropRate(EMobType::Dandelion, ERarity::Ultra, EPetalType::Dandelion, ERarity::Legendary, 0.330892);
    RegisterDropRate(EMobType::Dandelion, ERarity::Super, EPetalType::Dandelion, ERarity::Super, 0.00001);
    RegisterDropRate(EMobType::Dandelion, ERarity::Super, EPetalType::Dandelion, ERarity::Ultra, 0.6681);
    RegisterDropRate(EMobType::Dandelion, ERarity::Super, EPetalType::Dandelion, ERarity::Mythic, 0.33189);

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

    RegisterWikiAntDropRates();
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

