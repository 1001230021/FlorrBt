#pragma once
#include <cstdint>
#include <string_view>

// Layer names are interpreted only at the map boundary. Physics and gameplay
// consume these roles, never tile GIDs or texture names.
enum class EMapLayerRole : std::uint8_t { Terrain, Shortcut, ShortcutCover };
inline EMapLayerRole MapLayerRole(std::string_view name)
{
    if (name == "shortcut") return EMapLayerRole::Shortcut;
    if (name == "shortcut_cover") return EMapLayerRole::ShortcutCover;
    return EMapLayerRole::Terrain;
}

enum class EWallQuery : std::uint8_t { Solid = 1, Shortcut = 2, All = 3 };
inline bool IncludesWall(EWallQuery query, EWallQuery category)
{
    return (static_cast<unsigned>(query) & static_cast<unsigned>(category)) != 0;
}

enum class ECapabilityRestriction : std::uint8_t { None = 0, Petals = 1, StatModifiers = 2 };
enum class ERestrictionSource : std::uint8_t { Shortcut, Digging, Count };

// Runtime restrictions do not modify loadout ownership, skills, or CState.
// Sources compose: ending digging must not resume petals inside a shortcut.
class SCapabilityState
{
  public:
    bool Set(ERestrictionSource source, std::uint8_t restrictions)
    {
        auto& value = m_sources[static_cast<unsigned>(source)];
        if (value == restrictions) return false;
        value = restrictions;
        return true;
    }
    bool HasSource(ERestrictionSource source) const
    { return m_sources[static_cast<unsigned>(source)] != 0; }
    bool Restricted(ECapabilityRestriction restriction) const
    {
        for (auto value : m_sources)
            if ((value & static_cast<std::uint8_t>(restriction)) != 0) return true;
        return false;
    }
  private:
    std::uint8_t m_sources[static_cast<unsigned>(ERestrictionSource::Count)]{};
};

struct CShortcutRule
{
    static constexpr std::uint8_t Restrictions(bool player_controlled, bool touching)
    {
        return player_controlled && touching
            ? static_cast<std::uint8_t>(ECapabilityRestriction::Petals) |
              static_cast<std::uint8_t>(ECapabilityRestriction::StatModifiers)
            : 0;
    }
};
