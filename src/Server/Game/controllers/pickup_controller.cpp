#include "pickup_controller.h"
#include "../../HotReload/snapshot_archive.h"
#include "../../../Shared/tools.h"
#include "../entity.h"
#include "../entities/mob.h"
#include "../gameworld.h"
#include <algorithm>
#include <cmath>

CEntity* CPickupController::GetCarried(const CMobBase* mob) const
{
    if (!mob || !mob->GameWorld() || mob->IsMarkedForDes() || mob->IsDead() || !m_carried_ref.IsValid())
        return nullptr;

    CGameWorld* world = mob->GameWorld();
    CEntity* entity = world->ResolveEntity(m_carried_ref);
    if (!entity || entity->IsMarkedForDes() || entity->IsDead()) return nullptr;

    // Legacy snapshots did not carry world identity. ResolveEntity can still
    // safely find the entity by id+generation, but retaining that incomplete
    // key would make the carried-entity registry (which uses canonical
    // handles) impossible to unregister later. Canonicalize lazily because a
    // controller may be restored before the referenced entity is inserted.
    if (!m_carried_ref.HasWorldIdentity()) m_carried_ref = entity->GetHandle();
    return entity;
}

bool CPickupController::IsCarrying(const CMobBase* mob, const CEntity* carried) const
{
    return carried && GetCarried(mob) == carried;
}

bool CPickupController::TryPickup(CMobBase* mob, CEntity* candidate)
{
    if (!mob || !candidate || m_carry_pickup_timer > 0.f) return false;
    if (GetCarried(mob)) return false;
    if (!CanCarry(mob, candidate)) return false;
    if (candidate->GameWorld() != mob->GameWorld()) return false;

    const float contact_radius = std::max(0.f, mob->Radius()) + std::max(0.f, candidate->Radius()) +
                                 std::max(0.f, game_config::entity_collision_epsilon);
    const float reach = std::max(CarryContactRadius(mob, candidate), contact_radius);
    if (DistanceSq(mob->Pos(), candidate->Pos()) > reach * reach) return false;

    if (!mob->GameWorld()->TryRegisterCarriedEntity(*candidate, *mob)) return false;

    m_carried_ref = candidate->GetHandle();
    m_carry_pickup_timer = std::max(0.f, CarryPickupCooldown());
    OnCarryChanged(mob, candidate, true);
    return true;
}

void CPickupController::DropCarried(CMobBase* mob)
{
    // Teardown must work even after the carrier/target is marked or dead.
    // GetCarried intentionally excludes those for gameplay target selection.
    CEntity* carried = mob && mob->GameWorld() ? mob->GameWorld()->ResolveEntity(m_carried_ref) : nullptr;
    OnCarryChanged(mob, carried, false);
    if (mob && mob->GameWorld())
        mob->GameWorld()->UnregisterCarriedEntity(carried ? carried->GetHandle() : m_carried_ref, mob->GetHandle());
    m_carried_ref = {};
}

void CPickupController::TickCarried(CMobBase* mob, float dt)
{
    m_carry_pickup_timer = std::max(0.f, m_carry_pickup_timer - std::max(0.f, dt));
    if (!mob || !mob->GameWorld()) return;

    CEntity* carried = GetCarried(mob);
    if (!carried)
    {
        DropCarried(mob);
        return;
    }
    if (!mob->GameWorld()->TryRegisterCarriedEntity(*carried, *mob)) DropCarried(mob);
}

void CPickupController::SyncCarriedTransform(CMobBase* mob)
{
    if (!mob || mob->IsMarkedForDes() || mob->IsDead()) return;

    CEntity* carried = GetCarried(mob);
    if (!carried) return;

    const float angle = mob->HasFacing() ? mob->FacingAngle() : 0.f;
    const florrbt::Vector2f forward(std::cos(angle), std::sin(angle));
    const float contact_distance = std::max(0.f, mob->Radius()) + std::max(0.f, carried->Radius());
    carried->Pos() = mob->Pos() + forward * contact_distance;
    if (auto* carried_mob = dynamic_cast<CMobBase*>(carried)) carried_mob->m_vel = { 0.f, 0.f };
}

void CPickupController::ResolveCarriedConstraint(CMobBase* mob)
{
    if (!mob || mob->IsMarkedForDes() || mob->IsDead() || !mob->GameWorld()) return;

    CEntity* carried = GetCarried(mob);
    auto* carried_mob = dynamic_cast<CMobBase*>(carried);
    if (!carried || !carried_mob) return;

    const float angle = mob->HasFacing() ? mob->FacingAngle() : 0.f;
    const florrbt::Vector2f forward(std::cos(angle), std::sin(angle));
    const float contact_distance = std::max(0.f, mob->Radius()) + std::max(0.f, carried->Radius());
    const florrbt::Vector2f desired_carried_pos = mob->Pos() + forward * contact_distance;
    const florrbt::Vector2f error = carried->Pos() - desired_carried_pos;
    const float error_sq = LengthSq(error);
    const float epsilon = std::max(0.f, game_config::entity_collision_epsilon);
    carried_mob->m_vel = { 0.f, 0.f };
    if (error_sq <= epsilon * epsilon)
    {
        carried->Pos() = desired_carried_pos;
        return;
    }

    auto inverse_mass = [epsilon](float mass) {
        if (!std::isfinite(mass)) return 0.f;
        return mass > epsilon ? 1.f / mass : 1.f;
    };
    const float carrier_inverse_mass = inverse_mass(mob->Mass());
    const float carried_inverse_mass = inverse_mass(carried->Mass());
    const float inverse_mass_sum = carrier_inverse_mass + carried_inverse_mass;
    const float carrier_weight = inverse_mass_sum > epsilon ? carrier_inverse_mass / inverse_mass_sum : 0.5f;
    const float carried_weight = inverse_mass_sum > epsilon ? carried_inverse_mass / inverse_mass_sum : 0.5f;

    CGameWorld* world = mob->GameWorld();
    auto is_clear = [world](florrbt::Vector2f pos, float radius) {
        return !world->CircleBlockedByWall(pos, std::max(0.f, radius));
    };

    const florrbt::Vector2f weighted_carrier_pos = mob->Pos() + error * carrier_weight;
    const florrbt::Vector2f weighted_carried_pos = carried->Pos() - error * carried_weight;
    const bool weighted_carrier_blocked = !is_clear(weighted_carrier_pos, mob->WallCollisionRadius());
    const bool weighted_carried_blocked = !is_clear(weighted_carried_pos, carried->WallCollisionRadius());
    bool carried_wall_pushed_carrier = false;

    if (!weighted_carrier_blocked && !weighted_carried_blocked)
    {
        mob->Pos() = weighted_carrier_pos;
        carried->Pos() = weighted_carried_pos;
    } else if (weighted_carried_blocked && is_clear(mob->Pos() + error, mob->WallCollisionRadius()))
    {
        mob->Pos() += error;
        carried_wall_pushed_carrier = true;
    } else if (weighted_carrier_blocked &&
               is_clear(carried->Pos() - error, carried->WallCollisionRadius()))
    {
        carried->Pos() -= error;
    } else if (is_clear(mob->Pos() + error, mob->WallCollisionRadius()))
    {
        mob->Pos() += error;
        carried_wall_pushed_carrier = weighted_carried_blocked;
    } else if (is_clear(carried->Pos() - error, carried->WallCollisionRadius()))
    {
        carried->Pos() -= error;
    } else
    {
        return;
    }

    if (carried_wall_pushed_carrier)
    {
        const float error_length = std::sqrt(error_sq);
        const florrbt::Vector2f wall_normal = error / error_length;
        const float velocity_into_wall = mob->m_vel.x * wall_normal.x + mob->m_vel.y * wall_normal.y;
        if (velocity_into_wall < 0.f) mob->m_vel -= wall_normal * velocity_into_wall;
    }
}

void CPickupController::ApplyCarryingTurn(CMobBase* mob, float previous_angle, bool had_facing, float dt) const
{
    if (!mob || !had_facing || !mob->HasFacing()) return;

    const float desired_angle = mob->FacingAngle();
    float angle_delta = std::atan2(std::sin(desired_angle - previous_angle), std::cos(desired_angle - previous_angle));
    const float max_turn =
        std::max(0.f, game_config::mob_leafcutter_soldier_leaf_piece_turn_speed) * std::max(0.f, dt);
    angle_delta = std::clamp(angle_delta, -max_turn, max_turn);
    mob->FacingAngle() = std::atan2(std::sin(previous_angle + angle_delta), std::cos(previous_angle + angle_delta));

    const float speed = Length(mob->m_vel);
    if (speed > game_config::entity_collision_epsilon)
        mob->m_vel = florrbt::Vector2f(std::cos(mob->FacingAngle()), std::sin(mob->FacingAngle())) * speed;
}

void CPickupController::CaptureCarriedSnapshot(CSnapshotWriter& writer) const
{
    writer.Field("carried_world_id", m_carried_ref.world_id);
    writer.Field("carried_id", m_carried_ref.id);
    writer.UInt64("carried_generation", m_carried_ref.generation);
    writer.UInt64("carried_world_instance_generation", m_carried_ref.world_instance_generation);
    writer.Field("carry_pickup_timer", m_carry_pickup_timer);
}

void CPickupController::RestoreCarriedSnapshot(const CSnapshotReader& reader)
{
    m_carried_ref.world_id = reader.UInt32("carried_world_id");
    m_carried_ref.id = reader.Int("carried_id", -1);
    m_carried_ref.generation = reader.UInt64("carried_generation");
    m_carried_ref.world_instance_generation = reader.UInt64("carried_world_instance_generation");
    m_carry_pickup_timer = std::max(0.f, reader.Float("carry_pickup_timer"));
}

void CPickupController::DropCarriedFromWorld(CMobBase* mob, CGameWorld& source, const SEntityHandle& old_carrier)
{
    CEntity* carried = source.ResolveEntity(m_carried_ref);
    OnCarryChanged(mob, carried, false);
    source.UnregisterCarriedEntity(carried ? carried->GetHandle() : m_carried_ref, old_carrier);
    m_carried_ref = {};
}

float CPickupController::CarryContactRadius(const CMobBase* mob, const CEntity* carried) const
{
    (void)mob;
    (void)carried;
    return 0.f;
}

float CPickupController::CarryPickupCooldown() const
{
    return 0.f;
}

void CPickupController::OnCarryChanged(CMobBase* mob, CEntity* carried, bool now_carrying)
{
    (void)mob;
    (void)carried;
    (void)now_carrying;
}
