#pragma once
#include <array>
#include <string_view>

// 玩法专属生物按控制器分段声明，与 mob_type.h 的基础生物分开。
// ID 只在服务端使用，注册时必须指定基础生物的视觉代理；客户端生成器跳过此表。
// 快照保存数值 ID，不能重排或复用；每项须在下方名称清单中登记。

inline constexpr int kModMobTypeBase = 64;
// 注册表共享容量：基础生物 [1..40]，扩展内容 [64..127]。
inline constexpr int kMobIdCapacity = 128;

enum class EModMobType : int
{
    None = 0,

    // EOU 专属生物，预留 [64..79]。
    EouWorker = 64,
    EouFood,
    EouCell,
    EouEgg,
    EouQueen,
    EouSoldier,

    // 复合生物的内部实体，预留 [80..95]；使用基础视觉代理，不单独列入图鉴。
    FireflyLarvaBody = 80,
};

struct SModMobTypeName
{
    int id;
    std::string_view name;
};

// 名称使用 snake_case，可由控制台和区域工具直接匹配。
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
}

static_assert(mod_mob_type_detail::AllEntriesInRangeAndUnique(),
              "mod mob manifest entries must be unique, named, and inside [kModMobTypeBase, kMobIdCapacity)");
