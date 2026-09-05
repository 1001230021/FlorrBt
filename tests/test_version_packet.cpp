#include "test_framework.h"
#include "Shared/network_msg.h"
#include "Shared/version.h"

// Guards the client version handshake packet: the first packet a client sends
// after connecting must parse cleanly and carry the server's wire version.

TEST(VersionPacketParsesMatchingVersion)
{
    const uint8_t data[] = { 0xfb, florrbt::version_major, florrbt::version_minor, florrbt::version_patch };
    bool ok = false;
    const ClientVersionPacket packet = ClientVersionPacket::parse(data, sizeof(data), &ok);
    CHECK(ok);
    CHECK(packet.MatchesServer());
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
