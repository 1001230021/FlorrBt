#include "test_framework.h"
#include "Shared/network_msg.h"

TEST(SkillResultRoundTripsAndRejectsInvalidPayloads)
{
    ServerMessage message;
    message.type = ServerMessage::Type::SkillResult;
    message.skill_result = ESkillRequestResult::InvalidIndex;
    message.skill_index = 7;
    message.skill_count = 4;

    std::vector<uint8_t> bytes(ServerMessage::GetPackedSize(message));
    CHECK(bytes.size() == 4);
    CHECK(ServerMessage::pack(message, bytes.data()) == bytes.size());

    const ServerMessage parsed = ServerMessage::parse(bytes.data(), bytes.size());
    CHECK(parsed.type == ServerMessage::Type::SkillResult);
    CHECK(parsed.skill_result == ESkillRequestResult::InvalidIndex);
    CHECK(parsed.skill_index == 7);
    CHECK(parsed.skill_count == 4);

    CHECK(ServerMessage::parse(bytes.data(), bytes.size() - 1).type == ServerMessage::Type::Unknown);
    bytes[1] = static_cast<uint8_t>(ESkillRequestResult::Unavailable) + 1;
    CHECK(ServerMessage::parse(bytes.data(), bytes.size()).type == ServerMessage::Type::Unknown);
}
