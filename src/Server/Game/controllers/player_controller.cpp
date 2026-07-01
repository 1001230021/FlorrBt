#include "player_controller.h"
#include "../Entities/flower.h"
#include "../entities/petals/petal.h"

void CPlayerController::OnTick(CMobBase* mob, float dt)
{
    if (m_MoveDir.x != 0.f || m_MoveDir.y != 0.f)
    {
        sf::Vector2f target = mob->m_Pos + m_MoveDir * 100123.f;
        mob->MoveTowards(target, dt);
    } else {
        mob->MoveTowards(mob->m_Pos, dt);
    }

    while (!m_OpQueue.empty())
    {
        ExecuteOperate(m_OpQueue.front(), mob);
        m_OpQueue.pop();
    }
    auto* flower = dynamic_cast<CFlower*>(mob);
    if (flower)
    {
        flower->m_Attacking = m_Attacking;
        flower->m_Defending = m_Defending;
    }
}

void CPlayerController::PushOperate(const ClientOperate& op)
{
    m_OpQueue.push(op);
}

void CPlayerController::ExecuteOperate(const ClientOperate& op, CMobBase* mob)
{
    auto* flower = dynamic_cast<CFlower*>(mob);
    switch (op.type)
    {
    case ClientOperate::Type::Input:
        if (op.move_x.has_value() && op.move_y.has_value())
        {
            m_MoveDir = sf::Vector2f(static_cast<float>(*op.move_x) / 127.f, static_cast<float>(*op.move_y) / 127.f);
        }
        break;
    case ClientOperate::Type::AttackDefend:
        if (flower)
        {
            if (op.isAttacking.has_value())
                flower->m_Attacking = (bool)op.isAttacking;
            if (op.isDefending.has_value())
                flower->m_Defending = (bool)op.isDefending;
        }
        break;
    case ClientOperate::Type::Equip:
        if (op.petal_type.has_value() && op.slot_index.has_value() && op.rarity.has_value() && flower)
        {
            auto type = static_cast<EPetalType>(*op.petal_type);
            const auto& proto = g_PetalRegistry[type];
            if (proto)
            {
                flower->EquipPetal(*op.slot_index, proto.get(), static_cast<ERarity>(*op.rarity));
            }
        }
        break;
    case ClientOperate::Type::Unequip:
        if (op.slot_index.has_value() && flower)
        {
            flower->UnequipPetal(*op.slot_index);
        }
        break;
    default:
        break;
    }
}