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
    Dandelion,
    AntEgg,
    FireAntEgg,
    TermiteEgg,
    QueenAntEgg,
    QueenFireAntEgg,
    BabyFireAnt,
    WorkerFireAnt,
    FireQueenAnt,
    BabyTermite,
    WorkerTermite,
    TermiteOvermind,
    LeafPiece,
    LeafcutterSoldier,
    Titan,
    Firefly,
    Bubble,
    // Mod/controller-specific mobs live in this reserved range. Slots are
    // optional (the mob registry does not require them to be registered) so
    // game controllers can register their own mobs at runtime.
    Mod64 = 64,
    Mod65,
    Mod66,
    Mod67,
    Mod68,
    Mod69,
    Mod70,
    Mod71,
    Mod72,
    Mod73,
    Mod74,
    Mod75,
    Mod76,
    Mod77,
    Mod78,
    Mod79,
    Mod80,
    Mod81,
    Mod82,
    Mod83,
    Mod84,
    Mod85,
    Mod86,
    Mod87,
    Mod88,
    Mod89,
    Mod90,
    Mod91,
    Mod92,
    Mod93,
    Mod94,
    Mod95,
    Mod96,
    Mod97,
    Mod98,
    Mod99,
    Mod100,
    Mod101,
    Mod102,
    Mod103,
    Mod104,
    Mod105,
    Mod106,
    Mod107,
    Mod108,
    Mod109,
    Mod110,
    Mod111,
    Mod112,
    Mod113,
    Mod114,
    Mod115,
    Mod116,
    Mod117,
    Mod118,
    Mod119,
    Mod120,
    Mod121,
    Mod122,
    Mod123,
    Mod124,
    Mod125,
    Mod126,
    Mod127,
};

using MobType = EMobType;

inline constexpr std::array<std::string_view, 128> mob_type_names = {
    "None",          "Beetle",        "Gambler",        "NormalLadybug",  "MechaFlower",    "NormalFlower",
    "PlayerFlower",  "SoldierAnt",    "SoldierFireAnt", "SoldierTermite", "SummonedBeetle", "SummonedSoldierAnt",
    "BandageBeetle", "Bee",           "Hornet",         "BumbleBee",      "Rock",           "BabyAnt",
    "WorkerAnt",     "QueenAnt",      "AntHole",        "Spider",         "Sandstorm",      "Dummy",
    "Dandelion",     "AntEgg",        "FireAntEgg",     "TermiteEgg",     "QueenAntEgg",    "QueenFireAntEgg",
    "BabyFireAnt",   "WorkerFireAnt", "FireQueenAnt",   "BabyTermite",    "WorkerTermite",  "TermiteOvermind",
    "LeafPiece",     "LeafcutterSoldier", "Titan",      "Firefly",        "Bubble",         "None", "None",
    "None",          "None",          "None",           "None",           "None",           "None",
    "None",          "None",          "None",           "None",           "None",           "None",
    "None",          "None",          "None",           "None",           "None",           "None",
    "None",          "None",          "None",           "Mod64",          "Mod65",          "Mod66",
    "Mod67",         "Mod68",         "Mod69",          "Mod70",          "Mod71",          "Mod72",
    "Mod73",         "Mod74",         "Mod75",          "Mod76",          "Mod77",          "Mod78",
    "Mod79",         "Mod80",         "Mod81",          "Mod82",          "Mod83",          "Mod84",
    "Mod85",         "Mod86",         "Mod87",          "Mod88",          "Mod89",          "Mod90",
    "Mod91",         "Mod92",         "Mod93",          "Mod94",          "Mod95",          "Mod96",
    "Mod97",         "Mod98",         "Mod99",          "Mod100",         "Mod101",         "Mod102",
    "Mod103",        "Mod104",        "Mod105",         "Mod106",         "Mod107",         "Mod108",
    "Mod109",        "Mod110",        "Mod111",         "Mod112",         "Mod113",         "Mod114",
    "Mod115",        "Mod116",        "Mod117",         "Mod118",         "Mod119",         "Mod120",
    "Mod121",        "Mod122",        "Mod123",         "Mod124",         "Mod125",         "Mod126",
    "Mod127",
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
    case EMobType::MechaFlower:
        return text == "mf" || text == "mechaflower" || text == "mecha_flower";
    case EMobType::Beetle:
        return text == "btl";
    case EMobType::BandageBeetle:
        return text == "mummy" || text == "bandagebeetle" || text == "bandage_beetle";
    case EMobType::SoldierAnt:
        return text == "sa" || text == "sat" || text == "soldierant" || text == "soldier_ant" ||
               text == "soidierant";
    case EMobType::SoldierFireAnt:
        return text == "sfa" || text == "soldierfireant";
    case EMobType::SoldierTermite:
        return text == "sta" || text == "stm" || text == "soldiertermite";
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
    case EMobType::Bubble:
        return text == "bubble";
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
    case EMobType::Dandelion:
        return text == "dande" || text == "dandelion";
    case EMobType::AntEgg:
        return text == "egg" || text == "antegg" || text == "ant_egg";
    case EMobType::FireAntEgg:
        return text == "fgg" || text == "fireantegg" || text == "fire_ant_egg";
    case EMobType::TermiteEgg:
        return text == "tgg" || text == "termiteegg" || text == "termite_egg";
    case EMobType::QueenAntEgg:
        return text == "qaegg" || text == "queenantegg" || text == "queen_ant_egg";
    case EMobType::QueenFireAntEgg:
        return text == "fqaegg" || text == "queenfireantegg" || text == "queen_fire_ant_egg";
    case EMobType::BabyFireAnt:
        return text == "bfa" || text == "babyfireant" || text == "baby_fire_ant";
    case EMobType::WorkerFireAnt:
        return text == "wfa" || text == "workerfireant" || text == "worker_fire_ant";
    case EMobType::FireQueenAnt:
        return text == "fqa" || text == "firequeenant" || text == "fire_queen_ant";
    case EMobType::BabyTermite:
        return text == "bta" || text == "babytermite" || text == "baby_termite";
    case EMobType::WorkerTermite:
        return text == "wta" || text == "workertermite" || text == "worker_termite";
    case EMobType::TermiteOvermind:
        return text == "ovm" || text == "overmind" || text == "termiteovermind" || text == "termite_overmind";
    case EMobType::LeafPiece:
        return text == "lp" || text == "leafpiece" || text == "leaf_piece";
    case EMobType::LeafcutterSoldier:
        return text == "lcs" || text == "leafcuttersoldier" || text == "leafcutter_soldier";
    case EMobType::Titan:
        return text == "titan";
    default:
        return false;
    }
}
