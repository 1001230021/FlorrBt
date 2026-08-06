#pragma once

enum class EDamageType : int
{
    Normal = 0,
    Poison,
    Lighting,
    True,
    SharedNormal,
    SharedPoison,
    SharedLighting,
    SharedTrue
};

constexpr bool IsSharedDamageType(EDamageType damage_type)
{
    switch (damage_type)
    {
    case EDamageType::SharedNormal:
    case EDamageType::SharedPoison:
    case EDamageType::SharedLighting:
    case EDamageType::SharedTrue:
        return true;
    default:
        return false;
    }
}

constexpr EDamageType BaseDamageType(EDamageType damage_type)
{
    switch (damage_type)
    {
    case EDamageType::SharedNormal:
        return EDamageType::Normal;
    case EDamageType::SharedPoison:
        return EDamageType::Poison;
    case EDamageType::SharedLighting:
        return EDamageType::Lighting;
    case EDamageType::SharedTrue:
        return EDamageType::True;
    default:
        return damage_type;
    }
}

constexpr EDamageType ToSharedDamageType(EDamageType damage_type)
{
    switch (BaseDamageType(damage_type))
    {
    case EDamageType::Normal:
        return EDamageType::SharedNormal;
    case EDamageType::Poison:
        return EDamageType::SharedPoison;
    case EDamageType::Lighting:
        return EDamageType::SharedLighting;
    case EDamageType::True:
        return EDamageType::SharedTrue;
    default:
        return EDamageType::SharedNormal;
    }
}
