#pragma once
#include <array>
#include <string_view>

// Controller-owned mob content. Native (vanilla / open-world) mobs live in
// mob_type.h; everything a specific game controller brings along is declared
// HERE, one block per controller, and never leaks into EMobType.
//
// These ids are server-internal ONLY: every mod mob must register through
// RegisterModMob with a mandatory vanilla visual proxy, so the wire and the
// client id generator never see them (tools/generate_shared_ids.js skips this
// header on purpose).
//
// Allocation rules:
//  - one block per controller, explicit base, ids auto-increment inside it;
//  - values are stable forever (hot-reload snapshots store them numerically):
//    never renumber, never reuse a released id;
//  - every id used by code MUST have a row in mod_mob_type_names below -
//    RegisterModMob refuses unnamed ids at compile time. That table is the
//    controller's type manifest.

inline constexpr int kModMobTypeBase = 64;
// Registry slot capacity shared with the native range: natives [1..40],
// mod content [64..127].
inline constexpr int kMobIdCapacity = 128;

enum class EModMobType : int
{
    None = 0,

    // --- ant_kingdom controller (EOU) --- base 64
    EouWorker = 64,
    EouFood,
    EouCell,
    EouEgg,
    EouQueen,
    EouSoldier,
    // next ant_kingdom id: 70 (block reserves 64..79)

    // --- native implementation details --- base 80
    // These are not standalone bestiary entries. They use a native visual
    // proxy and only exist so compound native mobs can own real entities.
    FireflyLarvaBody = 80,
    // next internal id: 81 (block reserves 80..95)
};

struct SModMobTypeName
{
    int id;
    std::string_view name;
};

// Per-controller manifest: names are snake_case so console spawn commands and
// zone tools match them directly (spawn eou_worker).
inline constexpr std::array<SModMobTypeName, 7> mod_mob_type_names = { {
    { static_cast<int>(EModMobType::EouWorker), "eou_worker" },
    { static_cast<int>(EModMobType::EouFood), "eou_food" },
    { static_cast<int>(EModMobType::EouCell), "eou_cell" },
    { static_cast<int>(EModMobType::EouEgg), "eou_egg" },
    { static_cast<int>(EModMobType::EouQueen), "eou_queen" },
    { static_cast<int>(EModMobType::EouSoldier), "eou_soldier" },
    { static_cast<int>(EModMobType::FireflyLarvaBody), "firefly_larva_body" },
} };

inline constexpr std::string_view GetModMobTypeName(int type)
{
    for (const SModMobTypeName& entry : mod_mob_type_names)
        if (entry.id == type) return entry.name;
    return {};
}

inline constexpr bool IsModMobTypeId(int type) { return type >= kModMobTypeBase && type < kMobIdCapacity; }

namespace mod_mob_type_detail
{
constexpr bool AllEntriesInRangeAndUnique()
{
    for (std::size_t i = 0; i < mod_mob_type_names.size(); ++i)
    {
        if (!IsModMobTypeId(mod_mob_type_names[i].id)) return false;
        if (mod_mob_type_names[i].name.empty()) return false;
        for (std::size_t j = i + 1; j < mod_mob_type_names.size(); ++j)
        {
            if (mod_mob_type_names[i].id == mod_mob_type_names[j].id) return false;
            if (mod_mob_type_names[i].name == mod_mob_type_names[j].name) return false;
        }
    }
    return true;
}
} // namespace mod_mob_type_detail

static_assert(mod_mob_type_detail::AllEntriesInRangeAndUnique(),
              "mod mob manifest entries must be unique, named, and inside [kModMobTypeBase, kMobIdCapacity)");
