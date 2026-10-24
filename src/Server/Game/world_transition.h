#pragma once
#include "entity.h"
#include "extension.h"
#include <string>

class CPlayer;
enum class EPlayerTransitionKind { Transfer, Possession };
enum class EPlayerTransitionSide { Departure, Arrival };

// Preflight receives values only, never mutable world/player/entity access.
struct SPlayerTransitionView
{
    EPlayerTransitionKind kind = EPlayerTransitionKind::Transfer;
    std::uint32_t player_id = 0;
    std::uint64_t session_generation = 0;
    SWorldHandle source, destination;
    SEntityHandle previous_entity, destination_entity;
    florrbt::Vector2f destination_position{};
    std::size_t entity_count = 0;
};

// Post-commit gameplay (including death) cannot relabel a committed move as
// rejected. The result never exposes a potentially dangling entity pointer.
struct SPlayerTransitionResult
{
    bool committed = false;
    SEntityHandle entity;
    std::string error;
    bool deferred = false;
    explicit operator bool() const { return committed; }
};

struct SDeferredWorldTransfer
{
    SPlayerSessionRef player;
    SWorldHandle source, destination;
    SEntityHandle entity;
    florrbt::Vector2f position;
};

class CWorldTransfer
{
  public:
    static SPlayerTransitionResult Move(CPlayer&, CGameWorld& source, CGameWorld& destination,
                                        florrbt::Vector2f destination_position);
    static SPlayerTransitionResult Possess(CPlayer&, CGameWorld& destination, const SEntityHandle& entity);
  private:
    struct CWorldCheckpoint;
};
