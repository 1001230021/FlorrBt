#include "player.h"
#include "entities/flower.h"
#include "controllers/player_controller.h"
#include "entity.h"
#include "gameworld.h"

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
