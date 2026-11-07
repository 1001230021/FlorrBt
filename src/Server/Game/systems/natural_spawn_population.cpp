#include "natural_spawn_population.h"
#include "../entities/mob.h"
#include "../game_services.h"
#include "../gameworld.h"

void CNaturalSpawnPopulation::CollectWorld(const CGameWorld& world, std::unordered_set<int>& result)
{
    world.ForEachEntity([&](const CEntity* entity) {
        const auto* mob = dynamic_cast<const CMobBase*>(entity);
        if (!mob || mob->IsMarkedForDes() || mob->IsDead() || !mob->IsNaturallySpawned()) return;
        if (mob->GetRarity() == ERarity::Eternal || mob->GetRarity() == ERarity::Unique)
            result.insert(static_cast<int>(mob->GetMobType()));
    });
}

void CNaturalSpawnPopulation::Collect(std::unordered_set<int>& result)
{
    const auto tick = m_worlds.GetTick();
    if (!m_valid || m_tick != tick)
    {
        std::unordered_set<int> types;
        for (const auto* world : m_worlds.GetWorlds()) if (world) CollectWorld(*world, types);
        m_types = std::move(types); m_tick = tick; m_valid = true;
    }
    result = m_types;
}

void CNaturalSpawnPopulation::Remember(const CMobBase& mob)
{
    // 本帧生成后立即占位，后续世界不能重复刷出同种稀有生物。
    if (m_valid && m_tick == m_worlds.GetTick() && mob.IsNaturallySpawned() &&
        (mob.GetRarity() == ERarity::Eternal || mob.GetRarity() == ERarity::Unique))
        m_types.insert(static_cast<int>(mob.GetMobType()));
}
