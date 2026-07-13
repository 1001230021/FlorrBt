#include "network_module.h"
#include "../../Engine/account_data.h"
#include "../Game/controllers/player_controller.h"
#include "../Game/entities/drop.h"
#include "../Game/entities/flower.h"
#include "../Game/entities/petals/petal.h"
#include "../Game/entities/projectile.h"
#include "../Game/gamecontext.h"
#include "../Game/gameworld.h"
#include "../Game/player.h"
#include "../Game/states/states.h"
#include "../Game/zone_mob_tools.h"
#include "../report.h"
#include <SFML/Network.hpp>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cctype>
#include <limits>
#include <random>
#include <vector>

namespace
{
uint8_t GetEntityType(const CEntity& entity)
{
    if (const auto* mob = dynamic_cast<const CMobBase*>(&entity)) return static_cast<uint8_t>(mob->m_mob_type);
    if (const auto* drop = dynamic_cast<const CDrop*>(&entity))
        return server_drop_entity_type_offset + static_cast<uint8_t>(drop->GetType());
    if (const auto* petal = dynamic_cast<const CPetal*>(&entity))
        return server_petal_entity_type_offset + static_cast<uint8_t>(petal->m_type);
    return 0;
}

std::string GetEntityName(const CEntity& entity)
{
    if (const auto* player_flower = dynamic_cast<const CPlayerFlower*>(&entity)) return player_flower->m_name;
    if (const auto* mob = dynamic_cast<const CMobBase*>(&entity)) return std::string(GetMobTypeName(mob->m_mob_type));
    if (const auto* drop = dynamic_cast<const CDrop*>(&entity)) return std::string(GetPetalTypeName(drop->GetType()));
    if (const auto* petal = dynamic_cast<const CPetal*>(&entity)) return std::string(GetPetalTypeName(petal->m_type));
    if (dynamic_cast<const CPollenProjectile*>(&entity)) return "Pollen";
    if (dynamic_cast<const CMissile*>(&entity)) return "Missile";
    return "Entity";
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

float GetPrimordialDefaultMobRadius()
{
    float base_radius = std::max({game_config::mob_beetle_radius, game_config::mob_normal_ladybug_radius,
                                  game_config::default_flower_radius, game_config::mob_player_flower_radius,
                                  game_config::mob_summoned_beetle_radius, game_config::mob_soldier_ant_radius,
                                  game_config::mob_soldier_fire_ant_radius, game_config::mob_soldier_termite_radius,
                                  game_config::mob_summoned_soldier_ant_radius, game_config::mob_bee_radius,
                                  game_config::mob_hornet_radius, game_config::mob_bumblebee_radius});
    float primordial_scale =
        std::pow(static_cast<float>(GetLevel(ERarity::Primordial)), game_config::mob_radius_scale_exp);
    return base_radius * primordial_scale;
}

std::string ToLower(std::string text)
{
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return text;
}

bool FlowerHasAvailablePetal(const CFlower& flower, EPetalType type)
{
    for (const auto& slot : flower.GetSlots())
    {
        if (!slot.m_available || slot.m_banned || !slot.m_p_proto) continue;
        if (slot.m_p_proto->m_type == type) return true;
    }
    return false;
}

std::mt19937& RespawnRng()
{
    static std::mt19937 rng{std::random_device{}()};
    return rng;
}

bool RandomPointInCheckpoint(const FlorrBtMap::Checkpoint& checkpoint, sf::Vector2f& out_pos)
{
    if (checkpoint.w <= 0.f || checkpoint.h <= 0.f) return false;
    std::uniform_real_distribution<float> random_x(checkpoint.x, checkpoint.x + checkpoint.w);
    std::uniform_real_distribution<float> random_y(checkpoint.y, checkpoint.y + checkpoint.h);
    out_pos = {random_x(RespawnRng()), random_y(RespawnRng())};
    return true;
}

bool RandomPointInSpawnZone(const FlorrBtMap::Zone& zone, sf::Vector2f& out_pos)
{
    if (zone.vertices.empty()) return false;

    float min_x = std::numeric_limits<float>::max();
    float min_y = std::numeric_limits<float>::max();
    float max_x = std::numeric_limits<float>::lowest();
    float max_y = std::numeric_limits<float>::lowest();
    for (const auto& vertex : zone.vertices)
    {
        min_x = std::min(min_x, vertex.x);
        min_y = std::min(min_y, vertex.y);
        max_x = std::max(max_x, vertex.x);
        max_y = std::max(max_y, vertex.y);
    }
    if (min_x >= max_x || min_y >= max_y) return false;

    std::uniform_real_distribution<float> random_x(min_x, max_x);
    std::uniform_real_distribution<float> random_y(min_y, max_y);
    for (int attempt = 0; attempt < 32; ++attempt)
    {
        sf::Vector2f candidate = {random_x(RespawnRng()), random_y(RespawnRng())};
        if (!IsPointInZone(zone, candidate)) continue;
        out_pos = candidate;
        return true;
    }
    return false;
}

sf::Vector2f PickRespawnPosition(CGameWorld& world)
{
    const FlorrBtMap* map = world.GetMap();
    if (map)
    {
        std::vector<size_t> checkpoints;
        for (size_t i = 0; i < map->checkpoints.size(); ++i)
        {
            if (map->checkpoints[i].w > 0.f && map->checkpoints[i].h > 0.f) checkpoints.push_back(i);
        }
        if (!checkpoints.empty())
        {
            std::uniform_int_distribution<size_t> dist(0, checkpoints.size() - 1);
            sf::Vector2f pos;
            if (RandomPointInCheckpoint(map->checkpoints[checkpoints[dist(RespawnRng())]], pos)) return pos;
        }

        std::vector<size_t> zones;
        for (size_t i = 0; i < map->zones.size(); ++i)
        {
            if (map->zones[i].vertices.size() >= 3) zones.push_back(i);
        }
        if (!zones.empty())
        {
            std::uniform_int_distribution<size_t> dist(0, zones.size() - 1);
            for (size_t attempt = 0; attempt < zones.size(); ++attempt)
            {
                sf::Vector2f pos;
                if (RandomPointInSpawnZone(map->zones[zones[dist(RespawnRng())]], pos)) return pos;
            }
        }
    }

    return {game_config::player_respawn_x, game_config::player_respawn_y};
}
}

bool INetworkModule::Init()
{
    int port = game_config::port;
    if (m_listener_v6.listen(port, sf::IpAddress::AnyV6) == sf::Socket::Status::Done)
    {
        m_listener_v6.setBlocking(false);
        m_listening_v6 = true;
        LOG_INFO("network", "Listening on IPv6 port " + std::to_string(port));
    } else {
        LOG_WARN("network", "IPv6 listen failed on port " + std::to_string(port));
    }

    if (m_listener_v4.listen(port, sf::IpAddress::AnyV4) == sf::Socket::Status::Done)
    {
        m_listener_v4.setBlocking(false);
        m_listening_v4 = true;
        LOG_INFO("network", "Listening on IPv4 port " + std::to_string(port));
    } else {
        LOG_WARN("network", "IPv4 listen failed on port " + std::to_string(port));
    }

    if (!m_listening_v4 && !m_listening_v6)
    {
        LOG_FATAL("network", "Failed to listen on port " + std::to_string(port));
        return false;
    }
    return true;
}

void INetworkModule::Tick(float dt)
{
    AcceptConnections();
    ProcessMessages();
    RespawnDeadControlledEntities();
    ProcessDropPickups();
    TickBans(dt);
    m_snapshot_timer += dt;
    if (m_snapshot_timer >= game_config::network_snapshot_interval)
    {
        m_snapshot_timer = 0.f;
        SendSnapshots();
    }
    TickTimeouts(dt);
}

void INetworkModule::ProcessDropPickups()
{
    for (auto& player : m_players)
    {
        if (!player || !player->IsConnected() || !player->IsAuthenticated()) continue;
        if (player->TickDropPickup()) QueueInventory(*player);
    }
}

void INetworkModule::SendSnapshots()
{
    for (auto& player : m_players)
    {
        if (!player) continue;
        if (!player->IsConnected()) continue;
        if (!player->IsAuthenticated()) continue;

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

        QueueOwnerState(*player);

        float view_radius = owner_mob->GetFinalStats()->horizon;
        float snap_radius = view_radius + GetPrimordialDefaultMobRadius();
        auto visible_entities = owner->GameWorld()->GetSpatialGrid().QueryRange(
            owner->m_pos, snap_radius, [owner](const CEntity* entity)
            {
                return entity && entity->IsVisible() && entity != owner;
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
            if (!entity->IsVisible()) continue;
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
    m_listener_v6.close();
    m_listener_v4.close();
    m_listening_v4 = false;
    m_listening_v6 = false;
    m_players.clear();
    LOG_INFO("network", "Shut down");
}

void INetworkModule::AcceptConnections()
{
    auto accept_from = [this](sf::TcpListener& listener)
    {
        sf::TcpSocket new_socket;
        if (listener.accept(new_socket) != sf::Socket::Status::Done) return;

        auto remote = new_socket.getRemoteAddress();
        std::string remote_address = remote ? remote->toString() : "";
        if (IsIpBanned(remote_address))
        {
            LOG_INFO("network", "Rejected banned IP: " + remote_address);
            new_socket.disconnect();
            return;
        }

        new_socket.setBlocking(false);

        int id = GetNewPlayerId();
        auto player = std::make_unique<CPlayer>(std::move(new_socket), id, "Pending" + std::to_string(id));
        player->SetRemoteAddress(remote_address);
        m_players.push_back(std::move(player));
        LOG_INFO("network", "New connection accepted, pending auth ID: " + std::to_string(id));
    };

    if (m_listening_v4) accept_from(m_listener_v4);
    if (m_listening_v6) accept_from(m_listener_v6);
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
            EPlayerBufferResult result = ProcessPlayerBuffer(*player);
            if (result == EPlayerBufferResult::RequestedDisconnect)
            {
                LOG_INFO("network", "Player " + std::to_string(player->GetId()) + " requested disconnect");
                DropPlayer(i, "requested disconnect");
                continue;
            }
            if (result == EPlayerBufferResult::RemovePendingPlayer)
            {
                DropPlayer(i, "reconnected through account");
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

bool INetworkModule::QueueAuthResult(CPlayer& player, bool success, const std::string& message)
{
    ServerMessage msg;
    msg.type = ServerMessage::Type::AuthResult;
    msg.auth_success = success;
    msg.auth_message = message;
    return QueueMessage(player, msg);
}

bool INetworkModule::QueueWelcome(CPlayer& player)
{
    ServerMessage msg;
    msg.type = ServerMessage::Type::Welcome;
    msg.player_id = static_cast<uint16_t>(player.GetId());
    msg.tick_rate = static_cast<uint8_t>(std::round(1.f / game_config::server_fixed_dt));

    if (CEntity* entity = player.GetEntity())
    {
        msg.owner_entity_id = static_cast<net_entity_id>(entity->m_id);
        if (CGameWorld* world = entity->GameWorld()) msg.map_name = world->GetMapPath();
    }
    else
    {
        msg.owner_entity_id = 0;
        msg.map_name = m_lobby_world.GetMapPath();
    }

    return QueueMessage(player, msg);
}

bool INetworkModule::QueueOwnerState(CPlayer& player)
{
    ServerMessage msg;
    msg.type = ServerMessage::Type::OwnerState;
    msg.level = 1;
    msg.flags = 0;
    msg.exp = 0;
    msg.exp_required = 10;

    auto* player_flower = dynamic_cast<CPlayerFlower*>(player.GetEntity());
    if (player_flower)
    {
        msg.level = static_cast<uint8_t>(std::clamp(player_flower->m_level, 0, 255));
        msg.exp = static_cast<uint32_t>(std::max(0, player_flower->m_exp));
        msg.exp_required = static_cast<uint32_t>(std::max(1, player_flower->ExpRequired()));
    }

    auto* flower = dynamic_cast<CFlower*>(player.GetEntity());
    if (flower)
    {
        auto& slots = flower->GetSlots();
        msg.owner_slots.reserve(slots.size());
        for (const CPetalSlot& slot : slots)
        {
            SOwnerPetalSlot owner_slot;
            if (slot.m_p_proto)
            {
                owner_slot.petal_type = static_cast<uint8_t>(slot.m_p_proto->m_type);
                owner_slot.rarity = static_cast<uint8_t>(slot.m_stored_rarity);
            }
            msg.owner_slots.push_back(owner_slot);
        }
    }

    std::vector<SInventoryItem> secondary_slots = CAccountDataStore::GetSecondarySlots(player.GetAccountName());
    msg.secondary_slots.reserve(secondary_slots.size());
    for (const SInventoryItem& item : secondary_slots)
    {
        SOwnerPetalSlot slot;
        if (item.petal_type != 0 && item.rarity != 0)
        {
            slot.petal_type = item.petal_type;
            slot.rarity = item.rarity;
        }
        msg.secondary_slots.push_back(slot);
    }

    msg.petal_slots = static_cast<uint8_t>(std::min<size_t>(msg.owner_slots.size(), UINT8_MAX));
    msg.secondary_slots_count = static_cast<uint8_t>(std::min<size_t>(msg.secondary_slots.size(), UINT8_MAX));
    return QueueMessage(player, msg);
}

bool INetworkModule::QueueOwnerStateUpdate(CPlayer& player)
{
    return QueueOwnerState(player);
}

bool INetworkModule::QueueInventory(CPlayer& player)
{
    ServerMessage msg;
    msg.type = ServerMessage::Type::Inventory;
    msg.inventory = CAccountDataStore::GetInventory(player.GetAccountName());
    return QueueMessage(player, msg);
}

bool INetworkModule::QueueCraftResult(CPlayer& player, const SCraftResult& result)
{
    ServerMessage msg;
    msg.type = ServerMessage::Type::CraftResult;
    msg.craft_success = result.successes > 0;
    msg.craft_petal_type = result.petal_type;
    msg.craft_rarity = result.rarity;
    msg.craft_consumed = result.consumed;
    msg.craft_items = result.items;
    return QueueMessage(player, msg);
}

bool INetworkModule::QueueChat(CPlayer& player, const CServer::SChatEntry& chat)
{
    ServerMessage msg;
    msg.type = ServerMessage::Type::Chat;
    msg.chat.flag = chat.flag;
    msg.chat.player_id = static_cast<uint16_t>(std::min<uint32_t>(chat.player_id, UINT16_MAX));
    msg.chat.time = chat.system_time;
    msg.chat.player_name = chat.player_name;
    msg.chat.message = chat.message;
    return QueueMessage(player, msg);
}

bool INetworkModule::SendChatToPlayer(CPlayer& player, const CServer::SChatEntry& chat)
{
    if (!player.IsConnected() || !player.IsAuthenticated()) return false;
    return QueueChat(player, chat);
}

void INetworkModule::BroadcastChat(const CServer::SChatEntry& chat)
{
    for (auto& player : m_players)
    {
        if (!player || !player->IsConnected() || !player->IsAuthenticated()) continue;

        if (chat.flag == EChatFlag::Server)
        {
            if (chat.target_player_id >= 0 && static_cast<int>(player->GetId()) != chat.target_player_id) continue;
            QueueChat(*player, chat);
            continue;
        }

        if (chat.flag == EChatFlag::Whisper)
        {
            if (static_cast<int>(player->GetId()) == chat.target_player_id ||
                player->GetId() == chat.player_id)
                QueueChat(*player, chat);
            continue;
        }

        CEntity* entity = player->GetEntity();
        if (!entity || entity->m_is_marked_for_des || entity->GameWorld() != chat.world) continue;

        if (chat.flag == EChatFlag::Global)
        {
            QueueChat(*player, chat);
            continue;
        }

        auto* mob = dynamic_cast<CMobBase*>(entity);
        const SMobStats* stats = mob ? mob->GetFinalStats() : nullptr;
        float range = (stats ? stats->horizon : game_config::default_horizon) * 2.f;
        if (DistanceSq(entity->m_pos, chat.pos) <= range * range) QueueChat(*player, chat);
    }
}

INetworkModule::EPlayerBufferResult INetworkModule::ProcessPlayerBuffer(CPlayer& player)
{
    while (player.m_receive_buffer.size() >= 1)
    {
        if (ClientAuthRequest::IsPacketStart(player.m_receive_buffer[0]))
        {
            size_t packet_size = ClientAuthRequest::GetPacketSize(player.m_receive_buffer.data(), player.m_receive_buffer.size());
            if (packet_size == 0 || player.m_receive_buffer.size() < packet_size) return EPlayerBufferResult::Continue;

            bool ok = false;
            ClientAuthRequest request = ClientAuthRequest::parse(player.m_receive_buffer.data(), packet_size, &ok);
            player.m_receive_buffer.erase(player.m_receive_buffer.begin(), player.m_receive_buffer.begin() + packet_size);
            if (!ok)
            {
                QueueAuthResult(player, false, "Bad auth packet");
                continue;
            }

            EPlayerBufferResult result = HandleAuthRequest(player, request);
            if (result != EPlayerBufferResult::Continue) return result;
            continue;
        }

        if (ClientChatRequest::IsPacketStart(player.m_receive_buffer[0]))
        {
            size_t packet_size = ClientChatRequest::GetPacketSize(player.m_receive_buffer.data(), player.m_receive_buffer.size());
            if (packet_size == 0 || player.m_receive_buffer.size() < packet_size) return EPlayerBufferResult::Continue;

            bool ok = false;
            ClientChatRequest request = ClientChatRequest::parse(player.m_receive_buffer.data(), packet_size, &ok);
            player.m_receive_buffer.erase(player.m_receive_buffer.begin(), player.m_receive_buffer.begin() + packet_size);
            if (!player.IsAuthenticated())
            {
                QueueAuthResult(player, false, "Please login first");
                return EPlayerBufferResult::RequestedDisconnect;
            }
            if (ok) HandleChatRequest(player, request);
            continue;
        }

        if (ClientSecondarySlotRequest::IsPacketStart(player.m_receive_buffer[0]))
        {
            size_t packet_size = ClientSecondarySlotRequest::GetPacketSize(player.m_receive_buffer.data(), player.m_receive_buffer.size());
            if (packet_size == 0 || player.m_receive_buffer.size() < packet_size) return EPlayerBufferResult::Continue;

            bool ok = false;
            ClientSecondarySlotRequest request = ClientSecondarySlotRequest::parse(player.m_receive_buffer.data(), packet_size, &ok);
            player.m_receive_buffer.erase(player.m_receive_buffer.begin(), player.m_receive_buffer.begin() + packet_size);
            if (!player.IsAuthenticated())
            {
                QueueAuthResult(player, false, "Please login first");
                return EPlayerBufferResult::RequestedDisconnect;
            }
            if (ok) HandleSecondarySlotRequest(player, request);
            continue;
        }

        if (ClientCraftRequest::IsPacketStart(player.m_receive_buffer[0]))
        {
            size_t packet_size = ClientCraftRequest::GetPacketSize(player.m_receive_buffer.data(), player.m_receive_buffer.size());
            if (packet_size == 0 || player.m_receive_buffer.size() < packet_size) return EPlayerBufferResult::Continue;

            bool ok = false;
            ClientCraftRequest request = ClientCraftRequest::parse(player.m_receive_buffer.data(), packet_size, &ok);
            player.m_receive_buffer.erase(player.m_receive_buffer.begin(), player.m_receive_buffer.begin() + packet_size);
            if (!player.IsAuthenticated())
            {
                QueueAuthResult(player, false, "Please login first");
                return EPlayerBufferResult::RequestedDisconnect;
            }
            if (ok) HandleCraftRequest(player, request);
            continue;
        }

        if (!player.IsAuthenticated())
        {
            QueueAuthResult(player, false, "Please login first");
            return EPlayerBufferResult::RequestedDisconnect;
        }

        uint8_t type = player.m_receive_buffer[0] & client_type_mask;
        size_t packet_size = (type == static_cast<uint8_t>(ClientOperate::Type::Input) ||
                              type == static_cast<uint8_t>(ClientOperate::Type::Equip))
                                 ? client_extended_packet_size
                                 : client_compact_packet_size;
        if (player.m_receive_buffer.size() < packet_size) return EPlayerBufferResult::Continue;

        ClientOperate op = ClientOperate::parse(player.m_receive_buffer.data(), packet_size);
        if (op.type != ClientOperate::Type::Unknown)
        {
            if (op.disconnect.value_or(false)) return EPlayerBufferResult::RequestedDisconnect;
            else if (op.agree.value_or(false))
            {
                auto* flower = dynamic_cast<CPlayerFlower*>(player.GetEntity());
                if (!player.GetEntity() || (flower && flower->m_is_dead))
                    Respawn(player);
                else
                    player.HandleOperate(op);
            } else {
                auto* flower = dynamic_cast<CPlayerFlower*>(player.GetEntity());
                if (!flower || !flower->m_is_dead) player.HandleOperate(op);
            }
        }
        player.m_receive_buffer.erase(player.m_receive_buffer.begin(), player.m_receive_buffer.begin() + packet_size);
    }
    return EPlayerBufferResult::Continue;
}

void INetworkModule::HandleChatRequest(CPlayer& player, const ClientChatRequest& request)
{
    CEntity* entity = player.GetEntity();
    if (!entity || entity->m_is_marked_for_des) return;

    if (!request.message.empty() && request.message.front() == '/')
    {
        report::HandleServerCommand(player, request.message);
        return;
    }

    if (player.IsMuted())
    {
        CServer::SChatEntry entry;
        entry.flag = EChatFlag::Server;
        entry.target_player_id = static_cast<int>(player.GetId());
        entry.player_name = "Server";
        entry.message = "You are muted for " + std::to_string(static_cast<int>(std::ceil(player.GetMuteTimer()))) + "s.";
        entry.system_time = static_cast<uint32_t>(
            std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count());
        SendChatToPlayer(player, entry);
        return;
    }

    EChatFlag flag = request.flag == EChatFlag::Local ? EChatFlag::Local : EChatFlag::Global;
    if (const CServer::SChatEntry* chat = CServer::GetInstance()->SubmitChat(entity->GameWorld(), entity->m_pos, flag,
                                                                             player.GetId(), player.GetName(), request.message))
    {
        BroadcastChat(*chat);
    }
}

void INetworkModule::HandleSecondarySlotRequest(CPlayer& player, const ClientSecondarySlotRequest& request)
{
    if (request.slot_index >= game_config::default_flower_petal_num_max) return;

    if (request.petal_type == 0 || request.rarity == 0)
        CAccountDataStore::ClearSecondarySlot(player.GetAccountName(), request.slot_index);
    else
        CAccountDataStore::SetSecondarySlot(player.GetAccountName(), request.slot_index, request.petal_type, request.rarity);

    QueueInventory(player);
    QueueOwnerState(player);
}

void INetworkModule::HandleCraftRequest(CPlayer& player, const ClientCraftRequest& request)
{
    SCraftResult result;
    if (player.TryCraftPetal(request.petal_type, request.rarity, request.count, &result))
    {
        QueueCraftResult(player, result);
        QueueInventory(player);
    }
}

INetworkModule::EPlayerBufferResult INetworkModule::HandleAuthRequest(CPlayer& player, const ClientAuthRequest& request)
{
    if (player.IsAuthenticated())
    {
        QueueAuthResult(player, false, "Already authenticated");
        return EPlayerBufferResult::Continue;
    }

    std::string error;
    bool register_mode = request.mode == ClientAuthRequest::Mode::Register;
    if (IsNameBanned(request.name))
    {
        QueueAuthResult(player, false, "Name is banned");
        return EPlayerBufferResult::Continue;
    }

    if (!CAccountDataStore::LoginOrRegister(request.name, request.password, register_mode, &error))
    {
        QueueAuthResult(player, false, error.empty() ? "Auth failed" : error);
        return EPlayerBufferResult::Continue;
    }

    for (const auto& existing_player : m_players)
    {
        if (!existing_player || existing_player.get() == &player) continue;
        if (!existing_player->IsAuthenticated()) continue;
        if (existing_player->GetAccountName() != request.name) continue;
        if (existing_player->IsConnected())
        {
            QueueAuthResult(player, false, "Account is already online");
            return EPlayerBufferResult::Continue;
        }
    }

    if (CPlayer* reconnect_player = FindReconnectablePlayer(request.name, &player))
    {
        reconnect_player->AttachSocket(std::move(player.GetSocket()));
        if (!reconnect_player->GetEntity())
        {
            if (auto* controller = m_lobby_world.GetController())
                controller->OnPlayerConnect(m_lobby_world, reconnect_player);
        }
        QueueAuthResult(*reconnect_player, true, "Reconnected");
        QueueWelcome(*reconnect_player);
        QueueInventory(*reconnect_player);
        QueueOwnerState(*reconnect_player);
        LOG_INFO("network", "Account " + request.name + " reconnected as player " + std::to_string(reconnect_player->GetId()));
        return EPlayerBufferResult::RemovePendingPlayer;
    }

    player.Authenticate(request.name);
    if (auto* controller = m_lobby_world.GetController())
        controller->OnPlayerConnect(m_lobby_world, &player);

    QueueAuthResult(player, true, register_mode ? "Registered" : "Logged in");
    QueueWelcome(player);
    QueueInventory(player);
    QueueOwnerState(player);
    LOG_INFO("network", "Account " + request.name + " authenticated as player " + std::to_string(player.GetId()));
    return EPlayerBufferResult::Continue;
}

void INetworkModule::Respawn(CPlayer& player)
{
    if (auto* old_flower = dynamic_cast<CPlayerFlower*>(player.GetEntity()))
    {
        old_flower->PrepareRespawnDestroy();
        player.SetOwnedEntity(nullptr);
    }

    auto entity = CreateMob(EMobType::PlayerFlower, &m_lobby_world, PickRespawnPosition(m_lobby_world), ERarity::Common);
    auto* raw_flower = dynamic_cast<CPlayerFlower*>(entity.get());
    if (!raw_flower) return;

    raw_flower->m_name = player.GetName();
    raw_flower = dynamic_cast<CPlayerFlower*>(m_lobby_world.InsertEntity(std::move(entity)));
    if (!raw_flower) return;

    player.SetOwnedEntity(raw_flower);
    player.ApplySavedProgress();
    player.ApplySavedSlots();
    player.m_logged_missing_entity = false;
    LOG_INFO("network", "Player " + std::to_string(player.GetId()) + " respawned");
}

CPlayer* INetworkModule::FindPlayerById(uint32_t player_id) const
{
    for (const auto& player : m_players)
    {
        if (player && player->GetId() == player_id) return player.get();
    }
    return nullptr;
}

bool INetworkModule::KickPlayer(uint32_t player_id, const std::string& reason)
{
    for (size_t i = 0; i < m_players.size(); ++i)
    {
        if (!m_players[i] || m_players[i]->GetId() != player_id) continue;
        DropPlayer(i, reason);
        return true;
    }
    return false;
}

void INetworkModule::BanPlayerIp(uint32_t player_id, float seconds)
{
    CPlayer* player = FindPlayerById(player_id);
    if (!player || player->GetRemoteAddress().empty()) return;

    m_ip_bans.push_back({player->GetRemoteAddress(), seconds});
    KickPlayer(player_id, "IP banned");
}

void INetworkModule::BanName(const std::string& name, float seconds)
{
    std::string target = ToLower(name);
    m_name_bans.push_back({target, seconds});
    for (size_t i = 0; i < m_players.size();)
    {
        CPlayer* player = m_players[i].get();
        if (player && ToLower(player->GetAccountName()) == target)
        {
            DropPlayer(i, "name banned");
            continue;
        }
        ++i;
    }
}

bool INetworkModule::IsIpBanned(const std::string& ip) const
{
    for (const auto& [banned_ip, timer] : m_ip_bans)
    {
        if (banned_ip == ip && (timer < 0.f || timer > 0.f)) return true;
    }
    return false;
}

bool INetworkModule::IsNameBanned(const std::string& name) const
{
    std::string target = ToLower(name);
    for (const auto& [banned_name, timer] : m_name_bans)
    {
        if (banned_name == target && (timer < 0.f || timer > 0.f)) return true;
    }
    return false;
}

bool INetworkModule::AssignPlayerEntity(uint32_t player_id, int entity_id)
{
    CPlayer* player = FindPlayerById(player_id);
    if (!player || !player->IsAuthenticated()) return false;

    CEntity* entity = m_lobby_world.GetEntity(entity_id);
    auto* mob = dynamic_cast<CMobBase*>(entity);
    if (!mob || mob->IsDead()) return false;

    player->ResetControlledMob();
    mob->SetController(std::make_unique<CPlayerController>());
    player->SetOwnedEntity(mob);
    player->m_logged_missing_entity = false;
    QueueWelcome(*player);
    QueueOwnerState(*player);
    LOG_INFO("network", "Player " + std::to_string(player_id) + " now controls entity " + std::to_string(entity_id));
    return true;
}

CGameContext* INetworkModule::GameContext() const
{
    return m_lobby_world.GameContext();
}

void INetworkModule::RespawnDeadControlledEntities()
{
    for (auto& player : m_players)
    {
        if (!player || !player->IsAuthenticated()) continue;

        CEntity* entity = player->GetEntity();
        if (!entity)
        {
            if (!player->HasOwnedEntity()) continue;
            player->SetOwnedEntity(nullptr);
            Respawn(*player);
            if (player->IsConnected())
            {
                QueueWelcome(*player);
                QueueOwnerState(*player);
            }
            continue;
        }
        if (dynamic_cast<CPlayerFlower*>(entity)) continue;
        if (!entity->IsDead()) continue;

        player->SetOwnedEntity(nullptr);
        Respawn(*player);
        if (player->IsConnected())
        {
            QueueWelcome(*player);
            QueueOwnerState(*player);
        }
    }
}

CPlayer* INetworkModule::FindReconnectablePlayer(const std::string& account_name, const CPlayer* pending_player) const
{
    for (const auto& player : m_players)
    {
        if (!player || player.get() == pending_player) continue;
        if (!player->IsAuthenticated()) continue;
        if (player->GetAccountName() != account_name) continue;
        if (!player->IsConnected() && !player->IsTimedOut()) return player.get();
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

void INetworkModule::TickBans(float dt)
{
    auto tick = [dt](auto& bans)
    {
        for (auto& [key, timer] : bans)
        {
            (void)key;
            if (timer > 0.f) timer -= dt;
        }
        bans.erase(std::remove_if(bans.begin(), bans.end(),
                                  [](const auto& ban)
                                  {
                                      return ban.second == 0.f || (ban.second > -1.f && ban.second <= 0.f);
                                  }),
                   bans.end());
    };

    tick(m_ip_bans);
    tick(m_name_bans);
}

void INetworkModule::DropPlayer(size_t index, const std::string& reason)
{
    if (index >= m_players.size() || !m_players[index]) return;

    CPlayer& player = *m_players[index];
    LOG_INFO("network", "Player " + std::to_string(player.GetId()) + " dropped: " + reason);
    if (auto* flower = dynamic_cast<CPlayerFlower*>(player.GetEntity()))
        flower->PrepareRespawnDestroy();
    else if (CEntity* entity = player.GetEntity())
        entity->m_is_marked_for_des = true;
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
    snap.name = GetEntityName(entity);
    snap.angle = PackAngle(entity.m_facing_angle);

    if (const auto* mob = dynamic_cast<const CMobBase*>(&entity)) snap.rarity = static_cast<uint8_t>(mob->GetRarity());
    if (const auto* player_flower = dynamic_cast<const CPlayerFlower*>(&entity))
    {
        snap.rarity = static_cast<uint8_t>(std::clamp(player_flower->m_level, 0, static_cast<int>(UINT8_MAX)));
        const auto& slots = player_flower->GetSlots();
        snap.primary_slots.reserve(slots.size());
        for (const CPetalSlot& slot : slots)
        {
            SOwnerPetalSlot primary_slot;
            if (slot.m_p_proto)
            {
                primary_slot.petal_type = static_cast<uint8_t>(slot.m_p_proto->m_type);
                primary_slot.rarity = static_cast<uint8_t>(slot.m_stored_rarity);
            }
            snap.primary_slots.push_back(primary_slot);
        }
    }
    if (const auto* drop = dynamic_cast<const CDrop*>(&entity)) snap.rarity = static_cast<uint8_t>(drop->GetRarity());
    if (const auto* petal = dynamic_cast<const CPetal*>(&entity)) snap.rarity = static_cast<uint8_t>(petal->m_rarity);

    if (&entity == &owner) snap.flags |= static_cast<uint8_t>(ServerEntityFlag::Owner);
    if (entity.IsDead()) snap.flags |= static_cast<uint8_t>(ServerEntityFlag::Dead);

    if (const auto* attackable = dynamic_cast<const IAttackableMob*>(&entity))
    {
        if (attackable->IsAttacking()) snap.flags |= static_cast<uint8_t>(ServerEntityFlag::Attacking);
        if (attackable->IsDefending()) snap.flags |= static_cast<uint8_t>(ServerEntityFlag::Defending);
    }

    if (const auto* flower = dynamic_cast<const CFlower*>(&entity))
    {
        if (FlowerHasAvailablePetal(*flower, EPetalType::Relic))
            snap.flags |= static_cast<uint8_t>(ServerEntityFlag::Relic);
        if (FlowerHasAvailablePetal(*flower, EPetalType::Antennae))
            snap.flags |= static_cast<uint8_t>(ServerEntityFlag::Antennae);
    }
    if (const auto* mob = dynamic_cast<const CMobBase*>(&entity))
    {
        auto& mutable_mob = *const_cast<CMobBase*>(mob);
        if (!mutable_mob.FindStates<CUndeadState>().empty())
            snap.flags |= static_cast<uint8_t>(ServerEntityFlag::Undead);
        if (!mutable_mob.FindStates<CCorruptionState>().empty())
            snap.flags |= static_cast<uint8_t>(ServerEntityFlag::Corrupted);
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
