#include "world_transition.h"
#include "gameworld.h"
#include "gamecontext.h"
#include "player.h"
#include "chain.h"
#include "controllers/player_controller.h"
#include "controllers/pickup_controller.h"
#include "entities/mob.h"
#include "entities/flower.h"
#include "entities/petals/petal.h"
#include "entities/projectile.h"
#include "../Module/network_module.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <unordered_set>

namespace
{
struct CTransitionGuard
{
    bool& player;
    bool& source;
    bool& destination;
    CTransitionGuard(bool& p, bool& s, bool& d) : player(p), source(s), destination(d)
    { player = source = destination = true; }
    ~CTransitionGuard() { player = source = destination = false; }
};

bool ValidateTransition(const SPlayerTransitionView& view, const CGameWorld* source,
                        const CGameWorld& destination, std::string& error)
{
    try
    {
        if (source && source != &destination && source->GetController() &&
            !source->GetController()->ValidatePlayerTransition(view, EPlayerTransitionSide::Departure, error))
            return false;
        return !destination.GetController() ||
            destination.GetController()->ValidatePlayerTransition(view, EPlayerTransitionSide::Arrival, error);
    }
    catch (const std::exception& exception) { error = exception.what(); return false; }
    catch (...) { error = "Transition preflight threw an unknown exception"; return false; }
}

SPlayerTransitionResult Reject(std::string error)
{
    if (error.empty()) error = "Transition rejected by world policy";
    return {false, {}, std::move(error)};
}

bool Finite(florrbt::Vector2f value) { return std::isfinite(value.x) && std::isfinite(value.y); }
}

// Index/allocator copies are prepared before the first mutation. Restoring
// them is by move assignment, not by allocation or replaying entity hooks.
// Original entity objects remain owned throughout a failed staged move.
struct CWorldTransfer::CWorldCheckpoint
{
    CGameWorld& world;
    CGameWorld::entity_spatial_grid spatial;
    std::vector<CEntity*> live, cleanup, always, conditional, large;
    std::vector<CChainEntity*> chains;
    decltype(CGameWorld::m_spawn_habitat_cache) habitat;
    decltype(CGameWorld::m_free_ids) free_ids;
    decltype(CGameWorld::m_id_in_use) id_in_use;
    int next_id;
    std::uint64_t next_generation;
    std::size_t storage_size;

    explicit CWorldCheckpoint(CGameWorld& value)
        : world(value), spatial(value.m_spatial_grid), live(value.m_live_entities), cleanup(value.m_cleanup_entities),
          always(value.m_always_tick_entities), conditional(value.m_conditional_tick_entities), large(value.m_large_entities),
          chains(value.m_chains), habitat(value.m_spawn_habitat_cache), free_ids(value.m_free_ids),
          id_in_use(value.m_id_in_use), next_id(value.m_next_id), next_generation(value.m_next_generation),
          storage_size(value.m_p_entities.size()) {}

    void Restore() noexcept
    {
        world.m_spatial_grid = std::move(spatial);
        world.m_live_entities = std::move(live); world.m_cleanup_entities = std::move(cleanup);
        world.m_always_tick_entities = std::move(always); world.m_conditional_tick_entities = std::move(conditional);
        world.m_large_entities = std::move(large); world.m_chains = std::move(chains);
        world.m_spawn_habitat_cache = std::move(habitat);
        world.m_free_ids = std::move(free_ids); world.m_id_in_use = std::move(id_in_use);
        world.m_next_id = next_id; world.m_next_generation = next_generation;
        world.m_p_entities.resize(storage_size);
        for (CEntity* entity : world.m_live_entities)
        {
            entity->LiveIndex() = entity->CleanupIndex() = entity->AlwaysTickIndex() =
                entity->ConditionalTickIndex() = entity->LargeSpatialIndex() = std::numeric_limits<size_t>::max();
        }
        for (size_t i = 0; i < world.m_live_entities.size(); ++i) world.m_live_entities[i]->LiveIndex() = i;
        for (size_t i = 0; i < world.m_cleanup_entities.size(); ++i) world.m_cleanup_entities[i]->CleanupIndex() = i;
        for (size_t i = 0; i < world.m_always_tick_entities.size(); ++i) world.m_always_tick_entities[i]->AlwaysTickIndex() = i;
        for (size_t i = 0; i < world.m_conditional_tick_entities.size(); ++i) world.m_conditional_tick_entities[i]->ConditionalTickIndex() = i;
        for (size_t i = 0; i < world.m_large_entities.size(); ++i) world.m_large_entities[i]->LargeSpatialIndex() = i;
    }
};

SPlayerTransitionResult CWorldTransfer::Move(CPlayer& player, CGameWorld& source, CGameWorld& destination,
                                            florrbt::Vector2f position)
{
    if (player.IsTransitionActive() || source.IsTransitionActive() || destination.IsTransitionActive())
        return Reject("Nested world transition is not allowed");
    if (source.m_spatial_grid.HasActiveQueries() || destination.m_spatial_grid.HasActiveQueries())
        return Reject("Cannot transfer while a spatial query is active");
    CEntity* controlled = player.GetEntity();
    if (!controlled || controlled->GameWorld() != &source || controlled->IsMarkedForDes() || !Finite(position))
        return Reject("Invalid controlled entity or transfer position");
    if (auto* context = source.GameContext(); context && context->Worlds().DeferWorldTransfer({
        player.SessionRef(), {source.GetId(), source.GetInstanceGeneration()},
        {destination.GetId(), destination.GetInstanceGeneration()}, controlled->GetHandle(), position}))
        return {false, {}, {}, true};

    CTransitionGuard guard(player.m_transition_active, source.m_transition_active, destination.m_transition_active);
    struct SRecord
    {
        CEntity* entity;
        SEntityHandle before, after;
        florrbt::Vector2f position, previous;
        std::uint64_t active_marker, collision_marker;
        bool moved = false;
    };
    std::vector<SRecord> records;
    std::vector<std::pair<SEntityHandle, SEntityHandle>> mapping;
    std::vector<SEntityHandle> chain_refs;
    std::string error;
    const int fault_after = std::exchange(source.m_transfer_fault_after_insert, -1);
    try
    {
        // Index structural ownership once, rather than scanning the whole
        // world for every body segment, petal and attached ammunition item.
        std::unordered_map<SEntityHandle, std::vector<SEntityHandle>, SEntityHandleHash> attached;
        source.ForEachEntity([&](CEntity* candidate) {
            auto* projectile = dynamic_cast<CProjectile*>(candidate);
            if (!projectile || projectile->IsMarkedForDes()) return;
            const auto* missile = dynamic_cast<const CMissile*>(projectile);
            if (!dynamic_cast<const CPetal*>(projectile) && !(missile && missile->IsAttachedToOwner())) return;
            if (auto* owner = source.ResolveEntity(projectile->m_owner_ref))
                attached[owner->GetHandle()].push_back(projectile->GetHandle());
        });
        std::vector<SEntityHandle> pending{controlled->GetHandle()};
        std::unordered_set<SEntityHandle, SEntityHandleHash> visited;
        for (size_t index = 0; index < pending.size(); ++index)
        {
            CEntity* entity = source.ResolveEntity(pending[index]);
            if (!entity || entity->IsMarkedForDes()) return Reject("A structural transfer dependency is missing");
            if (!visited.insert(entity->GetHandle()).second) continue;
            if (!Finite(entity->Pos()) || !Finite(entity->PrevPos())) return Reject("Non-finite compound entity position");
            records.push_back({entity, entity->GetHandle(), {}, entity->Pos(), entity->PrevPos(),
                               entity->ActiveTickMarker(), entity->CollisionSyncMarker()});
            entity->CollectTransferDependencies(pending);
            if (auto* root = dynamic_cast<IMultiSegmentRootMob*>(entity))
            {
                for (const auto& reference : root->GetSegmentReferences())
                {
                    const auto* body = dynamic_cast<IMultiSegmentBodyMob*>(source.ResolveEntity(reference.entity_ref));
                    if (!body || !body->RootHandle().RefersTo(entity->GetHandle()) || body->SegmentIndex() != reference.index)
                        return Reject("Compound root/body references disagree");
                }
            }
            if (auto* body = dynamic_cast<IMultiSegmentBodyMob*>(entity); body && body->RootHandle().IsValid())
            {
                const auto* root = dynamic_cast<IMultiSegmentRootMob*>(source.ResolveEntity(body->RootHandle()));
                if (!root || std::none_of(root->GetSegmentReferences().begin(), root->GetSegmentReferences().end(),
                    [&](const auto& reference) { return reference.entity_ref.RefersTo(entity->GetHandle()) && reference.index == body->SegmentIndex(); }))
                    return Reject("Compound body is not owned by its root");
                if (body->NextSegmentHandle().IsValid())
                {
                    CEntity* next = source.ResolveEntity(body->NextSegmentHandle());
                    const auto* next_body = dynamic_cast<IMultiSegmentBodyMob*>(next);
                    if (!next || (!body->RootHandle().RefersTo(next->GetHandle()) &&
                        (!next_body || !next_body->RootHandle().RefersTo(body->RootHandle()))))
                        return Reject("Compound segment link leaves its assembly");
                }
            }
            // Attached ammo and petal slots are structural ownership, unlike
            // fired projectiles, grabbed targets or independently summoned AI.
            if (const auto found = attached.find(entity->GetHandle()); found != attached.end())
                pending.insert(pending.end(), found->second.begin(), found->second.end());
        }
        if (CGameContext* context = source.GameContext())
            for (const auto& other : context->Players())
                if (other && other.get() != &player && other->GetEntity() && visited.contains(other->GetEntity()->GetHandle()))
                    return Reject("Another player controls part of the compound entity");
        const auto delta = position - controlled->Pos();
        for (const auto& record : records)
            if (!Finite(record.position + delta)) return Reject("Compound transfer position overflow");
        SPlayerTransitionView view{EPlayerTransitionKind::Transfer, player.GetId(), player.SessionGeneration(),
            {source.GetId(), source.GetInstanceGeneration()}, {destination.GetId(), destination.GetInstanceGeneration()},
            controlled->GetHandle(), {}, position, records.size()};
        if (&source != &destination && !ValidateTransition(view, &source, destination, error)) return Reject(error);
        if (player.GetEntity() != controlled) return Reject("Control changed during preflight");
        for (const auto& record : records)
            if (source.ResolveEntity(record.before) != record.entity || record.entity->IsMarkedForDes())
                return Reject("Transfer dependency changed during preflight");

        if (&source == &destination)
        {
            CWorldCheckpoint saved(source);
            try
            {
                int inserted = 0;
                for (auto& record : records)
                {
                    record.entity->Pos() = record.position + delta;
                    record.entity->PrevPos() = record.entity->Pos();
                    source.m_spatial_grid.UpdateTracked(record.entity);
                    if (++inserted == fault_after) throw std::runtime_error("Injected transfer staging failure");
                }
            }
            catch (...)
            {
                for (const auto& record : records) { record.entity->Pos() = record.position; record.entity->PrevPos() = record.previous; }
                saved.Restore(); return Reject("Teleport staging failed and was rolled back");
            }
            return {true, controlled->GetHandle(), {}};
        }

        if (records.size() >= UINT64_MAX - destination.m_next_generation ||
            destination.m_next_id > std::numeric_limits<int>::max() - static_cast<int>(records.size()))
            return Reject("Destination entity identity space exhausted");
        mapping.reserve(records.size());
        for (CChainEntity* chain : source.m_chains)
            if (chain && !chain->IsMarkedForDes())
                for (const auto& record : records)
                    if (chain->HasEndpoint(*record.entity) || chain->HasLease(*record.entity))
                    { chain_refs.push_back(chain->GetHandle()); break; }

        CWorldCheckpoint before_source(source), before_destination(destination);
        try
        {
            source.m_free_ids.reserve(source.m_free_ids.size() + records.size());
            for (auto& record : records)
            {
                const int id = destination.GetNewID();
                if (id >= static_cast<int>(destination.m_p_entities.size())) destination.m_p_entities.resize(static_cast<size_t>(id) + 1);
                if (destination.m_p_entities[id]) throw std::runtime_error("Destination ID is occupied");
                record.after = {id, destination.m_next_generation++, destination.GetId(), destination.GetInstanceGeneration()};
                mapping.emplace_back(record.before, record.after);
            }
            int inserted = 0;
            for (auto& record : records)
            {
                source.UnregisterLiveEntity(record.entity);
                destination.m_p_entities[record.after.id] = std::move(source.m_p_entities[record.before.id]);
                record.moved = true;
                record.entity->SetIdentity(record.after.id, record.after.generation);
                record.entity->SetGameWorld(&destination);
                record.entity->ActiveTickMarker() = record.entity->CollisionSyncMarker() = 0;
                record.entity->Pos() = record.position + delta; record.entity->PrevPos() = record.entity->Pos();
                destination.RegisterLiveEntity(record.entity);
                if (++inserted == fault_after) throw std::runtime_error("Injected transfer staging failure");
            }
        }
        catch (...)
        {
            for (auto& record : records)
            {
                if (!record.moved) continue;
                source.m_p_entities[record.before.id] = std::move(destination.m_p_entities[record.after.id]);
                record.entity->SetIdentity(record.before.id, record.before.generation);
                record.entity->SetGameWorld(&source);
                record.entity->ActiveTickMarker() = record.active_marker;
                record.entity->CollisionSyncMarker() = record.collision_marker;
                record.entity->Pos() = record.position; record.entity->PrevPos() = record.previous;
            }
            before_source.Restore(); before_destination.Restore();
            try { throw; }
            catch (const std::exception& exception) { return Reject(std::string("Transfer rolled back: ") + exception.what()); }
            catch (...) { return Reject("Transfer rolled back after an unknown staging failure"); }
        }

        // No fallible acceptance checks or external callbacks beyond here.
        // Remapping only rewrites existing handles; no allocating graph rebuild.
        for (const auto& record : records) record.entity->RemapTransferredReferences(mapping);
        for (const auto& record : records) source.FreeID(record.before.id);
        player.CommitOwnedEntity(controlled);
        player.m_cp_stack.clear(); player.m_cp_check_phase = static_cast<uint8_t>(player.GetId() & 7);
        player.m_logged_missing_entity = false;
    }
    catch (const std::exception& exception)
    {
        // Preparation/copy failures occur before any world mutation. All
        // failures inside the staged move have their own rollback above.
        return Reject(exception.what());
    }

    // Committed cleanup uses old qualified handles, not owner resolution in
    // a world the owner has already left. External relations end here.
    for (const auto& record : records)
    {
        CEntity* live = destination.ResolveEntity(record.after);
        if (!live) continue;
        if (auto* mob = dynamic_cast<CMobBase*>(live))
            if (auto* pickup = mob->GetPickupController()) pickup->DropCarriedFromWorld(mob, source, record.before);
        const auto held = source.m_carried_entities.find(record.before);
        if (held != source.m_carried_entities.end())
        {
            auto* carrier = dynamic_cast<CMobBase*>(source.ResolveEntity(held->second.carrier));
            if (carrier && carrier->GetPickupController()) carrier->GetPickupController()->DropCarried(carrier);
            else source.m_carried_entities.erase(held);
        }
        source.DestroyProjectilesOwnedBy(record.before);
        source.DestroySummonedMobsOwnedBy(record.before);
        // Relic zones are world-local effects, not moving entities. Their
        // normal petal-clear callback now runs against the destination world.
        if (auto* relic = dynamic_cast<CRelicPetal*>(live))
        {
            if (CEntity* zone = source.ResolveEntity(relic->m_state_zone_ref))
                zone->MarkForDestroy(EEntityRemovalReason::OwnerRemoved);
            relic->m_state_zone_ref = {};
        }
        live->OnWorldTransferCommitted();
        if (auto* mob = dynamic_cast<CMobBase*>(destination.ResolveEntity(record.after)))
        {
            mob->m_vel = {};
            if (mob->GetController()) mob->GetController()->OnWorldChanged(mob);
        }
    }
    for (const auto& ref : chain_refs)
        if (auto* chain = dynamic_cast<CChainEntity*>(source.ResolveEntity(ref))) chain->Break();
    player.ResetControlledMob();
    if (source.GetController()) source.GetController()->OnPlayerDepartureCommitted(source, player, controlled);
    // Flower petals retain the existing world-change reload rule. Moving
    // their ownership first makes OnPetalCleared resolve the correct flower.
    for (const auto& record : records)
    {
        CEntity* live = destination.ResolveEntity(record.after);
        if (auto* mob = dynamic_cast<CMobBase*>(live)) mob->ClearTeamOverrideStates();
        if (auto* flower = dynamic_cast<CFlower*>(live)) flower->DestroyPetalEntities();
        if (auto* petal = dynamic_cast<CPetal*>(live)) { petal->NotifyCleared(); petal->MarkForDestroy(EEntityRemovalReason::OwnerRemoved); }
    }
    if (!player.SynchronizeGameplayScope(destination))
        throw std::runtime_error("Cannot initialize committed player progress scope");
    if (destination.GetController()) destination.GetController()->OnPlayerArrivalCommitted(destination, player);
    for (const auto& record : records)
        if (auto* mob = dynamic_cast<CMobBase*>(destination.ResolveEntity(record.after))) mob->SynchronizeEnvironment();
    if (CEntity* current = player.GetEntity())
        for (const auto& record : records)
            if (CEntity* live = destination.ResolveEntity(record.after)) live->Team() = current->Team();
    if (CGameContext* context = destination.GameContext()) context->Network().NotifyPlayerWorldChanged(player);
    CEntity* current = player.GetEntity();
    return {true, current ? current->GetHandle() : SEntityHandle{}, {}};
}

SPlayerTransitionResult CWorldTransfer::Possess(CPlayer& player, CGameWorld& destination, const SEntityHandle& target_ref)
{
    CEntity* previous = player.GetEntity();
    CGameWorld* source = previous ? previous->GameWorld() : nullptr;
    auto* target = dynamic_cast<CMobBase*>(destination.ResolveEntity(target_ref));
    if (!player.IsAuthenticated() || !target || target->IsMarkedForDes() || target->IsDead()) return Reject("Invalid control target");
    if (player.IsTransitionActive() || destination.IsTransitionActive() || (source && source->IsTransitionActive()))
        return Reject("Nested control transition is not allowed");
    if (destination.m_spatial_grid.HasActiveQueries() || (source && source->m_spatial_grid.HasActiveQueries()))
        return Reject("Cannot change control during a spatial query");
    if (CGameContext* context = destination.GameContext())
        for (const auto& other : context->Players())
            if (other && other.get() != &player && other->GetEntity() == target) return Reject("Control target is occupied");
    if (previous == target) return {true, target_ref, {}};
    bool absent_source = false;
    CTransitionGuard guard(player.m_transition_active, source ? source->m_transition_active : absent_source,
                           destination.m_transition_active);
    SPlayerTransitionView view{EPlayerTransitionKind::Possession, player.GetId(), player.SessionGeneration(),
        source ? SWorldHandle{source->GetId(), source->GetInstanceGeneration()} : SWorldHandle{},
        {destination.GetId(), destination.GetInstanceGeneration()}, previous ? previous->GetHandle() : SEntityHandle{},
        target_ref, target->Pos(), 1};
    std::string error;
    if (!ValidateTransition(view, source, destination, error)) return Reject(error);
    if (player.GetEntity() != previous || destination.ResolveEntity(target_ref) != target || target->IsMarkedForDes())
        return Reject("Control target changed during preflight");
    std::unique_ptr<CPlayerController> controller;
    try { controller = std::make_unique<CPlayerController>(); }
    catch (const std::exception& exception) { return Reject(exception.what()); }
    player.ResetControlledMob();
    player.CommitOwnedEntity(target);
    target->SetController(std::move(controller));
    if (source != &destination)
    {
        player.m_cp_stack.clear(); player.m_cp_check_phase = static_cast<uint8_t>(player.GetId() & 7);
        if (source && source->GetController()) source->GetController()->OnPlayerDepartureCommitted(*source, player, previous);
    }
    if (!player.SynchronizeGameplayScope(destination, source ? source->ResolveEntity(view.previous_entity) : nullptr))
        throw std::runtime_error("Cannot initialize committed player progress scope");
    if (destination.GetController()) destination.GetController()->OnPlayerArrivalCommitted(destination, player);
    // Arrival callbacks may remove the controlled entity. Never retain the
    // pre-callback pointer across that committed notification boundary.
    if (auto* live = dynamic_cast<CMobBase*>(destination.ResolveEntity(target_ref)))
        live->SynchronizeEnvironment();
    player.m_logged_missing_entity = false;
    if (CGameContext* context = destination.GameContext()) context->Network().NotifyPlayerWorldChanged(player);
    CEntity* current = player.GetEntity();
    return {true, current ? current->GetHandle() : SEntityHandle{}, {}};
}
