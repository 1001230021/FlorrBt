#pragma once
#include "../../Shared/talent_type.h"
#include "controller_params.h"
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
    virtual bool CanRestoreSnapshotKey(std::string_view key) const { return SnapshotKey() == key; }
    virtual std::uint32_t SnapshotVersion() const { return 1; }
    virtual void CaptureSnapshot(CSnapshotWriter&) const {}
    virtual bool RestoreSnapshot(const CSnapshotReader&, std::uint32_t version, std::string& error)
    {
        if (version <= SnapshotVersion()) return true;
        error = "Unsupported world controller snapshot version";
        return false;
    }
    virtual bool ResolveReferences(CGameWorld&, const IGameWorldResolver&, std::string&) { return true; }
    // Worlds that force every player flower into permanent primordial
    // nullification (phasing through mobs and walls).
    virtual bool ForcesPrimordialNullification() const { return false; }
    // Worlds where the player flower's petal loadout is locked: equip and
    // unequip requests from players are rejected outright.
    virtual bool LocksPetalLoadout() const { return false; }
    // Construction-time parameters declared by this controller type; the
    // waiting room exposes them through /setting before the game world is
    // created. Values are validated (审查函数) and stored as overrides.
    virtual std::vector<SControllerParamInfo> ParamSchema() const { return {}; }
    virtual void ModifyTalentContext(CGameWorld& world, CPlayer* player, ETalentEvent event, STalentContext& ctx)
    {
        (void)world;
        (void)player;
        (void)event;
        (void)ctx;
    }
    // Lets world controllers claim player-issued chat commands (e.g. /start in
    // the waiting room). `args[0]` is the raw command token including any
    // leading '/'. Return true when the command was handled.
    virtual bool HandlePlayerCommand(CGameWorld& world, CPlayer& player, const std::vector<std::string>& args)
    {
        (void)world;
        (void)player;
        (void)args;
        return false;
    }
    // Post-collection snapshot cull: CSnapshotService::BuildSnapshot gathers
    // the candidates inside the player's view radius exactly as usual, then
    // this filter runs once on the gathered candidates before budget trimming.
    // Return false to keep `entity` out of the player's snapshot (fog of war,
    // limited scout vision, ...). Not applied to the owner flower, mandatory
    // squad members, or the titan.
    virtual bool SnapshotEntityVisibleToPlayer(CGameWorld& world, const CPlayer& player, const CEntity& entity) const
    {
        (void)world;
        (void)player;
        (void)entity;
        return true;
    }
};
