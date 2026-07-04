#include "network_module.h"
#include "../Game/controllers/player_controller.h"
#include "../Game/entities/flower.h"
#include "../Game/gameworld.h"
#include "../Game/player.h"
#include <SFML/Network.hpp>

bool INetworkModule::Init()
{
    int port = game_config::default_port;
    if (m_listener.listen(port) != sf::Socket::Status::Done)
    {
        LOG_FATAL("network", "Failed to listen on port " + std::to_string(port));
        return false;
    }

    m_listener.setBlocking(false);
    LOG_INFO("network", "Listening on port " + std::to_string(port));
    return true;
}

void INetworkModule::Tick(float)
{
    AcceptConnections();
    ProcessMessages();
}

void INetworkModule::ShutDown()
{
    m_listener.close();
    m_players.clear();
    LOG_INFO("network", "Shut down");
}

void INetworkModule::AcceptConnections()
{
    sf::TcpSocket new_socket;
    if (m_listener.accept(new_socket) != sf::Socket::Status::Done) return;

    new_socket.setBlocking(false);

    int id = GetNewPlayerId();
    auto player = std::make_unique<CPlayer>(std::move(new_socket), id, "Player" + std::to_string(id));
    CPlayer* p_player = player.get();

    m_players.push_back(std::move(player));
    LOG_INFO("network", "New player connected, ID: " + std::to_string(id));

    if (auto* controller = m_lobby_world.GetController())
        controller->OnPlayerConnect(m_lobby_world, p_player);
}

void INetworkModule::ProcessMessages()
{
    uint8_t buffer[3];

    for (size_t i = 0; i < m_players.size();)
    {
        auto& player = m_players[i];
        size_t received = 0;
        auto status = player->GetSocket().receive(buffer, sizeof(buffer), received);

        if (status == sf::Socket::Status::Done && received > 0)
        {
            ClientOperate op = ClientOperate::parse(buffer, received);
            if (op.type != ClientOperate::Type::Unknown) player->HandleOperate(op);
            ++i;
        } else if (status == sf::Socket::Status::Disconnected) {
            LOG_INFO("network", "Player " + std::to_string(player->GetId()) + " disconnected");
            if (CEntity* entity = player->GetEntity()) entity->m_is_marked_for_des = true;
            FreePlayerId(player->GetId());
            m_players.erase(m_players.begin() + i);
        } else {
            ++i;
        }
    }
}

int INetworkModule::GetNewPlayerId()
{
    if (!m_free_player_ids.empty())
    {
        int id = *m_free_player_ids.begin();
        m_free_player_ids.erase(m_free_player_ids.begin());
        return id;
    }
    return m_next_player_id++;
}

void INetworkModule::FreePlayerId(int id)
{
    m_free_player_ids.insert(id);
}