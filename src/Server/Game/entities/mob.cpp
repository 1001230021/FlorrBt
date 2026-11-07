#include "mob.h"
#include "../../../Engine/logger.h"
#include "../../../Shared/game_config.h"
#include "../../../Shared/circle_contact.h"
#include "../controllers/melee_controller.h"
#include "../gamecontext.h"
#include "../gameworld.h"
#include "../player.h"
#include "../states/states.h"
#include "drop.h"
#include "projectile.h"
#include "petals/petal.h"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <unordered_set>

namespace
{
float MultiplyFiniteNonNegative(float lhs, float rhs)
{
    if (!std::isfinite(lhs) || !std::isfinite(rhs)) return 0.f;
    lhs = std::max(0.f, lhs);
    rhs = std::max(0.f, rhs);
    if (lhs == 0.f || rhs == 0.f) return 0.f;
    if (lhs > std::numeric_limits<float>::max() / rhs) return std::numeric_limits<float>::max();
    return lhs * rhs;
}

float ClampAngleAround(float angle, float anchor, float max_delta)
{
    if (!std::isfinite(angle)) angle = anchor;
    if (!std::isfinite(anchor)) anchor = 0.f;
    max_delta = std::clamp(std::isfinite(max_delta) ? max_delta : game_config::pi, 0.f, game_config::pi);
    const float delta = std::remainder(angle - anchor, 2.f * game_config::pi);
    return anchor + std::clamp(delta, -max_delta, max_delta);
}

SEntityHandle MultiSegmentRootHandle(const CEntity* entity)
{
    const auto* mob = dynamic_cast<const CMobBase*>(entity);
    if (!mob) return {};
    if (dynamic_cast<const IMultiSegmentRootMob*>(mob)) return mob->GetHandle();
    if (const auto* body = dynamic_cast<const IMultiSegmentBodyMob*>(mob)) return body->RootHandle();
    return {};
}
}

CMobBase* ResolveMultiSegmentRoot(const CGameWorld* world, const SEntityHandle& root_ref)
{
    if (!world || !root_ref.IsValid()) return nullptr;
    CEntity* entity = world->ResolveEntity(root_ref);
    if (!entity || entity->IsMarkedForDes()) return nullptr;
    auto* root = dynamic_cast<CMobBase*>(entity);
    return root && dynamic_cast<IMultiSegmentRootMob*>(root) ? root : nullptr;
}

CMobBase* ResolveMultiSegmentBody(const CGameWorld* world, const SEntityHandle& body_ref)
{
    if (!world || !body_ref.IsValid()) return nullptr;
    CEntity* entity = world->ResolveEntity(body_ref);
    if (!entity || entity->IsMarkedForDes()) return nullptr;
    auto* body = dynamic_cast<CMobBase*>(entity);
    return body && dynamic_cast<IMultiSegmentBodyMob*>(body) ? body : nullptr;
}

bool UsesMultiSegmentChainConstraint(const CMobBase& mob)
{
    const CMobBase* root = &mob;
    if (const auto* body = dynamic_cast<const IMultiSegmentBodyMob*>(&mob))
        root = ResolveMultiSegmentRoot(mob.GameWorld(), body->RootHandle());
    if (!root || !dynamic_cast<const IMultiSegmentRootMob*>(root)) return false;
    const CMobPrototype* prototype = FindMobPrototype(root->GetMobType());
    return prototype && prototype->m_constrain_segment_chain;
}

bool SharesMultiSegmentRootHealth(const CMobBase& mob)
{
    const CMobBase* root = &mob;
    if (const auto* body = dynamic_cast<const IMultiSegmentBodyMob*>(&mob))
        root = ResolveMultiSegmentRoot(mob.GameWorld(), body->RootHandle());
    if (!root || !dynamic_cast<const IMultiSegmentRootMob*>(root)) return false;
    const CMobPrototype* prototype = FindMobPrototype(root->GetMobType());
    return prototype && prototype->m_share_segment_health;
}

bool ShareMultiSegmentAssembly(const CEntity* lhs, const CEntity* rhs)
{
    if (!lhs || !rhs) return false;
    const SEntityHandle lhs_root = MultiSegmentRootHandle(lhs);
    const SEntityHandle rhs_root = MultiSegmentRootHandle(rhs);
    return lhs_root.RefersTo(rhs_root);
}

void DestroyMultiSegmentChildren(CMobBase& root, const std::vector<SMobSegmentReference>& references)
{
    CGameWorld* world = root.GameWorld();
    if (!world) return;

    const SEntityHandle root_ref = root.GetHandle();
    for (const SMobSegmentReference& reference : references)
    {
        CEntity* entity = world->ResolveEntity(reference.entity_ref);
        if (!entity || entity == &root || entity->IsMarkedForDes()) continue;

        auto* body = dynamic_cast<IMultiSegmentBodyMob*>(entity);
        if (!body || body->SegmentIndex() != reference.index || !body->RootHandle().RefersTo(root_ref)) continue;
        entity->MarkForDestroy(EEntityRemovalReason::OwnerRemoved);
    }
}

void DetachMultiSegmentBody(CMobBase& body, const SEntityHandle& root_ref, int segment_index)
{
    if (!body.GameWorld() || !root_ref.IsValid() || segment_index <= 0) return;
    CMobBase* root = ResolveMultiSegmentRoot(body.GameWorld(), root_ref);
    if (!root) return;

    auto* root_contract = dynamic_cast<IMultiSegmentRootMob*>(root);
    if (!root_contract) return;
    root_contract->DetachSegment(body.GetHandle(), segment_index);
    // 硬约束多节体作为整体存续；部件异常消失时一并回收根，避免残留半条链。
    if (UsesMultiSegmentChainConstraint(*root) && !root->IsMarkedForDes())
        root->MarkForDestroy(EEntityRemovalReason::OwnerRemoved);
}

void AssembleMultiSegmentMob(CMobBase& root)
{
    auto* root_contract = dynamic_cast<IMultiSegmentRootMob*>(&root);
    if (!root_contract || !root.GameWorld()) return;

    const EMultiSegmentAssemblyState state = root_contract->SegmentAssemblyState();
    if (state == EMultiSegmentAssemblyState::Complete || state == EMultiSegmentAssemblyState::Failed ||
        state == EMultiSegmentAssemblyState::InProgress)
        return;

    const CMobPrototype* prototype = FindMobPrototype(root.GetMobType());
    const int segment_count = prototype ? prototype->ResolveSegmentCount(root.GetRarity()) : 0;
    if (!prototype || segment_count <= 0 || !prototype->m_segment_type_factory)
    {
        // 未声明体节布局的根视为空布局，不逐帧重复装配。
        root_contract->SetSegmentAssemblyState(EMultiSegmentAssemblyState::Complete);
        return;
    }

    root_contract->SetSegmentAssemblyState(EMultiSegmentAssemblyState::InProgress);
    std::vector<CMobBase*> inserted_segments;
    inserted_segments.reserve(static_cast<std::size_t>(segment_count));
    for (int index = 1; index <= segment_count; ++index)
    {
        const EMobType segment_type = prototype->m_segment_type_factory(index);
        if (segment_type == EMobType::None || segment_type == root.GetMobType())
        {
            LOG_ERROR("mob", "Invalid multi-segment type at index " + std::to_string(index) + " for " +
                                 std::string(GetMobTypeName(root.GetMobType())));
            root_contract->SetSegmentAssemblyState(EMultiSegmentAssemblyState::Failed);
            root.MarkForDestroy(EEntityRemovalReason::Despawned);
            return;
        }

        std::unique_ptr<CMobBase> segment =
            CreateMob(segment_type, root.GameWorld(), root.Pos(), root.GetRarity(), false, root.IsNaturallySpawned());
        if (!segment)
        {
            LOG_ERROR("mob", "No factory for multi-segment type " + std::to_string(static_cast<int>(segment_type)) +
                                 " used by " + std::string(GetMobTypeName(root.GetMobType())));
            root_contract->SetSegmentAssemblyState(EMultiSegmentAssemblyState::Failed);
            root.MarkForDestroy(EEntityRemovalReason::Despawned);
            return;
        }

        auto* body_contract = dynamic_cast<IMultiSegmentBodyMob*>(segment.get());
        if (!body_contract)
        {
            LOG_ERROR("mob", "Multi-segment type " + std::string(GetMobTypeName(segment_type)) +
                                 " is not a body mob");
            root_contract->SetSegmentAssemblyState(EMultiSegmentAssemblyState::Failed);
            root.MarkForDestroy(EEntityRemovalReason::Despawned);
            return;
        }

        segment->SetTeam(root.Team());
        body_contract->SetRootReference(root.GetHandle(), index);
        CEntity* inserted = root.GameWorld()->InsertEntity(std::move(segment));
        if (!inserted || !root_contract->AttachSegment(inserted->GetHandle(), index))
        {
            if (inserted) inserted->MarkForDestroy(EEntityRemovalReason::OwnerRemoved);
            LOG_ERROR("mob", "Failed to attach multi-segment body " + std::to_string(index) + " to " +
                                 std::string(GetMobTypeName(root.GetMobType())));
            root_contract->SetSegmentAssemblyState(EMultiSegmentAssemblyState::Failed);
            root.MarkForDestroy(EEntityRemovalReason::Despawned);
            return;
        }
        inserted_segments.push_back(static_cast<CMobBase*>(inserted));
    }

    for (std::size_t index = 0; index < inserted_segments.size(); ++index)
    {
        auto* body = dynamic_cast<IMultiSegmentBodyMob*>(inserted_segments[index]);
        if (!body) continue;
        const SEntityHandle next = index + 1 < inserted_segments.size()
                                       ? inserted_segments[index + 1]->GetHandle()
                                       : SEntityHandle{};
        body->SetNextSegmentReference(next);
    }
    root_contract->SetSegmentAssemblyState(EMultiSegmentAssemblyState::Complete);
    ConstrainMultiSegmentChain(root, 0.f);
}

void ResolveMultiSegmentReferences(CGameWorld& world)
{
    std::vector<CMobBase*> roots;
    std::vector<CMobBase*> bodies;
    world.ForEachEntity([&](CEntity* entity) {
        auto* mob = dynamic_cast<CMobBase*>(entity);
        if (!mob) return;
        if (dynamic_cast<IMultiSegmentRootMob*>(mob)) roots.push_back(mob);
        if (dynamic_cast<IMultiSegmentBodyMob*>(mob)) bodies.push_back(mob);
    });

    // 先剔除不再对应有效体节的根引用，允许捕获前刚被移除的体节自然退出。
    for (CMobBase* root : roots)
    {
        auto* root_contract = dynamic_cast<IMultiSegmentRootMob*>(root);
        if (!root_contract) continue;
        const auto references = root_contract->GetSegmentReferences();
        root_contract->ClearSegmentReferences();
        for (const SMobSegmentReference& reference : references)
        {
            CEntity* entity = world.ResolveEntity(reference.entity_ref);
            auto* body = entity ? dynamic_cast<IMultiSegmentBodyMob*>(entity) : nullptr;
            if (!body || body->SegmentIndex() != reference.index ||
                !body->RootHandle().RefersTo(root->GetHandle()))
                continue;
            root_contract->AttachSegment(entity->GetHandle(), reference.index);
        }
    }

    // 再从体节重建反向关系；找不到根的体节安全移除。
    for (CMobBase* body_mob : bodies)
    {
        auto* body = dynamic_cast<IMultiSegmentBodyMob*>(body_mob);
        if (!body || !body->RootHandle().IsValid())
        {
            body_mob->MarkForDestroy(EEntityRemovalReason::OwnerRemoved);
            continue;
        }

        CMobBase* root = ResolveMultiSegmentRoot(&world, body->RootHandle());
        auto* root_contract = root ? dynamic_cast<IMultiSegmentRootMob*>(root) : nullptr;
        if (!root_contract || root == body_mob || body->SegmentIndex() <= 0 ||
            !root_contract->AttachSegment(body_mob->GetHandle(), body->SegmentIndex()))
        {
            body_mob->MarkForDestroy(EEntityRemovalReason::OwnerRemoved);
        }
    }

    // 按已验证的根列表重建前向链接，清除捕获前移除体节留下的旧边。
    for (CMobBase* root : roots)
    {
        auto* root_contract = dynamic_cast<IMultiSegmentRootMob*>(root);
        if (!root_contract) continue;
        const auto& references = root_contract->GetSegmentReferences();
        for (std::size_t i = 0; i < references.size(); ++i)
        {
            CEntity* entity = world.ResolveEntity(references[i].entity_ref);
            auto* body = entity ? dynamic_cast<IMultiSegmentBodyMob*>(entity) : nullptr;
            if (!body) continue;
            const SEntityHandle next = i + 1 < references.size() && references[i + 1].index == references[i].index + 1
                                           ? references[i + 1].entity_ref
                                           : SEntityHandle{};
            body->SetNextSegmentReference(next);
        }
    }

    // 旧快照缺少多节字段时仅补装一次；新快照的 Complete/Failed 状态禁止重复装配。
    for (CMobBase* root : roots)
    {
        auto* root_contract = dynamic_cast<IMultiSegmentRootMob*>(root);
        if (!root_contract || root_contract->SegmentAssemblyState() != EMultiSegmentAssemblyState::NotStarted)
            continue;
        const CMobPrototype* prototype = FindMobPrototype(root->GetMobType());
        if (!prototype || prototype->ResolveSegmentCount(root->GetRarity()) <= 0) continue;
        if (root_contract->GetSegmentReferences().empty()) AssembleMultiSegmentMob(*root);
    }
}

void ConstrainMultiSegmentChain(CMobBase& root, float dt)
{
    auto* root_contract = dynamic_cast<IMultiSegmentRootMob*>(&root);
    const CMobPrototype* prototype = FindMobPrototype(root.GetMobType());
    if (!root_contract || !prototype || !prototype->m_constrain_segment_chain || !root.GameWorld()) return;
    if (root_contract->SegmentAssemblyState() != EMultiSegmentAssemblyState::Complete) return;

    const float spacing_scale = std::max(0.f, prototype->m_segment_center_distance_radius_scale);
    const float max_turn = std::clamp(prototype->m_segment_max_turn_radians, 0.f, game_config::pi);
    CMobBase* leader = &root;
    int expected_index = 1;
    const auto& references = root_contract->GetSegmentReferences();
    SEntityHandle linked_segment = references.empty() ? SEntityHandle{} : references.front().entity_ref;

    for (const SMobSegmentReference& reference : references)
    {
        // 根列表与体节的下一节点必须一致，避免环和跨根连接。
        if (reference.index != expected_index || !linked_segment.RefersTo(reference.entity_ref)) break;
        CMobBase* follower = ResolveMultiSegmentBody(root.GameWorld(), reference.entity_ref);
        auto* body = follower ? dynamic_cast<IMultiSegmentBodyMob*>(follower) : nullptr;
        if (!body || !body->RootHandle().RefersTo(root.GetHandle()) || follower->IsDead()) break;

        const florrbt::Vector2f toward_leader = leader->Pos() - follower->Pos();
        float candidate_angle = leader->HasFacing() ? leader->FacingAngle() : 0.f;
        if (LengthSq(toward_leader) > game_config::entity_collision_epsilon * game_config::entity_collision_epsilon)
            candidate_angle = std::atan2(toward_leader.y, toward_leader.x);
        const float angle = ClampAngleAround(candidate_angle, leader->FacingAngle(), max_turn);
        const florrbt::Vector2f forward{ std::cos(angle), std::sin(angle) };
        const float spacing = std::max(game_config::entity_collision_epsilon,
                                       std::min(leader->Radius(), follower->Radius()) * spacing_scale);
        const florrbt::Vector2f desired_pos = leader->Pos() - forward * spacing;

        follower->Pos() = desired_pos;
        if (dt <= 0.f) follower->PrevPos() = desired_pos;
        follower->m_vel = leader->m_vel;
        follower->FacingAngle() = angle;
        follower->HasFacing() = true;

        leader = follower;
        linked_segment = body->NextSegmentHandle();
        ++expected_index;
    }
}

CPlayer* CDamageData::ResolvePlayer(const CGameContext* context) const
{
    if (!context || m_account_name.empty()) return nullptr;
    for (const auto& player : context->Players())
        if (player && player->IsAuthenticated() && player->GetAccountName() == m_account_name &&
            (m_progress_account.empty() ? !player->HasIsolatedProgress() :
             player->GetProgressAccountName() == m_progress_account)) return player.get();
    return nullptr;
}

bool CDamageData::MatchesInventoryScope(const CPlayer& player) const
{
    const auto& scope = m_inventory_account.empty() ? (m_progress_account.empty() ? m_account_name : m_progress_account) : m_inventory_account;
    return player.IsAuthenticated() && scope == player.GetInventoryAccountName();
}

void CMobBase::ClearTeamOverrideStates()
{
    for (auto* state : FindStates<CCorruptionState>()) RemoveState(state);
}

CMobBase::~CMobBase()
{
    for (auto& state : m_states)
        if (state) state->DetachOwner();
}

void CMobBase::ClearStatesForRestore()
{
    for (auto& state : m_states)
        if (state) { RevokeStateEffects(state->EffectSourceId()); state->DetachOwner(); }
    m_states.clear();
    m_movement_target_states.clear();
}

void CMobBase::RebindStateOwners()
{
    for (auto& state : m_states)
        if (state) state->RebindOwner(this);
}

template <typename TStats> CMob<TStats>::~CMob() = default;

template class CMob<SMobStats>;
template class CMob<SFlowerStats>;

namespace
{
bool IsUndeadDamageSource(CEntity* entity)
{
    std::unordered_set<SEntityHandle, SEntityHandleHash> visited_refs;

    while (entity)
    {
        const SEntityHandle ref = entity->GetHandle();
        if (ref.IsValid() && !visited_refs.insert(ref).second) return false;

        if (auto* mob = dynamic_cast<CMobBase*>(entity))
        {
            if (mob->HasState<CUndeadState>()) return true;

            auto* controller = dynamic_cast<CSummonedMeleeController*>(mob->GetController());
            CGameWorld* world = mob->GameWorld();
            if (controller && world)
            {
                entity = controller->GetOwner(world);
                continue;
            }
        }

        if (auto* projectile = dynamic_cast<CProjectile*>(entity))
        {
            entity = projectile->GetOwner();
            continue;
        }

        return false;
    }

    return false;
}
}

bool ShouldBlockDiggingDamage(CMobBase* receiver, CEntity* attacker, EDamageType dmg_type)
{
    return receiver && receiver->HasState<CDiggingState>() && BaseDamageType(dmg_type) != EDamageType::Poison &&
           !IsDiggingEntity(attacker);
}

void CMobBase::AddState(std::unique_ptr<CState> state, EStateApplyTiming timing)
{
    if (!state) return;
    if (timing == EStateApplyTiming::EventBoundary && GameWorld() && GameWorld()->CombatEvents().QueueAddition(*this, state)) return;
    AddStateImmediate(std::move(state));
}

void CMobBase::AddStateImmediate(std::unique_ptr<CState> state)
{
    if (!state) return;
    CState* raw = state.get();
    // 先准备存储，再仲裁替换与发布来源，避免分配失败时已移除了旧状态。
    if (m_states.size() == m_states.capacity()) m_states.reserve(std::max<size_t>(4, m_states.capacity() * 2));
    if (raw->AffectsMovementTarget() && m_movement_target_states.size() == m_movement_target_states.capacity())
        m_movement_target_states.reserve(std::max<size_t>(4, m_movement_target_states.capacity() * 2));
    raw->RebindOwner(this);
    if (!raw->PrepareAttachment(*this)) return;
    m_states.push_back(std::move(state));
    if (raw->AffectsMovementTarget()) m_movement_target_states.push_back(raw);
    raw->AttachToOwner(*this);
    if (GameWorld()) GameWorld()->CombatEvents().StateChanged(*this);
}

bool CMobBase::DeferStateMutation(std::function<void(CMobBase&)> mutation)
{
    return GameWorld() && GameWorld()->CombatEvents().QueueMutation(*this, std::move(mutation));
}

CState* CMobBase::FindStateBySource(std::uint64_t source) const
{
    for (const auto& state : m_states) if (state && state->EffectSourceId() == source) return state.get();
    return nullptr;
}

void CMobBase::CommitActionView()
{
    if (GameWorld() && (m_environment_sample_world != GameWorld()->GetInstanceGeneration() || m_environment_sample_position != Pos()))
        SynchronizeEnvironmentAfterDisplacement();
}

void CMobBase::UnregisterMovementTargetState(CState* state)
{
    if (!state) return;
    m_movement_target_states.erase(
        std::remove(m_movement_target_states.begin(), m_movement_target_states.end(), state),
        m_movement_target_states.end());
}

florrbt::Vector2f CMobBase::ApplyMovementTargetStates(const florrbt::Vector2f& target_pos) const
{
    florrbt::Vector2f result = target_pos;
    for (const CState* state : m_movement_target_states)
        if (state) result = state->ModifyMovementTarget(*this, result);
    return result;
}

void CMobBase::CollectTickDependencies(std::vector<SEntityHandle>& output) const
{
    CEntity::CollectTickDependencies(output);
    if (const auto* pickup = GetPickupController())
        if (const auto* carried = pickup->GetCarried(this)) output.push_back(carried->GetHandle());
}

void CMobBase::OnTickBegin(float)
{
    BeginStateTick();
    SynchronizeEnvironment();
}

void CMobBase::OnTickIntent(float dt)
{
    if (m_p_controller && !IsDead() && !IsMarkedForDes()) m_p_controller->OnTick(this, dt);
}

void CMobBase::OnTickMotion(float dt)
{
    if (IsDead() || IsMarkedForDes()) return;
    if (IsCollisionPositionLocked()) { m_vel = {}; return; }
    IntegrateMovement(dt);
}

void CMobBase::OnTickEnvironment(float) { SynchronizeEnvironmentAfterDisplacement(); }

void CMobBase::OnTickEffects(float dt)
{
    if (!IsMarkedForDes()) TickStates(dt);
}

void CMobBase::OnTickActionRequests(float dt)
{
    if (m_p_controller && !IsDead() && !IsMarkedForDes()) m_p_controller->OnActions(this, dt);
}

void CMobBase::BeginStateTick()
{
    // 到期来源在意图生成前撤销；析构回调可能同时移除其他状态，因此每次重新定位。
    for (;;)
    {
        const auto expired = std::find_if(m_states.begin(), m_states.end(), [](const auto& state) {
            return !state || (state->m_timer != endless && state->m_timer <= 0.f);
        });
        if (expired == m_states.end()) break;
        if (!*expired) m_states.erase(expired);
        else RemoveState(expired->get());
    }
    m_frame_states.clear();
    m_frame_states.reserve(m_states.size());
    for (const auto& state : m_states)
        m_frame_states.push_back({state->EffectSourceId(), state->m_timer, state->CaptureContinuousEffect()});
    m_state_tick_prepared = true;
}

void CMobBase::TickStates(float dt)
{
    EnsureStateTickPrepared();
    struct SReset { bool& prepared; ~SReset() { prepared = false; } } reset{m_state_tick_prepared};
    if (!std::isfinite(dt) || dt < 0.f || IsDead()) return;
    for (const auto& frame : m_frame_states)
    {
        const auto found = std::find_if(m_states.begin(), m_states.end(), [&frame](const auto& state) {
            return state && state->EffectSourceId() == frame.source;
        });
        // 本帧新增状态不补算整帧时间；有限状态也不能越过零后碰巧变成 endless(-1)。
        const float elapsed = frame.remaining == endless ? dt : std::min(dt, std::max(0.f, frame.remaining));
        if (frame.effect)
        {
            if (found != m_states.end() && (*found)->m_timer != endless)
                (*found)->m_timer = std::max(0.f, (*found)->m_timer - elapsed);
            CEntity* applier = GameWorld() ? GameWorld()->ResolveEntity(frame.effect->applier) : nullptr;
            TakeDamage(frame.effect->damage_per_second * elapsed, applier, frame.effect->damage_type);
        }
        else if (found != m_states.end()) (*found)->Tick(elapsed);
        if (IsDead()) return;
    }
}

bool CMobBase::TickDropPickup(CPlayer* player)
{
    if (!player || !player->IsAuthenticated() || player->GetAccountName().empty()) return false;
    if (IsMarkedForDes() || IsDead()) return false;

    CGameWorld* world = GameWorld();
    const SMobStats* stats = GetFinalStats();
    if (!world || !stats || stats->max_absorb_range <= 0.f) return false;

    bool picked_any = false;
    world->GetSpatialGrid().ForEachInRange(Pos(), stats->max_absorb_range, [&](CEntity* candidate) {
        auto* drop = dynamic_cast<CDrop*>(candidate);
        if (!drop || !drop->CanBePickedUpBy(*player)) return;
        picked_any = drop->PickUpTo(*player) || picked_any;
    });

    return picked_any;
}

void CMobBase::ApplyDamageDirect(float dmg, CEntity* attacker, EDamageType damage_type)
{
    if (!std::isfinite(dmg) || dmg <= 0.f) return;
    if (HasState<CInvincibleState>()) return;
    if (HasState<CUndeadState>())
    {
        Health() = std::max(1.f, Health());
        return;
    }

    CGameContext* context = GameContext();
    CPlayer* player = context && !IsUndeadDamageSource(attacker) ? context->FindPlayerFromEntity(attacker) : nullptr;
    if (player && player->IsAuthenticated() && !player->GetAccountName().empty())
    {
        auto it = std::find_if(m_damage_data.begin(), m_damage_data.end(),
                               [player](const CDamageData& data) {
                               return data.m_account_name == player->GetAccountName() && data.MatchesInventoryScope(*player) &&
                                       (data.m_progress_account == player->GetProgressAccountName() ||
                                        (data.m_progress_account.empty() && !player->HasIsolatedProgress()));
                               });
        if (it == m_damage_data.end())
        {
            CDamageData data;
            data.m_account_name = player->GetAccountName();
            data.m_progress_account = player->GetProgressAccountName();
            data.m_inventory_account = player->GetInventoryAccountName();
            data.m_total_dmg = dmg;
            data.ResetTimer();
            m_damage_data.push_back(data);
        } else
        {
            it->m_progress_account = player->GetProgressAccountName();
            it->m_inventory_account = player->GetInventoryAccountName();
            it->m_total_dmg += dmg;
            it->ResetTimer();
        }
    }

    if (GameWorld()) GameWorld()->RecordCombatDamage(*this, std::min(dmg, std::max(0.f, Health())), damage_type, attacker);
    Health() -= dmg;
    if (m_p_controller) m_p_controller->OnDamaged(this, attacker);
    if (Health() <= 0.f)
    {
        Health() = 0.f;
        MarkForDestroy(EEntityRemovalReason::Defeated);
    }
}

void CMobBase::MoveTowards(const florrbt::Vector2f& target_pos, float dt, float target_radius)
{
    MoveTowards(target_pos, dt, target_radius, 0.f);
}

void CMobBase::MoveTowardsWithWave(const florrbt::Vector2f& target_pos, float dt, float target_radius,
                                   float full_speed_distance, float& wave_timer)
{
    const SMobStats* stats = GetFinalStats();
    if (!stats || !std::isfinite(dt) || dt <= 0.f) return;
    if (!std::isfinite(Pos().x) || !std::isfinite(Pos().y) || !std::isfinite(target_pos.x) ||
        !std::isfinite(target_pos.y))
    {
        m_vel = { 0.f, 0.f };
        return;
    }
    if (!std::isfinite(m_vel.x) || !std::isfinite(m_vel.y)) m_vel = { 0.f, 0.f };

    wave_timer = std::isfinite(wave_timer) ? wave_timer + dt : 0.f;
    if (!std::isfinite(wave_timer)) wave_timer = 0.f;
    const float wave_frequency = game_config::mob_bee_wave_frequency;
    if (wave_frequency > game_config::entity_collision_epsilon && std::isfinite(wave_frequency))
    {
        const float wave_period = 2.f * game_config::pi / wave_frequency;
        if (std::isfinite(wave_period) && wave_period > game_config::entity_collision_epsilon)
            wave_timer = std::fmod(wave_timer, wave_period);
    }
    const float raw_speed_multiplier = GetMobMovementSpeedMultiplier(this);
    const float speed_multiplier = std::isfinite(raw_speed_multiplier) ? std::max(0.f, raw_speed_multiplier) : 0.f;
    float max_velocity = MultiplyFiniteNonNegative(stats->max_velocity, speed_multiplier);
    const float acceleration = std::isfinite(stats->acceleration) ? std::max(0.f, stats->acceleration) : 0.f;

    const florrbt::Vector2f effective_target_pos = ApplyMovementTargetStates(target_pos);
    if (!std::isfinite(effective_target_pos.x) || !std::isfinite(effective_target_pos.y))
    {
        m_vel = { 0.f, 0.f };
        return;
    }
    const florrbt::Vector2f delta = effective_target_pos - Pos();
    const float len = Length(delta);
    if (!std::isfinite(len) || len <= game_config::entity_collision_epsilon)
    {
        m_vel = florrbt::StepVelocityTowards(m_vel, {}, acceleration, dt);
        return;
    }

    const bool analog = std::isfinite(full_speed_distance) && full_speed_distance > 0.f;
    const float stop_distance = analog && target_radius < 0.f ? 0.f : Radius() + std::max(0.f, target_radius);
    if (len <= stop_distance)
    {
        m_vel = florrbt::StepVelocityTowards(m_vel, {}, acceleration, dt);
        if (LengthSq(m_vel) <= game_config::mob_stop_velocity_epsilon) m_vel = { 0.f, 0.f };
        return;
    }
    if (analog) max_velocity *= std::clamp((len - stop_distance) / full_speed_distance, 0.f, 1.f);

    const florrbt::Vector2f forward = delta / len;
    const florrbt::Vector2f left(-forward.y, forward.x);
    const float wave_range = std::max(game_config::mob_bee_wave_near_range_min,
                                      Radius() * game_config::mob_bee_wave_near_range_radius_multiplier);
    const float near_factor = std::clamp((len - Radius()) / wave_range,
                                         game_config::mob_bee_wave_near_factor_min, 1.f);
    const float wave = std::sin(wave_timer * wave_frequency) * game_config::mob_bee_wave_strength * near_factor;
    florrbt::Vector2f desired_dir = forward + left * wave;
    const float desired_length = Length(desired_dir);
    if (!std::isfinite(desired_length) || desired_length <= game_config::entity_collision_epsilon)
        desired_dir = forward;
    else
        desired_dir /= desired_length;

    FacingAngle() = std::atan2(desired_dir.y, desired_dir.x);
    HasFacing() = true;
    m_vel = florrbt::StepVelocityTowards(m_vel, desired_dir * max_velocity, acceleration, dt);
}

void CMobBase::MoveTowards(const florrbt::Vector2f& target_pos, float dt, float target_radius,
                          float full_speed_distance)
{
    const SMobStats* stats = GetFinalStats();
    if (!stats) return;
    if (!std::isfinite(dt) || dt <= 0.f) return;
    if (!std::isfinite(Pos().x) || !std::isfinite(Pos().y) || !std::isfinite(target_pos.x) ||
        !std::isfinite(target_pos.y))
    {
        m_vel = { 0.f, 0.f };
        return;
    }
    if (!std::isfinite(m_vel.x) || !std::isfinite(m_vel.y)) m_vel = { 0.f, 0.f };
    const florrbt::Vector2f effective_target_pos = ApplyMovementTargetStates(target_pos);
    if (!std::isfinite(effective_target_pos.x) || !std::isfinite(effective_target_pos.y))
    {
        m_vel = { 0.f, 0.f };
        return;
    }

    float speed_multiplier = GetMobMovementSpeedMultiplier(this);
    if (!std::isfinite(speed_multiplier)) speed_multiplier = 0.f;
    float max_velocity = MultiplyFiniteNonNegative(stats->max_velocity, speed_multiplier);
    // 移动修正只影响目标速度，保留原加速度，使减速不额外拖慢转向与制动。
    const float acceleration = std::isfinite(stats->acceleration) ? std::max(0.f, stats->acceleration) : 0.f;

    florrbt::Vector2f delta = effective_target_pos - Pos();
    float len = Length(delta);
    const bool analog = std::isfinite(full_speed_distance) && full_speed_distance > 0.f;
    const float stop_distance = analog && target_radius < 0.f ? 0.f : Radius() + std::max(0.f, target_radius);
    if (len <= stop_distance)
    {
        m_vel = florrbt::StepVelocityTowards(m_vel, {}, acceleration, dt);
        if (LengthSq(m_vel) <= game_config::mob_stop_velocity_epsilon) m_vel = { 0.f, 0.f };
        return;
    }

    if (analog) max_velocity *= std::clamp((len - stop_distance) / full_speed_distance, 0.f, 1.f);

    float target_angle = std::atan2(delta.y, delta.x);
    bool facing_locked = IsFacingLocked();
    if (stats->turn_speed > 0.f)
    {
        if (!HasFacing() && !facing_locked)
        {
            if (LengthSq(m_vel) > game_config::entity_collision_epsilon) FacingAngle() = std::atan2(m_vel.y, m_vel.x);
            else FacingAngle() = target_angle;
            HasFacing() = true;
        }

        if (!facing_locked)
        {
            float angle_delta =
                std::atan2(std::sin(target_angle - FacingAngle()), std::cos(target_angle - FacingAngle()));
            float max_turn = stats->turn_speed * dt;
            angle_delta = std::clamp(angle_delta, -max_turn, max_turn);
            FacingAngle() += angle_delta;
        }

        florrbt::Vector2f forward(std::cos(FacingAngle()), std::sin(FacingAngle()));
        // 朝向只约束驱动方向，不能抹掉外部推挤产生的侧向或反向速度。
        m_vel = florrbt::StepVelocityTowards(m_vel, forward * max_velocity, acceleration, dt);
        return;
    }

    if (!facing_locked)
    {
        FacingAngle() = target_angle;
        HasFacing() = true;
    }

    const florrbt::Vector2f forward(std::cos(FacingAngle()), std::sin(FacingAngle()));
    florrbt::Vector2f desired_vel = forward * max_velocity;
    m_vel = florrbt::StepVelocityTowards(m_vel, desired_vel, acceleration, dt);
}

bool CMobBase::RemoveState(CState* state, EStateApplyTiming timing)
{
    if (!state) return false;
    const auto found = std::find_if(m_states.begin(), m_states.end(), [state](const auto& owned) { return owned.get() == state; });
    if (found == m_states.end()) return false;
    const auto source = state->EffectSourceId();
    if (timing == EStateApplyTiming::EventBoundary && GameWorld() && GameWorld()->CombatEvents().QueueRemoval(*this, source)) return true;
    return RemoveStateImmediate(source);
}

bool CMobBase::RemoveStateImmediate(std::uint64_t source)
{
    for (auto it = m_states.begin(); it != m_states.end(); ++it)
    {
        if (*it && (*it)->EffectSourceId() == source)
        {
            CState* state = it->get();
            RevokeStateEffects(state->EffectSourceId());
            UnregisterMovementTargetState(state);
            std::unique_ptr<CState> removed = std::move(*it);
            m_states.erase(it);
            removed.reset();
            if (GameWorld()) GameWorld()->CombatEvents().StateChanged(*this);
            return true;
        }
    }
    return false;
}

const CMobPrototype* FindMobPrototype(EMobType type)
{
    return GetMobRegistry().Find(type);
}

std::unique_ptr<CMobBase> CreateMob(EMobType type, CGameWorld* world, florrbt::Vector2f pos, ERarity rarity,
                                    bool resolve_special_rarity, bool naturally_spawned)
{
    const CMobPrototype* prototype = FindMobPrototype(type);
    if (!prototype || !prototype->m_factory) return nullptr;
    if (resolve_special_rarity && world && prototype->m_rarity_resolver)
        rarity = prototype->m_rarity_resolver(*world, pos, rarity);
    std::unique_ptr<CMobBase> mob = prototype->m_factory(world, pos, rarity);
    if (mob) mob->SetNaturallySpawned(naturally_spawned);
    return mob;
}

CPickupController* CMobBase::GetPickupController()
{
    return dynamic_cast<CPickupController*>(GetController());
}

void CMobBase::SetController(std::unique_ptr<IController> controller)
{
    // 控制器销毁时释放其携带关系；黄蜂等由生物持有的技能可跨附身保留。
    if (auto* pickup = dynamic_cast<CPickupController*>(m_p_controller.get())) pickup->DropCarried(this);
    m_suspended_controller.reset();
    m_control_return_equipment.reset();
    m_has_control_lease = false;
    m_p_controller = std::move(controller);
    if (Id() >= 0) SynchronizeEnvironment();
}

std::unique_ptr<SControlEquipment> CMobBase::CaptureControlEquipment() const
{
    const auto* flower = dynamic_cast<const CFlower*>(this);
    if (!flower || dynamic_cast<const CPlayerFlower*>(flower)) return {};
    auto result = std::make_unique<SControlEquipment>();
    result->capacity = flower->GetPetalSlotCapacity();
    for (const auto& slot : flower->GetSlots())
        result->slots.push_back({slot.m_p_proto ? static_cast<uint8_t>(slot.m_p_proto->m_type) : uint8_t{0},
            static_cast<uint8_t>(slot.m_stored_rarity), slot.m_available, slot.IsPolicyBanned()});
    return result;
}

void CMobBase::AcquirePlayerControl(std::unique_ptr<IController> controller, std::unique_ptr<SControlEquipment> equipment)
{
    if (!controller || !controller->IsPlayerControlled()) return;
    if (auto* pickup = dynamic_cast<CPickupController*>(m_p_controller.get())) pickup->DropCarried(this);
    auto previous = std::move(m_p_controller);
    const int team = BaseTeam();
    SetController(std::move(controller));
    if (previous && !previous->IsPlayerControlled()) m_suspended_controller = std::move(previous);
    m_control_return_equipment = std::move(equipment);
    m_has_control_lease = true;
    m_control_return_team = team;
}

void CMobBase::ReleasePlayerControl(std::unique_ptr<IController> default_ai)
{
    if (IsMarkedForDes()) return;
    if (IsDead()) { MarkForDestroy(EEntityRemovalReason::Defeated); return; }
    const auto* body = dynamic_cast<const IMultiSegmentBodyMob*>(this);
    const auto* root = body ? ResolveMultiSegmentRoot(GameWorld(), body->RootHandle()) : nullptr;
    const auto* assembly = dynamic_cast<const IMultiSegmentRootMob*>(root);
    const bool attached_part = assembly && std::any_of(assembly->GetSegmentReferences().begin(), assembly->GetSegmentReferences().end(),
        [&](const auto& part) { return part.index == body->SegmentIndex() && part.entity_ref.RefersTo(GetHandle()); });
    if ((!default_ai || default_ai->IsPlayerControlled()) && !attached_part)
    {
        // 无自主 AI 的身体按非击杀原因移除，不触发经验或掉落。
        AddTag(EEntityTag::NoDefeatRewards);
        MarkForDestroy(EEntityRemovalReason::Replaced);
        SetController(nullptr);
        return;
    }
    const bool leased = m_has_control_lease;
    const int team = m_control_return_team;
    auto equipment = std::move(m_control_return_equipment);
    auto resumed = m_suspended_controller ? std::move(m_suspended_controller) : std::move(default_ai);
    // 兼容旧存档中的单体节附身：恢复跟随根的状态，不能因没有独立 AI 而拆掉体节。
    if (attached_part && resumed && resumed->IsPlayerControlled()) resumed.reset();
    SetController(std::move(resumed));
    if (leased) SetTeam(team);
    if (auto* flower = dynamic_cast<CFlower*>(this); flower && equipment)
    {
        flower->DestroyPetalEntities();
        for (size_t i = 0; i < flower->GetSlots().size(); ++i) flower->ForceUnequipPetal(static_cast<int>(i));
        flower->SetPetalSlotCapacity(equipment->capacity);
        flower->SetPetalSlotCount(static_cast<int>(equipment->slots.size()));
        for (size_t i = 0; i < equipment->slots.size(); ++i)
        {
            const auto& slot = equipment->slots[i];
            if (slot.type) flower->LoadPetalSlot(static_cast<int>(i), FindPetalPrototype(static_cast<EPetalType>(slot.type)), static_cast<ERarity>(slot.rarity));
        }
        for (size_t i = 0; i < equipment->slots.size(); ++i)
        {
            flower->GetSlots()[i].m_available = equipment->slots[i].available;
            flower->SetBanned(equipment->slots[i].policy_banned, static_cast<int>(i));
        }
        flower->MarkFinalStatsDirty(); flower->RebuildFinalStats();
    }
    if (m_p_controller) m_p_controller->OnWorldChanged(this);
}

bool CMobBase::RestorePlayerControlLease(bool active, int team, std::unique_ptr<IController> controller, std::string& error,
                                        std::unique_ptr<SControlEquipment> equipment)
{
    if ((active && !IsPlayerControlled()) || (controller && (!active || controller->IsPlayerControlled())))
    {
        error = "Invalid suspended AI/player control lease";
        return false;
    }
    m_has_control_lease = active;
    m_control_return_team = team;
    m_suspended_controller = std::move(controller);
    m_control_return_equipment = std::move(equipment);
    return true;
}

void CMobBase::IntegrateMovement(float dt)
{
    const auto start = Pos();
    Pos() += m_vel * dt;
    // 先处理实墙再判断密道接触，避免被墙拒绝的路径误清空已加载花瓣。
    if (IsPlayerControlled() && GameWorld() && GameWorld()->IsTicking())
        GameWorld()->ConstrainControlledMotion(*this, start);
}

void CMobBase::SynchronizeEnvironment(std::optional<florrbt::Vector2f> movement_start)
{
    if (auto* world = GameWorld())
    {
        world->Environment().Synchronize(*world, *this, movement_start);
        m_environment_sample_position = Pos();
        m_environment_sample_world = world->GetInstanceGeneration();
    }
}

void CMobBase::SynchronizeEnvironmentAfterDisplacement()
{
    std::optional<florrbt::Vector2f> from;
    if (GameWorld() && m_environment_sample_world == GameWorld()->GetInstanceGeneration())
        from = m_environment_sample_position;
    SynchronizeEnvironment(from);
}

void CMobBase::SetShortcutContact(bool touching)
{
    if (touching && !IsInShortcut()) m_shortcut_probe_radius = EnvironmentProbeRadius();
    if (!touching) m_shortcut_probe_radius = 0.f;
    if (m_capabilities.Set(ERestrictionSource::Shortcut, CShortcutRule::Restrictions(true, touching)))
        OnEnvironmentChanged();
}

const CPickupController* CMobBase::GetPickupController() const
{
    return dynamic_cast<const CPickupController*>(GetController());
}
