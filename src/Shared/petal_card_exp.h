#pragma once
#include "rarity.h"

inline int PetalCardExpForRarity(ERarity rarity)
{
    switch (rarity)
    {
    case ERarity::Common:
        return 1;
    case ERarity::Unusual:
        return 1;
    case ERarity::Rare:
        return 9;
    case ERarity::Epic:
        return 108;
    case ERarity::Legendary:
        return 1857;
    case ERarity::Mythic:
        return 11142;
    case ERarity::Ultra:
        return 44568;
    case ERarity::Exotic:
        return 89136;
    case ERarity::Super:
        return 178272;
    case ERarity::Eternal:
    case ERarity::Unique:
        return 534816;
    case ERarity::Primordial:
        return 1604448;
    default:
        return 0;
    }
}
