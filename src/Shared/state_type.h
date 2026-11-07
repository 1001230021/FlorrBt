#pragma once
#include <cstdint>

// 状态协议 ID 必须保持稳定。
enum class EStateType : std::uint8_t
{
    None = 0,
    Poison = 1,
    BanSlot = 2,
    PincerSpeedReduce = 3,
    WebSpeedReduce = 4,
    AntiHeal = 5,
    Nullification = 6,
    Undead = 7,
    Corruption = 8,
    NoRevive = 9,
    Invincible = 10,
    Digging = 11,
    PsionicConnection = 12,
    Hallucinogenic = 13,
    DropStack = 14,
    Pulse = 15,
};

// DropStack 仅携带最高 255 的掉落堆叠数用于角标，不参与玩法状态生命周期。

inline constexpr bool IsKnownStateType(EStateType type)
{
    return type > EStateType::None && type <= EStateType::Pulse;
}
