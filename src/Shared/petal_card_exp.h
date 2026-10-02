#pragma once
#include "rarity.h"
#include <cstdint>

inline std::int64_t GetPetalCardExpForRarity(ERarity rarity)
{
    switch (rarity)
    {
    case ERarity::Common:
        return 1;
    case ERarity::Unusual:
        return 7;
    case ERarity::Rare:
        return 36;
    case ERarity::Epic:
        return 108;
    case ERarity::Legendary:
        return 1857;
    case ERarity::Mythic:
        return 11142;
    case ERarity::Ultra:
        return 164328;
    case ERarity::Exotic:
        return 1;
    case ERarity::Super:
        return 3086952;
    case ERarity::Eternal:
    case ERarity::Unique:
        return 22442963;
    case ERarity::Primordial:
        return 9036632724LL;
    default:
        return 0;
    }
}
