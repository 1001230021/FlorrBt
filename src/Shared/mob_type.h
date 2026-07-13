#pragma once
#include <array>
#include <cstdint>
#include <string_view>

enum class EMobType : int
{
    None = 0,
    Beetle,
    Gambler,
    NormalLadybug,
    MechaFlower,
    NormalFlower,
    PlayerFlower,
    SoldierAnt,
    SoldierFireAnt,
    SoldierTermite,
    SummonedBeetle,
    SummonedSoldierAnt,
    BandageBeetle,
    Bee,
    Hornet,
    BumbleBee,
};

using MobType = EMobType;

inline constexpr std::array<std::string_view, 16> mob_type_names = {
    "None",
    "Beetle",
    "Gambler",
    "NormalLadybug",
    "MechaFlower",
    "NormalFlower",
    "PlayerFlower",
    "SoldierAnt",
    "SoldierFireAnt",
    "SoldierTermite",
    "SummonedBeetle",
    "SummonedSoldierAnt",
    "BandageBeetle",
    "Bee",
    "Hornet",
    "BumbleBee",
};

inline std::string_view GetMobTypeName(EMobType type)
{
    size_t index = static_cast<size_t>(type);
    if (index >= mob_type_names.size()) return "UnknownMob";
    return mob_type_names[index];
}

inline std::string_view GetMobTypeName(uint8_t type) { return GetMobTypeName(static_cast<EMobType>(type)); }

inline bool MatchMobTypeAlias(std::string_view text, EMobType type)
{
    if (text == GetMobTypeName(type)) return true;

    switch (type)
    {
    case EMobType::Beetle:
        return text == "btl";
    case EMobType::BandageBeetle:
        return text == "mummy" || text == "bandagebeetle" || text == "bandage_beetle";
    case EMobType::SoldierAnt:
        return text == "sat" || text == "soidierant";
    case EMobType::SoldierFireAnt:
        return text == "sfa" || text == "soldierfireant";
    case EMobType::SoldierTermite:
        return text == "stm" || text == "soldiertermite";
    case EMobType::SummonedSoldierAnt:
        return text == "ssat";
    case EMobType::Bee:
        return text == "bee";
    case EMobType::Hornet:
        return text == "hornet";
    case EMobType::BumbleBee:
        return text == "bumblebee" || text == "bumble_bee" || text == "bbb";
    default:
        return false;
    }
}
