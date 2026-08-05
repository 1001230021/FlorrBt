#pragma once
#include "entity_type.h"
#include "inventory.h"
#include "state_type.h"
#include "talent_type.h"
#include <SFML/System/Vector2.hpp>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <vector>

constexpr uint8_t bit_mask = 0x01;
constexpr uint8_t client_type_mask = 0x03;
constexpr uint8_t chore_agree_bit = 4;
constexpr uint8_t chore_attack_bit = 2;
constexpr uint8_t chore_defend_bit = 3;
constexpr uint8_t chore_disconnect_bit = 5;
constexpr uint8_t chore_digging_bit = 6;
constexpr uint8_t client_auth_packet_type = 0xf0;
constexpr uint8_t client_chat_packet_type = 0xf1;
constexpr uint8_t client_secondary_slot_packet_type = 0xf2;
constexpr uint8_t client_craft_packet_type = 0xf3;
constexpr uint8_t client_talent_packet_type = 0xf4;
constexpr uint8_t client_state_request_packet_type = 0xf5;
constexpr uint8_t client_forge_packet_type = 0xf6;
constexpr uint8_t client_snapshot_ack_packet_type = 0xf7;
constexpr uint8_t client_input_frame_packet_type = 0xf8;
constexpr size_t client_compact_packet_size = 1;
constexpr size_t client_extended_packet_size = 3;
constexpr size_t client_auth_header_size = 6;
constexpr size_t client_chat_header_size = 3;
constexpr size_t client_secondary_slot_packet_size = 4;
constexpr size_t client_craft_packet_size = 7;
constexpr size_t client_forge_packet_size = client_craft_packet_size;
constexpr size_t client_snapshot_ack_packet_size = 5;
constexpr size_t client_input_frame_packet_size = 16;
constexpr size_t client_talent_header_size = 3;
constexpr size_t talent_packet_item_size = 4;
constexpr size_t max_client_talent_items = 64;
constexpr size_t inventory_item_packet_size = 6;
constexpr size_t owner_slot_packet_size = 2;
constexpr size_t entity_state_packet_size = 2;
constexpr size_t max_auth_name_size = 32;
constexpr size_t max_auth_password_size = 64;
constexpr size_t max_auth_email_size = 254;
constexpr size_t max_auth_code_size = 32;
constexpr size_t max_chat_message_size = 180;
constexpr size_t packet_length_prefix_size = 2;
// Client packets:
// Input:        [type:2 bits][move_x:1 byte][move_y:1 byte]
// EquipPetal:   [type:2 bits][petal_type:1 byte][slot:4 bits][rarity:4 bits]
// UnequipPetal: [type:2 bits][slot:4 bits]
// Chores:       [type:2 bits][attack:1 bit][defend:1 bit][agree:1 bit][disconnect:1 bit]
// Auth:         [0xf0][mode:1 byte][name_len:1 byte][password_len:1 byte][email_len:1 byte][code_len:1 byte]
//               [name][password][email][code]
// Chat:         [0xf1][flag:1 byte][message_len:1 byte][message]
// SecondarySlot:[0xf2][slot:1 byte][petal_type:1 byte][rarity:1 byte]
// Craft:        [0xf3][petal_type:1 byte][rarity:1 byte][count:4 bytes]
// Talent:       [0xf4][action:1 byte][count:1 byte][talent_id:2 bytes][rarity:1 byte][rank:1 byte]...
// StateRequest: [0xf5]
// Forge:        [0xf6][petal_type:1 byte][rarity:1 byte][count:4 bytes]
// SnapshotAck:  [0xf7][snapshot_id:4 bytes], UINT32_MAX requests a full snapshot
// InputFrame:   [0xf8][sequence:4 bytes][target_server_tick:8 bytes][move_x:1 byte][move_y:1 byte][flags:1 byte]

enum class EChatFlag : uint8_t
{
    Global = 0,
    Local = 1,
    Server = 2,
    Whisper = 3,
    Squad = 4,
};

enum class ClientMsgType : uint8_t
{
    PlayerInput = 0,
    EquipPetal,
    UnequipPetal,
    Chores,
};

struct ClientOperate
{
    enum class Type : uint8_t
    {
        Input = 0x00,
        Equip = 0x01,
        Unequip = 0x02,
        Chores = 0x03,
        Unknown = 0xff,
    } type = Type::Unknown;

    std::optional<int8_t> move_x;
    std::optional<int8_t> move_y;

    std::optional<uint8_t> petal_type;
    std::optional<uint8_t> slot_index;
    std::optional<uint8_t> rarity;

    std::optional<bool> is_attacking;
    std::optional<bool> is_defending;
    std::optional<bool> agree;
    std::optional<bool> disconnect;
    std::optional<bool> is_digging;

    static ClientOperate parse(const uint8_t* data, size_t len)
    {
        ClientOperate op{};
        if (!data || len < 1) return op;

        uint8_t first = data[0];
        uint8_t type_code = first & client_type_mask;
        op.type = static_cast<Type>(type_code);

        switch (op.type)
        {
        case Type::Input:
            if (len < 3)
            {
                op.type = Type::Unknown;
                break;
            }
            op.move_x = static_cast<int8_t>(data[1]);
            op.move_y = static_cast<int8_t>(data[2]);
            break;
        case Type::Equip:
            if (len < 3)
            {
                op.type = Type::Unknown;
                break;
            }
            op.petal_type = data[1];
            op.slot_index = (data[2] >> 4) & 0x0F;
            op.rarity = data[2] & 0x0F;
            break;
        case Type::Unequip:
            op.slot_index = (first >> 2) & 0x0F;
            break;
        case Type::Chores:
            op.is_attacking = ((first >> chore_attack_bit) & bit_mask) != 0;
            op.is_defending = ((first >> chore_defend_bit) & bit_mask) != 0;
            op.agree = ((first >> chore_agree_bit) & bit_mask) != 0;
            op.disconnect = ((first >> chore_disconnect_bit) & bit_mask) != 0;
            op.is_digging = ((first >> chore_digging_bit) & bit_mask) != 0;
            break;
        default:
            op.type = Type::Unknown;
            break;
        }
        return op;
    }

    static size_t pack(const ClientOperate& op, uint8_t* out)
    {
        if (!out) return 0;

        switch (op.type)
        {
        case Type::Input:
            out[0] = 0x00;
            out[1] = static_cast<uint8_t>(op.move_x.value_or(0));
            out[2] = static_cast<uint8_t>(op.move_y.value_or(0));
            return 3;
        case Type::Equip:
            out[0] = 0x01;
            out[1] = op.petal_type.value_or(0);
            out[2] = ((op.slot_index.value_or(0) & 0x0F) << 4) | (op.rarity.value_or(0) & 0x0F);
            return 3;
        case Type::Unequip:
            out[0] = 0x02 | ((op.slot_index.value_or(0) & 0x0F) << 2);
            return 1;
        case Type::Chores:
            out[0] = static_cast<uint8_t>(Type::Chores);
            if (op.is_attacking.value_or(false)) out[0] |= (bit_mask << chore_attack_bit);
            if (op.is_defending.value_or(false)) out[0] |= (bit_mask << chore_defend_bit);
            if (op.agree.value_or(false)) out[0] |= (bit_mask << chore_agree_bit);
            if (op.disconnect.value_or(false)) out[0] |= (bit_mask << chore_disconnect_bit);
            if (op.is_digging.value_or(false)) out[0] |= (bit_mask << chore_digging_bit);
            return 1;
        default:
            return 0;
        }
    }
};

struct ClientAuthRequest
{
    enum class Mode : uint8_t
    {
        Login = 0,
        Register = 1,
        RequestRegistrationCode = 2,
        RequestBindingCode = 3,
        ConfirmBinding = 4,
    };

    Mode mode = Mode::Login;
    std::string name;
    std::string password;
    std::string email;
    std::string code;

    static bool IsPacketStart(uint8_t value) { return value == client_auth_packet_type; }

    static size_t GetPacketSize(const uint8_t* data, size_t len)
    {
        if (!data || len < client_auth_header_size) return 0;
        if (!IsPacketStart(data[0])) return 0;
        return client_auth_header_size + static_cast<size_t>(data[2]) + static_cast<size_t>(data[3]) +
               static_cast<size_t>(data[4]) + static_cast<size_t>(data[5]);
    }

    static ClientAuthRequest parse(const uint8_t* data, size_t len, bool* ok = nullptr)
    {
        ClientAuthRequest request;
        if (ok) *ok = false;
        if (!data || len < client_auth_header_size) return request;
        if (!IsPacketStart(data[0])) return request;

        size_t offset = 0;
        offset++;
        const uint8_t mode = data[offset++];
        if (mode > static_cast<uint8_t>(Mode::ConfirmBinding)) return request;
        request.mode = static_cast<Mode>(mode);
        uint8_t name_len = data[offset++];
        uint8_t password_len = data[offset++];
        uint8_t email_len = data[offset++];
        uint8_t code_len = data[offset++];
        if (name_len == 0 || name_len > max_auth_name_size) return request;
        if (password_len > max_auth_password_size) return request;
        if (email_len > max_auth_email_size || code_len > max_auth_code_size) return request;
        if (offset + name_len + password_len + email_len + code_len > len) return request;

        request.name.assign(reinterpret_cast<const char*>(data + offset),
                            reinterpret_cast<const char*>(data + offset + name_len));
        offset += name_len;
        request.password.assign(reinterpret_cast<const char*>(data + offset),
                                reinterpret_cast<const char*>(data + offset + password_len));
        offset += password_len;
        request.email.assign(reinterpret_cast<const char*>(data + offset),
                             reinterpret_cast<const char*>(data + offset + email_len));
        offset += email_len;
        request.code.assign(reinterpret_cast<const char*>(data + offset),
                            reinterpret_cast<const char*>(data + offset + code_len));
        if (ok) *ok = true;
        return request;
    }

    static size_t pack(const ClientAuthRequest& request, uint8_t* out)
    {
        if (!out) return 0;

        size_t name_len = std::min(request.name.size(), max_auth_name_size);
        size_t password_len = std::min(request.password.size(), max_auth_password_size);
        size_t email_len = std::min(request.email.size(), max_auth_email_size);
        size_t code_len = std::min(request.code.size(), max_auth_code_size);
        if (name_len == 0) return 0;

        size_t offset = 0;
        out[offset++] = client_auth_packet_type;
        out[offset++] = static_cast<uint8_t>(request.mode);
        out[offset++] = static_cast<uint8_t>(name_len);
        out[offset++] = static_cast<uint8_t>(password_len);
        out[offset++] = static_cast<uint8_t>(email_len);
        out[offset++] = static_cast<uint8_t>(code_len);
        std::copy_n(request.name.data(), name_len, out + offset);
        offset += name_len;
        std::copy_n(request.password.data(), password_len, out + offset);
        offset += password_len;
        std::copy_n(request.email.data(), email_len, out + offset);
        offset += email_len;
        std::copy_n(request.code.data(), code_len, out + offset);
        offset += code_len;
        return offset;
    }

    static size_t GetPackedSize(const ClientAuthRequest& request)
    {
        size_t name_len = std::min(request.name.size(), max_auth_name_size);
        if (name_len == 0) return 0;
        return client_auth_header_size + name_len + std::min(request.password.size(), max_auth_password_size) +
               std::min(request.email.size(), max_auth_email_size) +
               std::min(request.code.size(), max_auth_code_size);
    }
};

struct ClientChatRequest
{
    EChatFlag flag = EChatFlag::Global;
    std::string message;

    static bool IsPacketStart(uint8_t value) { return value == client_chat_packet_type; }

    static size_t GetPacketSize(const uint8_t* data, size_t len)
    {
        if (!data || len < client_chat_header_size) return 0;
        if (!IsPacketStart(data[0])) return 0;
        return client_chat_header_size + data[2];
    }

    static ClientChatRequest parse(const uint8_t* data, size_t len, bool* ok = nullptr)
    {
        ClientChatRequest request;
        if (ok) *ok = false;
        if (!data || len < client_chat_header_size) return request;
        if (!IsPacketStart(data[0])) return request;

        uint8_t flag = data[1];
        if (flag > static_cast<uint8_t>(EChatFlag::Squad)) return request;
        request.flag = static_cast<EChatFlag>(flag);

        uint8_t message_len = data[2];
        if (message_len == 0 || message_len > max_chat_message_size) return request;
        if (client_chat_header_size + message_len > len) return request;

        request.message.assign(reinterpret_cast<const char*>(data + client_chat_header_size),
                               reinterpret_cast<const char*>(data + client_chat_header_size + message_len));
        if (ok) *ok = true;
        return request;
    }

    static size_t pack(const ClientChatRequest& request, uint8_t* out)
    {
        if (!out) return 0;
        size_t message_len = std::min(request.message.size(), max_chat_message_size);
        if (message_len == 0) return 0;

        out[0] = client_chat_packet_type;
        out[1] = static_cast<uint8_t>(request.flag);
        out[2] = static_cast<uint8_t>(message_len);
        std::copy_n(request.message.data(), message_len, out + client_chat_header_size);
        return client_chat_header_size + message_len;
    }

    static size_t GetPackedSize(const ClientChatRequest& request)
    {
        size_t message_len = std::min(request.message.size(), max_chat_message_size);
        return message_len == 0 ? 0 : client_chat_header_size + message_len;
    }
};

struct ClientSecondarySlotRequest
{
    uint8_t slot_index = 0;
    uint8_t petal_type = 0;
    uint8_t rarity = 0;

    static bool IsPacketStart(uint8_t value) { return value == client_secondary_slot_packet_type; }

    static size_t GetPacketSize(const uint8_t* data, size_t len)
    {
        if (!data || len < client_secondary_slot_packet_size) return 0;
        if (!IsPacketStart(data[0])) return 0;
        return client_secondary_slot_packet_size;
    }

    static ClientSecondarySlotRequest parse(const uint8_t* data, size_t len, bool* ok = nullptr)
    {
        ClientSecondarySlotRequest request;
        if (ok) *ok = false;
        if (!data || len < client_secondary_slot_packet_size) return request;
        if (!IsPacketStart(data[0])) return request;

        request.slot_index = data[1];
        request.petal_type = data[2];
        request.rarity = data[3];
        if (ok) *ok = true;
        return request;
    }

    static size_t pack(const ClientSecondarySlotRequest& request, uint8_t* out)
    {
        if (!out) return 0;
        out[0] = client_secondary_slot_packet_type;
        out[1] = request.slot_index;
        out[2] = request.petal_type;
        out[3] = request.rarity;
        return client_secondary_slot_packet_size;
    }
};

struct ClientCraftRequest
{
    uint8_t petal_type = 0;
    uint8_t rarity = 0;
    uint32_t count = 0;

    static bool IsPacketStart(uint8_t value) { return value == client_craft_packet_type; }

    static size_t GetPacketSize(const uint8_t* data, size_t len)
    {
        if (!data || len < client_craft_packet_size) return 0;
        if (!IsPacketStart(data[0])) return 0;
        return client_craft_packet_size;
    }

    static ClientCraftRequest parse(const uint8_t* data, size_t len, bool* ok = nullptr)
    {
        ClientCraftRequest request;
        if (ok) *ok = false;
        if (!data || len < client_craft_packet_size) return request;
        if (!IsPacketStart(data[0])) return request;

        size_t offset = 1;
        request.petal_type = data[offset++];
        request.rarity = data[offset++];
        request.count = static_cast<uint32_t>(data[offset]) | (static_cast<uint32_t>(data[offset + 1]) << 8) |
                        (static_cast<uint32_t>(data[offset + 2]) << 16) |
                        (static_cast<uint32_t>(data[offset + 3]) << 24);
        if (ok) *ok = true;
        return request;
    }

    static size_t pack(const ClientCraftRequest& request, uint8_t* out)
    {
        if (!out) return 0;
        size_t offset = 0;
        out[offset++] = client_craft_packet_type;
        out[offset++] = request.petal_type;
        out[offset++] = request.rarity;
        out[offset++] = static_cast<uint8_t>(request.count & 0xFF);
        out[offset++] = static_cast<uint8_t>((request.count >> 8) & 0xFF);
        out[offset++] = static_cast<uint8_t>((request.count >> 16) & 0xFF);
        out[offset++] = static_cast<uint8_t>((request.count >> 24) & 0xFF);
        return client_craft_packet_size;
    }
};

struct ClientForgeRequest
{
    uint8_t petal_type = 0;
    uint8_t rarity = 0;
    uint32_t count = 0;

    static bool IsPacketStart(uint8_t value) { return value == client_forge_packet_type; }

    static size_t GetPacketSize(const uint8_t* data, size_t len)
    {
        if (!data || len < client_forge_packet_size) return 0;
        if (!IsPacketStart(data[0])) return 0;
        return client_forge_packet_size;
    }

    static ClientForgeRequest parse(const uint8_t* data, size_t len, bool* ok = nullptr)
    {
        ClientForgeRequest request;
        if (ok) *ok = false;
        if (!data || len < client_forge_packet_size) return request;
        if (!IsPacketStart(data[0])) return request;

        size_t offset = 1;
        request.petal_type = data[offset++];
        request.rarity = data[offset++];
        request.count = static_cast<uint32_t>(data[offset]) | (static_cast<uint32_t>(data[offset + 1]) << 8) |
                        (static_cast<uint32_t>(data[offset + 2]) << 16) |
                        (static_cast<uint32_t>(data[offset + 3]) << 24);
        if (ok) *ok = true;
        return request;
    }

    static size_t pack(const ClientForgeRequest& request, uint8_t* out)
    {
        if (!out) return 0;
        size_t offset = 0;
        out[offset++] = client_forge_packet_type;
        out[offset++] = request.petal_type;
        out[offset++] = request.rarity;
        out[offset++] = static_cast<uint8_t>(request.count & 0xFF);
        out[offset++] = static_cast<uint8_t>((request.count >> 8) & 0xFF);
        out[offset++] = static_cast<uint8_t>((request.count >> 16) & 0xFF);
        out[offset++] = static_cast<uint8_t>((request.count >> 24) & 0xFF);
        return client_forge_packet_size;
    }
};

enum class ClientTalentAction : uint8_t
{
    Add = 1,
    Remove = 2,
};

struct STalentPacketItem
{
    ETalentId id = ETalentId::None;
    uint8_t rarity = 0;
    uint8_t rank = 0;
};

struct ClientTalentRequest
{
    ClientTalentAction action = ClientTalentAction::Add;
    std::vector<STalentPacketItem> talents;

    static bool IsPacketStart(uint8_t value) { return value == client_talent_packet_type; }

    static size_t GetPacketSize(const uint8_t* data, size_t len)
    {
        if (!data || len < client_talent_header_size) return 0;
        if (!IsPacketStart(data[0])) return 0;
        uint8_t count = data[2];
        if (count == 0 || count > max_client_talent_items) return 0;
        return client_talent_header_size + static_cast<size_t>(count) * talent_packet_item_size;
    }

    static ClientTalentRequest parse(const uint8_t* data, size_t len, bool* ok = nullptr)
    {
        ClientTalentRequest request;
        if (ok) *ok = false;
        if (!data || len < client_talent_header_size) return request;
        if (!IsPacketStart(data[0])) return request;

        size_t offset = 1;
        uint8_t action = data[offset++];
        if (action != static_cast<uint8_t>(ClientTalentAction::Add) &&
            action != static_cast<uint8_t>(ClientTalentAction::Remove))
            return request;
        request.action = static_cast<ClientTalentAction>(action);

        uint8_t count = data[offset++];
        if (count == 0 || count > max_client_talent_items) return request;
        if (offset + static_cast<size_t>(count) * talent_packet_item_size > len) return request;

        request.talents.reserve(count);
        for (uint8_t i = 0; i < count; ++i)
        {
            STalentPacketItem talent;
            uint16_t id = static_cast<uint16_t>(data[offset]) | (static_cast<uint16_t>(data[offset + 1]) << 8);
            offset += 2;
            talent.id = static_cast<ETalentId>(id);
            talent.rarity = data[offset++];
            talent.rank = data[offset++];
            request.talents.push_back(talent);
        }

        if (ok) *ok = true;
        return request;
    }

    static size_t pack(const ClientTalentRequest& request, uint8_t* out)
    {
        if (!out || request.talents.empty()) return 0;
        const size_t count = std::min(request.talents.size(), max_client_talent_items);

        size_t offset = 0;
        out[offset++] = client_talent_packet_type;
        out[offset++] = static_cast<uint8_t>(request.action);
        out[offset++] = static_cast<uint8_t>(count);
        for (size_t i = 0; i < count; ++i)
        {
            const STalentPacketItem& talent = request.talents[i];
            uint16_t id = static_cast<uint16_t>(talent.id);
            out[offset++] = static_cast<uint8_t>(id & 0xff);
            out[offset++] = static_cast<uint8_t>((id >> 8) & 0xff);
            out[offset++] = talent.rarity;
            out[offset++] = talent.rank;
        }
        return offset;
    }

    static size_t GetPackedSize(const ClientTalentRequest& request)
    {
        if (request.talents.empty()) return 0;
        return client_talent_header_size +
               std::min(request.talents.size(), max_client_talent_items) * talent_packet_item_size;
    }
};

struct ClientSnapshotAck
{
    uint32_t snapshot_id = std::numeric_limits<uint32_t>::max();

    static bool IsPacketStart(uint8_t value) { return value == client_snapshot_ack_packet_type; }

    static size_t GetPacketSize(const uint8_t* data, size_t len)
    {
        if (!data || len < client_snapshot_ack_packet_size) return 0;
        if (!IsPacketStart(data[0])) return 0;
        return client_snapshot_ack_packet_size;
    }

    static ClientSnapshotAck parse(const uint8_t* data, size_t len, bool* ok = nullptr)
    {
        ClientSnapshotAck ack;
        if (ok) *ok = false;
        if (!data || len < client_snapshot_ack_packet_size || !IsPacketStart(data[0])) return ack;

        size_t offset = 1;
        ack.snapshot_id = static_cast<uint32_t>(data[offset]) |
                          (static_cast<uint32_t>(data[offset + 1]) << 8) |
                          (static_cast<uint32_t>(data[offset + 2]) << 16) |
                          (static_cast<uint32_t>(data[offset + 3]) << 24);
        if (ok) *ok = true;
        return ack;
    }

    static size_t pack(const ClientSnapshotAck& ack, uint8_t* out)
    {
        if (!out) return 0;
        out[0] = client_snapshot_ack_packet_type;
        out[1] = static_cast<uint8_t>(ack.snapshot_id & 0xff);
        out[2] = static_cast<uint8_t>((ack.snapshot_id >> 8) & 0xff);
        out[3] = static_cast<uint8_t>((ack.snapshot_id >> 16) & 0xff);
        out[4] = static_cast<uint8_t>((ack.snapshot_id >> 24) & 0xff);
        return client_snapshot_ack_packet_size;
    }
};

struct ClientInputFrame
{
    static constexpr uint8_t attacking_bit = 0;
    static constexpr uint8_t defending_bit = 1;
    static constexpr uint8_t digging_bit = 2;

    uint32_t sequence = 0;
    uint64_t target_server_tick = 0;
    int8_t move_x = 0;
    int8_t move_y = 0;
    bool is_attacking = false;
    bool is_defending = false;
    bool is_digging = false;

    static bool IsPacketStart(uint8_t value) { return value == client_input_frame_packet_type; }

    static size_t GetPacketSize(const uint8_t* data, size_t len)
    {
        if (!data || len < client_input_frame_packet_size || !IsPacketStart(data[0])) return 0;
        return client_input_frame_packet_size;
    }

    static ClientInputFrame parse(const uint8_t* data, size_t len, bool* ok = nullptr)
    {
        ClientInputFrame frame;
        if (ok) *ok = false;
        if (!data || len < client_input_frame_packet_size || !IsPacketStart(data[0])) return frame;

        size_t offset = 1;
        frame.sequence = static_cast<uint32_t>(data[offset]) |
                         (static_cast<uint32_t>(data[offset + 1]) << 8) |
                         (static_cast<uint32_t>(data[offset + 2]) << 16) |
                         (static_cast<uint32_t>(data[offset + 3]) << 24);
        offset += 4;
        for (uint8_t byte = 0; byte < 8; ++byte)
            frame.target_server_tick |= static_cast<uint64_t>(data[offset++]) << (byte * 8);
        frame.move_x = static_cast<int8_t>(data[offset++]);
        frame.move_y = static_cast<int8_t>(data[offset++]);
        const uint8_t flags = data[offset];
        frame.is_attacking = ((flags >> attacking_bit) & bit_mask) != 0;
        frame.is_defending = ((flags >> defending_bit) & bit_mask) != 0;
        frame.is_digging = ((flags >> digging_bit) & bit_mask) != 0;
        if (ok) *ok = true;
        return frame;
    }

    static size_t pack(const ClientInputFrame& frame, uint8_t* out)
    {
        if (!out) return 0;
        size_t offset = 0;
        out[offset++] = client_input_frame_packet_type;
        for (uint8_t byte = 0; byte < 4; ++byte)
            out[offset++] = static_cast<uint8_t>((frame.sequence >> (byte * 8)) & 0xff);
        for (uint8_t byte = 0; byte < 8; ++byte)
            out[offset++] = static_cast<uint8_t>((frame.target_server_tick >> (byte * 8)) & 0xff);
        out[offset++] = static_cast<uint8_t>(frame.move_x);
        out[offset++] = static_cast<uint8_t>(frame.move_y);
        uint8_t flags = 0;
        if (frame.is_attacking) flags |= bit_mask << attacking_bit;
        if (frame.is_defending) flags |= bit_mask << defending_bit;
        if (frame.is_digging) flags |= bit_mask << digging_bit;
        out[offset++] = flags;
        return offset;
    }

    ClientOperate ToOperate() const
    {
        ClientOperate op;
        op.type = ClientOperate::Type::Input;
        op.move_x = move_x;
        op.move_y = move_y;
        op.is_attacking = is_attacking;
        op.is_defending = is_defending;
        op.is_digging = is_digging;
        return op;
    }
};

// Server packets:
// Welcome:    [type:1 byte][player_id:2 bytes][owner_entity_id:2 bytes][tick_rate:1 byte][map_len:1 byte][map_name]
// Snapshot:   [type:1 byte][snapshot_id:4 bytes][base_snapshot_id:4 bytes][server_tick:8 bytes]
// [owner_entity_id:2 bytes][view_radius:4 bytes][changed_count:2 bytes][removed_count:2 bytes]
// [entity_snap...][removed_entity_id:2 bytes]... A base_snapshot_id of UINT32_MAX denotes a full snapshot.
// EntitySnap full:    [format:1 byte][entity_id:2 bytes][entity_type:1 byte][team:1 byte][x:4
// bytes][y:4 bytes][radius:2 bytes][hp_percent:1 byte][shield_percent:1 byte][flags:2 bytes][angle:2
// bytes][rarity:1 byte][name_len:1
// byte][name][primary_slot_count:1 byte][petal_type:1 byte][rarity:1 byte][copy_count:1 byte]
// [copy_state:1 byte][copy_progress:1 byte]......[state_count:1 byte]
// [state_type:1 byte][rarity:1 byte]... EntitySnap compact: [format:1
// byte][entity_id:2 bytes][entity_type:1 byte][team:1 byte][rel_x:2 bytes][rel_y:2 bytes][radius:2 bytes][hp_percent:1
// byte][shield_percent:1 byte][flags:2 bytes][angle:2 bytes][rarity:1 byte]
//             live petal entity_type = 100 + petal_type, drop entity_type = 180 + petal_type.
// OwnerState: [type:1 byte][level:1 byte][flags:1 byte][petal_slots:1 byte][secondary_slots:1 byte][exp_progress_bps:2
// bytes][petal_type:1 byte][rarity:1 byte]...[talent_points:2 bytes][talent_count:1 byte][talent_id:2 bytes][rarity:1
// byte][rank:1 byte]... Inventory:  [type:1 byte][count:2 bytes][petal_type:1 byte][rarity:1 byte][count:4 bytes]...
// AuthResult: [type:1 byte][result_code:1 byte][message_len:1 byte][message]
// Chat:       [type:1 byte][flag:1 byte][player_id:2 bytes][time:4 bytes][name_len:1 byte][message_len:1
// byte][name][message] CraftResult:[type:1 byte][success:1 byte][petal_type:1 byte][rarity:1 byte][consumed:4
// bytes][count:2 bytes][item...]

using net_coord = int32_t;
using net_relative_coord = int16_t;
using net_entity_id = uint16_t;

constexpr uint8_t server_entity_snap_full = 0;
constexpr uint8_t server_entity_snap_compact = 1;
constexpr float net_coord_scale = 64.f;
constexpr float net_relative_coord_scale = 1.f;
constexpr float net_radius_scale = 1.f;
constexpr float net_angle_scale = 1000.f;
constexpr float net_percent_scale = 255.f;
constexpr size_t server_entity_fixed_size = 22;
constexpr size_t server_entity_format_size = 1;
constexpr size_t server_entity_compact_size = 18;
constexpr uint32_t full_snapshot_base_id = std::numeric_limits<uint32_t>::max();
constexpr size_t server_snapshot_header_size = 27;

inline net_coord PackCoord(float value) { return static_cast<net_coord>(std::round(value * net_coord_scale)); }

inline float UnpackCoord(net_coord value) { return static_cast<float>(value) / net_coord_scale; }

inline bool CanPackRelativeCoord(float value)
{
    float packed = std::round(value * net_relative_coord_scale);
    return packed >= static_cast<float>(std::numeric_limits<net_relative_coord>::min()) &&
           packed <= static_cast<float>(std::numeric_limits<net_relative_coord>::max());
}

inline net_relative_coord PackRelativeCoord(float value)
{
    float packed = std::round(value * net_relative_coord_scale);
    packed = std::clamp(packed, static_cast<float>(std::numeric_limits<net_relative_coord>::min()),
                        static_cast<float>(std::numeric_limits<net_relative_coord>::max()));
    return static_cast<net_relative_coord>(packed);
}

inline float UnpackRelativeCoord(net_relative_coord value)
{
    return static_cast<float>(value) / net_relative_coord_scale;
}

inline net_coord PackViewRadius(float value) { return PackCoord(value); }

inline float UnpackViewRadius(net_coord value) { return UnpackCoord(value); }

inline uint16_t PackRadius(float value)
{
    return static_cast<uint16_t>(std::clamp(std::round(std::max(0.f, value) * net_radius_scale), 0.f, 65535.f));
}

inline float UnpackRadius(uint16_t value) { return static_cast<float>(value) / net_radius_scale; }

inline uint8_t PackPercent(float value)
{
    return static_cast<uint8_t>(std::clamp(std::round(value * net_percent_scale), 0.f, net_percent_scale));
}

inline float UnpackPercent(uint8_t value) { return static_cast<float>(value) / net_percent_scale; }

inline int16_t PackAngle(float radians) { return static_cast<int16_t>(std::round(radians * net_angle_scale)); }

inline float UnpackAngle(int16_t value) { return static_cast<float>(value) / net_angle_scale; }

inline void WriteU16(uint8_t* out, size_t& offset, uint16_t value)
{
    out[offset++] = static_cast<uint8_t>(value & 0xff);
    out[offset++] = static_cast<uint8_t>((value >> 8) & 0xff);
}

inline uint16_t ReadU16(const uint8_t* data, size_t& offset)
{
    uint16_t value = static_cast<uint16_t>(data[offset]) | (static_cast<uint16_t>(data[offset + 1]) << 8);
    offset += 2;
    return value;
}

inline void WriteU32(uint8_t* out, size_t& offset, uint32_t value)
{
    out[offset++] = static_cast<uint8_t>(value & 0xff);
    out[offset++] = static_cast<uint8_t>((value >> 8) & 0xff);
    out[offset++] = static_cast<uint8_t>((value >> 16) & 0xff);
    out[offset++] = static_cast<uint8_t>((value >> 24) & 0xff);
}

inline uint32_t ReadU32(const uint8_t* data, size_t& offset)
{
    uint32_t value = static_cast<uint32_t>(data[offset]) | (static_cast<uint32_t>(data[offset + 1]) << 8) |
                     (static_cast<uint32_t>(data[offset + 2]) << 16) | (static_cast<uint32_t>(data[offset + 3]) << 24);
    offset += 4;
    return value;
}

inline void WriteU64(uint8_t* out, size_t& offset, uint64_t value)
{
    WriteU32(out, offset, static_cast<uint32_t>(value & 0xffffffffu));
    WriteU32(out, offset, static_cast<uint32_t>(value >> 32));
}

inline uint64_t ReadU64(const uint8_t* data, size_t& offset)
{
    uint64_t low = ReadU32(data, offset);
    uint64_t high = ReadU32(data, offset);
    return low | (high << 32);
}

inline void WriteI32(uint8_t* out, size_t& offset, int32_t value)
{
    out[offset++] = static_cast<uint8_t>(value & 0xff);
    out[offset++] = static_cast<uint8_t>((value >> 8) & 0xff);
    out[offset++] = static_cast<uint8_t>((value >> 16) & 0xff);
    out[offset++] = static_cast<uint8_t>((value >> 24) & 0xff);
}

inline int32_t ReadI32(const uint8_t* data, size_t& offset)
{
    uint32_t value = static_cast<uint32_t>(data[offset]) | (static_cast<uint32_t>(data[offset + 1]) << 8) |
                     (static_cast<uint32_t>(data[offset + 2]) << 16) | (static_cast<uint32_t>(data[offset + 3]) << 24);
    offset += 4;
    return static_cast<int32_t>(value);
}

enum class ServerEntityFlag : uint16_t
{
    None = 0,
    Attacking = 1 << 0,
    Defending = 1 << 1,
    Dead = 1 << 2,
    Owner = 1 << 3,
    Antennae = 1 << 7,
    Summoned = 1 << 8,
    Attached = 1 << 10,
    CarryingLeafPiece = 1 << 10,
    SkillWindupMask = 0xf000,
};

constexpr uint16_t server_entity_skill_windup_shift = 12;
constexpr uint16_t server_entity_skill_windup_mask = 0xf000;

inline uint16_t PackServerEntitySkillWindup(uint8_t skill_id)
{
    return static_cast<uint16_t>((static_cast<uint16_t>(skill_id) & 0x0f) << server_entity_skill_windup_shift);
}

inline ServerEntityFlag operator|(ServerEntityFlag lhs, ServerEntityFlag rhs)
{
    return static_cast<ServerEntityFlag>(static_cast<uint16_t>(lhs) | static_cast<uint16_t>(rhs));
}

inline bool HasFlag(ServerEntityFlag flags, ServerEntityFlag flag)
{
    return (static_cast<uint16_t>(flags) & static_cast<uint16_t>(flag)) != 0;
}

struct SOwnerPetalSlot
{
    uint8_t petal_type = 0;
    uint8_t rarity = 0;
    enum class ECopyState : uint8_t
    {
        Alive = 0,
        Loading = 1,
    };

    struct SCopy
    {
        ECopyState state = ECopyState::Loading;
        uint8_t progress = 0;
    };

    std::vector<SCopy> copies;
};

constexpr size_t entity_owner_slot_header_size = 3;
constexpr size_t entity_owner_slot_copy_size = 2;

inline bool ParseEntityOwnerPetalSlot(const uint8_t* data, size_t len, size_t& offset, SOwnerPetalSlot& slot)
{
    if (!data || offset + entity_owner_slot_header_size > len) return false;

    slot.petal_type = data[offset++];
    slot.rarity = data[offset++];
    const uint8_t copy_count = data[offset++];
    if (offset + static_cast<size_t>(copy_count) * entity_owner_slot_copy_size > len) return false;

    slot.copies.clear();
    slot.copies.reserve(copy_count);
    for (uint8_t i = 0; i < copy_count; ++i)
    {
        SOwnerPetalSlot::SCopy copy;
        copy.state = static_cast<SOwnerPetalSlot::ECopyState>(data[offset++]);
        copy.progress = data[offset++];
        if (copy.state != SOwnerPetalSlot::ECopyState::Alive &&
            copy.state != SOwnerPetalSlot::ECopyState::Loading)
            return false;
        slot.copies.push_back(copy);
    }
    return true;
}

inline void PackEntityOwnerPetalSlot(const SOwnerPetalSlot& slot, uint8_t* out, size_t& offset)
{
    out[offset++] = slot.petal_type;
    out[offset++] = slot.rarity;
    const uint8_t copy_count = static_cast<uint8_t>(std::min<size_t>(slot.copies.size(), UINT8_MAX));
    out[offset++] = copy_count;
    for (uint8_t i = 0; i < copy_count; ++i)
    {
        out[offset++] = static_cast<uint8_t>(slot.copies[i].state);
        out[offset++] = slot.copies[i].progress;
    }
}

inline size_t GetEntityOwnerPetalSlotPackedSize(const SOwnerPetalSlot& slot)
{
    return entity_owner_slot_header_size +
           std::min<size_t>(slot.copies.size(), UINT8_MAX) * entity_owner_slot_copy_size;
}

struct SEntityStateSnap
{
    EStateType type = EStateType::None;
    uint8_t rarity = 0;
};

struct ServerEntitySnap
{
    net_entity_id entity_id = 0;
    uint8_t entity_type = 0;
    uint8_t team = 0;
    sf::Vector2f pos = { 0.f, 0.f };
    float radius = 0.f;
    float hp_percent = 1.f;
    float shield_percent = 0.f;
    uint16_t flags = 0;
    int16_t angle = 0;
    uint8_t rarity = 0;
    std::string name;
    std::vector<SOwnerPetalSlot> primary_slots;
    std::vector<SEntityStateSnap> states;

    static bool CanPackCompact(const ServerEntitySnap& snap, sf::Vector2f origin, net_entity_id owner_entity_id)
    {
        if (snap.entity_id == owner_entity_id) return false;
        if ((snap.flags & static_cast<uint16_t>(ServerEntityFlag::Owner)) != 0) return false;
        if (!snap.name.empty() || !snap.primary_slots.empty() || !snap.states.empty()) return false;
        return CanPackRelativeCoord(snap.pos.x - origin.x) && CanPackRelativeCoord(snap.pos.y - origin.y);
    }

    static bool parse(const uint8_t* data, size_t len, size_t& offset, ServerEntitySnap& snap,
                      sf::Vector2f origin = { 0.f, 0.f }, bool has_origin = false)
    {
        if (!data || offset + server_entity_format_size > len) return false;

        snap.name.clear();
        snap.primary_slots.clear();
        snap.states.clear();

        uint8_t format = data[offset++];
        if (format == server_entity_snap_compact)
        {
            if (!has_origin || offset + (server_entity_compact_size - server_entity_format_size) > len) return false;

            snap.entity_id = ReadU16(data, offset);
            snap.entity_type = data[offset++];
            snap.team = data[offset++];
            float rel_x = UnpackRelativeCoord(static_cast<net_relative_coord>(ReadU16(data, offset)));
            float rel_y = UnpackRelativeCoord(static_cast<net_relative_coord>(ReadU16(data, offset)));
            snap.pos = sf::Vector2f(origin.x + rel_x, origin.y + rel_y);
            snap.radius = UnpackRadius(ReadU16(data, offset));
            snap.hp_percent = UnpackPercent(data[offset++]);
            snap.shield_percent = UnpackPercent(data[offset++]);
            snap.flags = ReadU16(data, offset);
            snap.angle = static_cast<int16_t>(ReadU16(data, offset));
            snap.rarity = data[offset++];
            return true;
        }

        if (format != server_entity_snap_full) return false;
        if (offset + server_entity_fixed_size > len) return false;

        snap.entity_id = ReadU16(data, offset);
        snap.entity_type = data[offset++];
        snap.team = data[offset++];
        snap.pos = sf::Vector2f(UnpackCoord(ReadI32(data, offset)), UnpackCoord(ReadI32(data, offset)));
        snap.radius = UnpackRadius(ReadU16(data, offset));
        snap.hp_percent = UnpackPercent(data[offset++]);
        snap.shield_percent = UnpackPercent(data[offset++]);
        snap.flags = ReadU16(data, offset);
        snap.angle = static_cast<int16_t>(ReadU16(data, offset));
        snap.rarity = data[offset++];
        uint8_t name_len = data[offset++];
        if (offset + name_len > len) return false;

        snap.name.assign(reinterpret_cast<const char*>(data + offset),
                         reinterpret_cast<const char*>(data + offset + name_len));
        offset += name_len;
        if (offset + 1 > len) return false;

        uint8_t slot_count = data[offset++];
        snap.primary_slots.reserve(slot_count);
        for (uint8_t i = 0; i < slot_count; ++i)
        {
            SOwnerPetalSlot slot;
            if (!ParseEntityOwnerPetalSlot(data, len, offset, slot)) return false;
            snap.primary_slots.push_back(slot);
        }

        if (offset + 1 > len) return false;
        uint8_t state_count = data[offset++];
        if (offset + static_cast<size_t>(state_count) * entity_state_packet_size > len) return false;
        snap.states.reserve(state_count);
        for (uint8_t i = 0; i < state_count; ++i)
        {
            SEntityStateSnap state;
            state.type = static_cast<EStateType>(data[offset++]);
            state.rarity = data[offset++];
            if (!IsKnownStateType(state.type)) return false;
            snap.states.push_back(state);
        }
        return true;
    }

    static void pack(const ServerEntitySnap& snap, uint8_t* out, size_t& offset, sf::Vector2f origin = { 0.f, 0.f },
                     net_entity_id owner_entity_id = 0, bool has_origin = false)
    {
        if (has_origin && CanPackCompact(snap, origin, owner_entity_id))
        {
            out[offset++] = server_entity_snap_compact;
            WriteU16(out, offset, snap.entity_id);
            out[offset++] = snap.entity_type;
            out[offset++] = snap.team;
            WriteU16(out, offset, static_cast<uint16_t>(PackRelativeCoord(snap.pos.x - origin.x)));
            WriteU16(out, offset, static_cast<uint16_t>(PackRelativeCoord(snap.pos.y - origin.y)));
            WriteU16(out, offset, PackRadius(snap.radius));
            out[offset++] = PackPercent(snap.hp_percent);
            out[offset++] = PackPercent(snap.shield_percent);
            WriteU16(out, offset, snap.flags);
            WriteU16(out, offset, static_cast<uint16_t>(snap.angle));
            out[offset++] = snap.rarity;
            return;
        }

        out[offset++] = server_entity_snap_full;
        WriteU16(out, offset, snap.entity_id);
        out[offset++] = snap.entity_type;
        out[offset++] = snap.team;
        WriteI32(out, offset, PackCoord(snap.pos.x));
        WriteI32(out, offset, PackCoord(snap.pos.y));
        WriteU16(out, offset, PackRadius(snap.radius));
        out[offset++] = PackPercent(snap.hp_percent);
        out[offset++] = PackPercent(snap.shield_percent);
        WriteU16(out, offset, snap.flags);
        WriteU16(out, offset, static_cast<uint16_t>(snap.angle));
        out[offset++] = snap.rarity;
        uint8_t name_len = static_cast<uint8_t>(std::min<size_t>(snap.name.size(), UINT8_MAX));
        out[offset++] = name_len;
        std::copy_n(snap.name.data(), name_len, out + offset);
        offset += name_len;
        uint8_t slot_count = static_cast<uint8_t>(std::min<size_t>(snap.primary_slots.size(), UINT8_MAX));
        out[offset++] = slot_count;
        for (uint8_t i = 0; i < slot_count; ++i)
            PackEntityOwnerPetalSlot(snap.primary_slots[i], out, offset);
        uint8_t state_count = static_cast<uint8_t>(std::min<size_t>(snap.states.size(), UINT8_MAX));
        out[offset++] = state_count;
        for (uint8_t i = 0; i < state_count; ++i)
        {
            out[offset++] = static_cast<uint8_t>(snap.states[i].type);
            out[offset++] = snap.states[i].rarity;
        }
    }

    static size_t GetPackedSize(const ServerEntitySnap& snap)
    {
        size_t size = server_entity_format_size + server_entity_fixed_size +
                      std::min<size_t>(snap.name.size(), UINT8_MAX) + 1 + 1 +
                      std::min<size_t>(snap.states.size(), UINT8_MAX) * entity_state_packet_size;
        const size_t slot_count = std::min<size_t>(snap.primary_slots.size(), UINT8_MAX);
        for (size_t i = 0; i < slot_count; ++i) size += GetEntityOwnerPetalSlotPackedSize(snap.primary_slots[i]);
        return size;
    }

    static size_t GetPackedSize(const ServerEntitySnap& snap, sf::Vector2f origin, net_entity_id owner_entity_id,
                                bool has_origin)
    {
        return has_origin && CanPackCompact(snap, origin, owner_entity_id) ? server_entity_compact_size
                                                                           : GetPackedSize(snap);
    }
};

struct SChatMessage
{
    EChatFlag flag = EChatFlag::Global;
    uint16_t player_id = 0;
    uint32_t time = 0;
    std::string player_name;
    std::string message;
};

enum class EAuthResultCode : uint8_t
{
    Failed = 0,
    Authenticated = 1,
    EmailBindingRequired = 2,
    VerificationCodeSending = 3,
    VerificationCodeSent = 4,
    EmailBound = 5,
};

inline bool IsKnownAuthResultCode(uint8_t value)
{
    return value <= static_cast<uint8_t>(EAuthResultCode::EmailBound);
}

struct ServerMessage
{
    enum class Type : uint8_t
    {
        Welcome = 0x00,
        Snapshot = 0x01,
        AuthResult = 0x02,
        OwnerState = 0x10,
        Inventory = 0x11,
        Chat = 0x12,
        CraftResult = 0x13,
        Unknown = 0xff,
    } type = Type::Unknown;

    std::optional<uint16_t> player_id;
    std::optional<net_entity_id> owner_entity_id;
    std::optional<uint8_t> tick_rate;
    std::string map_name;

    std::optional<uint32_t> snapshot_id;
    std::optional<uint32_t> base_snapshot_id;
    std::optional<uint64_t> server_tick;
    std::optional<float> view_radius;
    std::vector<ServerEntitySnap> entities;
    std::vector<net_entity_id> removed_entity_ids;

    std::optional<uint8_t> flags;

    std::optional<uint8_t> level;
    std::optional<uint16_t> exp_progress_bps;
    std::optional<uint8_t> petal_slots;
    std::vector<SOwnerPetalSlot> owner_slots;
    std::optional<uint8_t> secondary_slots_count;
    std::vector<SOwnerPetalSlot> secondary_slots;
    std::optional<uint16_t> talent_points;
    std::vector<STalentPacketItem> talents;
    std::optional<EAuthResultCode> auth_result_code;
    std::string auth_message;
    std::vector<SInventoryItem> inventory;
    SChatMessage chat;
    std::optional<bool> craft_success;
    uint8_t craft_petal_type = 0;
    uint8_t craft_rarity = 0;
    uint32_t craft_consumed = 0;
    std::vector<SInventoryItem> craft_items;

    static ServerMessage parse(const uint8_t* data, size_t len)
    {
        ServerMessage msg{};
        if (!data || len < 1) return msg;

        size_t offset = 0;
        msg.type = static_cast<Type>(data[offset++]);

        switch (msg.type)
        {
        case Type::Welcome:
            if (len < 6)
            {
                msg.type = Type::Unknown;
                break;
            }
            msg.player_id = ReadU16(data, offset);
            msg.owner_entity_id = ReadU16(data, offset);
            msg.tick_rate = data[offset++];
            if (offset < len)
            {
                uint8_t map_len = data[offset++];
                if (offset + map_len > len)
                {
                    msg.type = Type::Unknown;
                    break;
                }
                msg.map_name.assign(reinterpret_cast<const char*>(data + offset),
                                    reinterpret_cast<const char*>(data + offset + map_len));
                offset += map_len;
            }
            break;
        case Type::Snapshot: {
            if (len < server_snapshot_header_size)
            {
                msg.type = Type::Unknown;
                break;
            }
            msg.snapshot_id = ReadU32(data, offset);
            msg.base_snapshot_id = ReadU32(data, offset);
            msg.server_tick = ReadU64(data, offset);
            msg.owner_entity_id = ReadU16(data, offset);
            msg.view_radius = UnpackViewRadius(ReadI32(data, offset));

            uint16_t count = ReadU16(data, offset);
            uint16_t removed_count = ReadU16(data, offset);
            msg.entities.reserve(count);
            sf::Vector2f snapshot_origin = { 0.f, 0.f };
            bool has_snapshot_origin = false;
            for (uint16_t i = 0; i < count; ++i)
            {
                ServerEntitySnap snap;
                if (!ServerEntitySnap::parse(data, len, offset, snap, snapshot_origin, has_snapshot_origin))
                {
                    msg.entities.clear();
                    msg.type = Type::Unknown;
                    break;
                }
                if (!has_snapshot_origin)
                {
                    snapshot_origin = snap.pos;
                    has_snapshot_origin = true;
                }
                msg.entities.push_back(snap);
            }
            if (msg.type == Type::Unknown) break;
            if (offset + static_cast<size_t>(removed_count) * sizeof(net_entity_id) > len)
            {
                msg.entities.clear();
                msg.type = Type::Unknown;
                break;
            }
            msg.removed_entity_ids.reserve(removed_count);
            for (uint16_t i = 0; i < removed_count; ++i)
                msg.removed_entity_ids.push_back(ReadU16(data, offset));
            break;
        }
        case Type::AuthResult: {
            if (len < 3)
            {
                msg.type = Type::Unknown;
                break;
            }
            const uint8_t result_code = data[offset++];
            if (!IsKnownAuthResultCode(result_code))
            {
                msg.type = Type::Unknown;
                break;
            }
            msg.auth_result_code = static_cast<EAuthResultCode>(result_code);
            uint8_t message_len = data[offset++];
            if (offset + message_len > len)
            {
                msg.type = Type::Unknown;
                break;
            }
            msg.auth_message.assign(reinterpret_cast<const char*>(data + offset),
                                    reinterpret_cast<const char*>(data + offset + message_len));
            offset += message_len;
            break;
        }
        case Type::OwnerState:
            if (len < 7)
            {
                msg.type = Type::Unknown;
                break;
            }
            msg.level = data[offset++];
            msg.flags = data[offset++];
            msg.petal_slots = data[offset++];
            msg.secondary_slots_count = data[offset++];
            {
                const size_t slot_bytes =
                    (static_cast<size_t>(*msg.petal_slots) + static_cast<size_t>(*msg.secondary_slots_count)) *
                    owner_slot_packet_size;
                if (offset + 2 + slot_bytes <= len)
                {
                    msg.exp_progress_bps = ReadU16(data, offset);
                } else
                {
                    msg.exp_progress_bps = 0;
                }
                if (offset + slot_bytes > len)
                {
                    msg.type = Type::Unknown;
                    break;
                }
            }
            if (msg.type == Type::Unknown)
            {
                break;
            }
            msg.owner_slots.reserve(*msg.petal_slots);
            for (uint8_t i = 0; i < *msg.petal_slots; ++i)
            {
                SOwnerPetalSlot slot;
                slot.petal_type = data[offset++];
                slot.rarity = data[offset++];
                msg.owner_slots.push_back(slot);
            }
            msg.secondary_slots.reserve(*msg.secondary_slots_count);
            for (uint8_t i = 0; i < *msg.secondary_slots_count; ++i)
            {
                SOwnerPetalSlot slot;
                slot.petal_type = data[offset++];
                slot.rarity = data[offset++];
                msg.secondary_slots.push_back(slot);
            }
            if (offset < len)
            {
                if (offset + 3 > len)
                {
                    msg.type = Type::Unknown;
                    break;
                }
                msg.talent_points = ReadU16(data, offset);
                uint8_t talent_count = data[offset++];
                if (offset + static_cast<size_t>(talent_count) * talent_packet_item_size > len)
                {
                    msg.type = Type::Unknown;
                    break;
                }
                msg.talents.reserve(talent_count);
                for (uint8_t i = 0; i < talent_count; ++i)
                {
                    STalentPacketItem talent;
                    talent.id = static_cast<ETalentId>(ReadU16(data, offset));
                    talent.rarity = data[offset++];
                    talent.rank = data[offset++];
                    msg.talents.push_back(talent);
                }
            }
            break;
        case Type::Inventory: {
            if (len < 3)
            {
                msg.type = Type::Unknown;
                break;
            }
            uint16_t count = ReadU16(data, offset);
            if (offset + static_cast<size_t>(count) * inventory_item_packet_size > len)
            {
                msg.type = Type::Unknown;
                break;
            }
            msg.inventory.reserve(count);
            for (uint16_t i = 0; i < count; ++i)
            {
                SInventoryItem item;
                item.petal_type = data[offset++];
                item.rarity = data[offset++];
                item.count = ReadU32(data, offset);
                msg.inventory.push_back(item);
            }
            break;
        }
        case Type::Chat: {
            if (len < 10)
            {
                msg.type = Type::Unknown;
                break;
            }
            uint8_t flag = data[offset++];
            if (flag > static_cast<uint8_t>(EChatFlag::Squad))
            {
                msg.type = Type::Unknown;
                break;
            }
            msg.chat.flag = static_cast<EChatFlag>(flag);
            msg.chat.player_id = ReadU16(data, offset);
            msg.chat.time = ReadU32(data, offset);
            uint8_t name_len = data[offset++];
            uint8_t message_len = data[offset++];
            if (offset + name_len + message_len > len)
            {
                msg.type = Type::Unknown;
                break;
            }
            msg.chat.player_name.assign(reinterpret_cast<const char*>(data + offset),
                                        reinterpret_cast<const char*>(data + offset + name_len));
            offset += name_len;
            msg.chat.message.assign(reinterpret_cast<const char*>(data + offset),
                                    reinterpret_cast<const char*>(data + offset + message_len));
            offset += message_len;
            break;
        }
        case Type::CraftResult: {
            if (len < 10)
            {
                msg.type = Type::Unknown;
                break;
            }
            msg.craft_success = data[offset++] != 0;
            msg.craft_petal_type = data[offset++];
            msg.craft_rarity = data[offset++];
            msg.craft_consumed = ReadU32(data, offset);
            uint16_t count = ReadU16(data, offset);
            if (offset + static_cast<size_t>(count) * inventory_item_packet_size > len)
            {
                msg.type = Type::Unknown;
                break;
            }
            msg.craft_items.reserve(count);
            for (uint16_t i = 0; i < count; ++i)
            {
                SInventoryItem item;
                item.petal_type = data[offset++];
                item.rarity = data[offset++];
                item.count = ReadU32(data, offset);
                msg.craft_items.push_back(item);
            }
            break;
        }
        default:
            msg.type = Type::Unknown;
            break;
        }
        return msg;
    }

    static size_t pack(const ServerMessage& msg, uint8_t* out)
    {
        if (!out) return 0;

        size_t offset = 0;
        out[offset++] = static_cast<uint8_t>(msg.type);

        switch (msg.type)
        {
        case Type::Welcome:
            WriteU16(out, offset, msg.player_id.value_or(0));
            WriteU16(out, offset, msg.owner_entity_id.value_or(0));
            out[offset++] = msg.tick_rate.value_or(60);
            {
                uint8_t map_len = static_cast<uint8_t>(std::min<size_t>(msg.map_name.size(), UINT8_MAX));
                out[offset++] = map_len;
                std::copy_n(msg.map_name.data(), map_len, out + offset);
                offset += map_len;
            }
            return offset;
        case Type::Snapshot: {
            WriteU32(out, offset, msg.snapshot_id.value_or(0));
            WriteU32(out, offset, msg.base_snapshot_id.value_or(full_snapshot_base_id));
            WriteU64(out, offset, msg.server_tick.value_or(0));
            WriteU16(out, offset, msg.owner_entity_id.value_or(0));
            WriteI32(out, offset, PackViewRadius(msg.view_radius.value_or(0.f)));
            uint16_t count = static_cast<uint16_t>(std::min<size_t>(msg.entities.size(), UINT16_MAX));
            WriteU16(out, offset, count);
            uint16_t removed_count =
                static_cast<uint16_t>(std::min<size_t>(msg.removed_entity_ids.size(), UINT16_MAX));
            WriteU16(out, offset, removed_count);
            sf::Vector2f snapshot_origin = { 0.f, 0.f };
            bool has_snapshot_origin = false;
            net_entity_id owner_id = msg.owner_entity_id.value_or(0);
            for (uint16_t i = 0; i < count; ++i)
            {
                ServerEntitySnap::pack(msg.entities[i], out, offset, snapshot_origin, owner_id, has_snapshot_origin);
                if (!has_snapshot_origin)
                {
                    snapshot_origin = msg.entities[i].pos;
                    has_snapshot_origin = true;
                }
            }
            for (uint16_t i = 0; i < removed_count; ++i) WriteU16(out, offset, msg.removed_entity_ids[i]);
            return offset;
        }
        case Type::AuthResult: {
            out[offset++] = static_cast<uint8_t>(msg.auth_result_code.value_or(EAuthResultCode::Failed));
            uint8_t message_len = static_cast<uint8_t>(std::min<size_t>(msg.auth_message.size(), UINT8_MAX));
            out[offset++] = message_len;
            std::copy_n(msg.auth_message.data(), message_len, out + offset);
            offset += message_len;
            return offset;
        }
        case Type::OwnerState: {
            uint8_t slot_count = static_cast<uint8_t>(std::min<size_t>(msg.owner_slots.size(), UINT8_MAX));
            uint8_t secondary_slot_count =
                static_cast<uint8_t>(std::min<size_t>(msg.secondary_slots.size(), UINT8_MAX));
            out[offset++] = msg.level.value_or(1);
            out[offset++] = msg.flags.value_or(0);
            out[offset++] = slot_count;
            out[offset++] = secondary_slot_count;
            WriteU16(out, offset, std::min<uint16_t>(msg.exp_progress_bps.value_or(0), 10000));
            for (uint8_t i = 0; i < slot_count; ++i)
            {
                out[offset++] = msg.owner_slots[i].petal_type;
                out[offset++] = msg.owner_slots[i].rarity;
            }
            for (uint8_t i = 0; i < secondary_slot_count; ++i)
            {
                out[offset++] = msg.secondary_slots[i].petal_type;
                out[offset++] = msg.secondary_slots[i].rarity;
            }
            WriteU16(out, offset, msg.talent_points.value_or(0));
            uint8_t talent_count = static_cast<uint8_t>(std::min<size_t>(msg.talents.size(), UINT8_MAX));
            out[offset++] = talent_count;
            for (uint8_t i = 0; i < talent_count; ++i)
            {
                const STalentPacketItem& talent = msg.talents[i];
                WriteU16(out, offset, static_cast<uint16_t>(talent.id));
                out[offset++] = talent.rarity;
                out[offset++] = talent.rank;
            }
            return offset;
        }
        case Type::Inventory: {
            uint16_t count = static_cast<uint16_t>(std::min<size_t>(msg.inventory.size(), UINT16_MAX));
            WriteU16(out, offset, count);
            for (uint16_t i = 0; i < count; ++i)
            {
                const SInventoryItem& item = msg.inventory[i];
                out[offset++] = item.petal_type;
                out[offset++] = item.rarity;
                WriteU32(out, offset, std::min(item.count, max_inventory_item_count));
            }
            return offset;
        }
        case Type::Chat: {
            out[offset++] = static_cast<uint8_t>(msg.chat.flag);
            WriteU16(out, offset, msg.chat.player_id);
            WriteU32(out, offset, msg.chat.time);
            uint8_t name_len = static_cast<uint8_t>(std::min<size_t>(msg.chat.player_name.size(), UINT8_MAX));
            uint8_t message_len =
                static_cast<uint8_t>(std::min<size_t>(msg.chat.message.size(), max_chat_message_size));
            out[offset++] = name_len;
            out[offset++] = message_len;
            std::copy_n(msg.chat.player_name.data(), name_len, out + offset);
            offset += name_len;
            std::copy_n(msg.chat.message.data(), message_len, out + offset);
            offset += message_len;
            return offset;
        }
        case Type::CraftResult:
            out[offset++] = msg.craft_success.value_or(false) ? 1 : 0;
            out[offset++] = msg.craft_petal_type;
            out[offset++] = msg.craft_rarity;
            WriteU32(out, offset, msg.craft_consumed);
            {
                uint16_t count = static_cast<uint16_t>(std::min<size_t>(msg.craft_items.size(), UINT16_MAX));
                WriteU16(out, offset, count);
                for (uint16_t i = 0; i < count; ++i)
                {
                    const SInventoryItem& item = msg.craft_items[i];
                    out[offset++] = item.petal_type;
                    out[offset++] = item.rarity;
                    WriteU32(out, offset, std::min(item.count, max_inventory_item_count));
                }
            }
            return offset;
        default:
            return 0;
        }
    }

    static size_t GetPackedSize(const ServerMessage& msg)
    {
        switch (msg.type)
        {
        case Type::Welcome:
            return 7 + std::min<size_t>(msg.map_name.size(), UINT8_MAX);
        case Type::Snapshot: {
            size_t size = server_snapshot_header_size;
            size_t count = std::min<size_t>(msg.entities.size(), UINT16_MAX);
            sf::Vector2f snapshot_origin = { 0.f, 0.f };
            bool has_snapshot_origin = false;
            net_entity_id owner_id = msg.owner_entity_id.value_or(0);
            for (size_t i = 0; i < count; ++i)
            {
                size +=
                    ServerEntitySnap::GetPackedSize(msg.entities[i], snapshot_origin, owner_id, has_snapshot_origin);
                if (!has_snapshot_origin)
                {
                    snapshot_origin = msg.entities[i].pos;
                    has_snapshot_origin = true;
                }
            }
            size += std::min<size_t>(msg.removed_entity_ids.size(), UINT16_MAX) * sizeof(net_entity_id);
            return size;
        }
        case Type::AuthResult:
            return 3 + std::min<size_t>(msg.auth_message.size(), UINT8_MAX);
        case Type::OwnerState:
            return 10 +
                   (std::min<size_t>(msg.owner_slots.size(), UINT8_MAX) +
                    std::min<size_t>(msg.secondary_slots.size(), UINT8_MAX)) *
                       owner_slot_packet_size +
                   std::min<size_t>(msg.talents.size(), UINT8_MAX) * talent_packet_item_size;
        case Type::Inventory:
            return 3 + std::min<size_t>(msg.inventory.size(), UINT16_MAX) * inventory_item_packet_size;
        case Type::Chat:
            return 10 + std::min<size_t>(msg.chat.player_name.size(), UINT8_MAX) +
                   std::min<size_t>(msg.chat.message.size(), max_chat_message_size);
        case Type::CraftResult:
            return 10 + std::min<size_t>(msg.craft_items.size(), UINT16_MAX) * inventory_item_packet_size;
        default:
            return 0;
        }
    }
};
