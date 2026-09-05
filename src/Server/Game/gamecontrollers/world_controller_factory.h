#pragma once

#include "../controller_params.h"
#include <array>
#include <memory>
#include <string_view>
#include <vector>

class IGameController;

namespace world_controller_keys
{
inline constexpr std::string_view open = "world_controller.open";
inline constexpr std::string_view weather_diurnal_open = "world_controller.weather_diurnal_open";
inline constexpr std::string_view waiting = "world_controller.waiting";
inline constexpr std::string_view mutable_terrain = "world_controller.mutable_terrain";
inline constexpr std::string_view pvp = "world_controller.pvp";
inline constexpr std::string_view ant_kingdom = "world_controller.ant_kingdom";
inline constexpr std::string_view deathmatch = "world_controller.deathmatch";
inline constexpr std::string_view sandbox = "world_controller.sandbox";
}

namespace map_declarations
{
struct SMapDeclaration
{
    std::string_view map_path;
    std::string_view controller_key;
};

inline constexpr std::array<SMapDeclaration, 3> kMaps = {{
    { "data/maps/garden.tmj", world_controller_keys::weather_diurnal_open },
    { "data/maps/test.tmj", world_controller_keys::mutable_terrain },
    { "data/maps/ant_hel.tmj", world_controller_keys::open },
}};

inline std::string_view ControllerKeyForMap(std::string_view map_path)
{
    for (const SMapDeclaration& declaration : kMaps)
        if (declaration.map_path == map_path) return declaration.controller_key;
    return {};
}

inline bool DeclaresWeatherDiurnal(std::string_view map_path)
{
    return ControllerKeyForMap(map_path) == world_controller_keys::weather_diurnal_open;
}

// Resolves a friendly controller name (used by `/create`) to a controller
// key. Returns an empty view for unknown names.
inline std::string_view ControllerKeyForName(std::string_view name)
{
    if (name == "open" || name == "open_world") return world_controller_keys::open;
    if (name == "weather" || name == "weather_diurnal" || name == "weather_diurnal_open")
        return world_controller_keys::weather_diurnal_open;
    if (name == "waiting" || name == "waiting_room") return world_controller_keys::waiting;
    if (name == "mutable_terrain" || name == "terrain" || name == "brush")
        return world_controller_keys::mutable_terrain;
    if (name == "pvp" || name == "ant_hel" || name == "ant_hel_pvp" || name == "ant_pvp")
        return world_controller_keys::pvp;
    if (name == "ant_kingdom" || name == "eou")
        return world_controller_keys::ant_kingdom;
    if (name == "deathmatch" || name == "dm")
        return world_controller_keys::deathmatch;
    if (name == "sandbox" || name == "sand_box" || name == "sb")
        return world_controller_keys::sandbox;
    return {};
}
} // namespace map_declarations

// Creates a world controller for `snapshot_key`. Construction-time overrides
// (`params`) are applied by controllers that declare a ParamSchema; everything
// else ignores them.
std::unique_ptr<IGameController> CreateWorldController(std::string_view snapshot_key,
                                                       const CControllerParams& params = {});
// Declared parameter schema of the controller type for `snapshot_key` (empty
// when unknown or the controller declares none).
std::vector<SControllerParamInfo> ControllerParamSchemaForKey(std::string_view snapshot_key);
