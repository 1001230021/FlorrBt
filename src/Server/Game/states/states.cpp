#include "states.h"
#include "../controllers/melee_controller.h"
#include "../entities/projectile.h"
#include "../gameworld.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
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
        CEntity* owner = controller->GetOwner(world);
        if (const auto* owner_projectile = dynamic_cast<const CProjectile*>(owner))
        {
            CEntity* root_owner = owner_projectile->GetOwner();
            if (const auto* root_mob = dynamic_cast<const CMobBase*>(root_owner)) return root_mob;
        }

        if (const auto* owner_mob = dynamic_cast<const CMobBase*>(owner)) return owner_mob;
        return mob;
    }

    const auto* projectile = dynamic_cast<const CProjectile*>(entity);
    if (!projectile) return nullptr;

    CEntity* owner = projectile->GetOwner();
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
    if (GetRaritySortRank(rarity) > GetRaritySortRank(existing->m_rarity))
    {
        should_replace = true;
    } else if (GetRaritySortRank(rarity) == GetRaritySortRank(existing->m_rarity)) {
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

CDiggingState::~CDiggingState()
{
    if (auto* flower = dynamic_cast<CFlower*>(m_p_owner))
        flower->ReloadAllPetals();
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

CInvincibleState::CInvincibleState(CMobBase* owner, float timer, ERarity rarity)
    : CState(owner, timer, rarity)
{
    if (!owner) return;
    m_marked_for_destroy = owner->m_is_marked_for_des;
    const SMobStats* stats = owner->GetFinalStats();
    float max_health = stats ? stats->max_health : 0.f;
    if (max_health > game_config::entity_collision_epsilon)
        m_hp = std::max(0.f, owner->m_health / max_health);
}

void CInvincibleState::Tick(float dt)
{
    if (m_p_owner)
    {
        m_p_owner->m_is_marked_for_des = m_marked_for_destroy;
        const SMobStats* stats = m_p_owner->GetFinalStats();
        float max_health = stats ? stats->max_health : 0.f;
        if (max_health > game_config::entity_collision_epsilon)
        {
            float hp = std::max(0.f, m_p_owner->m_health / max_health);
            if (hp > m_hp) m_hp = hp;
            m_p_owner->m_health = max_health * m_hp;
        }
    }
    m_timer -= dt;
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
    if (GetRaritySortRank(rarity) > GetRaritySortRank(existing->m_rarity))
    {
        should_replace = true;
    } else if (GetRaritySortRank(rarity) == GetRaritySortRank(existing->m_rarity) && timer > existing->m_timer) {
        should_replace = true;
    }

    if (should_replace)
    {
        owner->RemoveState(existing);
        m_is_valid = true;
    }
}

namespace
{
float WebSlowMassMultiplier(float desired_multiplier, float target_mass, float reference_mass)
{
    const float m = std::clamp(desired_multiplier, 0.f, 1.f);
    const float x = std::max(game_config::entity_collision_epsilon, target_mass);
    const float f = std::max(game_config::entity_collision_epsilon, reference_mass);
    const float denominator = m * x + f * (1.f - m);
    if (denominator <= game_config::entity_collision_epsilon) return m;
    return std::clamp((m * x) / denominator, 0.f, 1.f);
}
}

CWebSpeedReduceState::CWebSpeedReduceState(CMobBase* owner, float timer, float desired_multiplier, float reference_mass)
    : CState(owner, timer, ERarity::Common)
{
    float target_mass = owner ? owner->m_mass : 1.f;
    if (owner)
    {
        if (const SMobStats* stats = owner->GetFinalStats(); stats && stats->mass > game_config::entity_collision_epsilon)
            target_mass = stats->mass;
    }

    m_multiplier = WebSlowMassMultiplier(desired_multiplier, target_mass, reference_mass);
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

CAntiHealState::CAntiHealState(CMobBase* owner, float timer, ERarity rarity, float medic_mult)
    : CState(owner, timer, rarity)
{
    m_medic_mult = std::clamp(medic_mult, 0.f, 1.f);
}

void CAntiHealState::Refresh(float timer, ERarity rarity, float medic_mult)
{
    m_timer = timer;
    if (GetRaritySortRank(rarity) > GetRaritySortRank(m_rarity)) m_rarity = rarity;
    m_medic_mult = std::clamp(m_medic_mult * std::clamp(medic_mult, 0.f, 1.f), 0.f, 1.f);
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

    if (GetRaritySortRank(rarity) > GetRaritySortRank(existing->m_rarity)) existing->m_rarity = rarity;
    if (existing->m_timer != endless && timer == endless) existing->m_timer = endless;
    else if (existing->m_timer != endless) existing->m_timer = std::max(existing->m_timer, timer);
}

bool BlocksNullifiedInteraction(const CEntity* lhs, const CEntity* rhs)
{
    const CMobBase* lhs_mob = GetInteractionMob(lhs);
    const CMobBase* rhs_mob = GetInteractionMob(rhs);
    return MobNullifiesTarget(lhs_mob, rhs_mob) || MobNullifiesTarget(rhs_mob, lhs_mob);
}

bool IsDiggingEntity(const CEntity* entity)
{
    const auto* mob = dynamic_cast<const CMobBase*>(entity);
    return mob && mob->HasState<CDiggingState>();
}

bool ShouldSkipDiggingCollision(const CEntity* lhs, const CEntity* rhs)
{
    return IsDiggingEntity(lhs) != IsDiggingEntity(rhs);
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

float GetMedicMultiplier(const CMobBase* mob)
{
    if (!mob) return 1.f;

    float multiplier = 1.f;
    mob->ForEachState<CAntiHealState>([&](const CAntiHealState* state)
    {
        if (!state) return;
        multiplier *= std::clamp(state->GetMedicMultiplier(), 0.f, 1.f);
    });
    return std::clamp(multiplier, 0.f, 1.f);
}

void ApplyDandelionAntiHeal(CMobBase* mob, ERarity rarity)
{
    if (!mob) return;

    const float timer = std::max(0.f, game_config::default_dandelion_medic_timer);
    const float multiplier = std::clamp(game_config::default_dandelion_heal_multiplier, 0.f, 1.f);
    if (timer <= 0.f || multiplier >= 1.f) return;

    if (auto* existing = mob->FindFirstState<CAntiHealState>())
    {
        existing->Refresh(timer, rarity, multiplier);
        return;
    }

    mob->AddState(std::make_unique<CAntiHealState>(mob, timer, rarity, multiplier));
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
    case ERarity::Exotic:
        return BlendUltraSuper(game_config::default_bandage_undead_ultra,
                               game_config::default_bandage_undead_super,
                               0.25f);
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
