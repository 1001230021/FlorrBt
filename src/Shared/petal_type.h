#pragma once
#include "entity_type.h"
#include "mod_petal_type.h"
#include <array>
#include <cstdint>
#include <string_view>

// Native (vanilla / open-world) petals ONLY. Controller-specific petals live
// in mod_petal_type.h (EModPetalType) and register through
// RegisterModPetalDefinition; keep this enum free of controller content.
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
    Shovel,
    Yucca,
    WhiteFungus,
    BlackFungus,
    Broccoli,
    Douli,
    Trapper,
    Amulet,
    Plank,
    Tomato,
    HallucinogenicMushroom,
    Lightning,
    Battery,
    Bulb,
    // 68..70: pinned mod petal wire ids (mod_petal_type.h: Brush/Eraser/
    // EouPheromone). Ids persist in accounts and hot-reload snapshots, so the
    // holes below are permanent - never allocate natives inside them.
    Magnet = 71,
    Linker,
    Orb,
    MagicLeaf,
    MagicStinger,
    MagicMissile,
    MagicEye,
    MagicBubble,
    MagicLightning,
    MagicCactus,
};

using PetalType = EPetalType;

// Controller-specific ids map into the shared petal id space (they carry
// their pinned wire id, so this is a pure value cast).
constexpr EPetalType ToPetalType(EModPetalType type) { return static_cast<EPetalType>(type); }

inline constexpr std::array<std::string_view, 81> petal_type_names = {
    "None",           "Air",         "AntEgg",        "Antennae", "Basic",     "BeetleEgg", "Bone",       "Bubble",
    "Carrot",         "Coin",        "Compass",       "Cogwheel", "Disc",      "Dust",      "GoldenLeaf", "Iris",
    "Lentil",         "Moon",        "Nullification", "Pincer",   "Relic",     "Rose",      "YinYang",    "Missile",
    "BloodSacrifice", "Corruption",  "Bandage",       "Heavy",    "Faster",    "Yggdrasil", "Dahlia",     "Wing",
    "Triangle",       "Sawblade",    "Fragment",      "Mimic",    "Glass",     "Stinger",   "BrokenEgg",  "Light",
    "Leaf",           "Rock",        "Web",           "Cactus",   "Pollen",    "Corn",      "Rice",       "Basil",
    "Soil",           "Honey",       "Wax",           "ThirdEye", "Dandelion", "Orange",    "Shovel",     "Yucca",
    "WhiteFungus",    "BlackFungus", "Broccoli",      "Douli",     "Trapper",   "Amulet", "Plank", "Tomato",
    "HallucinogenicMushroom", "Lightning", "Battery", "Bulb", "None", "None", "None", "Magnet", "Linker",
    "Orb", "MagicLeaf", "MagicStinger", "MagicMissile", "MagicEye", "MagicBubble", "MagicLightning",
    "MagicCactus",
};

static_assert(static_cast<size_t>(EPetalType::MagicCactus) + 1 == petal_type_names.size(),
              "petal type names must stay aligned with EPetalType");
// The mod petal holes must stay holes on the native side and must be filled
// on the mod side; native Magnet (71) must never be shadowed by a mod entry.
static_assert(petal_type_names[static_cast<size_t>(EModPetalType::Brush)] == "None" &&
                  petal_type_names[static_cast<size_t>(EModPetalType::Eraser)] == "None" &&
                  petal_type_names[static_cast<size_t>(EModPetalType::EouPheromone)] == "None",
              "reserved mod petal slots must stay None in the native names table");
static_assert(!GetModPetalTypeName(static_cast<int>(EModPetalType::Brush)).empty() &&
                  !GetModPetalTypeName(static_cast<int>(EModPetalType::Eraser)).empty() &&
                  !GetModPetalTypeName(static_cast<int>(EModPetalType::EouPheromone)).empty(),
              "mod petal ids must be declared in the mod_petal_type.h manifest");
static_assert(GetModPetalTypeName(static_cast<int>(EPetalType::Magnet)).empty(),
              "native Magnet id must not appear in the mod petal manifest");
// Live petals retain their compact `100 + id` network encoding. Drops no
// longer share that byte range, so only the live-petal limit remains.
static_assert(server_petal_entity_type_offset + kPetalIdCapacity - 1 <= 255,
              "petal network IDs no longer fit in uint8_t");

inline std::string_view GetPetalTypeName(EPetalType type)
{
    const size_t index = static_cast<size_t>(type);
    if (index < petal_type_names.size())
    {
        const std::string_view name = petal_type_names[index];
        // Reserved mod slots read "None" natively; fall through to the mod
        // manifest so brush/eraser/pheromone keep their real names.
        if (name != "None" || index == 0) return name;
    }
    if (index >= kPetalIdCapacity) return "UnknownPetal";
    const std::string_view mod_name = GetModPetalTypeName(static_cast<int>(index));
    return mod_name.empty() ? "UnknownPetal" : mod_name;
}

inline std::string_view GetPetalTypeName(uint8_t type) { return GetPetalTypeName(static_cast<EPetalType>(type)); }

// Single validity check for numeric petal ids from the wire, persistence, or
// console input: true for every native and declared mod petal id.
inline bool IsKnownPetalTypeId(int value)
{
    if (value <= 0 || value >= kPetalIdCapacity) return false;
    if (static_cast<size_t>(value) < petal_type_names.size() &&
        petal_type_names[static_cast<size_t>(value)] != "None")
        return true;
    return !GetModPetalTypeName(value).empty();
}

inline bool UsesBaseAndDefenseReach(EPetalType type)
{
    switch (type)
    {
    case EPetalType::Rose:
    case EPetalType::Dahlia:
    case EPetalType::Yucca:
    case EPetalType::Broccoli:
    case EPetalType::Amulet:
        return true;
    default:
        return false;
    }
}

inline bool IgnoresReachBonus(EPetalType type)
{
    switch (type)
    {
    case EPetalType::BrokenEgg:
    case EPetalType::Basil:
    case EPetalType::Web:
    case EPetalType::Pollen:
    case EPetalType::Honey:
    case EPetalType::Wax:
    case EPetalType::ThirdEye:
    case EPetalType::Carrot:
    case EPetalType::Orange:
    case EPetalType::Missile:
    case EPetalType::Shovel:
    case EPetalType::Trapper:
    case EPetalType::Plank:
    case EPetalType::Battery:
    case EPetalType::Bulb:
        return true;
    default:
        return false;
    }
}

inline bool MatchPetalTypeAlias(std::string_view text, EPetalType type)
{
    if (text == GetPetalTypeName(type)) return true;
    if (IsModPetalTypeId(static_cast<int>(type)))
        return MatchModPetalTypeAlias(text, static_cast<int>(type));

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
    case EPetalType::Yucca:
        return text == "yucca" || text == "yuc";
    case EPetalType::WhiteFungus:
        return text == "white_fungus" || text == "whitefungus" || text == "wf";
    case EPetalType::BlackFungus:
        return text == "black_fungus" || text == "blackfungus" || text == "bf";
    case EPetalType::Broccoli:
        return text == "broccoli" || text == "broc";
    case EPetalType::Douli:
        return text == "douli" || text == "dl";
    case EPetalType::Trapper:
        return text == "trapper" || text == "trap" || text == "trp";
    case EPetalType::Amulet:
        return text == "amulet" || text == "amu";
    case EPetalType::Plank:
        return text == "plank" || text == "plk";
    case EPetalType::Tomato:
        return text == "tomato" || text == "tom";
    case EPetalType::HallucinogenicMushroom:
        return text == "hallucinogenic_mushroom" || text == "hallucinogenicmushroom" || text == "hm";
    case EPetalType::Lightning:
        return text == "lightning" || text == "ltn";
    case EPetalType::Battery:
        return text == "battery" || text == "btr";
    case EPetalType::Bulb:
        return text == "bulb";
    case EPetalType::Linker:
        return text == "linker" || text == "lnk";
    case EPetalType::Orb:
        return text == "orb" || text == "magic_orb" || text == "magicorb";
    case EPetalType::MagicLeaf:
        return text == "magic_leaf" || text == "magicleaf" || text == "mleaf";
    case EPetalType::MagicStinger:
        return text == "magic_stinger" || text == "magicstinger" || text == "mstinger";
    case EPetalType::MagicMissile:
        return text == "magic_missile" || text == "magicmissile" || text == "mmissile";
    case EPetalType::MagicEye:
        return text == "magic_eye" || text == "magiceye" || text == "meye";
    case EPetalType::MagicBubble:
        return text == "magic_bubble" || text == "magicbubble" || text == "mbubble";
    case EPetalType::MagicLightning:
        return text == "magic_lightning" || text == "magiclightning" || text == "mlightning";
    case EPetalType::MagicCactus:
        return text == "magic_cactus" || text == "magiccactus" || text == "mcactus";
    default:
        return false;
    }
}
