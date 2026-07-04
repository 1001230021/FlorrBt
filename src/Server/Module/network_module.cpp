#include "network_module.h"
#include "../Game/controllers/player_controller.h"
#include "../Game/entities/flower.h"
#include "../Game/entities/petals/petal.h"
#include "../Game/gameworld.h"
#include "../Game/player.h"
#include <SFML/Network.hpp>
#include <algorithm>
#include <vector>

namespace
{
uint8_t GetEntityType(const CEntity& entity)
{
    if (const auto* mob = dynamic_cast<const CMobBase*>(&entity)) return static_cast<uint8_t>(mob->m_mob_type);
    if (const auto* petal = dynamic_cast<const CPetal*>(&entity))
        return game_config::network_petal_type_offset + static_cast<uint8_t>(petal->m_type);
    return 0;
}

float GetHealthPercent(const CEntity& entity)
{
    if (const auto* mob = dynamic_cast<const CMobBase*>(&entity))
    {
        const SMobStats* stats = mob->GetFinalStats();
        if (stats && stats->max_health > 0.f) return entity.m_health / stats->max_health;
    }
    return entity.m_health;
}
}

bool INetworkModule::Init()
{
    int port = game_config::port;
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
    SendSnapshots();
    TickTimeouts(dt);
}

void INetworkModule::SendSnapshots()
{
    for (auto& player : m_players)
    {
        if (!player) continue;
        if (!player->IsConnected()) continue;

        CEntity* owner = player->GetEntity();
        if (!owner)
        {
            if (player->HasOwnedEntity() && !player->m_logged_missing_entity)
            {
                LOG_WARN("network", "Player " + std::to_string(player->GetId()) + " has no living owner entity");
                player->m_logged_missing_entity = true;
            }
            continue;
        }
        player->m_logged_missing_entity = false;

        auto* owner_mob = dynamic_cast<CMobBase*>(owner);
        if (!owner_mob || !owner_mob->GetFinalStats()) continue;

        float view_radius = owner_mob->GetFinalStats()->horizon;
        auto visible_entities = owner->GameWorld()->GetSpatialGrid().QueryRange(
            owner->m_pos, view_radius, [owner](const CEntity* entity)
            {
                return entity && !entity->m_is_marked_for_des && entity != owner;
            });

        ServerMessage msg;
        msg.type = ServerMessage::Type::Snapshot;
        msg.snapshot_id = m_snapshot_id;
        msg.owner_entity_id = static_cast<net_entity_id>(owner->m_id);
        msg.view_radius = view_radius;
        msg.entities.reserve(visible_entities.size() + 1);
        msg.entities.push_back(BuildEntitySnap(*owner, *owner));

        for (const auto* entity : visible_entities)
        {
            if (!entity) continue;
            msg.entities.push_back(BuildEntitySnap(*entity, *owner));
        }

        if (!QueueMessage(*player, msg))
        {
            LOG_WARN("network", "Failed to queue snapshot for player " + std::to_string(player->GetId()));
        }
        if (!FlushSendBuffer(*player))
        {
            LOG_INFO("network", "Player " + std::to_string(player->GetId()) + " disconnected while flushing snapshot");
            player->DetachSocket();
        }
    }
    m_snapshot_id++;
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

    if (CPlayer* player = FindReconnectablePlayer())
    {
        player->AttachSocket(std::move(new_socket));
        LOG_INFO("network", "Player " + std::to_string(player->GetId()) + " reconnected");
        return;
    }

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
    std::vector<uint8_t> buffer(game_config::network_receive_chunk_size);

    for (size_t i = 0; i < m_players.size();)
    {
        auto& player = m_players[i];
        if (!player->IsConnected())
        {
            ++i;
            continue;
        }

        bool disconnected = !FlushSendBuffer(*player);
        if (disconnected)
        {
            LOG_INFO("network", "Player " + std::to_string(player->GetId()) + " disconnected while sending");
            player->DetachSocket();
            ++i;
            continue;
        }

        size_t received = 0;
        auto status = player->GetSocket().receive(buffer.data(), buffer.size(), received);

        if (status == sf::Socket::Status::Done && received > 0)
        {
            player->m_receive_buffer.insert(player->m_receive_buffer.end(), buffer.begin(), buffer.begin() + received);
            if (player->m_receive_buffer.size() > game_config::network_max_receive_buffer_size)
            {
                LOG_WARN("network", "Player " + std::to_string(player->GetId()) + " input buffer overflow");
                DropPlayer(i, "input buffer overflow");
                continue;
            }
            if (ProcessPlayerBuffer(*player))
            {
                LOG_INFO("network", "Player " + std::to_string(player->GetId()) + " requested disconnect");
                DropPlayer(i, "requested disconnect");
                continue;
            }
            ++i;
        } else if (status == sf::Socket::Status::Disconnected) {
            LOG_INFO("network", "Player " + std::to_string(player->GetId()) + " disconnected");
            player->DetachSocket();
            ++i;
        } else {
            ++i;
        }
    }
}

bool INetworkModule::FlushSendBuffer(CPlayer& player)
{
    while (!player.m_send_buffer.empty())
    {
        size_t sent = 0;
        auto status = player.GetSocket().send(player.m_send_buffer.data(), player.m_send_buffer.size(), sent);
        if (sent > 0)
        {
            player.m_send_buffer.erase(player.m_send_buffer.begin(), player.m_send_buffer.begin() + sent);
        }

        if (status == sf::Socket::Status::Done) continue;
        if (status == sf::Socket::Status::Partial || status == sf::Socket::Status::NotReady) return true;
        if (status == sf::Socket::Status::Disconnected) return false;
        LOG_WARN("network", "Send failed for player " + std::to_string(player.GetId()));
        return false;
    }
    return true;
}

bool INetworkModule::QueueMessage(CPlayer& player, const ServerMessage& msg)
{
    size_t len = ServerMessage::GetPackedSize(msg);
    if (len == 0 || len > UINT16_MAX) return false;
    if (player.m_send_buffer.size() + len + packet_length_prefix_size > game_config::network_max_send_buffer_size)
    {
        LOG_WARN("network", "Player " + std::to_string(player.GetId()) + " output buffer overflow");
        return false;
    }

    std::vector<uint8_t> buffer(len);
    size_t packed_len = ServerMessage::pack(msg, buffer.data());
    if (packed_len == 0 || packed_len > UINT16_MAX) return false;

    uint16_t packet_len = static_cast<uint16_t>(packed_len);
    player.m_send_buffer.push_back(static_cast<uint8_t>(packet_len & 0xff));
    player.m_send_buffer.push_back(static_cast<uint8_t>((packet_len >> 8) & 0xff));
    player.m_send_buffer.insert(player.m_send_buffer.end(), buffer.begin(), buffer.begin() + packed_len);
    return true;
}

bool INetworkModule::ProcessPlayerBuffer(CPlayer& player)
{
    while (player.m_receive_buffer.size() >= 1)
    {
        uint8_t type = player.m_receive_buffer[0] & client_type_mask;
        size_t packet_size = (type == static_cast<uint8_t>(ClientOperate::Type::Input) ||
                              type == static_cast<uint8_t>(ClientOperate::Type::Equip))
                                 ? client_extended_packet_size
                                 : client_compact_packet_size;
        if (player.m_receive_buffer.size() < packet_size) return false;

        ClientOperate op = ClientOperate::parse(player.m_receive_buffer.data(), packet_size);
        if (op.type != ClientOperate::Type::Unknown)
        {
            if (op.disconnect.value_or(false)) return true;
            else if (!player.GetEntity() && op.agree.value_or(false)) Respawn(player);
            else player.HandleOperate(op);
        }
        player.m_receive_buffer.erase(player.m_receive_buffer.begin(), player.m_receive_buffer.begin() + packet_size);
    }
    return false;
}

void INetworkModule::Respawn(CPlayer& player)
{
    auto entity = CreateMob(EMobType::PlayerFlower, &m_lobby_world,
                            {game_config::player_respawn_x, game_config::player_respawn_y}, ERarity::Common);
    auto* raw_flower = dynamic_cast<CPlayerFlower*>(entity.get());
    if (!raw_flower) return;

    raw_flower->m_name = player.GetName();
    raw_flower->m_level = 1;
    raw_flower = dynamic_cast<CPlayerFlower*>(m_lobby_world.InsertEntity(std::move(entity)));
    if (!raw_flower) return;

    player.SetOwnedEntity(raw_flower);
    player.m_logged_missing_entity = false;
    LOG_INFO("network", "Player " + std::to_string(player.GetId()) + " respawned");
}

CPlayer* INetworkModule::FindReconnectablePlayer() const
{
    for (const auto& player : m_players)
    {
        if (player && !player->IsConnected() && !player->IsTimedOut()) return player.get();
    }
    return nullptr;
}

void INetworkModule::TickTimeouts(float dt)
{
    for (size_t i = 0; i < m_players.size();)
    {
        CPlayer* player = m_players[i].get();
        if (!player)
        {
            m_players.erase(m_players.begin() + i);
            continue;
        }

        player->TickTimeout(dt);
        if (player->IsTimedOut())
        {
            DropPlayer(i, "timed out");
            continue;
        }
        ++i;
    }
}

void INetworkModule::DropPlayer(size_t index, const std::string& reason)
{
    if (index >= m_players.size() || !m_players[index]) return;

    CPlayer& player = *m_players[index];
    LOG_INFO("network", "Player " + std::to_string(player.GetId()) + " dropped: " + reason);
    if (CEntity* entity = player.GetEntity()) entity->m_is_marked_for_des = true;
    FreePlayerId(player.GetId());
    m_players.erase(m_players.begin() + index);
}

ServerEntitySnap INetworkModule::BuildEntitySnap(const CEntity& entity, const CEntity& owner) const
{
    ServerEntitySnap snap;
    snap.entity_id = static_cast<net_entity_id>(entity.m_id);
    snap.entity_type = GetEntityType(entity);
    snap.team = static_cast<uint8_t>(std::clamp(entity.m_team, 0, static_cast<int>(UINT8_MAX)));
    snap.pos = entity.m_pos;
    snap.radius = entity.m_radius;
    snap.hp_percent = GetHealthPercent(entity);

    if (&entity == &owner) snap.flags |= static_cast<uint8_t>(ServerEntityFlag::Owner);
    if (entity.m_is_marked_for_des) snap.flags |= static_cast<uint8_t>(ServerEntityFlag::Dead);

    if (const auto* flower = dynamic_cast<const CFlower*>(&entity))
    {
        if (flower->m_attacking) snap.flags |= static_cast<uint8_t>(ServerEntityFlag::Attacking);
        if (flower->m_defending) snap.flags |= static_cast<uint8_t>(ServerEntityFlag::Defending);
        snap.angle = PackAngle(flower->m_facing_angle);
    }
    return snap;
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

