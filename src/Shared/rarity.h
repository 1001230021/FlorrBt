#pragma once
#include <array>
#include <cstdint>
#include <string_view>

enum class ERarity : int
{
    Null = 0,
    Common,
    Unusual,
    Rare,
    Epic,
    Legendary,
    Mythic,
    Ultra,
    Super,
    Eternal,
    Unique,
    Primordial
};

struct SRarityColor
{
    uint8_t r = 0;
    uint8_t g = 0;
    uint8_t b = 0;
    uint8_t e = 0;
    uint8_t s = 0;
    uint8_t l = 0;
};

inline constexpr std::array<SRarityColor, 12> rarity_colors = {
    SRarityColor{0, 0, 0, 0, 0, 0},
    SRarityColor{111, 211, 96, 75, 193, 164},
    SRarityColor{255, 230, 93, 34, 240, 164},
    SRarityColor{68, 72, 200, 159, 131, 126},
    SRarityColor{134, 31, 222, 182, 181, 119},
    SRarityColor{219, 31, 31, 0, 180, 118},
    SRarityColor{31, 219, 222, 121, 181, 119},
    SRarityColor{225, 38, 103, 226, 182, 124},
    SRarityColor{40, 240, 153, 103, 209, 132},
    SRarityColor{238, 238, 238, 160, 0, 224},
    SRarityColor{53, 53, 53, 160, 0, 50},
    SRarityColor{110, 110, 110, 0, 0, 103},
};

inline SRarityColor GetRarityColor(ERarity rarity)
{
    int index = static_cast<int>(rarity);
    if (index < 0 || index >= static_cast<int>(rarity_colors.size())) return rarity_colors[0];
    return rarity_colors[index];
}

inline std::string_view GetRarityName(ERarity rarity)
{
    switch (rarity)
    {
    case ERarity::Common:
        return "Common";
    case ERarity::Unusual:
        return "Unusual";
    case ERarity::Rare:
        return "Rare";
    case ERarity::Epic:
        return "Epic";
    case ERarity::Legendary:
        return "Legendary";
    case ERarity::Mythic:
        return "Mythic";
    case ERarity::Ultra:
        return "Ultra";
    case ERarity::Super:
        return "Super";
    case ERarity::Eternal:
        return "Eternal";
    case ERarity::Unique:
        return "Unique";
    case ERarity::Primordial:
        return "Primordial";
    default:
        return "Null";
    }
}
