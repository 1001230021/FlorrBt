#include "network_module.h"
#include "../Game/controllers/player_controller.h"
#include "../Game/entities/mob.h"
#include "../Game/entities/projectile.h"
#include "../Game/entities/flower.h"
#include "../Game/entities/petals/petal.h"
#include "../Game/gameworld.h"
#include "../Game/player.h"
#include "../../Shared/game_config.h"
#include "../../Shared/network_msg.h"
#include <SFML/Network.hpp>
#include <algorithm>
#include <cstddef>

namespace
{
constexpr float snapshot_interval = 1.f / 20.f;

NetEntityKind EntityKind(const CEntity* entity)
{
    if (dynamic_cast<const CPetal*>(entity)) return NetEntityKind::Petal;
    if (dynamic_cast<const CFlower*>(entity)) return NetEntityKind::Flower;
    if (dynamic_cast<const CProjectile*>(entity)) return NetEntityKind::Projectile;
    if (dynamic_cast<const CMobBase*>(entity)) return NetEntityKind::Mob;
    return NetEntityKind::Generic;
}

uint8_t EntityFlags(const CEntity* entity)
{
    uint8_t flags = 0;
    if (const auto* flower = dynamic_cast<const CFlower*>(entity))
    {
        if (flower->m_attacking) flags |= 1 << 0;
        if (flower->m_defending) flags |= 1 << 1;
    }
    return flags;
}
} // namespace

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

void INetworkModule::Tick(float dt)
{
    AcceptConnections();
    ProcessMessages();
    BroadcastSnapshots(dt);
    FlushOutgoing();
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
    uint8_t buffer[256];

    for (size_t i = 0; i < m_players.size();)
    {
        auto& player = m_players[i];
        bool disconnected = false;

        while (true)
        {
            size_t received = 0;
            auto status = player->GetSocket().receive(buffer, sizeof(buffer), received);
            if (status == sf::Socket::Status::Done && received > 0)
            {
                auto& receive_buffer = player->ReceiveBuffer();
                receive_buffer.insert(receive_buffer.end(), buffer, buffer + received);
                continue;
            }
            if (status == sf::Socket::Status::Disconnected)
            {
                disconnected = true;
            }
            break;
        }

        auto& receive_buffer = player->ReceiveBuffer();
        while (!receive_buffer.empty())
        {
            const size_t packet_len = ClientOperatePackedLength(receive_buffer.front());
            if (packet_len == 0)
            {
                receive_buffer.erase(receive_buffer.begin());
                continue;
            }
            if (receive_buffer.size() < packet_len) break;

            ClientOperate op = ClientOperate::parse(receive_buffer.data(), packet_len);
            receive_buffer.erase(receive_buffer.begin(), receive_buffer.begin() + static_cast<std::ptrdiff_t>(packet_len));
            if (op.type != ClientOperate::Type::Unknown) player->HandleOperate(op);
        }

        if (disconnected)
        {
            LOG_INFO("network", "Player " + std::to_string(player->GetId()) + " disconnected");
            if (CEntity* entity = player->GetEntity()) entity->m_is_marked_for_des = true;
            FreePlayerId(player->GetId());
            m_players.erase(m_players.begin() + i);
        } else {
            ++i;
        }
    }
}

void INetworkModule::BroadcastSnapshots(float dt)
{
    m_snapshot_timer += dt;
    if (m_snapshot_timer < snapshot_interval) return;

    m_snapshot_timer = 0.f;
    ++m_snapshot_sequence;

    for (auto& player : m_players)
    {
        if (player) QueueSnapshot(*player, BuildSnapshot(*player));
    }
}

void INetworkModule::QueueSnapshot(CPlayer& player, const ServerSnapshot& snapshot)
{
    std::vector<uint8_t> frame = PackServerSnapshotFrame(snapshot);
    if (frame.empty()) return;

    auto& send_buffer = player.SendBuffer();
    send_buffer.insert(send_buffer.end(), frame.begin(), frame.end());
}

void INetworkModule::FlushOutgoing()
{
    for (size_t i = 0; i < m_players.size();)
    {
        auto& player = m_players[i];
        auto& send_buffer = player->SendBuffer();
        bool disconnected = false;

        while (!send_buffer.empty())
        {
            size_t sent = 0;
            auto status = player->GetSocket().send(send_buffer.data(), send_buffer.size(), sent);
            if (sent > 0)
            {
                send_buffer.erase(send_buffer.begin(), send_buffer.begin() + static_cast<std::ptrdiff_t>(sent));
            }
            if (status == sf::Socket::Status::Done || status == sf::Socket::Status::Partial) continue;
            if (status == sf::Socket::Status::NotReady) break;

            disconnected = true;
            break;
        }

        if (disconnected)
        {
            LOG_INFO("network", "Player " + std::to_string(player->GetId()) + " disconnected while sending");
            if (CEntity* entity = player->GetEntity()) entity->m_is_marked_for_des = true;
            FreePlayerId(player->GetId());
            m_players.erase(m_players.begin() + i);
        } else {
            ++i;
        }
    }
}

ServerSnapshot INetworkModule::BuildSnapshot(const CPlayer& player) const
{
    ServerSnapshot snapshot;
    snapshot.sequence = m_snapshot_sequence;
    snapshot.player_entity_id = player.GetEntityId();

    std::vector<CEntity*> entities = m_lobby_world.GetAllEntities();
    snapshot.entities.reserve(entities.size());
    for (const CEntity* entity : entities)
    {
        if (!entity || entity->m_is_marked_for_des || entity->m_id < 0) continue;

        ServerEntitySnapshot item;
        item.id = entity->m_id;
        item.kind = EntityKind(entity);
        item.team = entity->m_team;
        item.x = entity->m_pos.x;
        item.y = entity->m_pos.y;
        item.radius = entity->m_radius;
        item.health = entity->m_health;
        item.flags = EntityFlags(entity);
        snapshot.entities.push_back(item);
    }

    return snapshot;
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
