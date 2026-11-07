#pragma once
#include <cstdint>
#include <string>
#include <vector>

enum class EFeatureSurface : std::uint8_t { QuickActions = 1, TopActions = 2, AccountMenu = 4, Hud = 8 };
enum class EFeatureInteraction : std::uint8_t { Panel = 0, Toggle = 1, Action = 2 };
namespace feature_access { constexpr std::uint8_t view = 1, edit = 2, forge = 4, all = 7; }
struct SFeatureRule
{
    std::string key;
    bool visible = true;
    std::uint8_t access = feature_access::all;
    bool operator==(const SFeatureRule&) const = default;
};
struct SClientFeature
{
    std::string key, title, icon, reason;
    std::uint8_t surfaces = static_cast<std::uint8_t>(EFeatureSurface::QuickActions);
    EFeatureInteraction interaction = EFeatureInteraction::Panel;
    int order = 0;
    bool visible = true, badge = false;
    std::uint8_t access = 0;
    bool operator==(const SClientFeature&) const = default;
};
