#include "mob.h"
#include "drop.h"
#include "projectile.h"
#include "petals/petal.h"
#include "../controllers/melee_controller.h"
#include "../gamecontext.h"
#include "../gameworld.h"
#include "../player.h"
#include "../states/states.h"
#include "../../../Shared/game_config.h"
#include <algorithm>
#include <cmath>
#include <unordered_set>

CMobBase::~CMobBase() = default;

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
        if (entity->m_id >= 0 && !visited_ids.insert(entity->m_id).second) return false;

        if (auto* mob = dynamic_cast<CMobBase*>(entity))
        {
            if (!mob->FindStates<CUndeadState>().empty()) return true;

            auto* controller = dynamic_cast<CSummonedMeleeController*>(mob->GetController());
            CGameWorld* world = mob->GameWorld();
            if (controller && world)
            {
                entity = world->GetEntity(controller->GetOwnerId());
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

void CMobBase::AddState(std::unique_ptr<CState> state)
{
    if (state) m_states.push_back(std::move(state));
}

void CMobBase::TickStates(float dt)
{
    for (auto it = m_states.begin(); it != m_states.end();)
    {
        (*it)->Tick(dt);
        if ((*it)->m_timer != endless && (*it)->m_timer <= 0.0f)
        {
            it = m_states.erase(it);
        } else {
            ++it;
        }
    }
}

void CMobBase::TickPetalContacts()
{
    CGameWorld* world = GameWorld();
    const SMobStats* stats = GetFinalStats();
    if (!world || !stats) return;

    float max_petal_radius = std::max({game_config::default_petal_radius, game_config::default_goldenleaf_base_radius,
                                       game_config::default_yinyang_base_radius, game_config::default_basic_base_radius,
                                       game_config::default_beetleegg_base_radius, game_config::default_antegg_base_radius,
                                       game_config::default_bubble_base_radius, game_config::default_cogwheel_base_radius,
                                       game_config::default_dust_base_radius, game_config::default_iris_base_radius,
                                       game_config::default_pincer_base_radius, game_config::default_rose_base_radius,
                                       game_config::default_yggdrasil_base_radius});
    float query_radius = m_radius + max_petal_radius;
    auto petals = world->GetSpatialGrid().QueryRange(m_pos, query_radius, [this](const CEntity* entity)
    {
        if (!entity || entity->m_is_marked_for_des || entity == this) return false;
        if (entity->IsDead() || !entity->CanCollide()) return false;
        if (CheckTeam(entity->m_team, m_team)) return false;
        if (BlocksNullifiedInteraction(this, entity)) return false;
        return dynamic_cast<const CPetal*>(entity) != nullptr;
    });

    for (CEntity* entity : petals)
    {
        auto* petal = dynamic_cast<CPetal*>(entity);
        if (!petal || petal->m_is_marked_for_des) continue;
        if (BlocksNullifiedInteraction(this, petal)) continue;
        if (!IsCollision(*petal)) continue;
        petal->TakeDamage(stats->damage, this, EDamageType::Normal);
    }
}

bool CMobBase::TickDropPickup(CPlayer* player)
{
    if (!player || !player->IsAuthenticated() || player->GetAccountName().empty()) return false;
    if (m_is_marked_for_des || IsDead()) return false;

    CGameWorld* world = GameWorld();
    const SMobStats* stats = GetFinalStats();
    if (!world || !stats || stats->max_absorb_range <= 0.f) return false;

    bool picked_any = false;
    std::vector<CEntity*> drops = world->GetSpatialGrid().QueryRange(
        m_pos, stats->max_absorb_range,
        [player](const CEntity* candidate) -> bool
        {
            auto* drop = dynamic_cast<const CDrop*>(candidate);
            return drop && drop->CanBePickedUpBy(player->GetId());
        });

    for (CEntity* candidate : drops)
    {
        auto* drop = dynamic_cast<CDrop*>(candidate);
        if (!drop) continue;
        picked_any = drop->PickUpTo(player->GetAccountName(), player->GetId()) || picked_any;
    }

    return picked_any;
}

void CMobBase::ApplyDamageDirect(float dmg, CEntity* attacker)
{
    if (dmg <= 0.f) return;
    if (!FindStates<CInvincibleState>().empty()) return;
    if (!FindStates<CUndeadState>().empty())
    {
        m_health = std::max(1.f, m_health);
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
        } else {
            it->m_total_dmg += dmg;
            it->ResetTimer();
        }
    }

    m_health -= dmg;
    if (m_p_controller) m_p_controller->OnDamaged(this, attacker);
    if (m_health <= 0.f)
    {
        m_health = 0.f;
        m_is_marked_for_des = true;
    }
}

void CMobBase::MoveTowards(const sf::Vector2f& target_pos, float dt)
{
    const SMobStats* stats = GetFinalStats();
    if (!stats) return;

    float speed_multiplier = GetPincerSpeedMultiplier(this) * game_config::mob_velocity_multiplier;
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

    sf::Vector2f delta = target_pos - m_pos;
    float len = Length(delta);
    if (len <= m_radius)
    {
        m_vel *= game_config::mob_stop_damping;
        if (LengthSq(m_vel) <= game_config::mob_stop_velocity_epsilon) m_vel = {0.f, 0.f};
        return;
    }

    float target_angle = std::atan2(delta.y, delta.x);
    bool facing_locked = IsFacingLocked();
    if (stats->turn_speed > 0.f)
    {
        if (!m_has_facing && !facing_locked)
        {
            if (LengthSq(m_vel) > game_config::entity_collision_epsilon)
                m_facing_angle = std::atan2(m_vel.y, m_vel.x);
            else
                m_facing_angle = target_angle;
            m_has_facing = true;
        }

        if (!facing_locked)
        {
            float angle_delta = std::atan2(std::sin(target_angle - m_facing_angle), std::cos(target_angle - m_facing_angle));
            float max_turn = stats->turn_speed * dt;
            angle_delta = std::clamp(angle_delta, -max_turn, max_turn);
            m_facing_angle += angle_delta;
        }

        sf::Vector2f forward(std::cos(m_facing_angle), std::sin(m_facing_angle));
        float speed = Length(m_vel);
        speed = std::min(max_velocity, speed + acceleration * dt);
        m_vel = forward * speed;
        return;
    }

    if (!facing_locked)
    {
        m_facing_angle = target_angle;
        m_has_facing = true;
    }

    sf::Vector2f desired_vel = delta / len * max_velocity;
    sf::Vector2f diff = desired_vel - m_vel;
    float diff_len = Length(diff);
    float max_accel = acceleration * dt;

    if (diff_len <= max_accel)
    {
        m_vel = desired_vel;
    } else {
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
            m_states.erase(it);
            return true;
        }
    }
    return false;
}

const CMobPrototype* FindMobPrototype(EMobType type)
{
    auto it = g_mob_registry.find(type);
    if (it == g_mob_registry.end()) return nullptr;
    return it->second.get();
}

std::unique_ptr<CMobBase> CreateMob(EMobType type, CGameWorld* world, sf::Vector2f pos, ERarity rarity)
{
    const CMobPrototype* prototype = FindMobPrototype(type);
    if (!prototype || !prototype->m_factory) return nullptr;
    return prototype->m_factory(world, pos, rarity);
}

void RegisterMobs()
{
    RegisterBeetle();
    RegisterBandageBeetle();
    RegisterNormalLadybug();
    RegisterNormalFlower();
    RegisterPlayerFlower();
    RegisterSoldierAnt();
    RegisterSoldierFireAnt();
    RegisterSoldierTermite();
    RegisterSummonedBeetle();
    RegisterSummonedSoldierAnt();
    RegisterBee();
    RegisterHornet();
}
