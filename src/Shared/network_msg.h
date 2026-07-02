#pragma once
#include <SFML/System/Vector2.hpp>
#include <cstdint>
#include <optional>

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