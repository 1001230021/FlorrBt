#pragma once
#include <SFML/System/Vector2.hpp>
#include <cstdint>
#include <cstring>
#include <optional>

enum class ClientMsgType : uint8_t
{
    PlayerInput = 0,
    EquipPetal,
    UnequipPetal,
    AttDef,
};

// 分爲4種情況發包:
// 1、[3byte] Input:        [1b] Type ,  [1b] MoveX ,      [1b] MoveY
// 2、[3byte] EquipPetal:   [1b] Type ,  [1b] PetalType ,  [1b] Slot(4bits)+Rarity(4bits) (slot rarity最大<15)
// 3、[1byte] UnequipPetal: [1b] Type(2bits)+Slot(4bits)
// 4、[1byte] Att&Def:      [1b] Type(2bits)+Att(1bit)+Def(1bit)
// 平均每次帶寬僅需 2bytes !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!

struct ClientOperate
{
    // ---------- Type ----------
    enum class Type : uint8_t
    {
        Input = 0x00,
        Equip = 0x01,
        Unequip = 0x02,
        AttackDefend = 0x03,
        Unknow = 0xff,
    } type = Type::Unknow;

    // ---------- Input 字段 ----------
    std::optional<int8_t> move_x; // [-128, 127]
    std::optional<int8_t> move_y; // [-128, 127]

    // ---------- Equip 字段 ----------
    std::optional<uint8_t> petal_type; // Petal Type [0, 255]
    std::optional<uint8_t> slot_index; // Slot       [0, 15]
    std::optional<uint8_t> rarity;     // Rarity     [0, 15]

    // ---------- Att&Def 字段 ----------
    std::optional<bool> isAttacking;
    std::optional<bool> isDefending;

    // ---------- Static Methods ----------
    static ClientOperate parse(const uint8_t* data, size_t len)
    {
        ClientOperate op{};
        if (len < 1)
            return op;

        uint8_t first = data[0];
        uint8_t typeCode = first & 0x03;
        op.type = static_cast<Type>(typeCode);

        switch (typeCode)
        {
        case static_cast<int>(Type::Input):
        {
            if (len < 3)
            {
                op.type = Type::Unknow;
                break;
            }
            op.move_x = static_cast<int8_t>(data[1]);
            op.move_y = static_cast<int8_t>(data[2]);
            break;
        }
        case static_cast<int>(Type::Equip):
        {
            if (len < 3)
            {
                op.type = Type::Unknow;
                break;
            }
            op.petal_type = data[1];
            op.slot_index = (data[2] >> 4) & 0x0F;
            op.rarity = data[2] & 0x0F;
            break;
        }
        case static_cast<int>(Type::Unequip):
        {
            if (len < 1)
            {
                op.type = Type::Unknow;
                break;
            }
            op.slot_index = (first >> 2) & 0x0F;
            break;
        }
        case static_cast<int>(Type::AttackDefend):
        {
            if (len < 1)
            {
                op.type = Type::Unknow;
                break;
            }
            op.isAttacking = (first >> 2) & 0x01;
            op.isDefending = (first >> 3) & 0x01;
            break;
        }
        default:
            break;
        }
        return op;
    }

    static size_t pack(const ClientOperate& op, uint8_t* out)
    {
        if (op.type == Type::Input)
        {
            out[0] = 0x00;
            out[1] = static_cast<uint8_t>(op.move_x.value_or(0));
            out[2] = static_cast<uint8_t>(op.move_y.value_or(0));
            return 3;
        }
        if (op.type == Type::Equip)
        {
            out[0] = 0x01;
            out[1] = op.petal_type.value_or(0);
            out[2] = ((op.slot_index.value_or(0) & 0x0F) << 4) | (op.rarity.value_or(0) & 0x0F);
            return 3;
        }
        if (op.type == Type::Unequip)
        {
            out[0] = 0x02 | ((op.slot_index.value_or(0) & 0x0F) << 2);
            return 1;
        }
        if (op.type == Type::AttackDefend)
        {
            out[0] = 0x03;
            if (op.isAttacking.value_or(false))
                out[0] |= (1 << 2);
            if (op.isDefending.value_or(false))
                out[0] |= (1 << 3);
            return 1;
        }
        return 0;
    }
};