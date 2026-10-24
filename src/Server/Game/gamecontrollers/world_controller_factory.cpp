#include "world_controller_factory.h"
#include "ant_kingdom_controller.h"
#include "deathmatch_controller.h"
#include "mutable_terrain_controller.h"
#include "opencontroller.h"
#include "pvp_controller.h"
#include "sandbox_controller.h"
#include "waitingcontroller.h"

std::unique_ptr<IGameController> CreateWorldController(std::string_view snapshot_key,
                                                       const CControllerParams& params)
{
    if (snapshot_key == world_controller_keys::open) return std::make_unique<COpenController>();
    if (snapshot_key == "world_controller.weather_diurnal_open") return std::make_unique<COpenController>();
    if (snapshot_key == world_controller_keys::waiting) return std::make_unique<CWaitingController>();
    if (snapshot_key == world_controller_keys::mutable_terrain)
        return std::make_unique<CMutableTerrainController>();
    if (snapshot_key == world_controller_keys::pvp) return std::make_unique<CAntHelPvpController>(params);
    if (snapshot_key == world_controller_keys::ant_kingdom)
        return std::make_unique<CAntKingdomController>(params);
    if (snapshot_key == world_controller_keys::deathmatch)
        return std::make_unique<CDeathmatchController>(params);
    if (snapshot_key == world_controller_keys::sandbox) return std::make_unique<CSandboxController>(params);
    return {};
}

std::vector<SControllerParamInfo> ControllerParamSchemaForKey(std::string_view snapshot_key)
{
    std::unique_ptr<IGameController> controller = CreateWorldController(snapshot_key);
    return controller ? controller->ParamSchema() : std::vector<SControllerParamInfo>{};
}
