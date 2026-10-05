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

// Per-player reward finalization only. Equivalent to merging matching stacks
// until no remaining pair fits, in one pass; never touches existing loot or
// rerolls probabilities, and ordinary mobs retain every individual result.
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
