#include "world_module.h"
#include "../../Shared/game_config.h"
#include "../../Shared/tools.h"
#include "../Game/gamecontrollers/world_controller_factory.h"
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <random>

namespace
{

constexpr const char* ant_hel_map_path = "data/maps/ant_hel.tmj";

std::unique_ptr<CGameWorld> CreateOpenWorld(std::uint32_t world_id, const std::string& map_path)
{
    auto world = std::make_unique<CGameWorld>(map_path, world_id);
    world->SetController(CreateWorldController(world_controller_keys::open));
    return world;
}

std::string NormalizeMapName(std::string name)
{
    std::replace(name.begin(), name.end(), '\\', '/');
    std::filesystem::path path(name);
    if (path.has_extension()) name = path.stem().generic_string();
    else if (path.has_parent_path()) name = path.filename().generic_string();

    std::transform(name.begin(), name.end(), name.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return name;
}

} // namespace

IWorldModule::IWorldModule()
{
    m_worlds.emplace_back(CreateOpenWorld(0, game_config::lobby_map_path));
    m_worlds.emplace_back(CreateOpenWorld(1, ant_hel_map_path));
}

bool IWorldModule::Init() { return true; }

void IWorldModule::Tick(float dt)
{
    for (const auto& world : m_worlds)
    {
        if (world) world->Tick(dt);
    }
}

void IWorldModule::ShutDown() {}

CGameWorld* IWorldModule::FindWorldById(std::uint32_t world_id) const
{
    for (const auto& world : m_worlds)
    {
        if (world && world->GetId() == world_id) return world.get();
    }
    return nullptr;
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
