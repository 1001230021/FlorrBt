#pragma once
#include <cstdint>

// Runtime families are intentionally coarse. Projectile specializations live in
// EProjectileType so every projectile can be selected with one family check.
enum class EEntityType : std::uint8_t
{
    None = 0,
    Mob = 1u << 0,
    Projectile = 1u << 1,
    Drop = 1u << 2,
    StateZone = 1u << 3,
    Effect = 1u << 4,
    Portal = 1u << 5,
};

enum class EProjectileType : std::uint8_t
{
    None = 0,
    Petal,
    Missile,
    Pollen,
    Trap,
};

inline constexpr std::uint8_t server_petal_entity_type_offset = 100;
inline constexpr std::uint8_t server_drop_entity_type_offset = 180;
inline constexpr std::uint8_t server_trap_projectile_entity_type = 93;
inline constexpr std::uint8_t server_blood_sacrifice_entity_type = 94;
inline constexpr std::uint8_t server_dandelion_missile_entity_type = 95;
inline constexpr std::uint8_t server_pollen_entity_type = 96;
inline constexpr std::uint8_t server_spider_web_entity_type = 97;
inline constexpr std::uint8_t server_missile_entity_type = 98;
inline constexpr std::uint8_t server_portal_entity_type = 99;

struct SEntityTypeInfo
{
    EEntityType family = EEntityType::None;
    EProjectileType projectile = EProjectileType::None;
    std::uint8_t network_type = 0;

    constexpr bool Is(EEntityType type) const { return family == type; }
    constexpr bool Is(EProjectileType type) const
    {
        return family == EEntityType::Projectile && projectile == type;
    }
    constexpr bool IsNetworked() const { return network_type != 0; }
};

constexpr SEntityTypeInfo MakeEntityType(EEntityType family, std::uint8_t network_type = 0)
{
    return { family, EProjectileType::None, network_type };
}

constexpr SEntityTypeInfo MakeMobEntityType(std::uint8_t mob_type)
{
    return MakeEntityType(EEntityType::Mob, mob_type);
}

constexpr SEntityTypeInfo MakeProjectileEntityType(EProjectileType projectile, std::uint8_t network_type)
{
    return { EEntityType::Projectile, projectile, network_type };
}

constexpr std::uint8_t PetalNetworkType(std::uint8_t petal_type)
{
    return static_cast<std::uint8_t>(server_petal_entity_type_offset + petal_type);
}

constexpr SEntityTypeInfo MakePetalEntityType(std::uint8_t petal_type)
{
    return MakeProjectileEntityType(EProjectileType::Petal, PetalNetworkType(petal_type));
}

constexpr std::uint8_t DropNetworkType(std::uint8_t petal_type)
{
    return static_cast<std::uint8_t>(server_drop_entity_type_offset + petal_type);
}

constexpr SEntityTypeInfo MakeDropEntityType(std::uint8_t petal_type)
{
    return MakeEntityType(EEntityType::Drop, DropNetworkType(petal_type));
}
