#pragma once
#include <SFML/System/Vector2.hpp>
#include <algorithm>
#include <cstring>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

// Client packets are intentionally compact:
// Input:        [type:2 bits][move_x:1 byte][move_y:1 byte]
// EquipPetal:   [type:2 bits][petal_type:1 byte][slot:4 bits][rarity:4 bits]
// UnequipPetal: [type:2 bits][slot:4 bits]
// AttackDefend: [type:2 bits][attack:1 bit][defend:1 bit]

enum class ClientMsgType : uint8_t
{
    PlayerInput = 0,
    EquipPetal,
    UnequipPetal,
    AttackDefend,
};

struct ClientOperate
{
    enum class Type : uint8_t
    {
        Input = 0x00,
        Equip = 0x01,
        Unequip = 0x02,
        AttackDefend = 0x03,
        Unknown = 0xff,
    } type = Type::Unknown;

    std::optional<int8_t> move_x;
    std::optional<int8_t> move_y;

    std::optional<uint8_t> petal_type;
    std::optional<uint8_t> slot_index;
    std::optional<uint8_t> rarity;

    std::optional<bool> isAttacking;
    std::optional<bool> isDefending;

    static ClientOperate parse(const uint8_t* data, size_t len)
    {
        ClientOperate op{};
        if (!data || len < 1) return op;

        uint8_t first = data[0];
        uint8_t type_code = first & 0x03;
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
        case Type::AttackDefend:
            op.isAttacking = ((first >> 2) & 0x01) != 0;
            op.isDefending = ((first >> 3) & 0x01) != 0;
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
        case Type::AttackDefend:
            out[0] = 0x03;
            if (op.isAttacking.value_or(false)) out[0] |= (1 << 2);
            if (op.isDefending.value_or(false)) out[0] |= (1 << 3);
            return 1;
        default:
            return 0;
        }
    }
};

enum class ServerMsgType : uint8_t
{
    Snapshot = 1,
};

enum class NetEntityKind : uint8_t
{
    Generic = 0,
    Mob,
    Flower,
    Petal,
    Projectile,
};

struct ServerEntitySnapshot
{
    int32_t id = -1;
    NetEntityKind kind = NetEntityKind::Generic;
    int32_t team = 0;
    float x = 0.f;
    float y = 0.f;
    float radius = 0.f;
    float health = 0.f;
    uint8_t flags = 0;
};

struct ServerSnapshot
{
    uint32_t sequence = 0;
    int32_t player_entity_id = -1;
    std::vector<ServerEntitySnapshot> entities;
};

inline size_t ClientOperatePackedLength(uint8_t first)
{
    switch (static_cast<ClientOperate::Type>(first & 0x03))
    {
    case ClientOperate::Type::Input:
    case ClientOperate::Type::Equip:
        return 3;
    case ClientOperate::Type::Unequip:
    case ClientOperate::Type::AttackDefend:
        return 1;
    default:
        return 0;
    }
}

template <typename T> inline void AppendBytes(std::vector<uint8_t>& out, const T& value)
{
    const auto* bytes = reinterpret_cast<const uint8_t*>(&value);
    out.insert(out.end(), bytes, bytes + sizeof(T));
}

inline void AppendU16(std::vector<uint8_t>& out, uint16_t value)
{
    out.push_back(static_cast<uint8_t>(value & 0xff));
    out.push_back(static_cast<uint8_t>((value >> 8) & 0xff));
}

template <typename T> inline bool ReadBytes(const std::vector<uint8_t>& data, size_t& offset, T& value)
{
    if (offset + sizeof(T) > data.size()) return false;
    std::memcpy(&value, data.data() + offset, sizeof(T));
    offset += sizeof(T);
    return true;
}

inline uint16_t ReadU16(const std::vector<uint8_t>& data, size_t offset)
{
    return static_cast<uint16_t>(data[offset]) | (static_cast<uint16_t>(data[offset + 1]) << 8);
}

inline std::vector<uint8_t> PackServerSnapshotFrame(const ServerSnapshot& snapshot)
{
    std::vector<uint8_t> payload;
    payload.reserve(16 + snapshot.entities.size() * 24);

    payload.push_back(static_cast<uint8_t>(ServerMsgType::Snapshot));
    AppendBytes(payload, snapshot.sequence);
    AppendBytes(payload, snapshot.player_entity_id);

    uint16_t count = static_cast<uint16_t>(std::min<size_t>(snapshot.entities.size(), 1024));
    AppendBytes(payload, count);

    for (size_t i = 0; i < count; ++i)
    {
        const ServerEntitySnapshot& entity = snapshot.entities[i];
        const uint8_t kind = static_cast<uint8_t>(entity.kind);
        AppendBytes(payload, entity.id);
        AppendBytes(payload, kind);
        AppendBytes(payload, entity.team);
        AppendBytes(payload, entity.x);
        AppendBytes(payload, entity.y);
        AppendBytes(payload, entity.radius);
        AppendBytes(payload, entity.health);
        AppendBytes(payload, entity.flags);
    }

    std::vector<uint8_t> frame;
    if (payload.size() > 0xffff) return frame;
    frame.reserve(payload.size() + 2);
    AppendU16(frame, static_cast<uint16_t>(payload.size()));
    frame.insert(frame.end(), payload.begin(), payload.end());
    return frame;
}

inline bool TryPopServerSnapshotFrame(std::vector<uint8_t>& buffer, ServerSnapshot& snapshot)
{
    if (buffer.size() < 2) return false;

    const uint16_t payload_size = ReadU16(buffer, 0);
    if (buffer.size() < static_cast<size_t>(payload_size) + 2) return false;

    std::vector<uint8_t> payload(buffer.begin() + 2, buffer.begin() + 2 + payload_size);
    buffer.erase(buffer.begin(), buffer.begin() + 2 + payload_size);

    size_t offset = 0;
    uint8_t type = 0;
    if (!ReadBytes(payload, offset, type) || type != static_cast<uint8_t>(ServerMsgType::Snapshot)) return false;

    ServerSnapshot parsed;
    uint16_t count = 0;
    if (!ReadBytes(payload, offset, parsed.sequence)) return false;
    if (!ReadBytes(payload, offset, parsed.player_entity_id)) return false;
    if (!ReadBytes(payload, offset, count)) return false;

    parsed.entities.reserve(count);
    for (uint16_t i = 0; i < count; ++i)
    {
        ServerEntitySnapshot entity;
        uint8_t kind = 0;
        if (!ReadBytes(payload, offset, entity.id)) return false;
        if (!ReadBytes(payload, offset, kind)) return false;
        if (!ReadBytes(payload, offset, entity.team)) return false;
        if (!ReadBytes(payload, offset, entity.x)) return false;
        if (!ReadBytes(payload, offset, entity.y)) return false;
        if (!ReadBytes(payload, offset, entity.radius)) return false;
        if (!ReadBytes(payload, offset, entity.health)) return false;
        if (!ReadBytes(payload, offset, entity.flags)) return false;
        entity.kind = static_cast<NetEntityKind>(kind);
        parsed.entities.push_back(entity);
    }

    snapshot = std::move(parsed);
    return true;
}
