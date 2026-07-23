#include "player_controller.h"
#include "../entities/flower.h"
#include "../entities/mob.h"
#include "../entities/petals/petal.h"
#include "../states/states.h"
#include "../../../Shared/game_config.h"
#include <algorithm>
#include <cmath>
#include <memory>

void CPlayerController::OnTick(CMobBase* mob, float dt)
{
    if (!mob) return;

    while (!m_op_queue.empty())
    {
        ExecuteOperate(m_op_queue.front(), mob);
        m_op_queue.pop();
    }

    if (m_move_dir.x != 0.f || m_move_dir.y != 0.f)
    {
        sf::Vector2f target = mob->m_pos + m_move_dir * game_config::player_move_target_distance;
        mob->MoveTowards(target, dt);
    } else {
        mob->MoveTowards(mob->m_pos, dt);
    }

    if (auto* attackable = dynamic_cast<IAttackableMob*>(mob); attackable && attackable->IsDefending())
    {
        if (auto* flower = dynamic_cast<CFlower*>(mob))
            flower->TryStartBurrowFromShovel();
    }

    TryManualAttack(mob);
}

void CPlayerController::PushOperate(const ClientOperate& op)
{
    m_op_queue.push(op);
}

void CPlayerController::ResetOperate()
{
    m_move_dir = {0.f, 0.f};
    m_aim_dir = {1.f, 0.f};
    m_has_aim_dir = false;
    std::queue<ClientOperate> empty;
    m_op_queue.swap(empty);
}

void CPlayerController::ExecuteOperate(const ClientOperate& op, CMobBase* mob)
{
    auto* attackable = dynamic_cast<IAttackableMob*>(mob);
    switch (op.type)
    {
    case ClientOperate::Type::Input:
        if (op.move_x.has_value() && op.move_y.has_value())
        {
            float move_x = std::clamp(static_cast<float>(*op.move_x) / game_config::player_input_axis_max, -1.f, 1.f);
            float move_y = std::clamp(static_cast<float>(*op.move_y) / game_config::player_input_axis_max, -1.f, 1.f);
            m_move_dir = sf::Vector2f(move_y, move_x);
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
