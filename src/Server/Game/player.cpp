#include "player.h"
#include "../Persistence/account_store.h"
#include "../../Shared/game_config.h"
#include "../../Shared/petal_card_exp.h"
#include "../server.h"
#include "controllers/player_controller.h"
#include "entities/drop.h"
#include "entities/flower.h"
#include "entities/mob.h"
#include "entities/petals/petal.h"
#include "gameworld.h"
#include "talent.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace
{
bool SameTalent(const ITalent* lhs, const ITalent* rhs)
{
    return lhs && rhs && lhs->m_id == rhs->m_id && lhs->m_rarity == rhs->m_rarity && lhs->m_rank == rhs->m_rank;
}

CPlayerController* GetPlayerController(CPlayer& player)
{
    CEntity* entity = player.GetEntity();
    if (!entity || entity->IsMarkedForDes()) return nullptr;

    auto* mob = dynamic_cast<CMobBase*>(entity);
    return mob ? dynamic_cast<CPlayerController*>(mob->GetController()) : nullptr;
}

} // namespace

CPlayer::CPlayer(sf::TcpSocket&& socket, uint32_t id, const std::string& name)
    : m_socket(std::move(socket)), m_player_id(id), m_name(name)
{
}

bool CPlayer::HandleOperate(const ClientOperate& op)
{
    if (!m_authenticated) return false;

    if (op.type == ClientOperate::Type::Equip)
    {
        if (op.slot_index.has_value() && op.petal_type.has_value() && op.rarity.has_value())
            return TryEquipPetal(*op.slot_index, *op.petal_type, *op.rarity);
        return false;
    }
    if (op.type == ClientOperate::Type::Unequip)
    {
        return op.slot_index.has_value() && TryUnequipPetal(*op.slot_index);
    }

    if (auto* controller = GetPlayerController(*this))
    {
        controller->PushOperate(op);
        return true;
    }
    return false;
}

CPlayer::~CPlayer()
{
    CAccountDataStore::RemoveSandboxAccount(m_progress_account);
}

std::string CPlayer::ProgressKey(SWorldHandle world) const
{
    return "@sandbox:" + std::to_string(world.id) + ":" + std::to_string(world.generation) + ":" +
        std::to_string(SessionGeneration()) + ":" + m_account_name;
}

bool CPlayer::RestoreIsolatedProgress(SWorldHandle world, const SPlayerAccount& state)
{
    if (!world.generation || !IsAuthenticated()) return false;
    const auto key = ProgressKey(world);
    if (!CAccountDataStore::RestoreSandboxAccount(key, state)) return false;
    if (key != m_progress_account) CAccountDataStore::RemoveSandboxAccount(m_progress_account);
    m_progress_account = key;
    m_progress_world = world;
    return true;
}

void CPlayer::RefreshScopedProgress(CPlayerFlower* flower)
{
    if (!flower || flower->IsMarkedForDes()) return;
    const auto* old_stats = flower->GetFinalStats();
    const float health_ratio = old_stats && old_stats->max_health > 0.f ?
        std::clamp(flower->Health() / old_stats->max_health, 0.f, 1.f) : 1.f;
    int level = 1; std::int64_t exp = 0;
    if (CAccountDataStore::GetProgress(GetProgressAccountName(), level, exp))
    { flower->m_level = std::max(1, level); flower->m_exp = std::max<std::int64_t>(0, exp); }
    // Never trim the previous scope's slots through the normal persisted
    // unequip path. Rebuild them without returning temporary cards.
    RestoreSavedLoadout(flower);
    flower->Health() = health_ratio * flower->GetFinalStats()->max_health;
}

bool CPlayer::SynchronizeGameplayScope(CGameWorld& world, CEntity* departing)
{
    const bool isolated = world.GetController() && world.GetController()->UsesIsolatedProgress();
    const SWorldHandle scope{world.GetId(), world.GetInstanceGeneration()};
    if ((!isolated && !HasIsolatedProgress()) ||
        (isolated && HasIsolatedProgress() && m_progress_world == scope)) return true;
    const std::string next = isolated ? ProgressKey(scope) : std::string{};
    if (isolated && !CAccountDataStore::SandboxCloneAccount(next, m_account_name)) return false;
    const bool leaving_isolated = HasIsolatedProgress();
    CAccountDataStore::RemoveSandboxAccount(m_progress_account);
    m_progress_account = next;
    m_progress_world = isolated ? scope : SWorldHandle{};
    LoadSavedTalents(false);
    LoadCodex(CAccountDataStore::GetCodex(GetProgressAccountName()));
    auto* current = dynamic_cast<CPlayerFlower*>(GetEntity());
    if (leaving_isolated && departing != current) RefreshScopedProgress(dynamic_cast<CPlayerFlower*>(departing));
    RefreshScopedProgress(current);
    return true;
}

bool CPlayer::HandleScheduledOperate(const ClientOperate& op, std::uint32_t delay_ticks, std::uint32_t sequence)
{
    if (!m_authenticated) return false;
    auto* controller = GetPlayerController(*this);
    return controller && controller->PushOperate(op, delay_ticks, sequence);
}

void CPlayer::AttachSocket(sf::TcpSocket&& socket)
{
    m_rcon_authorized = false;
    if (auto* controller = GetPlayerController(*this)) controller->ResetOperate();
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
    m_rcon_authorized = false;
    m_timeout_left = game_config::timeout_protection_seconds;
    ResetControlledMob();
}

void CPlayer::RestoreDisconnectedSession(const std::string& remote_address, float timeout_left, float mute_timer,
                                         bool report_disabled, int invalid_report_count,
                                         float second_chance_cooldown, bool use_new_player_spawn)
{
    m_socket.disconnect();
    m_send_buffer.clear();
    m_send_offset = 0;
    m_receive_buffer.clear();
    m_connected = false;
    m_timeout_left = std::isfinite(timeout_left)
                         ? std::max(timeout_left, game_config::timeout_protection_seconds)
                         : game_config::timeout_protection_seconds;
    m_remote_address = remote_address;
    m_mute_timer = std::isfinite(mute_timer) ? std::max(0.f, mute_timer) : 0.f;
    m_report_disabled = report_disabled;
    m_invalid_report_count = std::max(0, invalid_report_count);
    m_second_chance_cooldown =
        std::isfinite(second_chance_cooldown) ? std::max(0.f, second_chance_cooldown) : 0.f;
    m_use_new_player_spawn = use_new_player_spawn;
    m_rcon_authorized = false;
}

void CPlayer::TickTimeout(float dt)
{
    if (!std::isfinite(dt) || dt < 0.f) return;
    if (m_mute_timer > 0.f) m_mute_timer = std::max(0.f, m_mute_timer - dt);
    if (m_second_chance_cooldown > 0.f) m_second_chance_cooldown = std::max(0.f, m_second_chance_cooldown - dt);
    if (m_connected || m_timeout_left <= 0.f) return;
    m_timeout_left -= dt;
}

void CPlayer::MuteFor(float seconds)
{
    if (!std::isfinite(seconds) || seconds <= 0.f) return;
    m_mute_timer = std::max(m_mute_timer, seconds);
}

void CPlayer::Unmute() { m_mute_timer = 0.f; }

void CPlayer::RegisterValidReport() { m_invalid_report_count = 0; }

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
    if (!entity || entity->IsMarkedForDes() || entity->IsDead()) return false;

    auto* mob = dynamic_cast<CMobBase*>(entity);
    return mob && mob->TickDropPickup(this);
}

void CPlayer::ResetControlledMob()
{
    auto* mob = dynamic_cast<CMobBase*>(GetEntity());
    if (!mob) return;

    mob->MoveTowards(mob->Pos(), 0.f);
    mob->m_vel = { 0.f, 0.f };

    if (auto* attackable = dynamic_cast<IAttackableMob*>(mob)) attackable->ClearAttackState();

    if (auto* controller = dynamic_cast<CPlayerController*>(mob->GetController()))
    {
        controller->ResetOperate();
    }
}

void CPlayer::UnequipAllEquippedPetals()
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
        if (!flower->UnequipPetal(static_cast<int>(i))) continue;
        CAccountDataStore::AddItem(GetProgressAccountName(), old_type, old_rarity, 1);
        CAccountDataStore::ClearSlot(GetProgressAccountName(), static_cast<uint8_t>(i));
    }
}

void CPlayer::ApplySavedProgress()
{
    if (!m_authenticated) return;

    auto* flower = dynamic_cast<CPlayerFlower*>(GetEntity());
    if (!flower) return;

    int level = 1;
    std::int64_t exp = 0;
    if (!CAccountDataStore::GetProgress(GetProgressAccountName(), level, exp)) return;

    flower->m_level = std::max(1, level);
    flower->m_exp = std::max<std::int64_t>(0, exp);
    flower->RebuildFinalStats();
    if (const SFlowerStats* stats = flower->GetFinalStats()) flower->Health() = stats->max_health;
}

void CPlayer::LoadSavedSlots(CFlower* target)
{
    if (!m_authenticated) return;

    auto* flower = target ? target : dynamic_cast<CFlower*>(GetEntity());
    if (!flower) return;

    std::vector<SInventoryItem> saved_slots = CAccountDataStore::GetSlots(GetProgressAccountName());
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

void CPlayer::RestoreSavedLoadout(CPlayerFlower* target)
{
    if (!m_authenticated) return;

    auto* flower = target ? target : dynamic_cast<CPlayerFlower*>(GetEntity());
    if (!flower) return;

    // Controller-owned petals (and any stale runtime petals) must be removed
    // without touching the account. The account's primary slots are restored
    // below, after the flower has returned to the normal player slot shape.
    auto& slots = flower->GetSlots();
    for (size_t i = 0; i < slots.size(); ++i)
    {
        if (slots[i].m_p_proto) flower->ForceUnequipPetal(static_cast<int>(i));
        flower->SetBanned(false, static_cast<int>(i));
    }
    // Also clear any controller-created copies belonging to an empty slot
    // before a controller-specific slot count is removed.
    flower->DestroyPetalEntities();

    flower->SetPetalSlotCapacity(game_config::mob_player_flower_max_petal_slots);
    // Do not call RefreshTalentSlotCount() here: that routine persists
    // petals removed from slots beyond the talent count. Those slots may hold
    // controller-owned petals (EOU pheromones), which must never be written
    // into the player's account while restoring a normal loadout.
    flower->SetPetalSlotCount(CountTalentSlots());
    LoadSavedSlots(flower);
}

void CPlayer::LoadSavedTalents(bool refresh_effects)
{
    if (!m_authenticated) return;

    m_talent_points = CAccountDataStore::GetTalentPoints(GetProgressAccountName());
    m_talents.clear();

    for (const SAccountTalent& saved : CAccountDataStore::GetTalents(GetProgressAccountName()))
    {
        ITalent* talent = FindBuiltinTalent(saved.id, saved.rarity, saved.rank);
        if (!talent) continue;
        if (std::find_if(m_talents.begin(), m_talents.end(),
                         [talent](const ITalent* existing) { return SameTalent(existing, talent); }) != m_talents.end())
            continue;
        m_talents.push_back(talent);
    }

    NormalizeTalentOrder();
    if (refresh_effects) RefreshTalentEffects();
}

bool CPlayer::TryEquipPetal(uint8_t slot_index, uint8_t petal_type, uint8_t rarity)
{
    if (!m_authenticated) return false;
    if (petal_type == 0) return false;
    if (!IsKnownRarity(static_cast<ERarity>(rarity))) return false;

    auto* flower = dynamic_cast<CFlower*>(GetEntity());
    if (!flower) return false;
    // A dead player flower is a corpse until the respawn path restores it.
    // Reject loadout mutations here as well as in the network dispatcher so
    // every caller (commands, tests and future controllers) observes the
    // same invariant and cannot desynchronise the primary slots.
    if (auto* player_flower = dynamic_cast<CPlayerFlower*>(flower); player_flower && player_flower->m_is_dead)
        return false;
    IGameController* controller = nullptr;
    if (CGameWorld* world = flower->GameWorld()) controller = world->GetController();
    if (controller && (controller->LocksPetalLoadout() ||
                       !controller->CanEquipPetalType(static_cast<EPetalType>(petal_type))))
        return false;

    auto& slots = flower->GetSlots();
    if (slot_index >= slots.size()) return false;

    CPetalSlot& slot = slots[slot_index];
    // A banned slot is still allowed to contain its saved petal, but it is
    // not an operable loadout slot. EquipPetal() intentionally rejects it;
    // reject it here as well so inventory is never mutated before that
    // rejection is reached.
    if (!slot.m_available || slot.m_banned) return false;

    const CPetalPrototype* proto = FindPetalPrototype(static_cast<EPetalType>(petal_type));
    if (!proto || !proto->m_p_behavior) return false;

    const bool replacing = slot.m_p_proto != nullptr;
    const uint8_t old_type = replacing ? static_cast<uint8_t>(slot.m_p_proto->m_type) : 0;
    const uint8_t old_rarity = replacing ? static_cast<uint8_t>(slot.m_stored_rarity) : 0;
    if (slot.m_p_proto)
    {
        if (controller && !controller->CanUnequipPetalType(slot.m_p_proto->m_type)) return false;
        if (!flower->CanUnequipPetal(slot_index)) return false;
    }

    // Keep the inventory and the runtime slot mutation in one save batch. In
    // particular, EquipPetal() used to be void and could silently reject a
    // banned slot after this TakeItem(), permanently eating the new petal.
    CAccountDataStore::CSaveBatch save_batch;
    if (!CAccountDataStore::TakeItem(GetProgressAccountName(), petal_type, rarity, 1)) return false;

    if (!flower->EquipPetal(slot_index, proto, static_cast<ERarity>(rarity)))
    {
        // This is a defensive rollback for any future validation added to
        // CFlower::EquipPetal(). The preflight above handles the known failure
        // paths, but a failed operation must never consume inventory.
        CAccountDataStore::AddItem(GetProgressAccountName(), petal_type, rarity, 1);
        return false;
    }

    if (replacing) CAccountDataStore::AddItem(GetProgressAccountName(), old_type, old_rarity, 1);
    CAccountDataStore::SetSlot(GetProgressAccountName(), slot_index, petal_type, rarity);
    return true;
}

bool CPlayer::TryUnequipPetal(uint8_t slot_index)
{
    if (!m_authenticated) return false;

    auto* flower = dynamic_cast<CFlower*>(GetEntity());
    if (!flower) return false;
    if (auto* player_flower = dynamic_cast<CPlayerFlower*>(flower); player_flower && player_flower->m_is_dead)
        return false;

    auto& slots = flower->GetSlots();
    if (slot_index >= slots.size()) return false;

    CPetalSlot& slot = slots[slot_index];
    if (!slot.m_p_proto) return false;
    if (CGameWorld* world = flower->GameWorld())
        if (IGameController* controller = world->GetController())
            if (controller->LocksPetalLoadout() || !controller->CanUnequipPetalType(slot.m_p_proto->m_type))
                return false;
    if (!flower->CanUnequipPetal(slot_index)) return false;

    uint8_t old_type = static_cast<uint8_t>(slot.m_p_proto->m_type);
    uint8_t old_rarity = static_cast<uint8_t>(slot.m_stored_rarity);
    CAccountDataStore::CSaveBatch save_batch;
    if (!flower->UnequipPetal(slot_index)) return false;
    CAccountDataStore::AddItem(GetProgressAccountName(), old_type, old_rarity, 1);
    CAccountDataStore::ClearSlot(GetProgressAccountName(), slot_index);
    return true;
}

bool CPlayer::TryCraftPetal(uint8_t petal_type, uint8_t rarity, uint32_t count, SCraftResult* result)
{
    if (!m_authenticated) return false;
    const CPetalPrototype* proto = FindPetalPrototype(static_cast<EPetalType>(petal_type));
    if (petal_type == 0 || !proto) return false;
    if (rarity < static_cast<uint8_t>(ERarity::Common) || rarity > static_cast<uint8_t>(ERarity::Eternal)) return false;
    if (count < 5) return false;

    SCraftResult local_result;
    SCraftResult* craft_result = result ? result : &local_result;
    if (!CAccountDataStore::CraftItem(GetProgressAccountName(), petal_type, rarity, count, craft_result)) return false;

    if (craft_result->successes > 0 && craft_result->result_rarity != 0)
    {
        ObtainPetalCard(petal_type, craft_result->result_rarity, craft_result->successes, false);
        ERarity result_rarity = static_cast<ERarity>(craft_result->result_rarity);
        if (CServer::MeetsPetalReportRarity(result_rarity, game_config::min_craft_report_rarity))
        {
            if (CServer* server = CServer::GetInstance())
                server->BroadcastPetalReport("crafted", result_rarity, proto->m_name, m_name);
        }
    }
    return true;
}

bool CPlayer::ObtainPetalCard(uint8_t petal_type, uint8_t rarity, uint32_t count, bool add_to_inventory)
{
    if (!m_authenticated || m_account_name.empty()) return false;
    if (petal_type == 0 || count == 0) return false;
    if (!FindPetalPrototype(static_cast<EPetalType>(petal_type))) return false;

    ERarity card_rarity = static_cast<ERarity>(rarity);
    if (!IsKnownRarity(card_rarity)) return false;

    CAccountDataStore::CSaveBatch save_batch;
    if (add_to_inventory) CAccountDataStore::AddItem(GetProgressAccountName(), petal_type, rarity, count);

    auto* flower = dynamic_cast<CPlayerFlower*>(GetEntity());
    std::int64_t per_card_exp = GetPetalCardExpForRarity(card_rarity);
    if (flower && per_card_exp > 0)
    {
        const std::uint64_t max_exp = static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
        const std::uint64_t per_card = static_cast<std::uint64_t>(per_card_exp);
        std::uint64_t total_exp = max_exp;
        if (count == 0 || per_card <= max_exp / static_cast<std::uint64_t>(count))
            total_exp = per_card * static_cast<std::uint64_t>(count);
        std::int64_t clamped_exp = static_cast<std::int64_t>(std::min<std::uint64_t>(total_exp, max_exp));
        flower->TakeExp(clamped_exp);
    }
    return true;
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
    RefreshTalentEffects(true);
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
                                   [&removing](ITalent* talent) {
                                       return std::find(removing.begin(), removing.end(), talent) != removing.end();
                                   }),
                    m_talents.end());
    m_talent_points = std::max(0, m_talent_points + refund);
    NormalizeTalentOrder();
    SaveTalentState();
    RefreshTalentEffects(true);
    return true;
}

void CPlayer::AddTalentPoints(int amount)
{
    if (amount == 0) return;

    m_talent_points = std::max(0, m_talent_points + amount);
    if (m_authenticated) CAccountDataStore::SetTalentPoints(GetProgressAccountName(), m_talent_points);
}

bool CPlayer::HasTalent(const ITalent* talent) const
{
    if (!talent) return false;
    return std::find_if(m_talents.begin(), m_talents.end(),
                        [talent](const ITalent* owned) { return SameTalent(owned, talent); }) != m_talents.end();
}

void CPlayer::ApplyTalents(ETalentEvent event, STalentContext& ctx) const
{
    // Learned talents and inventory slot capacity are permanent information.
    // Only stat contributions are suppressed; state/skill events still run.
    if (event == ETalentEvent::RebuildFlowerStats || event == ETalentEvent::RebuildPetalStats)
    {
        const auto* actor = ctx.flower ? static_cast<const CMobBase*>(ctx.flower)
                                      : dynamic_cast<const CMobBase*>(ctx.entity);
        if (actor && !actor->AllowsStatModifiers()) return;
    }
    for (ITalent* talent : m_talents)
    {
        if (talent) talent->Apply(event, ctx);
    }

    if (m_p_world)
    {
        if (IGameController* controller = m_p_world->GetController())
            controller->ModifyTalentContext(*m_p_world, const_cast<CPlayer*>(this), event, ctx);
    }
}

int CPlayer::CountTalentSlots() const
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

void CPlayer::RefreshTalentEffects(bool reload_petals)
{
    auto* flower = dynamic_cast<CFlower*>(GetEntity());
    if (!flower) return;

    auto* player_flower = dynamic_cast<CPlayerFlower*>(flower);
    if (!player_flower)
    {
        flower->MarkFinalStatsDirty();
        return;
    }

    player_flower->RefreshTalentSlotCount();
    player_flower->RebuildFinalStats();
    if (reload_petals) player_flower->ReloadPetals();
}

void CPlayer::SetSecondChanceCooldown(float cooldown)
{
    m_second_chance_cooldown = std::isfinite(cooldown) ? std::max(0.f, cooldown) : 0.f;
}

bool CPlayer::ConsumeUseNewPlayerSpawn()
{
    bool value = m_use_new_player_spawn;
    m_use_new_player_spawn = false;
    return value;
}

void CPlayer::SetOwnedEntity(CEntity* entity)
{
    if (m_transition_active) return;
    CommitOwnedEntity(entity);
}

void CPlayer::CommitOwnedEntity(CEntity* entity)
{
    if (auto* old_flower = dynamic_cast<CFlower*>(GetEntity())) old_flower->MarkFinalStatsDirty();

    const SEntityHandle handle = entity ? entity->GetHandle() : SEntityHandle{};
    if (!entity || !entity->GameWorld() || !handle.IsValid())
    {
        m_p_world = nullptr;
        m_entity_ref = {};
        return;
    }

    m_p_world = entity->GameWorld();
    m_entity_ref = handle;
    m_needs_respawn = false;

    if (auto* new_flower = dynamic_cast<CFlower*>(entity)) new_flower->MarkFinalStatsDirty();
}

void CPlayer::ClearOwnedEntityFromWorld(CGameWorld* world)
{
    if (!world || m_p_world != world) return;
    m_needs_respawn = m_authenticated;
    CEntity* old_entity = GetEntity();
    if (auto* old_flower = dynamic_cast<CFlower*>(old_entity)) old_flower->MarkFinalStatsDirty();
    m_p_world = nullptr;
    m_entity_ref = {};
}

void CPlayer::Authenticate(const std::string& account_name)
{
    m_account_name = account_name;
    m_name = account_name;
    m_authenticated = true;
    m_cp_stack.clear();
    m_cp_check_phase = static_cast<uint8_t>(m_player_id & 0x07);
    LoadCodex(CAccountDataStore::GetCodex(account_name));
}

void CPlayer::RecordCodexKill(uint8_t mob_type, uint8_t rarity)
{
    if (!m_authenticated || mob_type == 0 || rarity == 0) return;
    const uint32_t key = (static_cast<uint32_t>(mob_type) << 8) | static_cast<uint32_t>(rarity);
    auto it = m_codex.find(key);
    if (it == m_codex.end()) m_codex[key] = 1;
    else it->second = std::min<uint32_t>(UINT32_MAX, it->second + 1);
    CAccountDataStore::AddCodexKill(GetProgressAccountName(), mob_type, rarity);
}

void CPlayer::LoadCodex(const std::vector<SAccountCodexEntry>& entries)
{
    m_codex.clear();
    for (const SAccountCodexEntry& entry : entries)
    {
        if (entry.mob_type == 0 || entry.rarity == 0 || entry.count == 0) continue;
        const uint32_t key = (static_cast<uint32_t>(entry.mob_type) << 8) | static_cast<uint32_t>(entry.rarity);
        m_codex[key] = entry.count;
    }
}

CEntity* CPlayer::GetEntity() const
{
    if (!m_p_world) return nullptr;
    return m_p_world->ResolveEntity(m_entity_ref);
}

CGameContext* CPlayer::GameContext() const { return m_p_world ? m_p_world->GameContext() : nullptr; }

void CPlayer::SaveTalentState() const
{
    if (!m_authenticated) return;

    std::vector<SAccountTalent> saved;
    saved.reserve(m_talents.size());
    for (const ITalent* talent : m_talents)
    {
        if (!talent) continue;
        saved.push_back({ talent->m_id, talent->m_rarity, talent->m_rank });
    }

    CAccountDataStore::SetTalentPoints(GetProgressAccountName(), m_talent_points);
    CAccountDataStore::SetTalents(GetProgressAccountName(), saved);
}

void CPlayer::NormalizeTalentOrder()
{
    std::vector<ITalent*> ordered;
    std::vector<ITalent*> builtins = BuiltinTalents();
    ordered.reserve(m_talents.size());

    for (ITalent* builtin : builtins)
    {
        auto it = std::find_if(m_talents.begin(), m_talents.end(),
                               [builtin](const ITalent* owned) { return SameTalent(owned, builtin); });
        if (it != m_talents.end()) ordered.push_back(builtin);
    }

    m_talents = std::move(ordered);

    for (;;)
    {
        auto it = std::find_if(m_talents.begin(), m_talents.end(), [this](const ITalent* talent) {
            return talent && talent->m_based && !HasTalent(talent->m_based);
        });
        if (it == m_talents.end()) break;
        m_talents.erase(it);
    }
}
