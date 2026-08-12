#include "world_controller_factory.h"
#include "opencontroller.h"
#include "weather_diurnal_open_controller.h"
#include "waitingcontroller.h"

std::unique_ptr<IGameController> CreateWorldController(std::string_view snapshot_key)
{
    if (snapshot_key == world_controller_keys::open) return std::make_unique<COpenController>();
    if (snapshot_key == world_controller_keys::weather_diurnal_open)
        return std::make_unique<CWeatherDiurnalOpenController>();
    if (snapshot_key == world_controller_keys::waiting) return std::make_unique<CWaitingController>();
    return {};
}
