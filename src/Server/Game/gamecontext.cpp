#include "gamecontext.h"
#include "../../Shared/tools.h"
#include "player.h"

CGameContext::CGameContext(IPlayerDirectory& players, IPlayerUpdates& updates, IWorldEffects& effects,
                           ISceneTransitions& scenes, IGameEventSink& events, IGameWorldResolver& worlds,
                           IUniquePetalAccess& unique_petals, CExtensionRuntime& extensions)
    : m_players(players), m_updates(updates), m_effects(effects), m_scenes(scenes),
      m_events(events), m_worlds(worlds), m_unique_petals(unique_petals), m_extensions(extensions), m_spawn_population(worlds)
{
}

const std::vector<std::unique_ptr<CPlayer>>& CGameContext::Players() const { return m_players.GetPlayers(); }

CPlayer* CGameContext::FindPlayerFromEntity(CEntity* entity) const { return ::FindPlayerFromEntity(entity, Players()); }
