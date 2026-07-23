#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

enum class ETalentId : uint16_t
{
    None = 0,
    PetalHealth = 1,
    PetalSplit = 2,
    FlowerHealth = 3,
    PetalRotationSpeed = 4,
    BodyDamage = 5,
    BodyDamagePoison = 6,
    PoisonDamage = 7,
    PetalReload = 8,
    SecondChance = 9,
    SlotNum = 10,
    Medic = 11,
    Reach = 12,
    Magnetism = 13,
    Summoner = 14,
    Antennae = 15,
    ConcentratedPoison = 16,
    Movement = 17,
};

using TalentId = ETalentId;

inline constexpr std::array<std::string_view, 18> talent_id_names = {
    "None",
    "PetalHealth",
    "PetalSplit",
    "FlowerHealth",
    "PetalRotationSpeed",
    "BodyDamage",
    "BodyDamagePoison",
    "PoisonDamage",
    "PetalReload",
    "SecondChance",
    "SlotNum",
    "Medic",
    "Reach",
    "Magnetism",
    "Summoner",
    "Antennae",
    "ConcentratedPoison",
    "Movement",
};

inline std::string_view GetTalentIdName(ETalentId id)
{
    size_t index = static_cast<size_t>(id);
    if (index >= talent_id_names.size()) return "UnknownTalent";
    return talent_id_names[index];
}

inline std::string_view GetTalentIdName(uint16_t id) { return GetTalentIdName(static_cast<ETalentId>(id)); }

inline bool MatchTalentIdAlias(std::string_view text, ETalentId id)
{
    if (text == GetTalentIdName(id)) return true;

    switch (id)
    {
    case ETalentId::PetalHealth:
        return text == "petal_health" || text == "petalhealth" || text == "ph";
    case ETalentId::PetalSplit:
        return text == "petal_split" || text == "petalsplit" || text == "split" || text == "duplicator" ||
               text == "duplicate" || text == "dup";
    case ETalentId::FlowerHealth:
        return text == "flower_health" || text == "flowerhealth" || text == "health" || text == "fh";
    case ETalentId::PetalRotationSpeed:
        return text == "petal_rotation_speed" || text == "petalrotationspeed" || text == "petal_rotation" ||
               text == "rotation" || text == "pr";
    case ETalentId::BodyDamage:
        return text == "body_damage" || text == "bodydamage" || text == "sharp_edges" || text == "sharpedges" ||
               text == "sharp" || text == "se";
    case ETalentId::BodyDamagePoison:
        return text == "body_damage_poison" || text == "bodydamagepoison" || text == "body_poison" ||
               text == "body_toxicity" || text == "bodytoxicity";
    case ETalentId::PoisonDamage:
        return text == "poison_damage" || text == "poisondamage" || text == "poison" || text == "poi";
    case ETalentId::PetalReload:
        return text == "petal_reload" || text == "petalreload" || text == "reload" || text == "cooldown" ||
               text == "re";
    case ETalentId::SecondChance:
        return text == "second_chance" || text == "secondchance" || text == "sc";
    case ETalentId::SlotNum:
        return text == "slot_num" || text == "slotnum" || text == "slots" || text == "slot" ||
               text == "loadout" || text == "lo";
    case ETalentId::Medic:
        return text == "medic" || text == "medicine" || text == "healing" || text == "med";
    case ETalentId::Reach:
        return text == "reach" || text == "attack_range";
    case ETalentId::Magnetism:
        return text == "magnetism" || text == "magnet" || text == "mag";
    case ETalentId::Summoner:
        return text == "summoner" || text == "summon";
    case ETalentId::Antennae:
        return text == "antennae" || text == "antenna" || text == "ante";
    case ETalentId::ConcentratedPoison:
        return text == "concentrated_poison" || text == "concentratedpoison" || text == "cpoison" ||
               text == "cpoi";
    case ETalentId::Movement:
        return text == "movement" || text == "move" || text == "speed" || text == "mmt";
    default:
        return false;
    }
}

enum class ETalentEvent : uint8_t
{
    RebuildFlowerStats = 0,
    RebuildPetalStats = 1,
    BeforeFlowerTakeDamage = 2,
    OnFlowerFatalDamage = 3,
    Tick = 4,
    RebuildSlotNum = 5,
};

using TalentEvent = ETalentEvent;

inline constexpr std::array<std::string_view, 6> talent_event_names = {
    "RebuildFlowerStats",
    "RebuildPetalStats",
    "BeforeFlowerTakeDamage",
    "OnFlowerFatalDamage",
    "Tick",
    "RebuildSlotNum",
};

inline std::string_view GetTalentEventName(ETalentEvent event)
{
    size_t index = static_cast<size_t>(event);
    if (index >= talent_event_names.size()) return "UnknownTalentEvent";
    return talent_event_names[index];
}
