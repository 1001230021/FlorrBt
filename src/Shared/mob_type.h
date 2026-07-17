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
    Rock,
    BabyAnt,
    WorkerAnt,
    QueenAnt,
    AntHole,
    Spider,
    Sandstorm,
    Dummy,
};

using MobType = EMobType;

inline constexpr std::array<std::string_view, 24> mob_type_names = {
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
    "Rock",
    "BabyAnt",
    "WorkerAnt",
    "QueenAnt",
    "AntHole",
    "Spider",
    "Sandstorm",
    "Dummy",
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
    case EMobType::NormalLadybug:
        return text == "lady" || text == "ladybug" || text == "clady" || text == "normal_ladybug";
    case EMobType::Beetle:
        return text == "btl";
    case EMobType::BandageBeetle:
        return text == "mummy" || text == "bandagebeetle" || text == "bandage_beetle";
    case EMobType::SoldierAnt:
        return text == "sa" || text == "sat" || text == "soldierant" || text == "soidierant";
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
    case EMobType::Rock:
        return text == "rock";
    case EMobType::BabyAnt:
        return text == "ba" || text == "babyant" || text == "baby_ant";
    case EMobType::WorkerAnt:
        return text == "wa" || text == "workerant" || text == "worker_ant";
    case EMobType::QueenAnt:
        return text == "qa" || text == "queenant" || text == "queen_ant";
    case EMobType::AntHole:
        return text == "ah" || text == "anthole" || text == "ant_hole";
    case EMobType::Spider:
        return text == "spider" || text == "spi";
    case EMobType::Sandstorm:
        return text == "sandstorm" || text == "ss";
    case EMobType::Dummy:
        return text == "dummy" || text == "training_dummy" || text == "trainingdummy";
    default:
        return false;
    }
}
