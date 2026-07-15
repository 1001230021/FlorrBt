#include "states.h"
#include "../controllers/melee_controller.h"
#include "../entities/projectile.h"
#include "../gameworld.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace
{
const CMobBase* GetInteractionMob(const CEntity* entity)
{
    if (!entity) return nullptr;
    if (const auto* mob = dynamic_cast<const CMobBase*>(entity))
    {
        auto* controller = dynamic_cast<CSummonedMeleeController*>(const_cast<CMobBase*>(mob)->GetController());
        if (!controller) return mob;

        CGameWorld* world = const_cast<CMobBase*>(mob)->GameWorld();
        CEntity* owner = world ? world->GetEntity(controller->GetOwnerId()) : nullptr;
        if (const auto* owner_projectile = dynamic_cast<const CProjectile*>(owner))
        {
            CEntity* root_owner = world ? world->GetEntity(owner_projectile->m_owner_id) : nullptr;
            if (const auto* root_mob = dynamic_cast<const CMobBase*>(root_owner)) return root_mob;
        }

        if (const auto* owner_mob = dynamic_cast<const CMobBase*>(owner)) return owner_mob;
        return mob;
    }

    const auto* projectile = dynamic_cast<const CProjectile*>(entity);
    if (!projectile) return nullptr;

    CGameWorld* world = const_cast<CProjectile*>(projectile)->GameWorld();
    CEntity* owner = world ? world->GetEntity(projectile->m_owner_id) : nullptr;
    return dynamic_cast<const CMobBase*>(owner);
}

bool MobNullifiesTarget(const CMobBase* nullified, const CMobBase* target)
{
    if (!nullified || !target) return false;

    const CNullificationState* state = nullified->FindFirstState<CNullificationState>();
    if (!state) return false;

    int null_level = GetLevel(state->m_rarity);
    int target_level = GetLevel(target->GetRarity());
    return target_level <= null_level - game_config::nullification_level_gap;
}
}

CPoisonState::CPoisonState(CMobBase* owner, float timer, float basic_dmg, ERarity rarity, CEntity* applier)
    : CState(owner, timer, rarity), m_basic_dmg(basic_dmg),
      m_applier_id(applier ? applier->m_id : -1),
      m_applier_generation(applier ? applier->m_generation : 0)
{
    CPoisonState* existing = owner ? owner->FindFirstState<CPoisonState>() : nullptr;

    if (!existing)
    {
        m_is_valid = true;
        return;
    }

    bool should_replace = false;
    if (GetLevel(rarity) > GetLevel(existing->m_rarity))
    {
        should_replace = true;
    } else if (GetLevel(rarity) == GetLevel(existing->m_rarity)) {
        if (basic_dmg > existing->m_basic_dmg) should_replace = true;
        else if (basic_dmg == existing->m_basic_dmg && timer > existing->m_timer) should_replace = true;
    }

    if (should_replace)
    {
        owner->RemoveState(existing);
        m_is_valid = true;
    }
}

void CPoisonState::Tick(float dt)
{
    if (!m_is_valid || !m_p_owner) return;

    CEntity* applier = nullptr;
    CGameWorld* world = m_p_owner->GameWorld();
    if (world && m_applier_id >= 0 && m_applier_generation != 0)
        applier = world->GetEntity(m_applier_id, m_applier_generation);

    m_p_owner->TakeDamage(m_basic_dmg * dt, applier, EDamageType::Poison);
    m_timer -= dt;
}

CUndeadState::CUndeadState(CMobBase* owner, float timer, ERarity rarity, int source_slot)
    : CState(owner, timer, rarity), m_source_slot(source_slot)
{
}

CUndeadState::~CUndeadState()
{
    if (!m_kill_on_destroy || !m_p_owner) return;
    if (auto* player_flower = dynamic_cast<CPlayerFlower*>(m_p_owner))
    {
        player_flower->EnterDeathState();
        return;
    }
    m_p_owner->m_health = 0.f;
    m_p_owner->m_is_marked_for_des = true;
}

CCorruptionState::CCorruptionState(CMobBase* owner, float timer, ERarity rarity)
    : CState(owner, timer, rarity)
{
    if (!owner) return;

    m_old_team = owner->m_team;
    for (auto* undead : owner->FindStates<CUndeadState>())
    {
        undead->CancelDeathOnDestroy();
        owner->RemoveState(undead);
    }
    owner->m_team = 0;
}

CCorruptionState::~CCorruptionState()
{
    if (m_p_owner) m_p_owner->m_team = m_old_team;
}

CBanSlotState::CBanSlotState(CMobBase* owner, float timer, int slot, ERarity rarity)
    : CState(owner, timer, rarity), m_slot_index(slot)
{
    if (auto* flower = dynamic_cast<CFlower*>(owner))
    {
        flower->SetBanned(true, slot);
        m_p_flower = flower;
    }
}

CBanSlotState::~CBanSlotState()
{
    if (m_p_flower) m_p_flower->SetBanned(false, m_slot_index);
}

void CBanSlotState::Tick(float dt)
{
    m_timer -= dt;
}

CPincerSpeedReduceState::CPincerSpeedReduceState(CMobBase* owner, float timer, ERarity rarity)
    : CState(owner, timer, rarity)
{
    CPincerSpeedReduceState* existing = owner ? owner->FindFirstState<CPincerSpeedReduceState>() : nullptr;

    if (!existing)
    {
        m_is_valid = true;
        return;
    }

    bool should_replace = false;
    if (GetLevel(rarity) > GetLevel(existing->m_rarity))
    {
        should_replace = true;
    } else if (GetLevel(rarity) == GetLevel(existing->m_rarity) && timer > existing->m_timer) {
        should_replace = true;
    }

    if (should_replace)
    {
        owner->RemoveState(existing);
        m_is_valid = true;
    }
}

CWebSpeedReduceState::CWebSpeedReduceState(CMobBase* owner, float timer, float desired_multiplier)
    : CState(owner, timer, ERarity::Common)
{
    desired_multiplier = std::clamp(desired_multiplier, 0.f, 1.f);
    float reference_mass = std::max(0.01f, game_config::mob_player_flower_mass);
    float mass = owner ? owner->m_mass : reference_mass;
    float normalized_mass = std::isfinite(mass) ? std::max(0.01f, mass / reference_mass)
                                                : std::numeric_limits<float>::infinity();
    m_multiplier = std::clamp(1.f - (1.f - desired_multiplier) / normalized_mass, 0.f, 1.f);
    if (owner && owner->m_mob_type == EMobType::Spider)
        m_multiplier = std::clamp(1.f - (1.f - m_multiplier) * 0.5f, 0.f, 1.f);

    CWebSpeedReduceState* existing = owner ? owner->FindFirstState<CWebSpeedReduceState>() : nullptr;
    if (!existing)
    {
        m_is_valid = true;
        return;
    }

    if (m_multiplier < existing->m_multiplier ||
        (m_multiplier == existing->m_multiplier && timer > existing->m_timer))
    {
        owner->RemoveState(existing);
        m_is_valid = true;
    }
}

CPsionicConnectionState::CPsionicConnectionState(CMobBase* owner, float timer, ERarity rarity)
    : CState(owner, timer, rarity)
{
    CPsionicConnectionState* existing = owner ? owner->FindFirstState<CPsionicConnectionState>() : nullptr;

    if (!existing)
    {
        m_is_valid = true;
        return;
    }

    if (GetLevel(rarity) > GetLevel(existing->m_rarity)) existing->m_rarity = rarity;
    if (existing->m_timer != endless && timer == endless) existing->m_timer = endless;
    else if (existing->m_timer != endless) existing->m_timer = std::max(existing->m_timer, timer);
}

bool BlocksNullifiedInteraction(const CEntity* lhs, const CEntity* rhs)
{
    const CMobBase* lhs_mob = GetInteractionMob(lhs);
    const CMobBase* rhs_mob = GetInteractionMob(rhs);
    return MobNullifiesTarget(lhs_mob, rhs_mob) || MobNullifiesTarget(rhs_mob, lhs_mob);
}

float GetPincerSpeedMultiplier(const CMobBase* mob)
{
    if (!mob) return 1.f;

    int mob_level = GetLevel(mob->GetRarity());
    float multiplier = 1.f;
    mob->ForEachState<CPincerSpeedReduceState>([&](const CPincerSpeedReduceState* state)
    {
        if (!state) return;

        int diff = GetLevel(state->m_rarity) - mob_level;
        if (diff >= 1)
            multiplier = std::min(multiplier, 0.1f);
        else if (diff == 0)
            multiplier = std::min(multiplier, 0.2f);
        else if (diff == -1)
            multiplier = std::min(multiplier, 0.9f);
    });

    mob->ForEachState<CWebSpeedReduceState>([&](const CWebSpeedReduceState* state)
    {
        if (!state) return;
        multiplier = std::min(multiplier, state->GetMultiplier());
    });
    return multiplier;
}

bool TrySharePsionicDamage(CMobBase* receiver, float dmg, CEntity* attacker, EDamageType dmg_type)
{
    if (!receiver || dmg <= 0.f) return false;
    if (!receiver->HasState<CPsionicConnectionState>()) return false;

    CGameWorld* world = receiver->GameWorld();
    if (!world) return false;

    std::vector<CMobBase*> candidates;
    world->GetSpatialGrid().ForEachInRange(receiver->m_pos, game_config::psionic_connection_range, [&](CEntity* entity)
    {
        if (!entity || entity->m_is_marked_for_des) return;
        auto* mob = dynamic_cast<CMobBase*>(entity);
        if (!mob) return;
        if (!CheckTeam(mob->m_team, receiver->m_team)) return;
        if (!mob->HasState<CPsionicConnectionState>()) return;
        candidates.push_back(mob);
    });

    if (candidates.empty()) return false;

    float shared_damage = dmg / static_cast<float>(candidates.size());
    for (CMobBase* mob : candidates)
        if (mob) mob->ApplyDamageDirect(shared_damage, attacker);
    return true;
}

float GetBandageUndeadDuration(ERarity rarity)
{
    switch (rarity)
    {
    case ERarity::Common:
        return game_config::default_bandage_undead_common;
    case ERarity::Unusual:
        return game_config::default_bandage_undead_unusual;
    case ERarity::Rare:
        return game_config::default_bandage_undead_rare;
    case ERarity::Epic:
        return game_config::default_bandage_undead_epic;
    case ERarity::Legendary:
        return game_config::default_bandage_undead_legendary;
    case ERarity::Mythic:
        return game_config::default_bandage_undead_mythic;
    case ERarity::Ultra:
        return game_config::default_bandage_undead_ultra;
    case ERarity::Super:
        return game_config::default_bandage_undead_super;
    case ERarity::Eternal:
        return game_config::default_bandage_undead_eternal;
    case ERarity::Unique:
        return game_config::default_bandage_undead_unique;
    case ERarity::Primordial:
        return game_config::default_bandage_undead_primordial;
    default:
        return game_config::default_bandage_undead_common;
    }
}
