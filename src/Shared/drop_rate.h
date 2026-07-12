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
    float drop_rate = 0.f;
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
                             float drop_rate)
{
    if (mob_type == EMobType::None || mob_rarity == ERarity::Null) return false;
    if (drop_type == EPetalType::None || drop_rarity == ERarity::Null) return false;
    if (drop_rate <= 0.f) return false;

    auto& drops = GetDropRateTable()[SDropRateKey{mob_type, mob_rarity}];
    drops.push_back({drop_type, drop_rarity, std::clamp(drop_rate, 0.f, 1.f)});
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

    RegisterDropRate(EMobType::SoldierAnt, ERarity::Common, EPetalType::Wing, ERarity::Common, 0.875f);
    RegisterDropRate(EMobType::SoldierAnt, ERarity::Common, EPetalType::Wing, ERarity::Unusual, 0.125f);
    RegisterDropRate(EMobType::SoldierAnt, ERarity::Common, EPetalType::Glass, ERarity::Common, 0.667f);
    RegisterDropRate(EMobType::SoldierAnt, ERarity::Common, EPetalType::Glass, ERarity::Unusual, 0.333f);

    RegisterDropRate(EMobType::NormalLadybug, ERarity::Common, EPetalType::Basic, ERarity::Common, 0.56f);
    RegisterDropRate(EMobType::NormalLadybug, ERarity::Common, EPetalType::Basic, ERarity::Unusual, 0.44f);
    RegisterDropRate(EMobType::NormalLadybug, ERarity::Common, EPetalType::Rose, ERarity::Common, 0.667f);
    RegisterDropRate(EMobType::NormalLadybug, ERarity::Common, EPetalType::Rose, ERarity::Unusual, 0.333f);
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
    float remaining_rate = 1.f;

    for (const SDropRate& rate : rates)
    {
        if (rate.type == EPetalType::None || rate.rarity == ERarity::Null || rate.drop_rate <= 0.f) continue;

        if (rate.type != current_type)
        {
            current_type = rate.type;
            remaining_rate = 1.f;
        }

        if (remaining_rate <= 0.f) continue;

        float check_rate = std::clamp(rate.drop_rate / remaining_rate, 0.f, 1.f);
        if (CheckChance(check_rate))
        {
            result.push_back(rate);
            current_type = rate.type;
            remaining_rate = 0.f;
            continue;
        }

        remaining_rate = std::max(0.f, remaining_rate - rate.drop_rate);
    }

    return result;
}

