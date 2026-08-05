#pragma once
#include <cstdint>

// Stable wire identifiers for gameplay states. Keep existing values unchanged.
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
};

inline constexpr bool IsKnownStateType(EStateType type)
{
    return type > EStateType::None && type <= EStateType::PsionicConnection;
}
