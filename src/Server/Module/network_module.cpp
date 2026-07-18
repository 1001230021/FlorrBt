#include "network_module.h"
#include "../../Engine/account_data.h"
#include "../../Engine/logger.h"
#include "../Game/controllers/player_controller.h"
#include "../Game/entities/flower.h"
#include "../Game/entities/petals/petal.h"
#include "../Game/gamecontext.h"
#include "../Game/gameworld.h"
#include "../Game/player.h"
#include "../Game/talent.h"
#include "../report.h"
#include "../../Shared/network_msg.h"
#include <SFML/Network.hpp>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cctype>
#include <exception>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
size_t PacketLengthAt(const std::vector<uint8_t>& buffer, size_t offset)
{
    if (offset + packet_length_prefix_size > buffer.size()) return 0;
    return static_cast<size_t>(buffer[offset]) | (static_cast<size_t>(buffer[offset + 1]) << 8);
}

void CompactSentPackets(CPlayer& player)
{
    if (player.m_send_offset == 0) return;
    if (player.m_send_offset >= player.m_send_buffer.size())
    {
        player.m_send_buffer.clear();
        player.m_send_offset = 0;
        return;
    }

    size_t erase_until = 0;
    while (erase_until + packet_length_prefix_size <= player.m_send_buffer.size())
    {
        size_t packet_len = PacketLengthAt(player.m_send_buffer, erase_until);
        size_t packet_end = erase_until + packet_length_prefix_size + packet_len;
        if (packet_end > player.m_send_buffer.size() || packet_end > player.m_send_offset) break;
        erase_until = packet_end;
    }

    if (erase_until == 0) return;
    player.m_send_buffer.erase(player.m_send_buffer.begin(), player.m_send_buffer.begin() + erase_until);
    player.m_send_offset -= erase_until;
}

size_t PendingSendBytes(const CPlayer& player)
{
    if (player.m_send_offset >= player.m_send_buffer.size()) return 0;
    return player.m_send_buffer.size() - player.m_send_offset;
}

std::string ToLower(std::string text)
{
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return text;
}

std::string Trim(std::string_view text)
{
    size_t begin = 0;
    while (begin < text.size() && std::isspace(static_cast<unsigned char>(text[begin]))) ++begin;

    size_t end = text.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1]))) --end;

    return std::string(text.substr(begin, end - begin));
}

std::string LogPriorityName(ELogPriority priority)
{
    switch (priority)
    {
    case ELogPriority::Debug: return "DEBUG";
    case ELogPriority::Info: return "INFO";
    case ELogPriority::Warning: return "WARN";
    case ELogPriority::Error: return "ERROR";
    case ELogPriority::Fatal: return "FATAL";
    }
    return "UNKNOWN";
}

bool ExtractRconPayload(const std::string& message, std::string& payload)
{
    if (message.empty() || message.front() != '/') return false;

    size_t command_begin = 1;
    size_t command_end = command_begin;
    while (command_end < message.size() && !std::isspace(static_cast<unsigned char>(message[command_end])))
        ++command_end;

    if (ToLower(message.substr(command_begin, command_end - command_begin)) != "rcon") return false;

    payload = Trim(std::string_view(message).substr(command_end));
    return true;
}

void SendPrivateServerMessage(INetworkModule& network, CPlayer& player, const std::string& message)
{
    auto* server = CServer::GetInstance();
    if (!server) return;

    std::string text = message.empty() ? "(empty)" : message;
    size_t offset = 0;
    while (offset < text.size())
    {
        std::string part = text.substr(offset, max_chat_message_size);
        offset += part.size();
        if (const CServer::SChatEntry* chat =
                server->SubmitChat(nullptr, {0.f, 0.f}, EChatFlag::Server, 0, "RCON", part,
                                   static_cast<int>(player.GetId())))
        {
            network.SendChatToPlayer(player, *chat);
        }
    }
}

bool HandleRconCommand(INetworkModule& network, CPlayer& player, const std::string& message)
{
    std::string payload;
    if (!ExtractRconPayload(message, payload)) return false;

    if (!player.IsAuthenticated()) return true;

    if (payload.empty())
    {
        SendPrivateServerMessage(network, player,
                                 player.IsRconAuthorized() ? "Usage: /rcon [server command]" :
                                                             "Usage: /rcon [password]");
        return true;
    }

    if (!player.IsRconAuthorized())
    {
        if (game_config::rcon_password.empty())
        {
            SendPrivateServerMessage(network, player, "RCON is disabled.");
            return true;
        }

        if (payload == game_config::rcon_password)
        {
            player.SetRconAuthorized(true);
            LOG_INFO("rcon", "Player " + player.GetName() + " authorized from " + player.GetRemoteAddress());
            SendPrivateServerMessage(network, player, "RCON authorized.");
        } else {
            LOG_WARN("rcon", "Failed RCON login for player " + player.GetName() + " from " + player.GetRemoteAddress());
            SendPrivateServerMessage(network, player, "RCON authorization failed.");
        }
        return true;
    }

    auto* server = CServer::GetInstance();
    if (!server)
    {
        SendPrivateServerMessage(network, player, "RCON failed: server is unavailable.");
        return true;
    }

    LOG_INFO("rcon", "Player " + player.GetName() + " executed: " + payload);

    std::vector<std::string> captured_lines;
    size_t sink_id = CLogger::AddSink([&captured_lines](const std::string& sender, ELogPriority priority,
                                                        const std::string& msg)
    {
        captured_lines.push_back("[" + sender + "][" + LogPriorityName(priority) + "] " + msg);
    });

    try
    {
        server->GetConsole().ExecuteLine(payload);
    }
    catch (const std::exception& e)
    {
        captured_lines.push_back(std::string("[rcon][ERROR] ") + e.what());
    }
    catch (...)
    {
        captured_lines.push_back("[rcon][ERROR] Unknown exception");
    }
    CLogger::RemoveSink(sink_id);

    if (captured_lines.empty())
    {
        SendPrivateServerMessage(network, player, "Executed: " + payload);
    } else {
        constexpr size_t max_rcon_reply_lines = 12;
        size_t count = std::min(captured_lines.size(), max_rcon_reply_lines);
        for (size_t i = 0; i < count; ++i)
            SendPrivateServerMessage(network, player, captured_lines[i]);
        if (captured_lines.size() > count)
            SendPrivateServerMessage(network, player,
                                     "... " + std::to_string(captured_lines.size() - count) + " more lines");
    }
    return true;
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
    m_player_lifecycle_service.RespawnDeadControlledEntities(m_players, m_lobby_world, *this);
    m_player_lifecycle_service.ProcessDropPickups(m_players, *this);
    TickBans(dt);
    m_snapshot_timer += dt;
    if (m_snapshot_timer >= game_config::network_snapshot_interval)
    {
        m_snapshot_timer = 0.f;
        SendSnapshots();
    }
    TickTimeouts(dt);
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

        DropQueuedSnapshots(*player);
        if (PendingSendBytes(*player) > CSnapshotService::backlog_skip_bytes)
        {
            if ((m_snapshot_id % 60) == 0)
            {
                LOG_WARN("network", "Skipping snapshot for player " + std::to_string(player->GetId()) +
                                        " because output backlog is " + std::to_string(PendingSendBytes(*player)) +
                                        " bytes");
            }
            if (!FlushSendBuffer(*player))
            {
                LOG_INFO("network", "Player " + std::to_string(player->GetId()) + " disconnected while flushing backlog");
                player->DetachSocket();
            }
            continue;
        }

        CSnapshotService::SBuildResult snapshot;
        if (!m_snapshot_service.BuildSnapshot(*player, m_snapshot_id, snapshot)) continue;

        if (snapshot.message.entities.size() < snapshot.visible_count && (m_snapshot_id % 60) == 0)
        {
            LOG_WARN("network", "Trimmed snapshot for player " + std::to_string(player->GetId()) + ": " +
                                    std::to_string(snapshot.message.entities.size()) + "/" +
                                    std::to_string(snapshot.visible_count) + " entities, " +
                                    std::to_string(snapshot.packed_size) + " bytes");
        }

        if (!QueueMessage(*player, snapshot.message))
        {
            LOG_WARN("network", "Failed to queue snapshot for player " + std::to_string(player->GetId()) +
                                    " (" + std::to_string(snapshot.message.entities.size()) + " entities, " +
                                    std::to_string(ServerMessage::GetPackedSize(snapshot.message)) +
                                    " bytes, backlog " + std::to_string(PendingSendBytes(*player)) + " bytes)");
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
    CompactSentPackets(player);
    while (!player.m_send_buffer.empty())
    {
        size_t sent = 0;
        auto status = player.GetSocket().send(player.m_send_buffer.data() + player.m_send_offset,
                                              player.m_send_buffer.size() - player.m_send_offset, sent);
        if (sent > 0)
        {
            player.m_send_offset += sent;
            CompactSentPackets(player);
        }

        if (status == sf::Socket::Status::Done)
        {
            if (sent == 0 && !player.m_send_buffer.empty()) return true;
            continue;
        }
        if (status == sf::Socket::Status::Partial || status == sf::Socket::Status::NotReady) return true;
        if (status == sf::Socket::Status::Disconnected) return false;
        LOG_WARN("network", "Send failed for player " + std::to_string(player.GetId()));
        return false;
    }
    return true;
}

void INetworkModule::DropQueuedSnapshots(CPlayer& player)
{
    CompactSentPackets(player);
    if (player.m_send_buffer.empty()) return;

    size_t scan_offset = 0;
    if (player.m_send_offset > 0)
    {
        size_t first_len = PacketLengthAt(player.m_send_buffer, 0);
        size_t first_end = packet_length_prefix_size + first_len;
        if (first_end > player.m_send_buffer.size()) return;
        scan_offset = first_end;
    }

    std::vector<uint8_t> kept;
    kept.reserve(player.m_send_buffer.size());
    kept.insert(kept.end(), player.m_send_buffer.begin(), player.m_send_buffer.begin() + scan_offset);

    size_t dropped_packets = 0;
    size_t offset = scan_offset;
    while (offset + packet_length_prefix_size <= player.m_send_buffer.size())
    {
        size_t packet_len = PacketLengthAt(player.m_send_buffer, offset);
        size_t packet_payload = offset + packet_length_prefix_size;
        size_t packet_end = packet_payload + packet_len;
        if (packet_len == 0 || packet_end > player.m_send_buffer.size())
        {
            kept.insert(kept.end(), player.m_send_buffer.begin() + offset, player.m_send_buffer.end());
            offset = player.m_send_buffer.size();
            break;
        }

        uint8_t type = player.m_send_buffer[packet_payload];
        if (type == static_cast<uint8_t>(ServerMessage::Type::Snapshot))
        {
            dropped_packets++;
        } else {
            kept.insert(kept.end(), player.m_send_buffer.begin() + offset, player.m_send_buffer.begin() + packet_end);
        }
        offset = packet_end;
    }

    if (offset < player.m_send_buffer.size())
    {
        kept.insert(kept.end(), player.m_send_buffer.begin() + offset, player.m_send_buffer.end());
    }
    if (dropped_packets == 0) return;

    player.m_send_buffer.swap(kept);
}

bool INetworkModule::QueueMessage(CPlayer& player, const ServerMessage& msg)
{
    CompactSentPackets(player);
    size_t len = ServerMessage::GetPackedSize(msg);
    if (len == 0 || len > UINT16_MAX)
    {
        if (msg.type == ServerMessage::Type::Snapshot)
        {
            LOG_WARN("network", "Snapshot packet size is invalid: " + std::to_string(len) + " bytes");
        }
        return false;
    }
    if (PendingSendBytes(player) + len + packet_length_prefix_size > game_config::network_max_send_buffer_size)
    {
        LOG_WARN("network", "Player " + std::to_string(player.GetId()) + " output buffer overflow");
        return false;
    }

    thread_local std::vector<uint8_t> payload(UINT16_MAX);
    if (payload.size() < UINT16_MAX) payload.resize(UINT16_MAX);
    size_t packed_len = ServerMessage::pack(msg, payload.data());
    if (packed_len == 0 || packed_len > UINT16_MAX)
        return false;
    if (PendingSendBytes(player) + packed_len + packet_length_prefix_size > game_config::network_max_send_buffer_size)
    {
        LOG_WARN("network", "Player " + std::to_string(player.GetId()) + " output buffer overflow");
        return false;
    }

    const size_t begin = player.m_send_buffer.size();
    uint16_t packet_len = static_cast<uint16_t>(packed_len);
    player.m_send_buffer.resize(begin + packet_length_prefix_size);
    player.m_send_buffer[begin] = static_cast<uint8_t>(packet_len & 0xff);
    player.m_send_buffer[begin + 1] = static_cast<uint8_t>((packet_len >> 8) & 0xff);
    player.m_send_buffer.insert(player.m_send_buffer.end(), payload.begin(), payload.begin() + packed_len);
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
    msg.talent_points = static_cast<uint16_t>(std::clamp(player.GetTalentPoints(), 0, static_cast<int>(UINT16_MAX)));
    for (const ITalent* talent : player.GetTalents())
    {
        if (!talent) continue;
        msg.talents.push_back({talent->m_id, static_cast<uint8_t>(talent->m_rarity),
                               static_cast<uint8_t>(std::clamp(talent->m_rank, 0, 255))});
    }

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

        if (ClientTalentRequest::IsPacketStart(player.m_receive_buffer[0]))
        {
            size_t packet_size = ClientTalentRequest::GetPacketSize(player.m_receive_buffer.data(), player.m_receive_buffer.size());
            if (packet_size == 0 || player.m_receive_buffer.size() < packet_size) return EPlayerBufferResult::Continue;

            bool ok = false;
            ClientTalentRequest request = ClientTalentRequest::parse(player.m_receive_buffer.data(), packet_size, &ok);
            player.m_receive_buffer.erase(player.m_receive_buffer.begin(), player.m_receive_buffer.begin() + packet_size);
            if (!player.IsAuthenticated())
            {
                QueueAuthResult(player, false, "Please login first");
                return EPlayerBufferResult::RequestedDisconnect;
            }
            if (ok) HandleTalentRequest(player, request);
            continue;
        }

        if (player.m_receive_buffer[0] == client_state_request_packet_type)
        {
            player.m_receive_buffer.erase(player.m_receive_buffer.begin());
            if (!player.IsAuthenticated())
            {
                QueueAuthResult(player, false, "Please login first");
                return EPlayerBufferResult::RequestedDisconnect;
            }
            QueueInventory(player);
            QueueOwnerState(player);
            if (!FlushSendBuffer(player))
                return EPlayerBufferResult::RequestedDisconnect;
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
                {
                    if (m_player_lifecycle_service.Respawn(player, m_lobby_world))
                        m_player_lifecycle_service.NotifyPlayerWorldChanged(player, *this);
                }
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
    if (HandleRconCommand(*this, player, request.message))
        return;

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

void INetworkModule::HandleTalentRequest(CPlayer& player, const ClientTalentRequest& request)
{
    bool changed = false;
    for (const STalentPacketItem& item : request.talents)
    {
        ERarity rarity = static_cast<ERarity>(item.rarity);
        int rank = static_cast<int>(item.rank);
        if (request.action == ClientTalentAction::Remove)
            changed = player.RemoveTalent(item.id, rarity, rank) || changed;
        else
            changed = player.AddTalent(item.id, rarity, rank) || changed;
    }

    QueueOwnerState(player);
    if (changed) QueueInventory(player);
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
        m_player_lifecycle_service.NotifyPlayerLogin(*reconnect_player, *this);
        if (!FlushSendBuffer(*reconnect_player))
        {
            reconnect_player->DetachSocket();
            return EPlayerBufferResult::RemovePendingPlayer;
        }
        LOG_INFO("network", "Account " + request.name + " reconnected as player " + std::to_string(reconnect_player->GetId()));
        return EPlayerBufferResult::RemovePendingPlayer;
    }

    player.Authenticate(request.name);
    player.SetUseNewPlayerSpawn(register_mode);
    if (auto* controller = m_lobby_world.GetController())
        controller->OnPlayerConnect(m_lobby_world, &player);

    QueueAuthResult(player, true, register_mode ? "Registered" : "Logged in");
    m_player_lifecycle_service.NotifyPlayerLogin(player, *this);
    if (!FlushSendBuffer(player))
    {
        player.DetachSocket();
        return EPlayerBufferResult::RequestedDisconnect;
    }
    LOG_INFO("network", "Account " + request.name + " authenticated as player " + std::to_string(player.GetId()));
    return EPlayerBufferResult::Continue;
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
    m_player_lifecycle_service.NotifyPlayerWorldChanged(*player, *this);
    LOG_INFO("network", "Player " + std::to_string(player_id) + " now controls entity " + std::to_string(entity_id));
    return true;
}

CGameContext* INetworkModule::GameContext() const
{
    return m_lobby_world.GameContext();
}

void INetworkModule::NotifyPlayerWorldChanged(CPlayer& player)
{
    m_player_lifecycle_service.NotifyPlayerWorldChanged(player, *this);
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
