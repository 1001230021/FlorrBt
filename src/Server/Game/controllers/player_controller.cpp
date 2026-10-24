#include "player_controller.h"
#include "../../../Shared/game_config.h"
#include "../../HotReload/snapshot_archive.h"
#include "../../Module/network_module.h"
#include "../entities/flower.h"
#include "../entities/mob.h"
#include "../entities/petals/petal.h"
#include "../gamecontext.h"
#include "../states/states.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>

namespace
{
void QueueSkillResult(CMobBase* mob, ESkillRequestResult result, int skill_index, int skill_count)
{
    if (!mob) return;
    CGameContext* context = mob->GameContext();
    CPlayer* player = context ? context->FindPlayerFromEntity(mob) : nullptr;
    if (!player) return;

    context->Network().QueueSkillResult(
        *player, result,
        static_cast<uint8_t>(std::clamp(skill_index, 0, static_cast<int>(std::numeric_limits<uint8_t>::max()))),
        static_cast<uint8_t>(std::clamp(skill_count, 0, static_cast<int>(std::numeric_limits<uint8_t>::max()))));
}
} // namespace

void CPlayerController::OnTick(CMobBase* mob, float dt)
{
    ++m_local_tick;
    if (!mob) return;

    SPendingOperate& pending = m_pending_operates[m_local_tick % INPUT_QUEUE_CAPACITY];
    if (pending.occupied && pending.target_tick == m_local_tick)
    {
        ClientOperate movement = pending.movement;
        ClientOperate chores = pending.chores;
        ClientOperate skill = pending.skill;
        bool has_movement = pending.has_movement;
        bool has_chores = pending.has_chores;
        bool has_skill = pending.has_skill;
        const auto movement_sequence = pending.movement_sequence;
        const auto chores_sequence = pending.chores_sequence;
        pending = {};

        if (has_movement && (!movement_sequence || !m_applied_movement_sequence ||
                             IsNewerSequence(*movement_sequence, *m_applied_movement_sequence)))
        {
            ExecuteOperate(movement, mob);
            if (movement_sequence) m_applied_movement_sequence = movement_sequence;
        }
        if (has_chores && (!chores_sequence || !m_applied_chores_sequence ||
                          IsNewerSequence(*chores_sequence, *m_applied_chores_sequence)))
        {
            ExecuteOperate(chores, mob);
            if (chores_sequence) m_applied_chores_sequence = chores_sequence;
        }
        if (has_skill) ExecuteOperate(skill, mob);
    }

    // A skill-driven movement owns the velocity for this tick.  Without this
    // guard player input would immediately steer a crab out of its dash.
    if (mob->IsMovementControlledBySkill()) return;
    if (const auto* crab = dynamic_cast<const ICrabDashMob*>(mob);
        crab && (crab->IsDashActive() || crab->IsDashRecovering()))
        return;

    // Input axes already encode an analog vector. Do not normalize it back
    // to full speed or aim at an arbitrarily distant point.
    const float input_length = Length(m_move_dir);
    const florrbt::Vector2f intent = input_length > 1.f ? m_move_dir / input_length : m_move_dir;
    const float full_speed_distance = std::max(1.f, game_config::player_move_target_distance);
    const florrbt::Vector2f target = mob->Pos() + intent * full_speed_distance;
    mob->MoveTowards(target, dt, -1.f, full_speed_distance);

    if (auto* attackable = dynamic_cast<IAttackableMob*>(mob); attackable && attackable->IsDefending())
    {
        if (auto* flower = dynamic_cast<CFlower*>(mob)) flower->TryStartBurrowFromShovel();
    }

    TryManualAttack(mob);
}

void CPlayerController::PushOperate(const ClientOperate& op) { ScheduleOperate(op, MIN_INPUT_DELAY_TICKS); }

bool CPlayerController::PushOperate(const ClientOperate& op, std::uint32_t delay_ticks, std::uint32_t sequence)
{
    if (m_has_last_sequence && !IsNewerSequence(sequence, m_last_sequence)) return false;
    if (!ScheduleOperate(op, delay_ticks, sequence)) return false;

    m_last_sequence = sequence;
    m_has_last_sequence = true;
    return true;
}

void CPlayerController::ResetOperate()
{
    m_move_dir = { 0.f, 0.f };
    m_aim_dir = { 1.f, 0.f };
    m_has_aim_dir = false;
    ClearPendingOperates();
}

void CPlayerController::CaptureSnapshot(CSnapshotWriter& writer) const
{
    writer.Field("move_dir", m_move_dir);
    writer.Field("aim_dir", m_aim_dir);
    writer.Field("has_aim_dir", m_has_aim_dir);
}

bool CPlayerController::RestoreSnapshot(const CSnapshotReader& reader, std::uint32_t version, std::string& error)
{
    if (version > SnapshotVersion())
    {
        error = "Unsupported player controller snapshot version " + std::to_string(version);
        return false;
    }
    m_move_dir = reader.Vector2("move_dir");
    m_aim_dir = reader.Vector2("aim_dir", { 1.f, 0.f });
    m_has_aim_dir = reader.Bool("has_aim_dir");
    ClearPendingOperates();
    return true;
}

bool CPlayerController::ScheduleOperate(const ClientOperate& op, std::uint32_t delay_ticks,
                                       std::optional<std::uint32_t> sequence)
{
    if (op.type != ClientOperate::Type::Input && op.type != ClientOperate::Type::Chores &&
        op.type != ClientOperate::Type::Skill)
        return false;

    if (!sequence)
    {
        // Legacy immediate packets are a fence for the fields they update;
        // an older queued InputFrame must not undo a legacy release either.
        const bool movement = op.move_x.has_value() || op.move_y.has_value();
        const bool chores = op.is_attacking.has_value() || op.is_defending.has_value() || op.is_digging.has_value();
        for (auto& queued : m_pending_operates)
        {
            if (movement) queued.has_movement = false;
            if (chores) queued.has_chores = false;
        }
        if (m_has_last_sequence)
        {
            if (movement) m_applied_movement_sequence = m_last_sequence;
            if (chores) m_applied_chores_sequence = m_last_sequence;
        }
    }

    delay_ticks = std::clamp(delay_ticks, MIN_INPUT_DELAY_TICKS, MAX_INPUT_DELAY_TICKS);
    std::uint64_t target_tick = m_local_tick + delay_ticks;
    SPendingOperate& pending = m_pending_operates[target_tick % INPUT_QUEUE_CAPACITY];
    if (!pending.occupied || pending.target_tick != target_tick)
    {
        pending = {};
        pending.target_tick = target_tick;
        pending.occupied = true;
    }

    MergeOperate(pending, op, sequence);
    return true;
}

bool CPlayerController::IsNewerSequence(std::uint32_t sequence, std::uint32_t previous)
{
    return IsNewerUint32Sequence(sequence, previous);
}

void CPlayerController::MergeOperate(SPendingOperate& pending, const ClientOperate& op,
                                    std::optional<std::uint32_t> sequence)
{
    if (op.move_x.has_value() || op.move_y.has_value())
    {
        pending.movement.type = ClientOperate::Type::Input;
        if (op.move_x.has_value()) pending.movement.move_x = op.move_x;
        if (op.move_y.has_value()) pending.movement.move_y = op.move_y;
        pending.has_movement = true;
        pending.movement_sequence = sequence;
    }

    if (op.is_attacking.has_value() || op.is_defending.has_value() || op.agree.has_value() ||
        op.disconnect.has_value() || op.is_digging.has_value())
    {
        pending.chores.type = ClientOperate::Type::Chores;
        if (op.is_attacking.has_value()) pending.chores.is_attacking = op.is_attacking;
        if (op.is_defending.has_value()) pending.chores.is_defending = op.is_defending;
        if (op.agree.has_value()) pending.chores.agree = op.agree;
        if (op.disconnect.has_value()) pending.chores.disconnect = op.disconnect;
        if (op.is_digging.has_value()) pending.chores.is_digging = op.is_digging;
        pending.has_chores = true;
        pending.chores_sequence = sequence;
    }

    if (op.skill_index.has_value())
    {
        pending.skill.type = ClientOperate::Type::Skill;
        pending.skill.skill_index = op.skill_index;
        pending.has_skill = true;
    }
}

void CPlayerController::ClearPendingOperates()
{
    for (SPendingOperate& pending : m_pending_operates) pending = {};
    m_last_sequence = 0;
    m_has_last_sequence = false;
    m_applied_movement_sequence.reset();
    m_applied_chores_sequence.reset();
}

void CPlayerController::ExecuteOperate(const ClientOperate& op, CMobBase* mob)
{
    auto* attackable = dynamic_cast<IAttackableMob*>(mob);
    switch (op.type)
    {
    case ClientOperate::Type::Input:
        if (op.move_x.has_value())
        {
            float move_x = std::clamp(static_cast<float>(*op.move_x) / game_config::player_input_axis_max, -1.f, 1.f);
            m_move_dir.y = move_x;
        }
        if (op.move_y.has_value())
        {
            float move_y = std::clamp(static_cast<float>(*op.move_y) / game_config::player_input_axis_max, -1.f, 1.f);
            m_move_dir.x = move_y;
        }
        if (op.move_x.has_value() || op.move_y.has_value())
        {
            float len_sq = LengthSq(m_move_dir);
            if (len_sq > game_config::entity_collision_epsilon * game_config::entity_collision_epsilon)
            {
                float len = std::sqrt(len_sq);
                m_aim_dir = m_move_dir / len;
                m_has_aim_dir = true;
            }
        }
        break;
    case ClientOperate::Type::Chores:
        if (attackable)
        {
            if (op.is_attacking.has_value()) attackable->SetAttacking(*op.is_attacking);
            if (op.is_defending.has_value()) attackable->SetDefending(*op.is_defending);
            // Independent holds: Bubble must still see defense during attack.
            // Petal-specific actions (e.g. EOU throwing) choose their own mode.
        }
        if (op.is_digging.value_or(false) || op.is_defending.value_or(false))
        {
            if (auto* flower = dynamic_cast<CFlower*>(mob)) flower->TryStartBurrowFromShovel();
        }
        break;
    case ClientOperate::Type::Skill: {
        const int skill_index = static_cast<int>(op.skill_index.value_or(0));
        auto* skill_caster = dynamic_cast<ISkillCasterMob*>(mob);
        if (!skill_caster)
        {
            QueueSkillResult(mob, ESkillRequestResult::NoSkills, skill_index, 0);
            break;
        }

        const int skill_count = std::max(0, skill_caster->GetSkillCount());
        if (!op.skill_index.has_value() || skill_index < 0 || skill_index >= skill_count)
        {
            QueueSkillResult(mob, skill_count == 0 ? ESkillRequestResult::NoSkills : ESkillRequestResult::InvalidIndex,
                             skill_index, skill_count);
            break;
        }

        // A manual skill request uses the same aim direction as manual
        // attacks. The skill itself owns target, cooldown and state limits;
        // this controller only reports the authoritative TryCastSkill result.
        if (m_has_aim_dir && !mob->IsFacingLocked())
        {
            mob->FacingAngle() = std::atan2(m_aim_dir.y, m_aim_dir.x);
            mob->HasFacing() = true;
        }
        const bool cast = skill_caster->TryCastSkill(skill_index, nullptr);
        QueueSkillResult(mob, cast ? ESkillRequestResult::Success : ESkillRequestResult::Unavailable, skill_index,
                         skill_count);
        break;
    }
    case ClientOperate::Type::Equip:
        break;
    case ClientOperate::Type::Unequip:
        break;
    default:
        break;
    }
}

void CPlayerController::TryManualAttack(CMobBase* mob)
{
    auto* attackable = dynamic_cast<IAttackableMob*>(mob);
    if (!mob || !attackable || !attackable->IsAttacking()) return;
    if (dynamic_cast<CFlower*>(mob)) return;

    if (m_has_aim_dir && !mob->IsFacingLocked())
    {
        mob->FacingAngle() = std::atan2(m_aim_dir.y, m_aim_dir.x);
        mob->HasFacing() = true;
    }
    attackable->TryAttack(nullptr);
}
