#include "player.h"
#include "entities/flower.h"
#include "entities/mob.h"
#include "controllers/player_controller.h"
#include "gameworld.h"
#include "../../Shared/game_config.h"

CPlayer::CPlayer(sf::TcpSocket&& socket, uint32_t id, const std::string& name)
    : m_socket(std::move(socket)), m_player_id(id), m_name(name)
{
}

void CPlayer::HandleOperate(const ClientOperate& op)
{
    CEntity* entity = GetEntity();
    if (!entity || entity->m_is_marked_for_des) return;

    auto* flower = dynamic_cast<CFlower*>(entity);
    if (!flower) return;

    auto* controller = dynamic_cast<CPlayerController*>(flower->GetController());
    if (!controller) return;

    controller->PushOperate(op);
}

void CPlayer::AttachSocket(sf::TcpSocket&& socket)
{
    m_socket = std::move(socket);
    m_socket.setBlocking(false);
    m_send_buffer.clear();
    m_receive_buffer.clear();
    m_connected = true;
    m_timeout_left = 0.f;
}

void CPlayer::DetachSocket()
{
    if (!m_connected) return;

    m_socket.disconnect();
    m_send_buffer.clear();
    m_receive_buffer.clear();
    m_connected = false;
    m_timeout_left = game_config::timeout_protection_seconds;
    ResetControlledMob();
}

void CPlayer::TickTimeout(float dt)
{
    if (m_connected || m_timeout_left <= 0.f) return;
    m_timeout_left -= dt;
}

void CPlayer::ResetControlledMob()
{
    auto* mob = dynamic_cast<CMobBase*>(GetEntity());
    if (!mob) return;

    mob->MoveTowards(mob->m_pos, 0.f);
    mob->m_vel = {0.f, 0.f};

    if (auto* flower = dynamic_cast<CFlower*>(mob))
    {
        flower->m_attacking = false;
        flower->m_defending = false;
    }

    if (auto* controller = dynamic_cast<CPlayerController*>(mob->GetController()))
    {
        controller->ResetOperate();
    }
}

void CPlayer::SetOwnedEntity(CEntity* entity)
{
    if (!entity)
    {
        m_p_world = nullptr;
        m_entity_id = -1;
        return;
    }

    m_p_world = entity->GameWorld();
    m_entity_id = entity->m_id;
}

CEntity* CPlayer::GetEntity() const
{
    if (!m_p_world || m_entity_id < 0) return nullptr;
    return m_p_world->GetEntity(m_entity_id);
}
