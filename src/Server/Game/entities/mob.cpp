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
} // namespace

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
    // A hard-constrained chain is one compound mob. If a body disappears for
    // any reason other than the root's own cleanup, retire the root as well so
    // later segments never survive as an orphaned partial chain.
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
        // A root marker without a registered body layout is a valid empty
        // layout, not an instruction to retry every tick.
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

        segment->Team() = root.Team();
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

    // First discard root references that do not point to the expected body.
    // Snapshots can legitimately contain a root whose body was marked for
    // removal immediately before capture; that should prune the stale link,
    // not make the whole world restore fail.
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

    // Then rebuild the reverse direction from bodies.  A body with a missing
    // root is destroyed safely; it never keeps a dangling raw pointer.
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

    // Canonicalise the forward body links from the validated root list. A
    // snapshot may contain a stale next handle if the next body was removed
    // immediately before capture; rebuilding here closes that dangling edge.
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

    // Old snapshots have no multi-segment fields.  In that case a declared
    // root is restored as NotStarted and can be assembled exactly once here.
    // New snapshots carry Complete/Failed and therefore are never duplicated.
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
        // The root list owns the body entities, while each body records the
        // next edge in the actual chain. Requiring both views to agree makes
        // the next handle part of the runtime invariant instead of passive
        // snapshot metadata, and closes cycles/cross-root links safely.
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
        if (state) state->DetachOwner();
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
} // namespace

bool ShouldBlockDiggingDamage(CMobBase* receiver, CEntity* attacker, EDamageType dmg_type)
{
    return receiver && receiver->HasState<CDiggingState>() && BaseDamageType(dmg_type) != EDamageType::Poison &&
           !IsDiggingEntity(attacker);
}

void CMobBase::AddState(std::unique_ptr<CState> state)
{
    if (!state) return;
    CState* raw = state.get();
    // A state may be assembled by a helper before it is attached. Bind the
    // lifetime token at the ownership boundary so its resolver never relies
    // on the caller having passed the same mob twice.
    raw->RebindOwner(this);
    m_states.push_back(std::move(state));
    if (raw->AffectsMovementTarget()) m_movement_target_states.push_back(raw);
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

void CMobBase::TickStates(float dt)
{
    for (size_t index = 0; index < m_states.size();)
    {
        CState* state = m_states[index].get();
        if (!state)
        {
            m_states.erase(m_states.begin() + static_cast<std::ptrdiff_t>(index));
            continue;
        }

        state->Tick(dt);
        if (IsDead()) return;
        if (index >= m_states.size() || m_states[index].get() != state) continue;

        if (state->m_timer != endless && state->m_timer <= 0.0f)
        {
            UnregisterMovementTargetState(state);
            std::unique_ptr<CState> expired = std::move(m_states[index]);
            m_states.erase(m_states.begin() + static_cast<std::ptrdiff_t>(index));
            expired.reset();
            if (IsDead()) return;
        } else
        {
            ++index;
        }
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
                                   return data.m_account_name == player->GetAccountName() &&
                                       (data.m_progress_account == player->GetProgressAccountName() ||
                                        (data.m_progress_account.empty() && !player->HasIsolatedProgress()));
                               });
        if (it == m_damage_data.end())
        {
            CDamageData data;
            data.m_account_name = player->GetAccountName();
            data.m_progress_account = player->GetProgressAccountName();
            data.m_total_dmg = dmg;
            data.ResetTimer();
            m_damage_data.push_back(data);
        } else
        {
            it->m_progress_account = player->GetProgressAccountName();
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
    // Movement modifiers change the requested maximum speed only. The mob's
    // own acceleration remains its base stat, so a slow does not also make it
    // turn or brake sluggishly.
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
        // Facing limits the desired drive direction, not the actual velocity.
        // Snapping m_vel to forward erased sideways/backwards crowd impulses.
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

bool CMobBase::RemoveState(CState* state)
{
    if (!state) return false;

    for (auto it = m_states.begin(); it != m_states.end(); ++it)
    {
        if (it->get() == state)
        {
            UnregisterMovementTargetState(state);
            std::unique_ptr<CState> removed = std::move(*it);
            m_states.erase(it);
            removed.reset();
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
    // Controller-owned carry state cannot outlive the controller. Mob-owned
    // skills (Hornet) deliberately survive possession and are not dropped here.
    if (auto* pickup = dynamic_cast<CPickupController*>(m_p_controller.get())) pickup->DropCarried(this);
    m_p_controller = std::move(controller);
    if (Id() >= 0) SynchronizeEnvironment();
}

void CMobBase::IntegrateMovement(float dt)
{
    const auto start = Pos();
    Pos() += m_vel * dt;
    // Resolve ordinary blockers before detecting shortcut entry. Otherwise
    // a proposed movement through a solid wall could discard loaded petals
    // even though the authoritative wall pass later rejects that movement.
    if (IsPlayerControlled() && GameWorld() && GameWorld()->IsTicking())
        GameWorld()->ConstrainControlledMotion(*this, start);
    SynchronizeEnvironment(start);
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
