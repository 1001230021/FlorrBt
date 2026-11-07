#pragma once
#include "game_services.h"
#include "systems/natural_spawn_population.h"
#include <memory>
#include <vector>

class CEntity;
class CGameWorld;
class CPlayer;
class IPlayerDirectory;
class CExtensionRuntime;

class CGameContext
{
  public:
    CGameContext(IPlayerDirectory& players, IPlayerUpdates& updates, IWorldEffects& effects,
                 ISceneTransitions& scenes, IGameEventSink& events, IGameWorldResolver& worlds,
                 IUniquePetalAccess& unique_petals, CExtensionRuntime& extensions);

    CGameWorld* MainWorld() const { return m_worlds.GetMainWorld(); }

    IPlayerDirectory& PlayerDirectory() const { return m_players; }
    IPlayerUpdates& PlayerUpdates() const { return m_updates; }
    IWorldEffects& WorldEffects() const { return m_effects; }
    ISceneTransitions& Scenes() const { return m_scenes; }
    IUniquePetalAccess& UniquePetals() const { return m_unique_petals; }
    CNaturalSpawnPopulation& NaturalSpawnPopulation() { return m_spawn_population; }

    IGameEventSink& Events() { return m_events; }
    const IGameEventSink& Events() const { return m_events; }

    IGameWorldResolver& Worlds() { return m_worlds; }
    const IGameWorldResolver& Worlds() const { return m_worlds; }

    CExtensionRuntime& Extensions() const { return m_extensions; }

    const std::vector<std::unique_ptr<CPlayer>>& Players() const;
    CPlayer* FindPlayerFromEntity(CEntity* entity) const;

  private:
    IPlayerDirectory& m_players;
    IPlayerUpdates& m_updates;
    IWorldEffects& m_effects;
    ISceneTransitions& m_scenes;
    IGameEventSink& m_events;
    IGameWorldResolver& m_worlds;
    IUniquePetalAccess& m_unique_petals;
    CExtensionRuntime& m_extensions;
    CNaturalSpawnPopulation m_spawn_population;
};
