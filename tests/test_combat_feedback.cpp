#include "test_framework.h"
#include "Shared/network_msg.h"
#include <limits>

TEST(CombatFeedbackRoundTripAndPackedSize)
{
    ServerMessage msg;
    msg.type = ServerMessage::Type::CombatFeedback;
    msg.combat_hits.push_back({ 42, { -10.5f, 20.25f }, 36.f, 120.5f, 1, 7, 6 });
    std::vector<uint8_t> bytes(ServerMessage::GetPackedSize(msg));
    CHECK(bytes.size() == 21);
    CHECK(ServerMessage::pack(msg, bytes.data()) == bytes.size());
    const auto read = ServerMessage::parse(bytes.data(), bytes.size());
    CHECK(read.type == msg.type);
    CHECK(read.combat_hits.size() == 1);
    CHECK_NEAR(read.combat_hits[0].amount, 120.5f, 0.0001);
    CHECK_NEAR(read.combat_hits[0].pos.x, -10.5f, 0.0001);
    CHECK(read.combat_hits[0].kind == 1 && read.combat_hits[0].source_type == 7);
    CHECK(ServerMessage::parse(bytes.data(), bytes.size() - 1).type == ServerMessage::Type::Unknown);
}

TEST(CombatFeedbackRejectsInvalidAmountsAndBoundsTheBatch)
{
    ServerMessage msg;
    msg.type = ServerMessage::Type::CombatFeedback;
    msg.combat_hits.resize(100);
    CHECK(ServerMessage::GetPackedSize(msg) == 2 + 64 * 19);
    std::vector<uint8_t> bytes(ServerMessage::GetPackedSize(msg));
    msg.combat_hits[0].amount = std::numeric_limits<float>::quiet_NaN();
    ServerMessage::pack(msg, bytes.data());
    CHECK(ServerMessage::parse(bytes.data(), bytes.size()).type == ServerMessage::Type::Unknown);
    bytes[1] = 65;
    CHECK(ServerMessage::parse(bytes.data(), bytes.size()).type == ServerMessage::Type::Unknown);
}
