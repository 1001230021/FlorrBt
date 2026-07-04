#include "player.h"
#include "entities/flower.h"
#include "controllers/player_controller.h"

CPlayer::CPlayer(sf::TcpSocket&& socket, uint32_t id, const std::string& name)
    : m_socket(std::move(socket)), m_player_id(id), m_name(name)
{
}

void CPlayer::HandleOperate(const ClientOperate& op)
{
    if (!m_p_entity) return;

    auto* flower = dynamic_cast<CFlower*>(m_p_entity);
    if (!flower) return;

    auto* controller = dynamic_cast<CPlayerController*>(flower->GetController());
    if (!controller) return;

    controller->PushOperate(op);
}
