#include "player_controller.h"
#include "../entities/mob.h"
#include "../entities/petals/petal.h"
#include "../../../Shared/game_config.h"
#include <algorithm>

void CPlayerController::OnTick(CMobBase* mob, float dt)
{
    if (!mob) return;

    if (m_move_dir.x != 0.f || m_move_dir.y != 0.f)
    {
        sf::Vector2f target = mob->m_pos + m_move_dir * game_config::player_move_target_distance;
        mob->MoveTowards(target, dt);
    } else {
        mob->MoveTowards(mob->m_pos, dt);
    }

    while (!m_op_queue.empty())
    {
        ExecuteOperate(m_op_queue.front(), mob);
        m_op_queue.pop();
    }
}

void CPlayerController::PushOperate(const ClientOperate& op)
{
    m_op_queue.push(op);
}

void CPlayerController::ResetOperate()
{
    m_move_dir = {0.f, 0.f};
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
        }
        break;
    case ClientOperate::Type::Chores:
        if (attackable)
        {
            if (op.is_attacking.has_value()) attackable->SetAttacking(*op.is_attacking);
            if (op.is_defending.has_value()) attackable->SetDefending(*op.is_defending);
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
