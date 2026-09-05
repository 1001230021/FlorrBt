#include "deathmatch_controller.h"
#include "../../../Engine/logger.h"
#include "../controller_params.h"
#include <algorithm>
#include <filesystem>

namespace
{
bool MapPathExists(std::string_view value)
{
    if (value.empty()) return false;
    std::string path(value);
    std::filesystem::path resolved_path = path;
    if (!std::filesystem::exists(resolved_path))
    {
        const std::filesystem::path candidates[] = {
            std::filesystem::path("..") / path,
            std::filesystem::path("..") / ".." / path,
        };
        for (const auto& candidate : candidates)
            if (std::filesystem::exists(candidate)) resolved_path = candidate;
    }
    return std::filesystem::exists(resolved_path);
}
} // namespace

bool CDeathmatchController::ValidateMapPath(const std::string& value)
{
    return MapPathExists(value);
}

CDeathmatchController::CDeathmatchController(CControllerParams params)
{
    if (params.Contains("map"))
    {
        m_map_path = params.Get("map", "");
        LOG_INFO("world", "Deathmatch room map param: " + m_map_path);
    }
}

std::vector<SControllerParamInfo> CDeathmatchController::ParamSchema() const
{
    return {
        { "map", EControllerParamType::String, "deathmatch_map",
          "game map path for this deathmatch room (change it in the waiting room)", &ValidateMapPath },
    };
}
