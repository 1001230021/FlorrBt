#include "mob.h"
#include "../../../Shared/game_config.h"
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
#include <unordered_set>

CMobBase::~CMobBase()
{
    for (auto& state : m_states)
        if (state) state->m_p_owner = nullptr;
}

void CMobBase::ClearStatesForRestore()
{
    for (auto& state : m_states)
        if (state) state->m_p_owner = nullptr;
    m_states.clear();
    m_movement_target_states.clear();
}

template <typename TStats> CMob<TStats>::~CMob() = default;

template class CMob<SMobStats>;
template class CMob<SFlowerStats>;

namespace
{
bool IsUndeadDamageSource(CEntity* entity)
{
    std::unordered_set<int> visited_ids;

    while (entity)
    {
        if (entity->Id() >= 0 && !visited_ids.insert(entity->Id()).second) return false;

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
        if (!drop || !drop->CanBePickedUpBy(player->GetId())) return;
        picked_any = drop->PickUpTo(*player) || picked_any;
    });

    return picked_any;
}

void CMobBase::ApplyDamageDirect(float dmg, CEntity* attacker, EDamageType damage_type)
{
    if (dmg <= 0.f) return;
    if (HasState<CInvincibleState>()) return;
    if (HasState<CUndeadState>())
    {
        Health() = std::max(1.f, Health());
        return;
    }

    CGameContext* context = GameContext();
    CPlayer* player = context && !IsUndeadDamageSource(attacker) ? context->FindPlayerFromEntity(attacker) : nullptr;
    if (player)
    {
        auto it = std::find_if(m_damage_data.begin(), m_damage_data.end(),
                               [player](const CDamageData& data) { return data.m_player == player; });
        if (it == m_damage_data.end())
        {
            CDamageData data;
            data.m_player = player;
            data.m_total_dmg = dmg;
            data.ResetTimer();
            m_damage_data.push_back(data);
        } else
        {
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

void CMobBase::MoveTowards(const florrbt::Vector2f& target_pos, float dt, float target_radius,
                          float full_speed_distance)
{
    const SMobStats* stats = GetFinalStats();
    if (!stats) return;
    const florrbt::Vector2f effective_target_pos = ApplyMovementTargetStates(target_pos);

    float speed_multiplier = GetPincerSpeedMultiplier(this) * game_config::mob_velocity_multiplier;
    if (HasState<CDiggingState>()) speed_multiplier *= game_config::mob_digging_speed_multiplier;
    float max_velocity = stats->max_velocity * speed_multiplier;
    float acceleration = stats->acceleration * speed_multiplier;

    float current_speed_sq = LengthSq(m_vel);
    float max_speed_sq = max_velocity * max_velocity;
    if (current_speed_sq > max_speed_sq && game_config::mob_slow_to_max_velocity_time > 0.f)
    {
        float current_speed = std::sqrt(current_speed_sq);
        float slow_amount = (current_speed - max_velocity) * dt / game_config::mob_slow_to_max_velocity_time;
        float next_speed = std::max(max_velocity, current_speed - slow_amount);
        m_vel *= next_speed / current_speed;
    }

    florrbt::Vector2f delta = effective_target_pos - Pos();
    float len = Length(delta);
    const bool analog = std::isfinite(full_speed_distance) && full_speed_distance > 0.f;
    const float stop_distance = analog && target_radius < 0.f ? 0.f : Radius() + std::max(0.f, target_radius);
    if (len <= stop_distance)
    {
        m_vel *= game_config::mob_stop_damping;
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
        float speed = Length(m_vel);
        if (analog) speed += std::clamp(max_velocity - speed, -acceleration * dt, acceleration * dt);
        else speed = std::min(max_velocity, speed + acceleration * dt);
        m_vel = forward * speed;
        return;
    }

    if (!facing_locked)
    {
        FacingAngle() = target_angle;
        HasFacing() = true;
    }

    florrbt::Vector2f desired_vel = delta / len * max_velocity;
    florrbt::Vector2f diff = desired_vel - m_vel;
    float diff_len = Length(diff);
    float max_accel = acceleration * dt;

    if (diff_len <= max_accel)
    {
        m_vel = desired_vel;
    } else
    {
        m_vel += diff / diff_len * max_accel;
    }
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
