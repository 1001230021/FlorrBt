#pragma once
#include <array>
#include <string_view>

// Controller-owned petal content. Native petals live in petal_type.h;
// everything a specific game controller brings along is declared HERE, one
// block per controller, and never leaks into EPetalType.
//
// UNLIKE mod mobs these ids ARE client-visible wire ids: petal projectiles,
// inventory messages, account persistence and hot-reload snapshots all store
// them numerically, and tools/generate_shared_ids.js merges them into the
// client id tables. Therefore every value below is pinned forever - never
// renumber, never reuse a released id.
//
// Wire budget (see entity_type.h): petal network id = 100 + id (< 180) and
// ground-drop network id = 180 + id (<= 255), so ids 76..79 are slot-only
// (never spawn them as ground drops) and 80+ is a hard cap enforced in
// petal_type.h.
//
// Every id used by code MUST have a row in mod_petal_type_names below -
// RegisterModPetalDefinition refuses unnamed ids at compile time. That table
// is the controller's type manifest.

inline constexpr int kModPetalTypeBase = 68;
inline constexpr int kPetalIdCapacity = 80;

enum class EModPetalType : int
{
    None = 0,

    // --- mutable_terrain controller (tile painting tools) --- legacy ids pinned
    Brush = 68,
    Eraser = 69,

    // --- ant_kingdom controller (EOU) ---
    EouPheromone = 70,

    // 71 = native Magnet (petal_type.h) - never allocate it here.
    // next free id: 72 (72..75 droppable, 76..79 slot-only)
};

struct SModPetalTypeName
{
    int id;
    std::string_view name;
};

// Names keep the exact PascalCase spelling the entries had inside EPetalType
// so logs, client PETAL_NAMES output, and generated snake_case slugs
// (brush / eraser / eou_pheromone) stay byte-identical.
inline constexpr std::array<SModPetalTypeName, 3> mod_petal_type_names = { {
    { static_cast<int>(EModPetalType::Brush), "Brush" },
    { static_cast<int>(EModPetalType::Eraser), "Eraser" },
    { static_cast<int>(EModPetalType::EouPheromone), "EouPheromone" },
} };

inline constexpr std::string_view GetModPetalTypeName(int type)
{
    for (const SModPetalTypeName& entry : mod_petal_type_names)
        if (entry.id == type) return entry.name;
    return {};
}

inline constexpr bool IsModPetalTypeId(int type) { return type >= kModPetalTypeBase && type < kPetalIdCapacity; }

// Console alias matching for mod petals (native aliases stay in petal_type.h).
inline bool MatchModPetalTypeAlias(std::string_view text, int type)
{
    switch (static_cast<EModPetalType>(type))
    {
    case EModPetalType::Brush:
        return text == "brush";
    case EModPetalType::Eraser:
        return text == "eraser" || text == "ers";
    case EModPetalType::EouPheromone:
        return text == "eou_pheromone" || text == "pheromone" || text == "phe";
    default:
        return false;
    }
}

namespace mod_petal_type_detail
{
constexpr bool AllEntriesInRangeAndUnique()
{
    for (std::size_t i = 0; i < mod_petal_type_names.size(); ++i)
    {
        if (!IsModPetalTypeId(mod_petal_type_names[i].id)) return false;
        if (mod_petal_type_names[i].name.empty()) return false;
        for (std::size_t j = i + 1; j < mod_petal_type_names.size(); ++j)
        {
            if (mod_petal_type_names[i].id == mod_petal_type_names[j].id) return false;
            if (mod_petal_type_names[i].name == mod_petal_type_names[j].name) return false;
        }
    }
    return true;
}
} // namespace mod_petal_type_detail

static_assert(mod_petal_type_detail::AllEntriesInRangeAndUnique(),
              "mod petal manifest entries must be unique, named, and inside [kModPetalTypeBase, kPetalIdCapacity)");
