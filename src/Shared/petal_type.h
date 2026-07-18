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
    Yggdrasil,
    Dahlia,
    Wing,
    Triangle,
    Sawblade,
    Fragment,
    Mimic,
    Glass,
    Stinger,
    BrokenEgg,
    Light,
    Leaf,
    Rock,
    Web,
    Cactus,
    Pollen,
    Corn,
    Rice,
    Basil,
    Soil,
    Honey,
    Wax,
    ThirdEye,
    Dandelion,
    Orange,
    Shovel
};

using PetalType = EPetalType;

inline constexpr std::array<std::string_view, 55> petal_type_names = {
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
    "Dahlia",
    "Wing",
    "Triangle",
    "Sawblade",
    "Fragment",
    "Mimic",
    "Glass",
    "Stinger",
    "BrokenEgg",
    "Light",
    "Leaf",
    "Rock",
    "Web",
    "Cactus",
    "Pollen",
    "Corn",
    "Rice",
    "Basil",
    "Soil",
    "Honey",
    "Wax",
    "ThirdEye",
    "Dandelion",
    "Orange",
    "Shovel",
};

inline std::string_view GetPetalTypeName(EPetalType type)
{
    size_t index = static_cast<size_t>(type);
    if (index >= petal_type_names.size()) return "UnknownPetal";
    return petal_type_names[index];
}

inline std::string_view GetPetalTypeName(uint8_t type) { return GetPetalTypeName(static_cast<EPetalType>(type)); }

inline bool PetalIgnoresReachBonus(EPetalType type)
{
    switch (type)
    {
    case EPetalType::BrokenEgg:
    case EPetalType::Basil:
    case EPetalType::Rose:
    case EPetalType::Dahlia:
    case EPetalType::Web:
    case EPetalType::Pollen:
    case EPetalType::Honey:
    case EPetalType::Wax:
    case EPetalType::ThirdEye:
    case EPetalType::Orange:
    case EPetalType::Missile:
    case EPetalType::Shovel:
        return true;
    default:
        return false;
    }
}

inline bool MatchPetalTypeAlias(std::string_view text, EPetalType type)
{
    if (text == GetPetalTypeName(type)) return true;

    switch (type)
    {
    case EPetalType::AntEgg:
        return text == "agg";
    case EPetalType::BeetleEgg:
        return text == "bgg";
    case EPetalType::Carrot:
        return text == "carrot";
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
    case EPetalType::Dahlia:
        return text == "dahlia";
    case EPetalType::Wing:
        return text == "wing";
    case EPetalType::Triangle:
        return text == "triangle";
    case EPetalType::Sawblade:
        return text == "sawblade";
    case EPetalType::Fragment:
        return text == "fragment";
    case EPetalType::Mimic:
        return text == "mimic";
    case EPetalType::Glass:
        return text == "glass";
    case EPetalType::Stinger:
        return text == "stinger";
    case EPetalType::BrokenEgg:
        return text == "begg" || text == "brokenegg" || text == "broken_egg";
    case EPetalType::Light:
        return text == "light" || text == "lgt";
    case EPetalType::Leaf:
        return text == "leaf";
    case EPetalType::Rock:
        return text == "rock";
    case EPetalType::Web:
        return text == "web";
    case EPetalType::Cactus:
        return text == "cactus";
    case EPetalType::Pollen:
        return text == "pollen";
    case EPetalType::Corn:
        return text == "corn";
    case EPetalType::Rice:
        return text == "rice";
    case EPetalType::Basil:
        return text == "basil";
    case EPetalType::Soil:
        return text == "soil";
    case EPetalType::Honey:
        return text == "honey";
    case EPetalType::Wax:
        return text == "wax";
    case EPetalType::ThirdEye:
        return text == "third_eye" || text == "thirdeye" || text == "te";
    case EPetalType::Dandelion:
        return text == "dande" || text == "dandelion";
    case EPetalType::Orange:
        return text == "orange" || text == "org";
    case EPetalType::Shovel:
        return text == "shovel" || text == "shv";
    default:
        return false;
    }
}
