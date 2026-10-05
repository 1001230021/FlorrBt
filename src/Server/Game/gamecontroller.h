#pragma once
#include "../../Shared/talent_type.h"
#include "controller_params.h"
#include "../../Shared/math_types.h"
#include "../../Shared/mob_type.h"
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
enum class EPetalType : int;
struct STalentContext;
class CSnapshotReader;
class CSnapshotWriter;
class CEouSlotProxy;

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
    virtual std::optional<florrbt::Vector2f> SelectPlayerSpawn(CGameWorld&, CPlayer&, EPlayerSpawnReason)
    {
        return std::nullopt;
    }
    virtual void OnPlayerEntityReady(CGameWorld&, CPlayer&, CPlayerFlower&, EPlayerSpawnReason) {}
    virtual void OnPlayerEnteredWorld(CGameWorld&, CPlayer&) {}
    virtual void OnPlayerLeftWorld(CGameWorld&, CPlayer&) {}
    virtual void OnEntityRemoved(CGameWorld&, CEntity&, EEntityRemovalReason) {}
    virtual void OnMobDefeated(CGameWorld&, CMobBase&) {}
    // Returns a mob from the spawn habitat surrounding `pos`. Controllers
    // without map-backed habitats return None; skills can then choose their
    // own fallback without depending on a concrete world-controller type.
    virtual EMobType PickHabitatMobType(CGameWorld&, florrbt::Vector2f) { return EMobType::None; }
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
    // Fine-grained loadout rules applied after LocksPetalLoadout(): whether a
    // player may equip the given petal type into a slot, and whether the petal
    // currently in a slot may be unequipped back into the backpack.
    virtual bool CanEquipPetalType(EPetalType type) const { return true; }
    virtual bool CanUnequipPetalType(EPetalType type) const { return true; }
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
    // Slot proxy (eou-style worlds): logical skill slots disguised as vanilla
    // petal slots on the wire. The snapshot service fills locked/empty player
    // slots from this when non-null.
    virtual const CEouSlotProxy* SlotProxy(int team_id) const { return nullptr; }
    // A pheromone petal finished its throw and reports its resting position;
    // eou controllers forward it to the bound pheromone group as its new
    // target (attack throw) or toggle the cells beneath it (defend throw).
    // Default ignores the report.
    virtual void OnPheromonePetalStopped(CGameWorld&, int team_id, int group_id, florrbt::Vector2f pos, bool defend)
    {
        (void)team_id;
        (void)group_id;
        (void)pos;
        (void)defend;
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
