#pragma once

#include "../controller_params.h"
#include "../extension.h"
#include <array>
#include <memory>
#include <string_view>
#include <vector>

class IGameController;

namespace world_controller_keys
{
inline constexpr std::string_view open = "world_controller.open";
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
    SWorldExtensionConfig extensions;
};

inline const std::array<SMapDeclaration, 3> kMaps = {{
    { "data/maps/garden.tmj", world_controller_keys::open, {true, true, true} },
    { "data/maps/test.tmj", world_controller_keys::mutable_terrain, {true, false, false} },
    { "data/maps/ant_hel.tmj", world_controller_keys::open, {true, false, false} },
}};

inline std::string_view ControllerKeyForMap(std::string_view map_path)
{
    for (const SMapDeclaration& declaration : kMaps)
        if (declaration.map_path == map_path) return declaration.controller_key;
    return {};
}

inline SWorldExtensionConfig ExtensionsForMap(std::string_view map_path)
{
    for (const SMapDeclaration& declaration : kMaps)
        if (declaration.map_path == map_path) return declaration.extensions;
    return {};
}

// 将 /create 使用的友好名称解析为控制器键，未知名称返回空。
inline std::string_view ControllerKeyForName(std::string_view name)
{
    if (name == "open" || name == "open_world") return world_controller_keys::open;
    if (name == "weather" || name == "weather_diurnal" || name == "weather_diurnal_open")
        return "world_controller.weather_diurnal_open"; // 旧命令蓝图标识，无对应控制器类。
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
}

// 按快照键创建玩法控制器，并由其参数模式处理构造覆盖值。
std::unique_ptr<IGameController> CreateWorldController(std::string_view snapshot_key,
                                                       const CControllerParams& params = {});
std::vector<SControllerParamInfo> ControllerParamSchemaForKey(std::string_view snapshot_key);
