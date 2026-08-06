#pragma once
#include "../../Shared/talent_type.h"
#include <SFML/System/Vector2.hpp>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

class CGameWorld;
class CEntity;
class CMobBase;
class CPlayer;
class CPlayerFlower;
class IGameWorldResolver;
enum class EEntityRemovalReason : std::uint8_t;
struct STalentContext;
class CSnapshotReader;
class CSnapshotWriter;

enum class EPlayerSpawnReason : std::uint8_t
{
    Login,
    Respawn,
};

class IGameController
{
  public:
    virtual ~IGameController() = default;
    virtual void OnActivate(CGameWorld&) {}
    virtual void OnDeactivate(CGameWorld&) {}
    virtual void OnTick(CGameWorld& world, float dt) = 0;
    virtual std::optional<sf::Vector2f> SelectPlayerSpawn(CGameWorld&, CPlayer&, EPlayerSpawnReason)
    {
        return std::nullopt;
    }
    virtual void OnPlayerEntityReady(CGameWorld&, CPlayer&, CPlayerFlower&, EPlayerSpawnReason) {}
    virtual void OnPlayerEnteredWorld(CGameWorld&, CPlayer&) {}
    virtual void OnPlayerLeftWorld(CGameWorld&, CPlayer&) {}
    virtual void OnEntityRemoved(CGameWorld&, CEntity&, EEntityRemovalReason) {}
    virtual void OnMobDefeated(CGameWorld&, CMobBase&) {}
    virtual std::string_view SnapshotKey() const = 0;
    virtual std::uint32_t SnapshotVersion() const { return 1; }
    virtual void CaptureSnapshot(CSnapshotWriter&) const {}
    virtual bool RestoreSnapshot(const CSnapshotReader&, std::uint32_t version, std::string& error)
    {
        if (version <= SnapshotVersion()) return true;
        error = "Unsupported world controller snapshot version";
        return false;
    }
    virtual bool ResolveReferences(CGameWorld&, const IGameWorldResolver&, std::string&) { return true; }
    virtual void ModifyTalentContext(CGameWorld& world, CPlayer* player, ETalentEvent event, STalentContext& ctx)
    {
        (void)world;
        (void)player;
        (void)event;
        (void)ctx;
    }
};
