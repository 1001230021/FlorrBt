#pragma once
#include <cstdint>

constexpr uint32_t max_inventory_item_count = 1000000000u;

struct SInventoryItem
{
    uint8_t petal_type = 0;
    uint8_t rarity = 0;
    uint32_t count = 0;
};

inline bool operator==(const SInventoryItem& lhs, const SInventoryItem& rhs)
{
    return lhs.petal_type == rhs.petal_type && lhs.rarity == rhs.rarity && lhs.count == rhs.count;
}
