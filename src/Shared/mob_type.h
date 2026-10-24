#pragma once
#include "mod_mob_type.h"
#include <array>
#include <cstdint>
#include <string_view>

// Native (vanilla / open-world) mobs ONLY. Controller-specific mobs live in
// mod_mob_type.h (EModMobType) and register through RegisterModMob; keep this
// enum free of controller content.
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
    Crab,
    Jellyfish,
    MagicFirefly,
    FireflyLarva,
};

using MobType = EMobType;

// Controller-specific ids map into the shared mob id space above the natives.
constexpr EMobType ToMobType(EModMobType type) { return static_cast<EMobType>(type); }

inline constexpr std::array<std::string_view, 45> mob_type_names = {
    "None",          "Beetle",        "Gambler",        "NormalLadybug",  "MechaFlower",    "NormalFlower",
    "PlayerFlower",  "SoldierAnt",    "SoldierFireAnt", "SoldierTermite", "SummonedBeetle", "SummonedSoldierAnt",
    "BandageBeetle", "Bee",           "Hornet",         "BumbleBee",      "Rock",           "BabyAnt",
    "WorkerAnt",     "QueenAnt",      "AntHole",        "Spider",         "Sandstorm",      "Dummy",
    "Dandelion",     "AntEgg",        "FireAntEgg",     "TermiteEgg",     "QueenAntEgg",    "QueenFireAntEgg",
    "BabyFireAnt",   "WorkerFireAnt", "FireQueenAnt",   "BabyTermite",    "WorkerTermite",  "TermiteOvermind",
    "LeafPiece",     "LeafcutterSoldier", "Titan",      "Firefly",        "Bubble",       "Crab",
    "Jellyfish",      "MagicFirefly", "FireflyLarva",
};

static_assert(static_cast<size_t>(EMobType::FireflyLarva) + 1 == mob_type_names.size(),
              "mob type names must stay aligned with EMobType");
static_assert(mob_type_names.size() <= static_cast<size_t>(kModMobTypeBase),
              "native mob ids grew into the reserved mod range");

inline std::string_view GetMobTypeName(EMobType type)
{
    size_t index = static_cast<size_t>(type);
    if (index < mob_type_names.size()) return mob_type_names[index];
    const std::string_view mod_name = GetModMobTypeName(static_cast<int>(index));
    return mod_name.empty() ? "UnknownMob" : mod_name;
}

inline std::string_view GetMobTypeName(uint8_t type) { return GetMobTypeName(static_cast<EMobType>(type)); }

// Numeric IDs cross persistence, console and debug boundaries. Keep one
// authoritative validity check covering both native and controller-owned
// manifests instead of relying on a factory lookup after an unchecked cast.
inline bool IsKnownMobTypeId(int value)
{
    if (value <= static_cast<int>(EMobType::None) || value >= kMobIdCapacity) return false;
    if (static_cast<size_t>(value) < mob_type_names.size()) return true;
    return !GetModMobTypeName(value).empty();
}

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
    case EMobType::Crab:
        return text == "crab";
    case EMobType::Jellyfish:
        return text == "jellyfish" || text == "jelly";
    case EMobType::MagicFirefly:
        return text == "magicfirefly" || text == "magic_firefly" || text == "mfirefly";
    case EMobType::FireflyLarva:
        return text == "fireflylarva" || text == "firefly_larva" || text == "fflarva";
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
