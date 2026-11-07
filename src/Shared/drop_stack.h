#pragma once
#include "petal_type.h"
#include "rarity.h"
#include <cstdint>
#include <limits>
#include <unordered_map>
#include <vector>

namespace florrbt
{
struct DropStack
{
    EPetalType type;
    ERarity rarity;
    std::uint16_t count = 1;
};

// 仅在单个玩家奖励生成时合并可容纳的同类堆叠，不改既有掉落或重掷概率；普通生物保留独立结果。
template <typename TDrop> std::vector<DropStack> FinalizeDropStacks(const std::vector<TDrop>& drops, bool merge)
{
    std::vector<DropStack> result;
    result.reserve(drops.size());
    std::unordered_map<std::uint32_t, std::size_t> partial;
    for (const auto& drop : drops)
    {
        const auto key = (static_cast<std::uint32_t>(drop.type) << 16) | static_cast<std::uint32_t>(drop.rarity);
        if (merge)
        {
            const auto found = partial.find(key);
            if (found != partial.end() && result[found->second].count < std::numeric_limits<std::uint16_t>::max())
            {
                ++result[found->second].count;
                continue;
            }
            partial[key] = result.size();
        }
        result.push_back({ drop.type, drop.rarity, 1 });
    }
    return result;
}
}
