#include "deathmatch_controller.h"
#include "../../../Engine/logger.h"
#include "../controller_params.h"
#include "../player.h"
#include "../entities/flower.h"
#include <algorithm>
#include <filesystem>

namespace
{
bool DoesMapPathExist(std::string_view value)
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

void CDeathmatchController::OnPlayerArrivalCommitted(CGameWorld&, CPlayer& player)
{
    if (auto* entity = player.GetEntity()) entity->Team() = 0;
}

void CDeathmatchController::OnPlayerEntityReady(CGameWorld&, CPlayer&, CPlayerFlower& flower, EPlayerSpawnReason)
{
    flower.Team() = 0;
}

void CDeathmatchController::OnPlayerDepartureCommitted(CGameWorld&, CPlayer&, CEntity* departing)
{
    if (departing) departing->Team() = game_config::mob_player_flower_team;
}

bool CDeathmatchController::ValidateMapPath(const std::string& value)
{
    return DoesMapPathExist(value);
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
