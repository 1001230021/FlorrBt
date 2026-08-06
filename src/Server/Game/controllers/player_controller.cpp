#include "player_controller.h"
#include "../../../Shared/game_config.h"
#include "../../HotReload/snapshot_archive.h"
#include "../entities/flower.h"
#include "../entities/mob.h"
#include "../entities/petals/petal.h"
#include "../states/states.h"
#include <algorithm>
#include <cmath>
#include <memory>

void CPlayerController::OnTick(CMobBase* mob, float dt)
{
    ++m_local_tick;
    if (!mob) return;

    SPendingOperate& pending = m_pending_operates[m_local_tick % INPUT_QUEUE_CAPACITY];
    if (pending.occupied && pending.target_tick == m_local_tick)
    {
        ClientOperate movement = pending.movement;
        ClientOperate chores = pending.chores;
        bool has_movement = pending.has_movement;
        bool has_chores = pending.has_chores;
        pending = {};

        if (has_movement) ExecuteOperate(movement, mob);
        if (has_chores) ExecuteOperate(chores, mob);
    }

    if (m_move_dir.x != 0.f || m_move_dir.y != 0.f)
    {
        sf::Vector2f target = mob->m_pos + m_move_dir * game_config::player_move_target_distance;
        mob->MoveTowards(target, dt);
    } else
    {
        mob->MoveTowards(mob->m_pos, dt);
    }

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
    if (!ScheduleOperate(op, delay_ticks)) return false;

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

bool CPlayerController::ScheduleOperate(const ClientOperate& op, std::uint32_t delay_ticks)
{
    if (op.type != ClientOperate::Type::Input && op.type != ClientOperate::Type::Chores) return false;

    delay_ticks = std::clamp(delay_ticks, MIN_INPUT_DELAY_TICKS, MAX_INPUT_DELAY_TICKS);
    std::uint64_t target_tick = m_local_tick + delay_ticks;
    SPendingOperate& pending = m_pending_operates[target_tick % INPUT_QUEUE_CAPACITY];
    if (!pending.occupied || pending.target_tick != target_tick)
    {
        pending = {};
        pending.target_tick = target_tick;
        pending.occupied = true;
    }

    MergeOperate(pending, op);
    return true;
}

bool CPlayerController::IsNewerSequence(std::uint32_t sequence, std::uint32_t previous)
{
    std::uint32_t distance = sequence - previous;
    return distance != 0 && distance < 0x80000000u;
}

void CPlayerController::MergeOperate(SPendingOperate& pending, const ClientOperate& op)
{
    if (op.move_x.has_value() || op.move_y.has_value())
    {
        pending.movement.type = ClientOperate::Type::Input;
        if (op.move_x.has_value()) pending.movement.move_x = op.move_x;
        if (op.move_y.has_value()) pending.movement.move_y = op.move_y;
        pending.has_movement = true;
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
    }
}

void CPlayerController::ClearPendingOperates()
{
    for (SPendingOperate& pending : m_pending_operates) pending = {};
    m_last_sequence = 0;
    m_has_last_sequence = false;
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
        }
        if (op.is_digging.value_or(false) || op.is_defending.value_or(false))
        {
            if (auto* flower = dynamic_cast<CFlower*>(mob)) flower->TryStartBurrowFromShovel();
        }
        break;
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
        mob->m_facing_angle = std::atan2(m_aim_dir.y, m_aim_dir.x);
        mob->m_has_facing = true;
    }
    attackable->TryAttack(nullptr);
}
