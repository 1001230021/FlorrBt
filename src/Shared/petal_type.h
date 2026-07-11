#pragma once
#include <array>
#include <cstdint>
#include <string_view>

enum class EPetalType : int
{
    None = 0,
    Air,
    AntEgg,
    Antennae,
    Basic,
    BeetleEgg,
    Bone,
    Bubble,
    Carrot,
    Coin,
    Compass,
    Cogwheel,
    Disc,
    Dust,
    GoldenLeaf,
    Iris,
    Lentil,
    Moon,
    Nullification,
    Pincer,
    Relic,
    Rose,
    YinYang,
    Missile,
    BloodSacrifice,
    Corruption,
    Bandage,
    Heavy,
    Faster,
    Yggdrasil
};

using PetalType = EPetalType;

inline constexpr std::array<std::string_view, 30> petal_type_names = {
    "None",
    "Air",
    "AntEgg",
    "Antennae",
    "Basic",
    "BeetleEgg",
    "Bone",
    "Bubble",
    "Carrot",
    "Coin",
    "Compass",
    "Cogwheel",
    "Disc",
    "Dust",
    "GoldenLeaf",
    "Iris",
    "Lentil",
    "Moon",
    "Nullification",
    "Pincer",
    "Relic",
    "Rose",
    "YinYang",
    "Missile",
    "BloodSacrifice",
    "Corruption",
    "Bandage",
    "Heavy",
    "Faster",
    "Yggdrasil",
};

inline std::string_view GetPetalTypeName(EPetalType type)
{
    size_t index = static_cast<size_t>(type);
    if (index >= petal_type_names.size()) return "UnknownPetal";
    return petal_type_names[index];
}

inline std::string_view GetPetalTypeName(uint8_t type) { return GetPetalTypeName(static_cast<EPetalType>(type)); }

inline bool MatchPetalTypeAlias(std::string_view text, EPetalType type)
{
    if (text == GetPetalTypeName(type)) return true;

    switch (type)
    {
    case EPetalType::AntEgg:
        return text == "agg";
    case EPetalType::BeetleEgg:
        return text == "bgg";
    case EPetalType::Cogwheel:
        return text == "cog";
    case EPetalType::Lentil:
        return text == "ltl" || text == "lentil";
    case EPetalType::Moon:
        return text == "moon";
    case EPetalType::Nullification:
        return text == "null";
    case EPetalType::Relic:
        return text == "rlc" || text == "relic";
    case EPetalType::Rose:
        return text == "rose";
    case EPetalType::Missile:
        return text == "missile" || text == "msl";
    case EPetalType::BloodSacrifice:
        return text == "blood_sacrifice" || text == "bloodsacrifice" || text == "bs";
    case EPetalType::Corruption:
        return text == "corruption" || text == "cor";
    case EPetalType::Bandage:
        return text == "bandage" || text == "bdg";
    case EPetalType::Heavy:
        return text == "heavy";
    case EPetalType::Faster:
        return text == "faster";
    case EPetalType::Yggdrasil:
        return text == "yggdrasil" || text == "ygg";
    default:
        return false;
    }
}
