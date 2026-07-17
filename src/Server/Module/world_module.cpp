#include "world_module.h"
#include "../Game/gamecontrollers/opencontroller.h"
#include "../../Shared/game_config.h"
#include "../../Shared/tools.h"
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <random>

IWorldModule::IWorldModule()
{
    auto world = std::make_unique<CGameWorld>(game_config::lobby_map_path);
    auto controller = std::make_unique<COpenController>();
    world->SetController(std::move(controller));
    m_worlds.emplace_back(std::move(world));
}

bool IWorldModule::Init()
{
    return true;
}

void IWorldModule::Tick(float dt)
{
    for (const auto& world : m_worlds)
    {
        if (world) world->Tick(dt);
    }
}

void IWorldModule::ShutDown() {}

namespace
{
std::string NormalizeMapName(std::string name)
{
    std::replace(name.begin(), name.end(), '\\', '/');
    std::filesystem::path path(name);
    if (path.has_extension()) name = path.stem().generic_string();
    else if (path.has_parent_path()) name = path.filename().generic_string();

    std::transform(name.begin(), name.end(), name.begin(), [](unsigned char ch)
    {
        return static_cast<char>(std::tolower(ch));
    });
    return name;
}
}

std::vector<CGameWorld*> IWorldModule::FindWorldsByMapName(const std::string& map_name) const
{
    std::vector<CGameWorld*> result;
    const std::string target = NormalizeMapName(map_name);
    if (target.empty()) return result;

    for (const auto& world : m_worlds)
    {
        if (!world) continue;
        if (NormalizeMapName(world->GetMapName()) == target || NormalizeMapName(world->GetMapPath()) == target)
            result.push_back(world.get());
    }
    return result;
}

CGameWorld* IWorldModule::FindRandomWorldByMapName(const std::string& map_name) const
{
    std::vector<CGameWorld*> worlds = FindWorldsByMapName(map_name);
    if (worlds.empty()) return nullptr;

    std::uniform_int_distribution<size_t> dist(0, worlds.size() - 1);
    return worlds[dist(GetRng())];
}
