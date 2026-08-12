#pragma once

#include <memory>
#include <string_view>

class IGameController;

namespace world_controller_keys
{
inline constexpr std::string_view open = "world_controller.open";
inline constexpr std::string_view weather_diurnal_open = "world_controller.weather_diurnal_open";
inline constexpr std::string_view waiting = "world_controller.waiting";
}

std::unique_ptr<IGameController> CreateWorldController(std::string_view snapshot_key);
