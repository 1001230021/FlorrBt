#include "states.h"
#include "../controllers/melee_controller.h"
#include "../entities/projectile.h"
#include "../gameworld.h"
#include <algorithm>
#include <array>
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

int CountHallucinogenicParameters(std::uint8_t mask)
{
    int count = 0;
    for (size_t i = 0; i < CHallucinogenicState::parameter_capacity; ++i)
        if ((mask & (1u << i)) != 0) ++count;
    return count;
}

int PickHallucinogenicParameter(std::uint8_t mask, bool claimed)
{
    std::array<int, CHallucinogenicState::parameter_capacity> candidates{};
    int count = 0;
    for (size_t i = 0; i < CHallucinogenicState::parameter_capacity; ++i)
    {
        const bool is_claimed = (mask & (1u << i)) != 0;
        if (is_claimed == claimed) candidates[count++] = static_cast<int>(i);
    }
    if (count <= 0) return -1;
    const int index = std::min(count - 1, static_cast<int>(GetLimitedRng(0.f, static_cast<float>(count))));
    return candidates[index];
}

std::uint8_t MutateHallucinogenicMask(std::uint8_t parent_mask)
{
    constexpr std::uint8_t valid_mask = (1u << CHallucinogenicState::parameter_capacity) - 1u;
    parent_mask &= valid_mask;
    const int parent_count = CountHallucinogenicParameters(parent_mask);
    const int max_parameters =
        std::clamp(game_config::hallucinogenic_max_parameters, 1,
                   static_cast<int>(CHallucinogenicState::parameter_capacity));

    if (parent_count <= 0)
    {
        const int parameter = PickHallucinogenicParameter(parent_mask, false);
        return parameter >= 0 ? static_cast<std::uint8_t>(1u << parameter) : 0;
    }

    const float add_probability = std::pow(1.f / 3.f, static_cast<float>(parent_count));
    const float remove_probability = static_cast<float>(parent_count - 1) / static_cast<float>(parent_count);
    const float roll = GetLimitedRng(0.f, 1.f);

    if (roll < add_probability && parent_count < max_parameters)
    {
        const int parameter = PickHallucinogenicParameter(parent_mask, false);
        if (parameter >= 0) parent_mask |= static_cast<std::uint8_t>(1u << parameter);
    } else if (roll < add_probability + remove_probability && parent_count > 1)
    {
        const int parameter = PickHallucinogenicParameter(parent_mask, true);
        if (parameter >= 0) parent_mask &= static_cast<std::uint8_t>(~(1u << parameter));
    }
    return parent_mask;
}

CHallucinogenicState::parameter_array GenerateHallucinogenicParameters(std::uint8_t mask, float effect_depth)
{
    CHallucinogenicState::parameter_array parameters{};
    double length_sq = 0.0;
    int first_parameter = -1;
    for (size_t i = 0; i < parameters.size(); ++i)
    {
        if ((mask & (1u << i)) == 0) continue;
        if (first_parameter < 0) first_parameter = static_cast<int>(i);
        parameters[i] = GetLimitedRng(-1.f, 1.f);
        length_sq += static_cast<double>(parameters[i]) * static_cast<double>(parameters[i]);
    }

    if (first_parameter < 0) return parameters;
    if (length_sq <= static_cast<double>(game_config::entity_collision_epsilon))
    {
        parameters[static_cast<size_t>(first_parameter)] = 1.f;
        length_sq = 1.0;
    }

    const double scale = static_cast<double>(std::max(0.f, effect_depth)) / std::sqrt(length_sq);
    for (float& parameter : parameters)
        parameter = static_cast<float>(static_cast<double>(parameter) * scale);
    return parameters;
}

ERarity GetHallucinogenicTargetRarity(const CMobBase& target)
{
    const auto* player_flower = dynamic_cast<const CPlayerFlower*>(&target);
    if (!player_flower) return target.GetRarity();

    ERarity highest_rarity = ERarity::Null;
    for (const CPetalSlot& slot : player_flower->GetSlots())
    {
        if (!slot.m_p_proto || slot.m_stored_rarity == ERarity::Null) continue;
        if (GetRaritySortRank(slot.m_stored_rarity) > GetRaritySortRank(highest_rarity))
            highest_rarity = slot.m_stored_rarity;
    }
    return highest_rarity == ERarity::Null ? target.GetRarity() : highest_rarity;
}

float GetHallucinogenicDuration(const CMobBase& target, ERarity effect_rarity)
{
    const int rarity_difference = GetLevel(effect_rarity) - GetLevel(GetHallucinogenicTargetRarity(target));
    const double base_duration = std::max(0.0, static_cast<double>(game_config::hallucinogenic_state_duration));
    const double rarity_base =
        std::max(1.0, static_cast<double>(game_config::hallucinogenic_rarity_duration_base));
    const double duration = base_duration * std::pow(rarity_base, static_cast<double>(rarity_difference));
    return static_cast<float>(std::clamp(duration, 0.0, static_cast<double>(std::numeric_limits<float>::max())));
}
} // namespace

CHallucinogenicState::CHallucinogenicState(CMobBase* owner, float timer, ERarity rarity,
                                           std::uint8_t parameter_mask, float effect_depth,
                                           float evolution_progress, parameter_array parameters)
    : CState(owner, timer, rarity, EStateType::Hallucinogenic),
      m_parameter_mask(parameter_mask & ((1u << parameter_capacity) - 1u)),
      m_effect_depth(std::max(0.f, effect_depth)), m_evolution_progress(std::max(0.f, evolution_progress)),
      m_parameters(parameters)
{
    for (size_t i = 0; i < m_parameters.size(); ++i)
    {
        if ((m_parameter_mask & (1u << i)) == 0 || !std::isfinite(m_parameters[i])) m_parameters[i] = 0.f;
    }
}

void CHallucinogenicState::Tick(float dt)
{
    if (m_timer != endless) m_timer -= dt;
}

int CHallucinogenicState::GetParameterCount() const
{
    return CountHallucinogenicParameters(m_parameter_mask);
}

void CHallucinogenicState::AddEvolution(float amount, float duration)
{
    m_evolution_progress = std::min(1000000.f, m_evolution_progress + std::max(0.f, amount));
    if (m_timer != endless) m_timer = std::max(m_timer, std::max(0.f, duration));
}

float CHallucinogenicState::ConsumeSplitProgress(float threshold)
{
    m_evolution_progress = std::max(0.f, m_evolution_progress - std::max(0.f, threshold));
    const float overflow = m_evolution_progress;
    m_evolution_progress = 0.f;
    return overflow;
}

// Effective parameters = raw parameters scaled by the rarity gap, then clamped to the same safe
// ranges MoveTowards uses. Shared by the movement takeover and the owner-facing snapshot so the
// client-side ghost preview always matches the server-side distortion exactly.
bool CHallucinogenicState::ComputeEffectiveParameters(const CMobBase& owner, parameter_array& out) const
{
    out = {};
    if (m_parameter_mask == 0) return false;

    const int rarity_difference = GetLevel(m_rarity) - GetLevel(GetHallucinogenicTargetRarity(owner));
    const double rarity_base = std::max(static_cast<double>(game_config::entity_collision_epsilon),
                                        static_cast<double>(game_config::hallucinogenic_rarity_effect_base));
    const double rarity_scale = std::pow(rarity_base, static_cast<double>(rarity_difference));
    std::array<double, parameter_capacity> values{};
    for (size_t i = 0; i < values.size(); ++i)
        if ((m_parameter_mask & (1u << i)) != 0)
            values[i] = static_cast<double>(m_parameters[i]) * rarity_scale;

    out[0] = static_cast<float>(std::remainder(values[0], static_cast<double>(2.f * game_config::pi)));
    out[1] = static_cast<float>(std::clamp(values[1], -4.0, 4.0));
    out[2] = static_cast<float>(std::clamp(values[2], -4.0, 4.0));
    out[3] = static_cast<float>(std::clamp(values[3], -8.0, 8.0));
    out[4] = static_cast<float>(std::clamp(values[4], -8.0, 8.0));
    out[5] = static_cast<float>(std::clamp(values[5], -8.0, 8.0));
    for (float& value : out)
        if (!std::isfinite(value)) value = 0.f;
    return true;
}

florrbt::Vector2f CHallucinogenicState::ModifyMovementTarget(const CMobBase& owner,
                                                        const florrbt::Vector2f& target_pos) const
{
    parameter_array effective{};
    if (!ComputeEffectiveParameters(owner, effective)) return target_pos;

    const double vx = static_cast<double>(target_pos.x) - static_cast<double>(owner.Pos().x);
    const double vy = static_cast<double>(target_pos.y) - static_cast<double>(owner.Pos().y);
    const double original_length = std::hypot(vx, vy);
    if (original_length <= static_cast<double>(game_config::entity_collision_epsilon)) return target_pos;

    const double angle = static_cast<double>(effective[0]);
    const double scale_x = std::exp(static_cast<double>(effective[1]));
    const double scale_y = std::exp(static_cast<double>(effective[2]));
    const double shear = static_cast<double>(effective[3]);
    const double translate_x = static_cast<double>(effective[4]) * original_length;
    const double translate_y = static_cast<double>(effective[5]) * original_length;

    const double local_x = scale_x * vx + shear * vy;
    const double local_y = scale_y * vy;
    const double cosine = std::cos(angle);
    const double sine = std::sin(angle);
    const double transformed_x = cosine * local_x - sine * local_y + translate_x;
    const double transformed_y = sine * local_x + cosine * local_y + translate_y;
    const double transformed_length = std::hypot(transformed_x, transformed_y);
    if (!std::isfinite(transformed_length) ||
        transformed_length <= static_cast<double>(game_config::entity_collision_epsilon))
        return target_pos;

    const double preserve_length = original_length / transformed_length;
    return { owner.Pos().x + static_cast<float>(transformed_x * preserve_length),
             owner.Pos().y + static_cast<float>(transformed_y * preserve_length) };
}

bool ApplyHallucinogenicExposure(CMobBase* mob, ERarity rarity, float evolution_amount)
{
    if (!mob || rarity == ERarity::Null || evolution_amount <= 0.f || mob->IsMarkedForDes() || mob->IsDead())
        return false;

    CHallucinogenicState* top = nullptr;
    int stack_count = 0;
    for (const auto& state : mob->GetStates())
    {
        auto* hallucinogenic = dynamic_cast<CHallucinogenicState*>(state.get());
        if (!hallucinogenic || hallucinogenic->m_rarity != rarity) continue;
        top = hallucinogenic;
        ++stack_count;
    }

    const float duration = GetHallucinogenicDuration(*mob, rarity);
    if (!top)
    {
        mob->AddState(std::make_unique<CHallucinogenicState>(mob, duration, rarity, 0, 0.f,
                                                             evolution_amount));
        return false;
    }

    top->AddEvolution(evolution_amount, duration);
    const float threshold = std::max(game_config::entity_collision_epsilon,
                                     game_config::hallucinogenic_split_threshold);
    if (top->GetEvolutionProgress() < threshold ||
        stack_count >= std::max(1, game_config::hallucinogenic_max_stacks_per_rarity))
        return false;

    const float overflow = top->ConsumeSplitProgress(threshold);
    const std::uint8_t child_mask = MutateHallucinogenicMask(top->GetParameterMask());
    const float child_depth =
        top->GetParameterCount() == 0
            ? std::max(0.f, game_config::hallucinogenic_base_effect_depth)
            : std::min(std::max(0.f, game_config::hallucinogenic_max_effect_depth),
                       top->GetEffectDepth() + std::max(0.f, game_config::hallucinogenic_effect_depth_growth));
    CHallucinogenicState::parameter_array parameters =
        GenerateHallucinogenicParameters(child_mask, child_depth);
    mob->AddState(std::make_unique<CHallucinogenicState>(mob, duration, rarity, child_mask, child_depth, overflow,
                                                         parameters));
    return child_mask != 0;
}

CPoisonState::CPoisonState(CMobBase* owner, float timer, float basic_dmg, ERarity rarity, CEntity* applier)
    : CState(owner, timer, rarity, EStateType::Poison), m_basic_dmg(basic_dmg),
      m_applier_id(applier ? applier->Id() : -1),
      m_applier_generation(applier ? applier->Generation() : 0)
{
    CPoisonState* existing = owner ? owner->FindFirstState<CPoisonState>() : nullptr;

    if (!existing)
    {
        m_is_valid = true;
        return;
    }

    bool should_replace = false;
    if (GetRarityValueRank(rarity) > GetRarityValueRank(existing->m_rarity))
    {
        should_replace = true;
    } else if (GetRarityValueRank(rarity) == GetRarityValueRank(existing->m_rarity))
    {
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
    : CState(owner, timer, rarity, EStateType::Undead), m_source_slot(source_slot)
{
}

void CUndeadState::Tick(float dt)
{
    m_timer -= dt;
    if (m_timer > 0.f || !m_kill_on_destroy || !m_p_owner) return;

    if (auto* player_flower = dynamic_cast<CPlayerFlower*>(m_p_owner))
    {
        m_kill_on_destroy = false;
        player_flower->EnterDeathState();
    }
}

CUndeadState::~CUndeadState()
{
    if (!m_kill_on_destroy || !m_p_owner) return;
    if (auto* player_flower = dynamic_cast<CPlayerFlower*>(m_p_owner))
    {
        player_flower->EnterDeathState();
        return;
    }
    m_p_owner->Health() = 0.f;
    m_p_owner->MarkForDestroy(EEntityRemovalReason::Defeated);
}

CDiggingState::~CDiggingState()
{
    if (auto* flower = dynamic_cast<CFlower*>(m_p_owner)) flower->ReloadAllPetals();
}

CCorruptionState::CCorruptionState(CMobBase* owner, float timer, ERarity rarity)
    : CState(owner, timer, rarity, EStateType::Corruption)
{
    if (!owner) return;

    m_old_team = owner->Team();
    for (auto* undead : owner->FindStates<CUndeadState>())
    {
        undead->CancelDeathOnDestroy();
        owner->RemoveState(undead);
    }
    owner->Team() = 0;
}

CCorruptionState::~CCorruptionState()
{
    if (m_p_owner) m_p_owner->Team() = m_old_team;
}

CInvincibleState::CInvincibleState(CMobBase* owner, float timer, ERarity rarity)
    : CState(owner, timer, rarity, EStateType::Invincible)
{
    if (!owner) return;
    m_marked_for_destroy = owner->IsMarkedForDes();
    m_removal_reason = owner->RemovalReason();
    const SMobStats* stats = owner->GetFinalStats();
    float max_health = stats ? stats->max_health : 0.f;
    if (max_health > game_config::entity_collision_epsilon) m_hp = std::max(0.f, owner->Health() / max_health);
}

void CInvincibleState::Tick(float dt)
{
    if (m_p_owner)
    {
        if (m_marked_for_destroy)
            m_p_owner->MarkForDestroy(m_removal_reason);
        else
            m_p_owner->CancelDestroy();
        const SMobStats* stats = m_p_owner->GetFinalStats();
        float max_health = stats ? stats->max_health : 0.f;
        if (max_health > game_config::entity_collision_epsilon)
        {
            float hp = std::max(0.f, m_p_owner->Health() / max_health);
            if (hp > m_hp) m_hp = hp;
            m_p_owner->Health() = max_health * m_hp;
        }
    }
    m_timer -= dt;
}

CBanSlotState::CBanSlotState(CMobBase* owner, float timer, int slot, ERarity rarity)
    : CState(owner, timer, rarity, EStateType::BanSlot), m_slot_index(slot)
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

void CBanSlotState::Tick(float dt) { m_timer -= dt; }

CPincerSpeedReduceState::CPincerSpeedReduceState(CMobBase* owner, float timer, ERarity rarity)
    : CState(owner, timer, rarity, EStateType::PincerSpeedReduce)
{
    CPincerSpeedReduceState* existing = owner ? owner->FindFirstState<CPincerSpeedReduceState>() : nullptr;

    if (!existing)
    {
        m_is_valid = true;
        return;
    }

    bool should_replace = false;
    if (GetRarityValueRank(rarity) > GetRarityValueRank(existing->m_rarity))
    {
        should_replace = true;
    } else if (GetRarityValueRank(rarity) == GetRarityValueRank(existing->m_rarity) && timer > existing->m_timer)
    {
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
} // namespace

CWebSpeedReduceState::CWebSpeedReduceState(CMobBase* owner, float timer, float desired_multiplier, float reference_mass)
    : CState(owner, timer, ERarity::Common, EStateType::WebSpeedReduce)
{
    float target_mass = owner ? owner->Mass() : 1.f;
    if (owner)
    {
        if (const SMobStats* stats = owner->GetFinalStats();
            stats && stats->mass > game_config::entity_collision_epsilon)
            target_mass = stats->mass;
    }

    m_multiplier = WebSlowMassMultiplier(desired_multiplier, target_mass, reference_mass);
    if (owner && owner->GetMobType() == EMobType::Spider)
        m_multiplier =
            std::clamp(1.f - (1.f - m_multiplier) * game_config::web_spider_slow_reduction_multiplier, 0.f, 1.f);

    CWebSpeedReduceState* existing = owner ? owner->FindFirstState<CWebSpeedReduceState>() : nullptr;
    if (!existing)
    {
        m_is_valid = true;
        return;
    }

    if (m_multiplier < existing->m_multiplier || (m_multiplier == existing->m_multiplier && timer > existing->m_timer))
    {
        owner->RemoveState(existing);
        m_is_valid = true;
    }
}

CAntiHealState::CAntiHealState(CMobBase* owner, float timer, ERarity rarity, float medic_mult)
    : CState(owner, timer, rarity, EStateType::AntiHeal)
{
    m_medic_mult = std::clamp(medic_mult, 0.f, 1.f);
}

void CAntiHealState::Refresh(float timer, ERarity rarity, float medic_mult)
{
    m_timer = timer;
    if (GetRarityValueRank(rarity) > GetRarityValueRank(m_rarity)) m_rarity = rarity;
    m_medic_mult = std::clamp(m_medic_mult * std::clamp(medic_mult, 0.f, 1.f), 0.f, 1.f);
}

CPsionicConnectionState::CPsionicConnectionState(CMobBase* owner, float timer, ERarity rarity)
    : CState(owner, timer, rarity, EStateType::PsionicConnection)
{
    CPsionicConnectionState* existing = owner ? owner->FindFirstState<CPsionicConnectionState>() : nullptr;

    if (!existing)
    {
        m_is_valid = true;
        return;
    }

    if (GetRarityValueRank(rarity) > GetRarityValueRank(existing->m_rarity)) existing->m_rarity = rarity;
    if (existing->m_timer != endless && timer == endless) existing->m_timer = endless;
    else if (existing->m_timer != endless) existing->m_timer = std::max(existing->m_timer, timer);
}

bool HasActivePsionicConnection(const CMobBase* mob)
{
    const auto* state = mob ? mob->FindFirstState<CPsionicConnectionState>() : nullptr;
    return state && state->IsValid() && (state->m_timer == endless || state->m_timer > 0.f);
}

bool BlocksNullifiedInteraction(const CEntity* lhs, const CEntity* rhs)
{
    return BlocksNullifiedMobInteraction(GetInteractionMob(lhs), GetInteractionMob(rhs));
}

const CMobBase* ResolveInteractionMob(const CEntity* entity) { return GetInteractionMob(entity); }

bool BlocksNullifiedMobInteraction(const CMobBase* lhs, const CMobBase* rhs)
{
    return MobNullifiesTarget(lhs, rhs) || MobNullifiesTarget(rhs, lhs);
}

int GetPreCorruptionTeam(const CEntity* entity, const CMobBase* interaction_mob)
{
    if (!entity) return 0;

    if (!interaction_mob) return entity->Team();
    if (const auto* corruption = interaction_mob->FindFirstState<CCorruptionState>())
        return corruption->GetOriginalTeam();
    return interaction_mob->Team();
}

int GetPreCorruptionTeam(const CEntity* entity)
{
    return GetPreCorruptionTeam(entity, GetInteractionMob(entity));
}

bool IsDiggingEntity(const CEntity* entity)
{
    const CMobBase* mob = GetInteractionMob(entity);
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
    mob->ForEachState<CPincerSpeedReduceState>([&](const CPincerSpeedReduceState* state) {
        if (!state) return;

        int diff = GetLevel(state->m_rarity) - mob_level;
        if (diff >= 1) multiplier = std::min(multiplier, game_config::pincer_speed_multiplier_higher_rarity);
        else if (diff == 0) multiplier = std::min(multiplier, game_config::pincer_speed_multiplier_same_rarity);
        else if (diff == -1) multiplier = std::min(multiplier, game_config::pincer_speed_multiplier_lower_rarity);
    });

    mob->ForEachState<CWebSpeedReduceState>([&](const CWebSpeedReduceState* state) {
        if (!state) return;
        multiplier = std::min(multiplier, state->GetMultiplier());
    });
    return multiplier;
}

float GetMedicMultiplier(const CMobBase* mob)
{
    if (!mob) return 1.f;

    float multiplier = 1.f;
    mob->ForEachState<CAntiHealState>([&](const CAntiHealState* state) {
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
    if (IsSharedDamageType(dmg_type)) return false;
    if (receiver->Team() == 0) return false;
    if (!HasActivePsionicConnection(receiver)) return false;

    CGameWorld* world = receiver->GameWorld();
    if (!world) return false;
    world->QueuePsionicDamage(receiver, dmg, attacker, dmg_type);
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
        return game_config::default_bandage_undead_common;
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
