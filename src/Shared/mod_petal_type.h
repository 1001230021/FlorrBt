#pragma once
#include <array>
#include <string_view>

// 玩法专属花瓣按控制器分段声明，与 petal_type.h 的基础花瓣分开。
// 这些 ID 同时用于协议、账号和快照，客户端生成器会合并此表；不能重排或复用。
// 活跃花瓣使用 100 + id 的单字节编码；掉落物单独携带花瓣 ID。
// 每项须在下方名称清单中登记，注册器拒绝未命名的 ID。

inline constexpr int kModPetalTypeBase = 68;
inline constexpr int kPetalIdCapacity = 128;

enum class EModPetalType : int
{
    None = 0,

    // 可变地形工具，保留旧 ID。
    Brush = 68,
    Eraser = 69,

    // EOU 专属花瓣。
    EouPheromone = 70,

    // 开放世界的扩展内容，保留既有协议 ID。
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

struct SModPetalTypeName
{
    int id;
    std::string_view name;
};

// 保留原 PascalCase 名称，使日志、客户端名称表与生成的 snake_case 标识兼容旧数据。
inline constexpr std::array<SModPetalTypeName, 13> mod_petal_type_names = { {
    { static_cast<int>(EModPetalType::Brush), "Brush" },
    { static_cast<int>(EModPetalType::Eraser), "Eraser" },
    { static_cast<int>(EModPetalType::EouPheromone), "EouPheromone" },
    { static_cast<int>(EModPetalType::Magnet), "Magnet" },
    { static_cast<int>(EModPetalType::Linker), "Linker" },
    { static_cast<int>(EModPetalType::Orb), "Orb" },
    { static_cast<int>(EModPetalType::MagicLeaf), "MagicLeaf" },
    { static_cast<int>(EModPetalType::MagicStinger), "MagicStinger" },
    { static_cast<int>(EModPetalType::MagicMissile), "MagicMissile" },
    { static_cast<int>(EModPetalType::MagicEye), "MagicEye" },
    { static_cast<int>(EModPetalType::MagicBubble), "MagicBubble" },
    { static_cast<int>(EModPetalType::MagicLightning), "MagicLightning" },
    { static_cast<int>(EModPetalType::MagicCactus), "MagicCactus" },
} };

inline constexpr std::string_view GetModPetalTypeName(int type)
{
    for (const SModPetalTypeName& entry : mod_petal_type_names)
        if (entry.id == type) return entry.name;
    return {};
}

inline constexpr bool IsModPetalTypeId(int type) { return !GetModPetalTypeName(type).empty(); }

// 扩展花瓣的控制台别名；基础花瓣别名仍定义在 petal_type.h。
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
    case EModPetalType::Magnet:
        return text == "magnet" || text == "mgt";
    case EModPetalType::Linker:
        return text == "linker" || text == "lnk";
    case EModPetalType::Orb:
        return text == "orb" || text == "magic_orb" || text == "magicorb";
    case EModPetalType::MagicLeaf:
        return text == "magic_leaf" || text == "magicleaf" || text == "mleaf";
    case EModPetalType::MagicStinger:
        return text == "magic_stinger" || text == "magicstinger" || text == "mstinger";
    case EModPetalType::MagicMissile:
        return text == "magic_missile" || text == "magicmissile" || text == "mmissile";
    case EModPetalType::MagicEye:
        return text == "magic_eye" || text == "magiceye" || text == "meye";
    case EModPetalType::MagicBubble:
        return text == "magic_bubble" || text == "magicbubble" || text == "mbubble";
    case EModPetalType::MagicLightning:
        return text == "magic_lightning" || text == "magiclightning" || text == "mlightning";
    case EModPetalType::MagicCactus:
        return text == "magic_cactus" || text == "magiccactus" || text == "mcactus";
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
}

static_assert(mod_petal_type_detail::AllEntriesInRangeAndUnique(),
              "mod petal manifest entries must be unique, named, and inside [kModPetalTypeBase, kPetalIdCapacity)");
