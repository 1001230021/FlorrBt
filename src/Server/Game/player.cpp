#include "player.h"
#include "../../Engine/account_data.h"
#include "entities/drop.h"
#include "entities/flower.h"
#include "entities/mob.h"
#include "entities/petals/petal.h"
#include "controllers/player_controller.h"
#include "gameworld.h"
#include "../../Shared/game_config.h"

CPlayer::CPlayer(sf::TcpSocket&& socket, uint32_t id, const std::string& name)
    : m_socket(std::move(socket)), m_player_id(id), m_name(name)
{
}

void CPlayer::HandleOperate(const ClientOperate& op)
{
    if (!m_authenticated) return;

    if (op.type == ClientOperate::Type::Equip)
    {
        if (op.slot_index.has_value() && op.petal_type.has_value() && op.rarity.has_value())
            TryEquipPetal(*op.slot_index, *op.petal_type, *op.rarity);
        return;
    }
    if (op.type == ClientOperate::Type::Unequip)
    {
        if (op.slot_index.has_value()) TryUnequipPetal(*op.slot_index);
        return;
    }

    CEntity* entity = GetEntity();
    if (!entity || entity->m_is_marked_for_des) return;

    auto* mob = dynamic_cast<CMobBase*>(entity);
    if (!mob) return;

    auto* controller = dynamic_cast<CPlayerController*>(mob->GetController());
    if (!controller) return;

    controller->PushOperate(op);
}

void CPlayer::AttachSocket(sf::TcpSocket&& socket)
{
    m_socket = std::move(socket);
    m_socket.setBlocking(false);
    m_send_buffer.clear();
    m_receive_buffer.clear();
    m_connected = true;
    m_timeout_left = 0.f;
}

void CPlayer::DetachSocket()
{
    if (!m_connected) return;

    m_socket.disconnect();
    m_send_buffer.clear();
    m_receive_buffer.clear();
    m_connected = false;
    m_timeout_left = game_config::timeout_protection_seconds;
    ResetControlledMob();
}

void CPlayer::TickTimeout(float dt)
{
    if (m_mute_timer > 0.f) m_mute_timer = std::max(0.f, m_mute_timer - dt);
    if (m_connected || m_timeout_left <= 0.f) return;
    m_timeout_left -= dt;
}

void CPlayer::MuteFor(float seconds)
{
    if (seconds <= 0.f) return;
    m_mute_timer = std::max(m_mute_timer, seconds);
}

void CPlayer::Unmute()
{
    m_mute_timer = 0.f;
}

void CPlayer::RegisterValidReport()
{
    m_invalid_report_count = 0;
}

void CPlayer::RegisterInvalidReport()
{
    if (m_report_disabled) return;
    ++m_invalid_report_count;
    if (game_config::report_invalid_disable_count > 0 &&
        m_invalid_report_count >= game_config::report_invalid_disable_count)
    {
        m_report_disabled = true;
    }
}

bool CPlayer::TickDropPickup()
{
    if (!m_authenticated || m_account_name.empty()) return false;

    CEntity* entity = GetEntity();
    if (!entity || entity->m_is_marked_for_des || entity->IsDead()) return false;

    auto* mob = dynamic_cast<CMobBase*>(entity);
    return mob && mob->TickDropPickup(this);
}

void CPlayer::ResetControlledMob()
{
    auto* mob = dynamic_cast<CMobBase*>(GetEntity());
    if (!mob) return;

    mob->MoveTowards(mob->m_pos, 0.f);
    mob->m_vel = {0.f, 0.f};

    if (auto* flower = dynamic_cast<CFlower*>(mob))
    {
        flower->m_attacking = false;
        flower->m_defending = false;
    }

    if (auto* controller = dynamic_cast<CPlayerController*>(mob->GetController()))
    {
        controller->ResetOperate();
    }
}

void CPlayer::UnequipAllPetals()
{
    if (!m_authenticated) return;

    auto* flower = dynamic_cast<CFlower*>(GetEntity());
    if (!flower) return;

    auto& slots = flower->GetSlots();
    for (size_t i = 0; i < slots.size(); ++i)
    {
        CPetalSlot& slot = slots[i];
        if (!slot.m_p_proto) continue;

        uint8_t old_type = static_cast<uint8_t>(slot.m_p_proto->m_type);
        uint8_t old_rarity = static_cast<uint8_t>(slot.m_stored_rarity);
        flower->UnequipPetal(static_cast<int>(i));
        CAccountDataStore::AddItem(m_account_name, old_type, old_rarity, 1);
    }
}

void CPlayer::ApplySavedSlots()
{
    if (!m_authenticated) return;

    auto* flower = dynamic_cast<CFlower*>(GetEntity());
    if (!flower) return;

    std::vector<SInventoryItem> saved_slots = CAccountDataStore::GetSlots(m_account_name);
    auto& slots = flower->GetSlots();
    for (size_t i = 0; i < saved_slots.size() && i < slots.size(); ++i)
    {
        const SInventoryItem& item = saved_slots[i];
        if (item.petal_type == 0 || item.rarity == 0) continue;

        const CPetalPrototype* proto = FindPetalPrototype(static_cast<EPetalType>(item.petal_type));
        if (!proto || !proto->m_p_behavior) continue;
        flower->LoadPetalSlot(static_cast<int>(i), proto, static_cast<ERarity>(item.rarity));
    }
}

bool CPlayer::TryEquipPetal(uint8_t slot_index, uint8_t petal_type, uint8_t rarity)
{
    if (!m_authenticated) return false;
    if (petal_type == 0) return false;
    if (rarity == 0 || rarity > static_cast<uint8_t>(ERarity::Primordial)) return false;

    auto* flower = dynamic_cast<CFlower*>(GetEntity());
    if (!flower) return false;

    auto& slots = flower->GetSlots();
    if (slot_index >= slots.size()) return false;

    CPetalSlot& slot = slots[slot_index];
    if (!slot.m_available) return false;

    const CPetalPrototype* proto = FindPetalPrototype(static_cast<EPetalType>(petal_type));
    if (!proto || !proto->m_p_behavior) return false;
    if (!CAccountDataStore::TakeItem(m_account_name, petal_type, rarity, 1)) return false;

    if (slot.m_p_proto)
    {
        CAccountDataStore::AddItem(m_account_name, static_cast<uint8_t>(slot.m_p_proto->m_type),
                                   static_cast<uint8_t>(slot.m_stored_rarity), 1);
    }

    flower->EquipPetal(slot_index, proto, static_cast<ERarity>(rarity));
    CAccountDataStore::SetSlot(m_account_name, slot_index, petal_type, rarity);
    return true;
}

bool CPlayer::TryUnequipPetal(uint8_t slot_index)
{
    if (!m_authenticated) return false;

    auto* flower = dynamic_cast<CFlower*>(GetEntity());
    if (!flower) return false;

    auto& slots = flower->GetSlots();
    if (slot_index >= slots.size()) return false;

    CPetalSlot& slot = slots[slot_index];
    if (!slot.m_p_proto) return false;
    if (!flower->CanUnequipPetal(slot_index)) return false;

    uint8_t old_type = static_cast<uint8_t>(slot.m_p_proto->m_type);
    uint8_t old_rarity = static_cast<uint8_t>(slot.m_stored_rarity);
    flower->UnequipPetal(slot_index);
    CAccountDataStore::AddItem(m_account_name, old_type, old_rarity, 1);
    CAccountDataStore::ClearSlot(m_account_name, slot_index);
    return true;
}

void CPlayer::SetOwnedEntity(CEntity* entity)
{
    if (!entity)
    {
        m_p_world = nullptr;
        m_entity_id = -1;
        return;
    }

    m_p_world = entity->GameWorld();
    m_entity_id = entity->m_id;
}

void CPlayer::Authenticate(const std::string& account_name)
{
    m_account_name = account_name;
    m_name = account_name;
    m_authenticated = true;
}

CEntity* CPlayer::GetEntity() const
{
    if (!m_p_world || m_entity_id < 0) return nullptr;
    return m_p_world->GetEntity(m_entity_id);
}

CGameContext* CPlayer::GameContext() const
{
    return m_p_world ? m_p_world->GameContext() : nullptr;
}
