#include "player.h"
#include "../../Engine/account_data.h"
#include "entities/drop.h"
#include "entities/flower.h"
#include "entities/mob.h"
#include "entities/petals/petal.h"
#include "controllers/player_controller.h"
#include "gameworld.h"
#include "talent.h"
#include "../../Shared/game_config.h"
#include <algorithm>

namespace
{
bool SameTalent(const ITalent* lhs, const ITalent* rhs)
{
    return lhs && rhs && lhs->m_id == rhs->m_id && lhs->m_rarity == rhs->m_rarity && lhs->m_rank == rhs->m_rank;
}

}

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
    m_send_offset = 0;
    m_receive_buffer.clear();
    m_connected = true;
    m_timeout_left = 0.f;
}

void CPlayer::DetachSocket()
{
    if (!m_connected) return;

    m_socket.disconnect();
    m_send_buffer.clear();
    m_send_offset = 0;
    m_receive_buffer.clear();
    m_connected = false;
    m_timeout_left = game_config::timeout_protection_seconds;
    ResetControlledMob();
}

void CPlayer::TickTimeout(float dt)
{
    if (m_mute_timer > 0.f) m_mute_timer = std::max(0.f, m_mute_timer - dt);
    if (m_second_chance_cooldown > 0.f) m_second_chance_cooldown = std::max(0.f, m_second_chance_cooldown - dt);
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

    if (auto* attackable = dynamic_cast<IAttackableMob*>(mob))
        attackable->ClearAttackState();

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
        if (!flower->CanUnequipPetal(static_cast<int>(i))) continue;

        uint8_t old_type = static_cast<uint8_t>(slot.m_p_proto->m_type);
        uint8_t old_rarity = static_cast<uint8_t>(slot.m_stored_rarity);
        flower->UnequipPetal(static_cast<int>(i));
        CAccountDataStore::AddItem(m_account_name, old_type, old_rarity, 1);
        CAccountDataStore::ClearSlot(m_account_name, static_cast<uint8_t>(i));
    }
}

void CPlayer::ApplySavedProgress()
{
    if (!m_authenticated) return;

    auto* flower = dynamic_cast<CPlayerFlower*>(GetEntity());
    if (!flower) return;

    int level = 1;
    int exp = 0;
    if (!CAccountDataStore::GetProgress(m_account_name, level, exp)) return;

    flower->m_level = std::max(1, level);
    flower->m_exp = std::max(0, exp);
    flower->RebuildFinalStats();
    if (const SFlowerStats* stats = flower->GetFinalStats())
        flower->m_health = stats->max_health;
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
    flower->RebuildFinalStats();
}

void CPlayer::ApplySavedTalents()
{
    if (!m_authenticated) return;

    m_talent_points = CAccountDataStore::GetTalentPoints(m_account_name);
    m_talents.clear();

    for (const SAccountTalent& saved : CAccountDataStore::GetTalents(m_account_name))
    {
        ITalent* talent = FindBuiltinTalent(saved.id, saved.rarity, saved.rank);
        if (!talent) continue;
        if (std::find_if(m_talents.begin(), m_talents.end(), [talent](const ITalent* existing)
            {
                return SameTalent(existing, talent);
            }) != m_talents.end())
            continue;
        m_talents.push_back(talent);
    }

    NormalizeTalentOrder();
    RefreshTalentEffects();
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
    if (slot.m_p_proto && !flower->CanUnequipPetal(slot_index)) return false;
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

bool CPlayer::TryCraftPetal(uint8_t petal_type, uint8_t rarity, uint32_t count, SCraftResult* result)
{
    if (!m_authenticated) return false;
    if (petal_type == 0 || !FindPetalPrototype(static_cast<EPetalType>(petal_type))) return false;
    if (rarity < static_cast<uint8_t>(ERarity::Common) || rarity > static_cast<uint8_t>(ERarity::Eternal)) return false;
    if (count < 5) return false;

    return CAccountDataStore::CraftItem(m_account_name, petal_type, rarity, count, result);
}

bool CPlayer::AddTalent(ETalentId id, ERarity rarity, int rank)
{
    if (!m_authenticated) return false;

    ITalent* talent = FindBuiltinTalent(id, rarity, rank);
    if (!talent) return false;
    if (HasTalent(talent)) return false;
    if (talent->m_based && !HasTalent(talent->m_based)) return false;
    if (m_talent_points < talent->m_cost) return false;

    m_talent_points -= talent->m_cost;
    m_talents.push_back(talent);
    NormalizeTalentOrder();
    SaveTalentState();
    RefreshTalentEffects();
    return true;
}

bool CPlayer::RemoveTalent(ETalentId id, ERarity rarity, int rank)
{
    if (!m_authenticated) return false;

    ITalent* target = FindBuiltinTalent(id, rarity, rank);
    if (!target || !HasTalent(target)) return false;

    std::vector<ITalent*> removing;
    removing.push_back(target);

    bool changed = true;
    while (changed)
    {
        changed = false;
        for (ITalent* owned : m_talents)
        {
            if (!owned || !owned->m_based) continue;
            if (std::find(removing.begin(), removing.end(), owned) != removing.end()) continue;
            if (std::find(removing.begin(), removing.end(), owned->m_based) == removing.end()) continue;
            removing.push_back(owned);
            changed = true;
        }
    }

    int refund = 0;
    for (ITalent* talent : removing)
        if (talent) refund += talent->m_cost;

    m_talents.erase(std::remove_if(m_talents.begin(), m_talents.end(),
                                   [&removing](ITalent* talent)
                                   {
                                       return std::find(removing.begin(), removing.end(), talent) != removing.end();
                                   }),
                    m_talents.end());
    m_talent_points = std::max(0, m_talent_points + refund);
    NormalizeTalentOrder();
    SaveTalentState();
    RefreshTalentEffects();
    return true;
}

void CPlayer::AddTalentPoints(int amount)
{
    if (amount == 0) return;

    m_talent_points = std::max(0, m_talent_points + amount);
    if (m_authenticated) CAccountDataStore::SetTalentPoints(m_account_name, m_talent_points);
}

bool CPlayer::HasTalent(const ITalent* talent) const
{
    if (!talent) return false;
    return std::find_if(m_talents.begin(), m_talents.end(), [talent](const ITalent* owned)
    {
        return SameTalent(owned, talent);
    }) != m_talents.end();
}

void CPlayer::ApplyTalents(ETalentEvent event, STalentContext& ctx) const
{
    for (ITalent* talent : m_talents)
    {
        if (talent) talent->Apply(event, ctx);
    }
}

int CPlayer::CalculateTalentSlotCount() const
{
    int slot_count = std::clamp(game_config::mob_player_flower_initial_petal_slots, 0,
                                std::max(0, game_config::mob_player_flower_max_petal_slots));

    STalentContext ctx;
    ctx.player = const_cast<CPlayer*>(this);
    ctx.entity = const_cast<CEntity*>(GetEntity());
    ctx.slot_num = &slot_count;
    ApplyTalents(ETalentEvent::RebuildSlotNum, ctx);

    return std::clamp(slot_count, 0, std::max(0, game_config::mob_player_flower_max_petal_slots));
}

void CPlayer::RefreshTalentEffects()
{
    auto* flower = dynamic_cast<CPlayerFlower*>(GetEntity());
    if (!flower) return;

    flower->RefreshTalentSlotCount();
    flower->RebuildFinalStats();
}

void CPlayer::SetSecondChanceCooldown(float cooldown)
{
    m_second_chance_cooldown = std::max(0.f, cooldown);
}

bool CPlayer::ConsumeUseNewPlayerSpawn()
{
    bool value = m_use_new_player_spawn;
    m_use_new_player_spawn = false;
    return value;
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

void CPlayer::SaveTalentState() const
{
    if (!m_authenticated) return;

    std::vector<SAccountTalent> saved;
    saved.reserve(m_talents.size());
    for (const ITalent* talent : m_talents)
    {
        if (!talent) continue;
        saved.push_back({talent->m_id, talent->m_rarity, talent->m_rank});
    }

    CAccountDataStore::SetTalentPoints(m_account_name, m_talent_points);
    CAccountDataStore::SetTalents(m_account_name, saved);
}

void CPlayer::NormalizeTalentOrder()
{
    std::vector<ITalent*> ordered;
    std::vector<ITalent*> builtins = BuiltinTalents();
    ordered.reserve(m_talents.size());

    for (ITalent* builtin : builtins)
    {
        auto it = std::find_if(m_talents.begin(), m_talents.end(), [builtin](const ITalent* owned)
        {
            return SameTalent(owned, builtin);
        });
        if (it != m_talents.end()) ordered.push_back(builtin);
    }

    m_talents = std::move(ordered);

    for (;;)
    {
        auto it = std::find_if(m_talents.begin(), m_talents.end(), [this](const ITalent* talent)
        {
            return talent && talent->m_based && !HasTalent(talent->m_based);
        });
        if (it == m_talents.end()) break;
        m_talents.erase(it);
    }
}
