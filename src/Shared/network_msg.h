#pragma once
#include <SFML/System/Vector2.hpp>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <vector>

constexpr uint8_t bit_mask = 0x01;
constexpr uint8_t client_type_mask = 0x03;
constexpr uint8_t chore_agree_bit = 4;
constexpr uint8_t chore_attack_bit = 2;
constexpr uint8_t chore_defend_bit = 3;
constexpr uint8_t chore_disconnect_bit = 5;
constexpr size_t client_compact_packet_size = 1;
constexpr size_t client_extended_packet_size = 3;
constexpr size_t packet_length_prefix_size = 2;

// Client packets:
// Input:        [type:2 bits][move_x:1 byte][move_y:1 byte]
// EquipPetal:   [type:2 bits][petal_type:1 byte][slot:4 bits][rarity:4 bits]
// UnequipPetal: [type:2 bits][slot:4 bits]
// Chores:       [type:2 bits][attack:1 bit][defend:1 bit][agree:1 bit][disconnect:1 bit]

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
            return 1;
        default:
            return 0;
        }
    }
};

// Server packets:
// Welcome:    [type:1 byte][player_id:2 bytes][owner_entity_id:2 bytes][tick_rate:1 byte]
// Snapshot:   [type:1 byte][snapshot_id:4 bytes][owner_entity_id:2 bytes][view_radius:4 bytes][count:2 bytes][entity_snap...]
// EntitySnap: [entity_id:2 bytes][entity_type:1 byte][team:1 byte][x:4 bytes][y:4 bytes][radius:2 bytes][hp_percent:1 byte][flags:1 byte][angle:2 bytes]
// OwnerState: [type:1 byte][level:1 byte][flags:1 byte][petal_slots:1 byte]

using net_coord = int32_t;
using net_entity_id = uint16_t;

constexpr float net_coord_scale = 64.f;
constexpr float net_radius_scale = 16.f;
constexpr float net_angle_scale = 1000.f;

inline net_coord PackCoord(float value)
{
    return static_cast<net_coord>(std::round(value * net_coord_scale));
}

inline float UnpackCoord(net_coord value)
{
    return static_cast<float>(value) / net_coord_scale;
}

inline net_coord PackViewRadius(float value)
{
    return PackCoord(value);
}

inline float UnpackViewRadius(net_coord value)
{
    return UnpackCoord(value);
}

inline uint16_t PackRadius(float value)
{
    return static_cast<uint16_t>(std::round(std::max(0.f, value) * net_radius_scale));
}

inline float UnpackRadius(uint16_t value)
{
    return static_cast<float>(value) / net_radius_scale;
}

inline uint8_t PackPercent(float value)
{
    return static_cast<uint8_t>(std::clamp(std::round(value * 255.f), 0.f, 255.f));
}

inline float UnpackPercent(uint8_t value)
{
    return static_cast<float>(value) / 255.f;
}

inline int16_t PackAngle(float radians)
{
    return static_cast<int16_t>(std::round(radians * net_angle_scale));
}

inline float UnpackAngle(int16_t value)
{
    return static_cast<float>(value) / net_angle_scale;
}

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

enum class ServerEntityFlag : uint8_t
{
    None = 0,
    Attacking = 1 << 0,
    Defending = 1 << 1,
    Dead = 1 << 2,
    Owner = 1 << 3,
};

inline ServerEntityFlag operator|(ServerEntityFlag lhs, ServerEntityFlag rhs)
{
    return static_cast<ServerEntityFlag>(static_cast<uint8_t>(lhs) | static_cast<uint8_t>(rhs));
}

inline bool HasFlag(ServerEntityFlag flags, ServerEntityFlag flag)
{
    return (static_cast<uint8_t>(flags) & static_cast<uint8_t>(flag)) != 0;
}

struct ServerEntitySnap
{
    net_entity_id entity_id = 0;
    uint8_t entity_type = 0;
    uint8_t team = 0;
    sf::Vector2f pos = {0.f, 0.f};
    float radius = 0.f;
    float hp_percent = 1.f;
    uint8_t flags = 0;
    int16_t angle = 0;

    static constexpr size_t packed_size = 18;

    static bool parse(const uint8_t* data, size_t len, size_t& offset, ServerEntitySnap& snap)
    {
        if (!data || offset + packed_size > len) return false;

        snap.entity_id = ReadU16(data, offset);
        snap.entity_type = data[offset++];
        snap.team = data[offset++];
        snap.pos = sf::Vector2f(UnpackCoord(ReadI32(data, offset)), UnpackCoord(ReadI32(data, offset)));
        snap.radius = UnpackRadius(ReadU16(data, offset));
        snap.hp_percent = UnpackPercent(data[offset++]);
        snap.flags = data[offset++];
        snap.angle = static_cast<int16_t>(ReadU16(data, offset));
        return true;
    }

    static void pack(const ServerEntitySnap& snap, uint8_t* out, size_t& offset)
    {
        WriteU16(out, offset, snap.entity_id);
        out[offset++] = snap.entity_type;
        out[offset++] = snap.team;
        WriteI32(out, offset, PackCoord(snap.pos.x));
        WriteI32(out, offset, PackCoord(snap.pos.y));
        WriteU16(out, offset, PackRadius(snap.radius));
        out[offset++] = PackPercent(snap.hp_percent);
        out[offset++] = snap.flags;
        WriteU16(out, offset, static_cast<uint16_t>(snap.angle));
    }
};

struct ServerMessage
{
    enum class Type : uint8_t
    {
        Welcome = 0x00,
        Snapshot = 0x01,
        OwnerState = 0x10,
        Unknown = 0xff,
    } type = Type::Unknown;

    std::optional<uint16_t> player_id;
    std::optional<net_entity_id> owner_entity_id;
    std::optional<uint8_t> tick_rate;

    std::optional<uint32_t> snapshot_id;
    std::optional<float> view_radius;
    std::vector<ServerEntitySnap> entities;

    std::optional<uint8_t> flags;

    std::optional<uint8_t> level;
    std::optional<uint8_t> petal_slots;

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
            break;
        case Type::Snapshot:
        {
            if (len < 14)
            {
                msg.type = Type::Unknown;
                break;
            }
            msg.snapshot_id = ReadU32(data, offset);
            msg.owner_entity_id = ReadU16(data, offset);
            msg.view_radius = UnpackViewRadius(ReadI32(data, offset));

            uint16_t count = ReadU16(data, offset);
            if (offset + static_cast<size_t>(count) * ServerEntitySnap::packed_size > len)
            {
                msg.entities.clear();
                msg.type = Type::Unknown;
                break;
            }

            msg.entities.reserve(count);
            for (uint16_t i = 0; i < count; ++i)
            {
                ServerEntitySnap snap;
                if (!ServerEntitySnap::parse(data, len, offset, snap))
                {
                    msg.entities.clear();
                    msg.type = Type::Unknown;
                    break;
                }
                msg.entities.push_back(snap);
            }
            break;
        }
        case Type::OwnerState:
            if (len < 4)
            {
                msg.type = Type::Unknown;
                break;
            }
            msg.level = data[offset++];
            msg.flags = data[offset++];
            msg.petal_slots = data[offset++];
            break;
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
            return offset;
        case Type::Snapshot:
        {
            WriteU32(out, offset, msg.snapshot_id.value_or(0));
            WriteU16(out, offset, msg.owner_entity_id.value_or(0));
            WriteI32(out, offset, PackViewRadius(msg.view_radius.value_or(0.f)));
            WriteU16(out, offset, static_cast<uint16_t>(msg.entities.size()));
            for (const ServerEntitySnap& snap : msg.entities)
            {
                ServerEntitySnap::pack(snap, out, offset);
            }
            return offset;
        }
        case Type::OwnerState:
            out[offset++] = msg.level.value_or(1);
            out[offset++] = msg.flags.value_or(0);
            out[offset++] = msg.petal_slots.value_or(0);
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
            return 6;
        case Type::Snapshot:
            return 14 + msg.entities.size() * ServerEntitySnap::packed_size;
        case Type::OwnerState:
            return 4;
        default:
            return 0;
        }
    }
};
