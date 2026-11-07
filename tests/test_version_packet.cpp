#include "test_framework.h"
#include "Shared/network_msg.h"
#include "Shared/version.h"

// 握手必须携带当前协议版本。

TEST(PartyAndScenePacketsKeepFullIdentityAndRejectTruncation)
{
    ServerMessage message;
    message.type = ServerMessage::Type::PartyState;
    message.party.leader_id = 70000; message.party.leader_generation = 0x20000000000001ULL;
    message.party.members.push_back({70000, message.party.leader_generation, "同名玩家", true,
        9, 0x20000000000002ULL, true, 80000, 0x20000000000003ULL});
    std::vector<uint8_t> bytes(ServerMessage::GetPackedSize(message));
    CHECK(ServerMessage::pack(message, bytes.data()) == bytes.size());
    const auto party = ServerMessage::parse(bytes.data(), bytes.size());
    CHECK(party.type == message.type && party.party == message.party);
    for (size_t size = 0; size < bytes.size(); ++size)
        CHECK(ServerMessage::parse(bytes.data(), size).type == ServerMessage::Type::Unknown);
    bytes.push_back(0);
    CHECK(ServerMessage::parse(bytes.data(), bytes.size()).type == ServerMessage::Type::Unknown);
    bytes.pop_back(); bytes[26] = 7;
    CHECK(ServerMessage::parse(bytes.data(), bytes.size()).type == ServerMessage::Type::Unknown);
    message.party.members.push_back(message.party.members.front());
    CHECK(ServerMessage::GetPackedSize(message) == 0);

    message.type = ServerMessage::Type::Welcome;
    message.player_id = 70000; message.player_session_generation = 0x20000000000001ULL;
    message.world_id = 9; message.world_generation = 0x20000000000002ULL;
    message.map_name = "garden.tmj"; message.owner_entity_id = 80000;
    bytes.resize(ServerMessage::GetPackedSize(message));
    CHECK(ServerMessage::pack(message, bytes.data()) == bytes.size());
    const auto welcome = ServerMessage::parse(bytes.data(), bytes.size());
    CHECK(welcome.type == message.type && welcome.player_id == message.player_id);
    CHECK(welcome.player_session_generation == message.player_session_generation && welcome.world_generation == message.world_generation);
    for (size_t size = 0; size < bytes.size(); ++size)
        CHECK(ServerMessage::parse(bytes.data(), size).type == ServerMessage::Type::Unknown);

    message.type = ServerMessage::Type::Chat;
    message.chat.player_id = 70000; message.chat.session_generation = 0x20000000000001ULL;
    message.chat.player_name = "玩家"; message.chat.message = "队内消息"; message.chat.flag = EChatFlag::Squad;
    bytes.resize(ServerMessage::GetPackedSize(message));
    CHECK(ServerMessage::pack(message, bytes.data()) == bytes.size());
    const auto chat = ServerMessage::parse(bytes.data(), bytes.size());
    CHECK(chat.type == message.type && chat.chat.player_id == message.chat.player_id);
    CHECK(chat.chat.session_generation == message.chat.session_generation && chat.chat.message == message.chat.message);
    for (size_t size = 0; size < bytes.size(); ++size)
        CHECK(ServerMessage::parse(bytes.data(), size).type == ServerMessage::Type::Unknown);
}

TEST(VersionPacketParsesMatchingVersion)
{
    const uint8_t data[] = { 0xfb, florrbt::version_major, florrbt::version_minor, florrbt::version_patch };
    bool ok = false;
    const ClientVersionPacket packet = ClientVersionPacket::parse(data, sizeof(data), &ok);
    CHECK(ok);
    CHECK(packet.MatchesServer());
}

TEST(ClientFeatureAndAccountLinkStatesRoundTripWithStrictLengths)
{
    for (auto type : {ServerMessage::Type::ClientFeatures, ServerMessage::Type::AccountLinks}) {
        ServerMessage message; message.type = type; message.service_payload = R"({"schema":1,"text":"绑定"})";
        std::vector<uint8_t> bytes(ServerMessage::GetPackedSize(message));
        CHECK(ServerMessage::pack(message, bytes.data()) == bytes.size());
        const auto result = ServerMessage::parse(bytes.data(), bytes.size());
        CHECK(result.type == type && result.service_payload == message.service_payload);
        for (size_t n = 0; n < bytes.size(); ++n) CHECK(ServerMessage::parse(bytes.data(), n).type == ServerMessage::Type::Unknown);
        bytes.push_back(0); CHECK(ServerMessage::parse(bytes.data(), bytes.size()).type == ServerMessage::Type::Unknown);
        message.service_payload.assign(max_service_payload_size + 1, 'a'); CHECK(ServerMessage::GetPackedSize(message) == 0);
    }
}

TEST(AccountMenuAuthModesRoundTripAndRejectUnknownModes)
{
    for (const auto mode : {ClientAuthRequest::Mode::AccountLogin, ClientAuthRequest::Mode::AccountRegister, ClientAuthRequest::Mode::EnterGame})
    {
        ClientAuthRequest request; request.mode = mode; request.name = "menu-fixture";
        std::vector<uint8_t> bytes(ClientAuthRequest::GetPackedSize(request));
        CHECK(ClientAuthRequest::pack(request, bytes.data()) == bytes.size());
        bool ok = false;
        const auto restored = ClientAuthRequest::parse(bytes.data(), bytes.size(), &ok);
        CHECK(ok && restored.mode == mode && restored.name == request.name);
        for (size_t size = 0; size < bytes.size(); ++size) { ClientAuthRequest::parse(bytes.data(), size, &ok); CHECK(!ok); }
        bytes[1] = 8; ClientAuthRequest::parse(bytes.data(), bytes.size(), &ok); CHECK(!ok);
    }
}

TEST(EconomyPacketsPreserveExactIdentityAndRejectTruncation)
{
    ClientEconomyRequest request;
    request.operation = EEconomyOperation::Loadout;
    request.context = {0x20000000000001ULL, 0x20000000000002ULL, "@sandbox:npc:fixture", "process:world:entity"};
    request.primary = {{1, 2, 1}, {}}; request.secondary = {{}, {2, 3, 1}};
    std::vector<uint8_t> bytes(ClientEconomyRequest::GetPackedSize(request));
    CHECK(ClientEconomyRequest::pack(request, bytes.data()) == bytes.size());
    bool ok = false;
    const auto copy = ClientEconomyRequest::parse(bytes.data(), bytes.size(), &ok);
    CHECK(ok && copy.context.request_id == request.context.request_id);
    CHECK(copy.context.expected_revision == request.context.expected_revision);
    CHECK(copy.context.inventory_scope == request.context.inventory_scope && copy.context.actor_scope == request.context.actor_scope);
    CHECK(copy.primary == request.primary && copy.secondary == request.secondary);
    CHECK(copy.Fingerprint() == request.Fingerprint());
    for (size_t size = 0; size < bytes.size(); ++size)
    {
        ClientEconomyRequest::parse(bytes.data(), size, &ok); CHECK(!ok);
    }
    auto bad = bytes; bad[1] = 0xff;
    ClientEconomyRequest::parse(bad.data(), bad.size(), &ok); CHECK(!ok);
    bad = bytes; bad[20 + bad[2] + bad[3]] = 65;
    ClientEconomyRequest::parse(bad.data(), bad.size(), &ok); CHECK(!ok);

    for (auto type : {ServerMessage::Type::Inventory, ServerMessage::Type::EconomyResult})
    {
        ServerMessage msg; msg.type = type;
        msg.inventory_scope = request.context.inventory_scope; msg.inventory_actor_scope = request.context.actor_scope;
        msg.inventory_revision = request.context.expected_revision; msg.inventory_last_request = request.context.request_id;
        msg.economy_request_id = request.context.request_id; msg.economy_operation = request.operation;
        msg.economy_recorded = true; msg.economy_outcome = EEconomyOutcome::Completed;
        msg.inventory = {{1, 1, 10}}; msg.craft_items = {{2, 3, 1}};
        bytes.resize(ServerMessage::GetPackedSize(msg));
        CHECK(ServerMessage::pack(msg, bytes.data()) == bytes.size());
        const auto restored = ServerMessage::parse(bytes.data(), bytes.size());
        CHECK(restored.type == type && restored.inventory_revision == msg.inventory_revision);
        CHECK(restored.inventory_scope == msg.inventory_scope);
        if (type == ServerMessage::Type::Inventory)
            CHECK(restored.inventory_last_request == msg.inventory_last_request && restored.inventory_actor_scope == msg.inventory_actor_scope);
        else CHECK(restored.economy_request_id == msg.economy_request_id && restored.craft_items == msg.craft_items && restored.economy_recorded);
        for (size_t size = 1; size < bytes.size(); ++size)
            CHECK(ServerMessage::parse(bytes.data(), size).type == ServerMessage::Type::Unknown);
    }
}

TEST(VersionPacketRejectsMismatchedVersion)
{
    const uint8_t data[] = { 0xfb, 9, 9, 9 };
    bool ok = false;
    const ClientVersionPacket packet = ClientVersionPacket::parse(data, sizeof(data), &ok);
    CHECK(ok); // structurally valid packet...
    CHECK(!packet.MatchesServer()); // ...but the version must not be accepted
}

TEST(VersionPacketRejectsTruncatedPacket)
{
    const uint8_t data[] = { 0xfb, 1, 0 };
    bool ok = true;
    ClientVersionPacket::parse(data, sizeof(data), &ok);
    CHECK(!ok);
}

TEST(VersionPacketRejectsWrongStartByte)
{
    const uint8_t data[] = { 0xfa, 1, 0, 1 };
    bool ok = true;
    ClientVersionPacket::parse(data, sizeof(data), &ok);
    CHECK(!ok);
}

TEST(VersionPacketSizeReportsZeroWhenIncomplete)
{
    const uint8_t data[] = { 0xfb, 1 };
    CHECK(ClientVersionPacket::GetPacketSize(data, sizeof(data)) == 0);
    CHECK(ClientVersionPacket::GetPacketSize(data, client_version_packet_size) == client_version_packet_size);
}

TEST(AuthRecoveryPacketsRoundTripAndRejectTruncatedCredentials)
{
    ClientAuthRequest request;
    request.name = "fixture";
    request.recovery_device_id = std::string(32, 'a');
    request.recovery_token = std::string(64, 'b');
    std::vector<uint8_t> bytes(ClientAuthRequest::GetPackedSize(request));
    CHECK(ClientAuthRequest::pack(request, bytes.data()) == bytes.size());
    bool ok = false;
    const auto restored = ClientAuthRequest::parse(bytes.data(), bytes.size(), &ok);
    CHECK(ok && restored.recovery_device_id == request.recovery_device_id && restored.recovery_token == request.recovery_token);
    for (size_t size = 0; size < bytes.size(); ++size)
    { ClientAuthRequest::parse(bytes.data(), size, &ok); CHECK(!ok); }
    request.recovery_token.pop_back();
    CHECK(ClientAuthRequest::GetPackedSize(request) == 0);
    CHECK(ClientAuthRequest::pack(request, bytes.data()) == 0);

    ServerMessage message; message.type = ServerMessage::Type::AuthResult;
    message.auth_result_code = EAuthResultCode::Authenticated;
    message.auth_recovery_device_id = std::string(32, 'a');
    message.auth_recovery_token = std::string(64, 'b');
    message.auth_recovery_expires_at = 2000000000;
    bytes.resize(ServerMessage::GetPackedSize(message));
    CHECK(ServerMessage::pack(message, bytes.data()) == bytes.size());
    const auto reply = ServerMessage::parse(bytes.data(), bytes.size());
    CHECK(reply.type == ServerMessage::Type::AuthResult);
    CHECK(reply.auth_recovery_device_id == message.auth_recovery_device_id && reply.auth_recovery_token == message.auth_recovery_token);
    CHECK(reply.auth_recovery_expires_at == message.auth_recovery_expires_at);
    CHECK(ServerMessage::parse(bytes.data(), bytes.size() - 1).type == ServerMessage::Type::Unknown);
}
