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
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <unordered_set>
#include <chrono>

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

// 修改前保存索引与分配器；失败时移动恢复，原实体始终保有所有权。
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

CWorldPreparation::CWorldPreparation(CGameWorld& world) : m_world(world) {}
CWorldPreparation::~CWorldPreparation() = default;
const FlorrBtMap* CWorldPreparation::Map() const { return m_world.GetMap(); }
bool CWorldPreparation::RequireTerrain(std::string& error)
{
    if (!Map()) { error = "玩法缺少地图资源"; return false; }
    try { m_world.Terrain().EnsureLoaded(); return true; }
    catch (const std::exception& exception) { error = exception.what(); return false; }
}
bool CWorldPreparation::RequireDigBoard(std::string& error)
{
    if (!RequireTerrain(error)) return false;
    try { m_board = m_world.Terrain().PrepareDigBoard(); return true; }
    catch (const std::exception& exception) { error = exception.what(); return false; }
}
void CWorldPreparation::Commit() noexcept { m_world.Terrain().InstallDigBoard(std::move(m_board)); }

SPlayerTransitionResult CWorldTransfer::Move(CPlayer& player, CGameWorld& source, CGameWorld& destination,
                                            florrbt::Vector2f position)
{
    const auto started = std::chrono::steady_clock::now();
    SWorldTransferCost cost;
    auto result = MovePrepared(player, source, destination, position, cost);
    cost.total_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
    result.cost = cost;
    return result;
}

SPlayerTransitionResult CWorldTransfer::MovePrepared(CPlayer& player, CGameWorld& source, CGameWorld& destination,
                                                    florrbt::Vector2f position, SWorldTransferCost& cost)
{
    if (CAccountDataStore::IsEconomyOperationActive()) return Reject("Cannot transfer during an economy operation");
    if (destination.IsRecovering()) return Reject("目标房间正在恢复");
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

    if (source.IsTicking() || destination.IsTicking() || source.IsCombatEventActive() || destination.IsCombatEventActive())
        return Reject("迁移必须在世界模拟屏障提交");

    CTransitionGuard guard(player.m_transition_active, source.m_transition_active, destination.m_transition_active);
    struct SRecord
    {
        CEntity* entity;
        SEntityHandle before, after;
        florrbt::Vector2f position, previous;
        std::uint64_t active_marker, collision_marker, birth_marker;
        bool moved = false;
    };
    std::vector<SRecord> records;
    std::vector<std::pair<SEntityHandle, SEntityHandle>> mapping;
    std::vector<SEntityHandle> chain_refs;
    std::unique_ptr<CPlayer::SPreparedGameplayScope> progress;
    std::string error;
    const int fault_after = std::exchange(source.m_transfer_fault_after_insert, -1);
    try
    {
        // 一次建立附属实体索引，避免为每个部件重新扫描世界。
        std::unordered_map<SEntityHandle, std::vector<SEntityHandle>, SEntityHandleHash> attached;
        source.ForEachEntity([&](CEntity* candidate) {
            ++cost.dependency_candidates;
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
                               entity->ActiveTickMarker(), entity->CollisionSyncMarker(), entity->BirthTickMarker()});
            cost.dependencies = records.size();
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
            // 花瓣和未发射弹药随所有者迁移；已发射投射物、被搬运目标和独立召唤物不在此列。
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
            // 同世界传送只改位置和空间索引，无需复制实体列表、ID 分配器和玩法索引。
            const auto checkpoint_started = std::chrono::steady_clock::now();
            auto saved_spatial = source.m_spatial_grid;
            cost.spatial_entries_copied = source.m_spatial_grid.TrackedCount();
            cost.checkpoint_ms = std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - checkpoint_started).count();
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
                source.m_spatial_grid = std::move(saved_spatial);
                return Reject("Teleport staging failed and was rolled back");
            }
            // 传送只采样落点，不能把路径上的地形当成实际经过。
            for (const auto& record : records)
                if (auto* mob = dynamic_cast<CMobBase*>(source.ResolveEntity(record.before)))
                    mob->SynchronizeEnvironment();
            CEntity* current = player.GetEntity();
            return {true, current ? current->GetHandle() : SEntityHandle{}, {}};
        }

        progress = player.PrepareGameplayScope(destination, destination.GetController(), controlled, error);
        if (!progress) return Reject(error);

        if (records.size() >= UINT64_MAX - destination.m_next_generation ||
            destination.m_next_id > std::numeric_limits<int>::max() - static_cast<int>(records.size()))
            return Reject("Destination entity identity space exhausted");
        mapping.reserve(records.size());
        for (CChainEntity* chain : source.m_chains)
            if (chain && !chain->IsMarkedForDes())
                for (const auto& record : records)
                    if (chain->HasEndpoint(*record.entity) || chain->HasLease(*record.entity))
                    { chain_refs.push_back(chain->GetHandle()); break; }

        const auto checkpoint_started = std::chrono::steady_clock::now();
        CWorldCheckpoint before_source(source), before_destination(destination);
        cost.checkpoint_ms = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - checkpoint_started).count();
        cost.spatial_entries_copied = source.m_spatial_grid.TrackedCount() + destination.m_spatial_grid.TrackedCount();
        const auto index_size = [](const CGameWorld& world) {
            return world.m_live_entities.size() + world.m_cleanup_entities.size() + world.m_always_tick_entities.size() +
                world.m_conditional_tick_entities.size() + world.m_large_entities.size() + world.m_chains.size() +
                world.m_spawn_habitat_cache.size() + world.m_free_ids.size() + world.m_id_in_use.size();
        };
        cost.entity_index_entries_copied = index_size(source) + index_size(destination);
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
                record.entity->m_birth_tick_marker = record.birth_marker;
                record.entity->Pos() = record.position; record.entity->PrevPos() = record.previous;
            }
            before_source.Restore(); before_destination.Restore();
            try { throw; }
            catch (const std::exception& exception) { return Reject(std::string("Transfer rolled back: ") + exception.what()); }
            catch (...) { return Reject("Transfer rolled back after an unknown staging failure"); }
        }

        // 结构提交只改已分配的身份和句柄，外部回调在提交后执行。
        for (const auto& record : records) record.entity->RemapTransferredReferences(mapping);
        for (const auto& record : records) source.FreeID(record.before.id);
        player.CommitOwnedEntity(controlled);
        player.m_cp_stack.clear(); player.m_cp_check_phase = static_cast<uint8_t>(player.GetId() & 7);
        player.m_logged_missing_entity = false;
    }
    catch (const std::exception& exception)
    {
        // 准备失败尚未改动世界；结构修改阶段的异常已在上方回滚。
        return Reject(exception.what());
    }

    try
    {
    // 使用源世界的完整旧句柄清理外部关系。
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
        // Relic 区域留在源世界清理，不能随花瓣搬走。
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
    // 先迁移所有权再清理花瓣，使清理回调能解析正确的所有者。
    for (const auto& record : records)
    {
        CEntity* live = destination.ResolveEntity(record.after);
        if (auto* mob = dynamic_cast<CMobBase*>(live)) mob->ClearTeamOverrideStates();
        if (auto* flower = dynamic_cast<CFlower*>(live)) flower->DestroyPetalEntities();
        if (auto* petal = dynamic_cast<CPetal*>(live)) { petal->NotifyCleared(); petal->MarkForDestroy(EEntityRemovalReason::OwnerRemoved); }
    }
    progress->Commit();
    if (destination.GetController()) destination.GetController()->OnPlayerArrivalCommitted(destination, player);
    for (const auto& record : records)
        if (auto* mob = dynamic_cast<CMobBase*>(destination.ResolveEntity(record.after))) mob->SynchronizeEnvironment();
    if (CEntity* current = player.GetEntity())
        for (const auto& record : records)
            if (CEntity* live = destination.ResolveEntity(record.after)) live->SetTeam(current->Team());
    if (CGameContext* context = destination.GameContext())
    {
        if (&source == &destination) context->Scenes().NotifyPlayerStateChanged(player);
        else context->Scenes().NotifyPlayerWorldChanged(player);
    }
    CEntity* current = player.GetEntity();
    return {true, current ? current->GetHandle() : SEntityHandle{}, {}};
    }
    catch (const std::exception& exception)
    {
        if (&source != &destination) source.EnterRecovery(exception.what());
        destination.EnterRecovery(exception.what());
    }
    catch (...)
    {
        if (&source != &destination) source.EnterRecovery("迁移提交回调发生未知异常");
        destination.EnterRecovery("迁移提交回调发生未知异常");
    }
    const auto* remaining = player.GetEntity();
    return {true, remaining ? remaining->GetHandle() : SEntityHandle{}, destination.RecoveryError(), false, true};

}

SPlayerTransitionResult CWorldTransfer::Possess(CPlayer& player, CGameWorld& destination, const SEntityHandle& requested_ref)
{
    if (CAccountDataStore::IsEconomyOperationActive()) return Reject("Cannot change control during an economy operation");
    if (destination.IsRecovering()) return Reject("目标房间正在恢复");
    CEntity* previous = player.GetEntity();
    CGameWorld* source = previous ? previous->GameWorld() : nullptr;
    if (destination.IsTicking() || destination.IsCombatEventActive() || (source && (source->IsTicking() || source->IsCombatEventActive())))
        return Reject("控制权切换必须在模拟帧边界提交");
    auto* target = dynamic_cast<CMobBase*>(destination.ResolveEntity(requested_ref));
    if (!player.IsAuthenticated() || !target || target->IsMarkedForDes() || target->IsDead()) return Reject("Invalid control target");
    if (const auto* body = dynamic_cast<const IMultiSegmentBodyMob*>(target))
    {
        auto* root = ResolveMultiSegmentRoot(&destination, body->RootHandle());
        const auto* assembly = dynamic_cast<const IMultiSegmentRootMob*>(root);
        if (!assembly || root->IsDead() ||
            std::none_of(assembly->GetSegmentReferences().begin(), assembly->GetSegmentReferences().end(),
                [&](const auto& part) { return part.index == body->SegmentIndex() && part.entity_ref.RefersTo(requested_ref); }))
            return Reject("Control target has no valid assembly root");
        // 体节由根统一驱动；接管任意部位都必须控制整条生物。
        target = root;
    }
    const auto target_ref = target->GetHandle();
    if (player.IsTransitionActive() || destination.IsTransitionActive() || (source && source->IsTransitionActive()))
        return Reject("Nested control transition is not allowed");
    if (destination.m_spatial_grid.HasActiveQueries() || (source && source->m_spatial_grid.HasActiveQueries()))
        return Reject("Cannot change control during a spatial query");
    if (CGameContext* context = destination.GameContext())
        for (const auto& other : context->Players())
            if (other && other.get() != &player &&
                (other->GetEntity() == target || ShareMultiSegmentAssembly(other->GetEntity(), target)))
                return Reject("Control target is occupied");
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
    std::unique_ptr<IController> release_controller;
    std::unique_ptr<SControlEquipment> equipment;
    std::unique_ptr<CPlayer::SPreparedGameplayScope> progress;
    struct SPreparedInventory
    {
        std::string key; std::uint64_t epoch = 0; bool committed = false;
        ~SPreparedInventory() { if (!committed && !key.empty()) CAccountDataStore::RemoveSandboxAccount(key); }
    } inventory;
    try
    {
        progress = player.PrepareGameplayScope(destination, destination.GetController(), target, error);
        if (!progress) return Reject(error);
        controller = std::make_unique<CPlayerController>();
        equipment = target->CaptureControlEquipment();
        if (!player.PrepareNpcInventory(*target, inventory.key, inventory.epoch, error)) return Reject(error);
        if (auto* body = dynamic_cast<CMobBase*>(previous); body && !body->IsMarkedForDes())
            if (const auto* prototype = FindMobPrototype(body->GetMobType()); prototype && prototype->m_controller_factory)
                release_controller = prototype->m_controller_factory(body->GetRarity());
    }
    catch (const std::exception& exception) { return Reject(exception.what()); }
    try
    {
    player.ResetControlledMob();
    player.CommitOwnedEntity(target);
    player.CommitNpcInventory(std::move(inventory.key), inventory.epoch); inventory.committed = true;
    target->AcquirePlayerControl(std::move(controller), std::move(equipment));
    if (auto* body = dynamic_cast<CMobBase*>(previous)) body->ReleasePlayerControl(std::move(release_controller));
    if (source != &destination)
    {
        player.m_cp_stack.clear(); player.m_cp_check_phase = static_cast<uint8_t>(player.GetId() & 7);
        if (source && source->GetController()) source->GetController()->OnPlayerDepartureCommitted(*source, player, previous);
    }
    progress->Commit(source ? source->ResolveEntity(view.previous_entity) : nullptr);
    if (destination.GetController()) destination.GetController()->OnPlayerArrivalCommitted(destination, player);
    // 进入回调可能删除实体，必须重新解析句柄。
    if (auto* live = dynamic_cast<CMobBase*>(destination.ResolveEntity(target_ref)))
        live->SynchronizeEnvironment();
    player.m_logged_missing_entity = false;
    if (CGameContext* context = destination.GameContext())
    {
        // 同世界附身只更新身体和背包；跨世界仍需完整场景屏障。
        if (source == &destination) context->Scenes().NotifyPlayerStateChanged(player);
        else context->Scenes().NotifyPlayerWorldChanged(player);
    }
    CEntity* current = player.GetEntity();
    return {true, current ? current->GetHandle() : SEntityHandle{}, {}};
    }
    catch (const std::exception& exception)
    {
        if (source && source != &destination) source->EnterRecovery(exception.what());
        destination.EnterRecovery(exception.what());
    }
    catch (...)
    {
        if (source && source != &destination) source->EnterRecovery("迁移提交回调发生未知异常");
        destination.EnterRecovery("迁移提交回调发生未知异常");
    }
    const auto* remaining = player.GetEntity();
    return {true, remaining ? remaining->GetHandle() : SEntityHandle{}, destination.RecoveryError(), false, true};

}
