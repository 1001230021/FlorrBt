#include "opencontroller.h"
#include "open_reward_service.h"
#include "../../../Engine/logger.h"
#include "../../HotReload/snapshot_archive.h"
#include "../../../Shared/game_config.h"
#include "../entities/flower.h"
#include "../entities/mob.h"
#include "../gamecontext.h"
#include "../gameworld.h"
#include "../player.h"
#include "../extensions/weather_view.h"
#include "../zone_mob_tools.h"
#include <cmath>
#include <algorithm>
#include <cmath>
#include <cctype>
#include <string>
#include <unordered_set>

namespace
{
std::string LowerOpenControllerText(std::string_view text)
{
    std::string result;
    result.reserve(text.size());
    for (char ch : text)
        result.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
    return result;
}

std::optional<ERarity> ParseOpenControllerRarity(std::string_view text)
{
    const std::string target = LowerOpenControllerText(text);
    for (int value = static_cast<int>(ERarity::Common); value <= static_cast<int>(ERarity::Exotic); ++value)
    {
        const ERarity rarity = static_cast<ERarity>(value);
        if (LowerOpenControllerText(GetRarityName(rarity)) == target) return rarity;
    }
    if (target == "ex") return ERarity::Exotic;
    return std::nullopt;
}

bool HasPreciseSpawnMob(CGameWorld& world, const FlorrBtMap::PreciseSpawn& spawn, EMobType type, ERarity rarity)
{
    constexpr float position_epsilon = 0.5f;
    bool found = false;
    world.ForEachEntity([&](CEntity* entity) {
        if (found || !entity || entity->IsMarkedForDes()) return;
        const auto* mob = dynamic_cast<const CMobBase*>(entity);
        if (!mob || mob->IsDead() || mob->GetMobType() != type || mob->GetRarity() != rarity) return;
        if (DistanceSq(mob->Pos(), { spawn.x, spawn.y }) <= position_epsilon * position_epsilon) found = true;
    });
    return found;
}
} // namespace

void COpenController::OnActivate(CGameWorld& world)
{
    InitializePreciseSpawns(world);
}

void COpenController::InitializePreciseSpawns(CGameWorld& world)
{
    if (m_precise_spawns_initialized) return;
    const FlorrBtMap* map = world.GetMap();
    if (!map || map->precise_spawns.empty())
    {
        m_precise_spawns_initialized = true;
        return;
    }

    // Worlds are constructed before RegisterMobs runs. Defer until the first tick in that case.
    if (!FindMobPrototype(EMobType::NormalFlower)) return;

    for (const FlorrBtMap::PreciseSpawn& spawn : map->precise_spawns)
    {
        EMobType mob_type = EMobType::None;
        if (!TryParseZoneMobType(spawn.type, mob_type))
        {
            LOG_WARN("opencontroller", "Invalid precise_spawn mob type '" + spawn.type + "' in " +
                                             world.GetMapPath());
            continue;
        }

        const std::optional<ERarity> rarity = ParseOpenControllerRarity(spawn.rarity);
        if (!rarity)
        {
            LOG_WARN("opencontroller", "Invalid precise_spawn rarity '" + spawn.rarity + "' in " +
                                             world.GetMapPath());
            continue;
        }
        if (HasPreciseSpawnMob(world, spawn, mob_type, *rarity)) continue;

        auto mob = CreateMob(mob_type, &world, { spawn.x, spawn.y }, *rarity, false);
        if (!mob)
        {
            LOG_WARN("opencontroller", "Failed to create precise_spawn " + std::string(GetRarityName(*rarity)) +
                                             " " + std::string(GetMobTypeName(mob_type)));
            continue;
        }
        CMobBase* raw_mob = dynamic_cast<CMobBase*>(world.InsertEntity(std::move(mob)));
        if (raw_mob) COpenRewardService::OnMobSpawned(world, *raw_mob);
    }
    m_precise_spawns_initialized = true;
}

void COpenController::UpdateSpawnWave(float dt)
{
    const float period = game_config::open_spawn_wave_period;
    const float minimum = std::clamp(game_config::open_spawn_wave_min_multiplier, 0.f, 1.f);
    const float maximum = std::clamp(game_config::open_spawn_wave_max_multiplier, minimum, 1.f);

    if (std::isfinite(dt) && dt > 0.f) m_spawn_wave_time += dt;
    if (!std::isfinite(period) || period <= game_config::entity_collision_epsilon)
    {
        m_spawn_wave_time = 0.f;
        m_spawn_density_multiplier = (minimum + maximum) * 0.5f;
        return;
    }

    if (!std::isfinite(m_spawn_wave_time)) m_spawn_wave_time = 0.f;
    m_spawn_wave_time = std::fmod(m_spawn_wave_time, period);
    if (m_spawn_wave_time < 0.f) m_spawn_wave_time += period;

    const float phase = 2.f * game_config::pi * m_spawn_wave_time / period;
    const float midpoint = (minimum + maximum) * 0.5f;
    const float amplitude = (maximum - minimum) * 0.5f;
    m_spawn_density_multiplier = std::clamp(midpoint + amplitude * std::sin(phase), minimum, maximum);
}

void COpenController::CaptureSnapshot(CSnapshotWriter& writer) const
{
    m_spawn_director.CaptureSnapshot(writer);
    writer.Field("spawn_wave_time", m_spawn_wave_time);
    writer.Field("spawn_density_multiplier", m_spawn_density_multiplier);

}

bool COpenController::RestoreSnapshot(const CSnapshotReader& reader, std::uint32_t version, std::string& error)
{
    if (version > SnapshotVersion())
    {
        error = "Unsupported open controller snapshot version " + std::to_string(version);
        return false;
    }

    if (!m_spawn_director.RestoreSnapshot(reader, error)) return false;

    m_spawn_wave_time = reader.Float("spawn_wave_time", 0.f);
    if (!std::isfinite(m_spawn_wave_time) || m_spawn_wave_time < 0.f) m_spawn_wave_time = 0.f;
    m_spawn_density_multiplier = reader.Float("spawn_density_multiplier", 0.6f);
    if (!std::isfinite(m_spawn_density_multiplier)) m_spawn_density_multiplier = 0.6f;
    m_spawn_density_multiplier = std::max(0.f, m_spawn_density_multiplier);
    UpdateSpawnWave(0.f);

    return true;
}

void COpenController::OnTick(CGameWorld& world, float dt)
{
    InitializePreciseSpawns(world);

    UpdateSpawnWave(dt);
    const auto* weather = world.ExtensionConfig().weather_spawns ? world.Extensions().Find<IWeatherDiurnalView>() : nullptr;
    const auto environment = weather ? std::optional{weather->GetWeatherDiurnalSnapshot()} : std::nullopt;
    m_spawn_director.Tick(world, dt, m_spawn_density_multiplier, &COpenRewardService::OnMobSpawned,
                          environment ? &*environment : nullptr);
}

void COpenController::SpawnMobs(CGameWorld& world)
{
    UpdateSpawnWave(0.f);
    const auto* weather = world.ExtensionConfig().weather_spawns ? world.Extensions().Find<IWeatherDiurnalView>() : nullptr;
    const auto environment = weather ? std::optional{weather->GetWeatherDiurnalSnapshot()} : std::nullopt;
    m_spawn_director.SpawnMobs(world, m_spawn_density_multiplier, &COpenRewardService::OnMobSpawned,
                               environment ? &*environment : nullptr);
}

EMobType COpenController::PickHabitatMobType(CGameWorld& world, florrbt::Vector2f pos)
{
    const auto* weather = world.ExtensionConfig().weather_spawns ? world.Extensions().Find<IWeatherDiurnalView>() : nullptr;
    const auto environment = weather ? std::optional{weather->GetWeatherDiurnalSnapshot()} : std::nullopt;
    return m_spawn_director.PickHabitatMobType(world, pos, environment ? &*environment : nullptr);
}

std::optional<florrbt::Vector2f> COpenController::SelectPlayerSpawn(CGameWorld& world, CPlayer& player,
                                                               EPlayerSpawnReason reason)
{
    return m_spawn_director.SelectPlayerSpawn(world, player.GetUseNewPlayerSpawn(), reason);
}

void COpenController::ModifyTalentContext(CGameWorld&, CPlayer*, ETalentEvent, STalentContext&) {}

void COpenController::OnEntityRemoved(CGameWorld&, CEntity& entity, EEntityRemovalReason)
{
    m_spawn_director.OnEntityRemoved(entity);
}

void COpenController::OnMobDefeated(CGameWorld& world, CMobBase& mob)
{
    COpenRewardService::OnMobDefeated(world, mob);
}
