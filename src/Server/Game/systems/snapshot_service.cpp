#include "snapshot_service.h"
#include "../../../Shared/game_config.h"
#include "../../../Shared/tools.h"
#include "../controllers/melee_controller.h"
#include "../entities/blood_sacrifice_ritual.h"
#include "../entities/drop.h"
#include "../entities/flower.h"
#include "../entities/mob.h"
#include "../entities/petals/petal.h"
#include "../entities/portal.h"
#include "../entities/projectile.h"
#include "../gamecontrollers/opencontroller.h"
#include "../gameworld.h"
#include "../player.h"
#include "../state_zone.h"
#include "../states/states.h"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace
{
std::string GetEntityName(const CEntity& entity)
{
    if (entity.IsEntityType(EEntityType::Mob))
    {
        const auto& mob = static_cast<const CMobBase&>(entity);
        if (mob.GetMobType() == EMobType::PlayerFlower) return static_cast<const CPlayerFlower&>(mob).m_name;
    }
    return "";
}

float GetHealthPercent(const CEntity& entity)
{
    if (entity.GetNetworkType() == server_blood_sacrifice_entity_type)
        return static_cast<const CBloodSacrificeRitual&>(entity).EffectProgress();
    if (entity.IsEntityType(EEntityType::Portal)) return 1.f;
    if (entity.IsEntityType(EEntityType::Mob))
    {
        const SMobStats* stats = static_cast<const CMobBase&>(entity).GetFinalStats();
        if (stats && stats->max_health > 0.f) return entity.m_health / stats->max_health;
    }
    return entity.m_health;
}

bool FlowerHasAvailablePetal(const CFlower& flower, EPetalType type)
{
    for (const auto& slot : flower.GetSlots())
    {
        if (!slot.m_available || slot.m_banned || !slot.m_p_proto) continue;
        if (slot.m_p_proto->m_type == type) return true;
    }
    return false;
}

void PopulateOwnerPetalRuntime(const CEntity& owner, ServerEntitySnap& snap)
{
    const auto* flower = dynamic_cast<const CFlower*>(&owner);
    if (!flower) return;

    const auto& slots = flower->GetSlots();
    const size_t slot_count = std::min(slots.size(), snap.primary_slots.size());
    for (size_t slot_index = 0; slot_index < slot_count; ++slot_index)
    {
        const CPetalSlot& slot = slots[slot_index];
        SOwnerPetalSlot& slot_snap = snap.primary_slots[slot_index];
        slot_snap.copies.clear();
        slot_snap.copies.reserve(std::min<size_t>(slot.m_p_petals.size(), UINT8_MAX));
        for (size_t copy_index = 0; copy_index < slot.m_p_petals.size() && copy_index < UINT8_MAX; ++copy_index)
        {
            SOwnerPetalSlot::SCopy copy_snap;
            const CPetal* petal = slot.m_p_petals[copy_index];
            if (petal)
            {
                copy_snap.state = SOwnerPetalSlot::ECopyState::Alive;
                const float max_health = petal->m_final_petal_stats.health;
                copy_snap.progress = PackPercent(max_health > 0.f ? petal->m_health / max_health : 0.f);
            } else
            {
                copy_snap.state = SOwnerPetalSlot::ECopyState::Loading;
                copy_snap.progress = PackPercent(slot.GetLoadProgress(static_cast<int>(copy_index)));
            }
            slot_snap.copies.push_back(copy_snap);
        }
    }
}

enum class snapshot_pool : std::uint8_t
{
    Mob,
    Projectile,
    Misc,
};

struct cached_snapshot_entity
{
    const CEntity* entity = nullptr;
    const CEntity* root_owner = nullptr;
    ServerEntitySnap snap;
    float rarity_rank = 0.f;
    int drop_owner_id = drop_owner_all;
    snapshot_pool pool = snapshot_pool::Misc;
    bool is_private_drop = false;
    bool player_entity = false;
    bool attached = false;
};

struct snapshot_world_cache
{
    std::unordered_map<const CEntity*, cached_snapshot_entity> entities;
};

struct snapshot_frame_cache
{
    std::uint32_t snapshot_id = std::numeric_limits<std::uint32_t>::max();
    std::unordered_map<const CGameWorld*, snapshot_world_cache> worlds;
};

struct visible_candidate
{
    const cached_snapshot_entity* cached = nullptr;
    float dist_sq = 0.f;
    float rarity_rank = 0.f;
    bool owner_related = false;
    bool player_entity = false;
    bool same_team = false;
    bool attached = false;
};

struct snapshot_scratch
{
    std::vector<visible_candidate> mobs;
    std::vector<visible_candidate> projectiles;
    std::vector<visible_candidate> misc;
    std::vector<visible_candidate> visible;
    std::vector<CEntity*> mandatory_squad_entities;
};

thread_local snapshot_frame_cache g_snapshot_frame_cache;
thread_local snapshot_scratch g_snapshot_scratch;

float SnapshotRarityRank(const CEntity& entity)
{
    switch (entity.GetEntityType())
    {
    case EEntityType::Mob:
        return GetRaritySortRank(static_cast<const CMobBase&>(entity).GetRarity());
    case EEntityType::Drop:
        return GetRaritySortRank(static_cast<const CDrop&>(entity).GetRarity());
    case EEntityType::Effect:
        if (entity.GetNetworkType() == server_blood_sacrifice_entity_type)
            return GetRaritySortRank(static_cast<const CBloodSacrificeRitual&>(entity).GetRarity());
        break;
    case EEntityType::Projectile:
        if (entity.IsProjectileType(EProjectileType::Petal))
            return GetRaritySortRank(static_cast<const CPetal&>(entity).m_rarity);
        if (entity.IsProjectileType(EProjectileType::Trap))
            return GetRaritySortRank(static_cast<const CTrapProjectile&>(entity).GetRarity());
        if (entity.IsProjectileType(EProjectileType::Missile) &&
            entity.GetNetworkType() == server_dandelion_missile_entity_type)
            return GetRaritySortRank(static_cast<const CDandelionMissile&>(entity).GetRarity());
        break;
    default:
        break;
    }
    return 0.f;
}

cached_snapshot_entity BuildCachedSnapshotEntity(const CSnapshotService& service, const CEntity& entity)
{
    cached_snapshot_entity cached;
    cached.entity = &entity;
    cached.root_owner = FindRootOwnerEntity(&entity);
    cached.snap = service.BuildEntitySnap(entity, entity);
    cached.snap.flags &= ~static_cast<std::uint16_t>(ServerEntityFlag::Owner);
    cached.rarity_rank = SnapshotRarityRank(entity);
    cached.player_entity = entity.IsEntityType(EEntityType::Mob) &&
                           static_cast<const CMobBase&>(entity).GetMobType() == EMobType::PlayerFlower;

    if (entity.IsEntityType(EEntityType::Drop))
    {
        const auto* drop = static_cast<const CDrop*>(&entity);
        cached.is_private_drop = drop->GetOwnerId() != drop_owner_all;
        cached.drop_owner_id = drop->GetOwnerId();
    }

    if (entity.IsEntityType(EEntityType::Mob)) cached.pool = snapshot_pool::Mob;
    else if (entity.IsEntityType(EEntityType::Projectile) || entity.IsEntityType(EEntityType::StateZone))
        cached.pool = snapshot_pool::Projectile;

    if (entity.IsProjectileType(EProjectileType::Missile))
        cached.attached = static_cast<const CMissile&>(entity).IsAttachedToOwner();
    return cached;
}

const cached_snapshot_entity& CachedSnapshotEntity(const CSnapshotService& service, const CGameWorld& world,
                                                   const CEntity& entity, std::uint32_t snapshot_id)
{
    if (g_snapshot_frame_cache.snapshot_id != snapshot_id)
    {
        g_snapshot_frame_cache.snapshot_id = snapshot_id;
        for (auto& [world, cache] : g_snapshot_frame_cache.worlds)
        {
            (void)world;
            cache.entities.clear();
        }
    }

    snapshot_world_cache& world_cache = g_snapshot_frame_cache.worlds[&world];
    if (world_cache.entities.empty()) world_cache.entities.reserve(1024);
    auto [it, inserted] = world_cache.entities.try_emplace(&entity);
    if (inserted) it->second = BuildCachedSnapshotEntity(service, entity);
    return it->second;
}

bool CanSnapshotEntityForPlayer(const cached_snapshot_entity& cached, const CPlayer& player)
{
    return !cached.is_private_drop || cached.drop_owner_id == static_cast<int>(player.GetId());
}

visible_candidate MakeVisibleCandidate(const cached_snapshot_entity& cached, const cached_snapshot_entity& owner,
                                       float dist_sq)
{
    visible_candidate candidate;
    candidate.cached = &cached;
    candidate.dist_sq = dist_sq;
    candidate.rarity_rank = cached.rarity_rank;
    candidate.owner_related = cached.root_owner && cached.root_owner == owner.root_owner;
    candidate.player_entity = cached.player_entity;
    candidate.same_team = CheckTeam(cached.entity->m_team, owner.entity->m_team);
    candidate.attached = cached.attached;
    return candidate;
}

bool BetterSnapshotCandidate(const visible_candidate& lhs, const visible_candidate& rhs)
{
    if (lhs.owner_related != rhs.owner_related) return lhs.owner_related;
    if (lhs.player_entity != rhs.player_entity) return lhs.player_entity;
    if (lhs.same_team != rhs.same_team) return lhs.same_team;
    if (lhs.rarity_rank != rhs.rarity_rank) return lhs.rarity_rank > rhs.rarity_rank;
    if (lhs.attached != rhs.attached) return lhs.attached;
    return lhs.dist_sq < rhs.dist_sq;
}

void TrimSnapshotPool(std::vector<visible_candidate>& pool, size_t budget)
{
    if (budget == 0)
    {
        pool.clear();
        return;
    }
    if (pool.size() > budget)
    {
        std::nth_element(pool.begin(), pool.begin() + static_cast<std::ptrdiff_t>(budget), pool.end(),
                         BetterSnapshotCandidate);
        pool.resize(budget);
    }
}
} // namespace

bool CSnapshotService::BuildSnapshot(CPlayer& player, std::uint32_t snapshot_id, SBuildResult& out) const
{
    out = {};

    CEntity* owner = player.GetEntity();
    auto* owner_mob = owner && owner->IsEntityType(EEntityType::Mob) ? static_cast<CMobBase*>(owner) : nullptr;
    const SMobStats* owner_stats = owner_mob ? owner_mob->GetFinalStats() : nullptr;
    if (!owner || !owner_stats || !owner->GameWorld()) return false;

    float view_radius = owner_stats->horizon;
    float snap_view_radius = view_radius * game_config::network_snapshot_view_radius_multiplier;
    if (game_config::network_snapshot_query_radius_cap > 0.f)
        snap_view_radius = std::min(snap_view_radius, game_config::network_snapshot_query_radius_cap);

    const size_t configured_entity_budget =
        std::clamp(game_config::network_snapshot_entity_budget, size_t{ 1 }, CSnapshotService::entity_budget);
    const size_t configured_mob_budget =
        std::clamp(game_config::network_snapshot_mob_budget, size_t{ 0 }, CSnapshotService::entity_budget);
    const size_t configured_projectile_budget =
        std::clamp(game_config::network_snapshot_projectile_budget, size_t{ 0 }, CSnapshotService::entity_budget);
    const size_t configured_misc_budget =
        std::clamp(game_config::network_snapshot_misc_budget, size_t{ 0 }, CSnapshotService::entity_budget);
    const size_t configured_packet_budget =
        std::clamp(game_config::network_snapshot_packet_budget, size_t{ 128 }, CSnapshotService::packet_budget);
    const cached_snapshot_entity& owner_cached = CachedSnapshotEntity(*this, *owner->GameWorld(), *owner, snapshot_id);

    auto& mandatory_squad_entities = g_snapshot_scratch.mandatory_squad_entities;
    mandatory_squad_entities.clear();
    mandatory_squad_entities.reserve(game_config::open_controller_max_squad_size);
    if (auto* open_controller = dynamic_cast<COpenController*>(owner->GameWorld()->GetController()))
    {
        for (CPlayer* squad_member : open_controller->GetSquadPlayerList(*owner->GameWorld(), player))
        {
            CEntity* squad_entity = squad_member ? squad_member->GetEntity() : nullptr;
            if (!squad_entity || squad_entity == owner || squad_entity->GameWorld() != owner->GameWorld() ||
                !squad_entity->IsVisible())
                continue;
            if (std::find(mandatory_squad_entities.begin(), mandatory_squad_entities.end(), squad_entity) ==
                mandatory_squad_entities.end())
                mandatory_squad_entities.push_back(squad_entity);
        }
    }
    CEntity* mandatory_titan_entity = owner->GameWorld()->FindClosestEntityByEdge(
        owner->m_pos, game_config::titan_forge_range, [](const CEntity* entity) {
            if (!entity || !entity->IsEntityType(EEntityType::Mob)) return false;
            const auto* mob = static_cast<const CMobBase*>(entity);
            return mob->GetMobType() == EMobType::Titan && mob->IsVisible();
        });

    const size_t mandatory_entity_count = mandatory_squad_entities.size() + (mandatory_titan_entity ? 1 : 0) + 1;
    const size_t effective_entity_budget =
        std::min(CSnapshotService::entity_budget, std::max(configured_entity_budget, mandatory_entity_count));
    const size_t regular_entity_budget = effective_entity_budget - mandatory_entity_count;
    auto& mob_entities = g_snapshot_scratch.mobs;
    auto& projectile_entities = g_snapshot_scratch.projectiles;
    auto& misc_entities = g_snapshot_scratch.misc;
    mob_entities.clear();
    projectile_entities.clear();
    misc_entities.clear();
    mob_entities.reserve(std::min(configured_mob_budget, game_config::network_snapshot_mob_reserve_cap));
    projectile_entities.reserve(
        std::min(configured_projectile_budget, game_config::network_snapshot_projectile_reserve_cap));
    misc_entities.reserve(std::min(configured_misc_budget, game_config::network_snapshot_misc_reserve_cap));

    size_t visible_count = mandatory_entity_count;

    owner->GameWorld()->ForEachEntityInEdgeRange(owner->m_pos, snap_view_radius, [&](CEntity* entity) {
        if (!entity || !entity->IsVisible() || entity == owner) return;
        if (entity == mandatory_titan_entity) return;
        if (std::find(mandatory_squad_entities.begin(), mandatory_squad_entities.end(), entity) !=
            mandatory_squad_entities.end())
            return;
        const cached_snapshot_entity& cached = CachedSnapshotEntity(*this, *owner->GameWorld(), *entity, snapshot_id);
        if (!CanSnapshotEntityForPlayer(cached, player)) return;
        float dist_sq = DistanceSq(owner->m_pos, entity->m_pos);
        ++visible_count;

        visible_candidate candidate = MakeVisibleCandidate(cached, owner_cached, dist_sq);
        if (cached.pool == snapshot_pool::Mob) mob_entities.push_back(candidate);
        else if (cached.pool == snapshot_pool::Projectile) projectile_entities.push_back(candidate);
        else misc_entities.push_back(candidate);
    });

    TrimSnapshotPool(mob_entities, configured_mob_budget);
    TrimSnapshotPool(projectile_entities, configured_projectile_budget);
    TrimSnapshotPool(misc_entities, configured_misc_budget);

    auto& visible_entities = g_snapshot_scratch.visible;
    visible_entities.clear();
    visible_entities.reserve(std::min(regular_entity_budget,
                                      mob_entities.size() + projectile_entities.size() + misc_entities.size() + 1));
    visible_entities.insert(visible_entities.end(), mob_entities.begin(), mob_entities.end());
    visible_entities.insert(visible_entities.end(), projectile_entities.begin(), projectile_entities.end());
    visible_entities.insert(visible_entities.end(), misc_entities.begin(), misc_entities.end());
    if (visible_entities.size() > regular_entity_budget)
    {
        std::nth_element(visible_entities.begin(),
                         visible_entities.begin() + static_cast<std::ptrdiff_t>(regular_entity_budget),
                         visible_entities.end(), BetterSnapshotCandidate);
        visible_entities.resize(regular_entity_budget);
    }
    std::sort(visible_entities.begin(), visible_entities.end(), BetterSnapshotCandidate);

    ServerMessage msg;
    msg.type = ServerMessage::Type::Snapshot;
    msg.snapshot_id = snapshot_id;
    msg.owner_entity_id = static_cast<net_entity_id>(owner->m_id);
    msg.view_radius = view_radius;
    msg.entities.reserve(std::min(visible_entities.size() + mandatory_entity_count, effective_entity_budget));

    ServerEntitySnap owner_snap = owner_cached.snap;
    owner_snap.flags |= static_cast<std::uint16_t>(ServerEntityFlag::Owner);
    PopulateOwnerPetalRuntime(*owner, owner_snap);
    size_t packed_size = snapshot_header_size + ServerEntitySnap::GetPackedSize(owner_snap);
    msg.entities.push_back(std::move(owner_snap));
    sf::Vector2f snapshot_origin = msg.entities.front().pos;
    net_entity_id owner_id = msg.owner_entity_id.value_or(0);

    for (const CEntity* squad_entity : mandatory_squad_entities)
    {
        const cached_snapshot_entity& cached =
            CachedSnapshotEntity(*this, *owner->GameWorld(), *squad_entity, snapshot_id);
        ServerEntitySnap snap = cached.snap;
        packed_size += ServerEntitySnap::GetPackedSize(snap, snapshot_origin, owner_id, true);
        msg.entities.push_back(std::move(snap));
    }
    if (mandatory_titan_entity)
    {
        const cached_snapshot_entity& cached =
            CachedSnapshotEntity(*this, *owner->GameWorld(), *mandatory_titan_entity, snapshot_id);
        ServerEntitySnap snap = cached.snap;
        packed_size += ServerEntitySnap::GetPackedSize(snap, snapshot_origin, owner_id, true);
        msg.entities.push_back(std::move(snap));
    }

    const size_t packet_budget_for_owner =
        std::max(configured_packet_budget, packed_size + server_entity_compact_size);

    for (const visible_candidate& candidate : visible_entities)
    {
        const cached_snapshot_entity* cached = candidate.cached;
        const CEntity* entity = cached ? cached->entity : nullptr;
        if (!entity || !entity->IsVisible()) continue;
        if (!CanSnapshotEntityForPlayer(*cached, player)) continue;
        if (msg.entities.size() >= effective_entity_budget) break;

        ServerEntitySnap snap = cached->snap;
        size_t snap_size = ServerEntitySnap::GetPackedSize(snap, snapshot_origin, owner_id, true);
        if (packed_size + snap_size > packet_budget_for_owner) break;
        packed_size += snap_size;
        msg.entities.push_back(std::move(snap));
    }

    out.message = std::move(msg);
    out.visible_count = visible_count;
    out.packed_size = packed_size;
    return true;
}

ServerEntitySnap CSnapshotService::BuildEntitySnap(const CEntity& entity, const CEntity& owner) const
{
    ServerEntitySnap snap;
    snap.entity_id = static_cast<net_entity_id>(entity.m_id);
    snap.entity_type = entity.GetNetworkType();
    snap.team = static_cast<uint8_t>(std::clamp(entity.m_team, 0, static_cast<int>(UINT8_MAX)));
    snap.pos = entity.m_pos;
    snap.radius = entity.m_radius;
    snap.hp_percent = GetHealthPercent(entity);
    snap.name = GetEntityName(entity);
    snap.angle = PackAngle(entity.m_facing_angle);

    if (const auto* mob = dynamic_cast<const CMobBase*>(&entity)) snap.rarity = static_cast<uint8_t>(mob->GetRarity());
    if (const auto* player_flower = dynamic_cast<const CPlayerFlower*>(&entity))
    {
        snap.rarity = static_cast<uint8_t>(std::clamp(player_flower->m_level, 0, static_cast<int>(UINT8_MAX)));
        const auto& slots = player_flower->GetSlots();
        snap.primary_slots.reserve(slots.size());
        for (const CPetalSlot& slot : slots)
        {
            SOwnerPetalSlot primary_slot;
            if (slot.m_p_proto)
            {
                primary_slot.petal_type = static_cast<uint8_t>(slot.m_p_proto->m_type);
                primary_slot.rarity = static_cast<uint8_t>(slot.m_stored_rarity);
            }
            snap.primary_slots.push_back(primary_slot);
        }
    }
    if (const auto* drop = dynamic_cast<const CDrop*>(&entity)) snap.rarity = static_cast<uint8_t>(drop->GetRarity());
    if (const auto* petal = dynamic_cast<const CPetal*>(&entity)) snap.rarity = static_cast<uint8_t>(petal->m_rarity);
    if (const auto* ritual = dynamic_cast<const CBloodSacrificeRitual*>(&entity))
        snap.rarity = static_cast<uint8_t>(ritual->GetRarity());
    if (const auto* trap = dynamic_cast<const CTrapProjectile*>(&entity))
    {
        snap.rarity = static_cast<uint8_t>(trap->GetRarity());
    } else if (const auto* missile = dynamic_cast<const CDandelionMissile*>(&entity))
    {
        snap.rarity = static_cast<uint8_t>(missile->GetRarity());
        if (missile->IsAttachedToOwner()) snap.flags |= static_cast<uint16_t>(ServerEntityFlag::Attached);
    } else if (const auto* missile = dynamic_cast<const CMissile*>(&entity))
    {
        if (missile->IsAttachedToOwner()) snap.flags |= static_cast<uint16_t>(ServerEntityFlag::Attached);
    }

    if (&entity == &owner) snap.flags |= static_cast<uint16_t>(ServerEntityFlag::Owner);
    if (entity.IsDead()) snap.flags |= static_cast<uint16_t>(ServerEntityFlag::Dead);

    if (const auto* attackable = dynamic_cast<const IAttackableMob*>(&entity))
    {
        if (attackable->IsAttacking()) snap.flags |= static_cast<uint16_t>(ServerEntityFlag::Attacking);
        if (attackable->IsDefending()) snap.flags |= static_cast<uint16_t>(ServerEntityFlag::Defending);
    }
    if (const auto* skill_caster = dynamic_cast<const ISkillCasterMob*>(&entity))
        snap.flags |= PackServerEntitySkillWindup(skill_caster->GetWindupSkillId());

    if (const auto* flower = dynamic_cast<const CFlower*>(&entity))
    {
        const SFlowerStats* stats = flower->GetFinalStats();
        if (stats && stats->max_health > 0.f)
            snap.shield_percent = std::clamp(flower->GetShield() / stats->max_health, 0.f, 1.f);
        if (FlowerHasAvailablePetal(*flower, EPetalType::Antennae))
            snap.flags |= static_cast<uint16_t>(ServerEntityFlag::Antennae);
    }
    if (const auto* mob = dynamic_cast<const CMobBase*>(&entity))
    {
        const auto& states = mob->GetStates();
        snap.states.reserve(states.size());
        for (const auto& state : states)
        {
            if (!state || !IsKnownStateType(state->GetType())) continue;
            snap.states.push_back(
                { state->GetType(), static_cast<uint8_t>(std::clamp(static_cast<int>(state->m_rarity), 0,
                                                                   static_cast<int>(UINT8_MAX))) });
        }

        auto& mutable_mob = *const_cast<CMobBase*>(mob);
        if (dynamic_cast<CSummonedMeleeController*>(mutable_mob.GetController()))
            snap.flags |= static_cast<uint16_t>(ServerEntityFlag::Summoned);
        if (const auto* controller = dynamic_cast<const CLeafcutterSoldierController*>(mob->GetController());
            controller && controller->IsCarryingLeafPiece(mob))
            snap.flags |= static_cast<uint16_t>(ServerEntityFlag::CarryingLeafPiece);
    }
    return snap;
}
