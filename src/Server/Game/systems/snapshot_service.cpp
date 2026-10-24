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
#include "../chain.h"
#include "../gamecontrollers/eou_slot_proxy.h"
#include "../extensions/extension_services.h"
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
    if (const auto* chain = dynamic_cast<const CChainEntity*>(&entity)) return chain->SnapshotName();
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
        if (stats && stats->max_health > 0.f) return entity.Health() / stats->max_health;
    }
    return entity.Health();
}

bool HasAvailablePetal(const CFlower& flower, EPetalType type)
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
        // Proxy-filled slots carry their copies in the primary pass and have
        // no runtime petal state to populate here.
        if (!slot.m_p_proto) continue;
        SOwnerPetalSlot& slot_snap = snap.primary_slots[slot_index];
        EPetalType runtime_type = slot.m_runtime_type;
        for (const CPetal* petal : slot.m_p_petals)
        {
            if (!petal) continue;
            runtime_type = petal->GetPetalType();
            break;
        }
        if (runtime_type == EPetalType::Compass)
            slot_snap.visual_type = SOwnerPetalSlot::EVisualType::Angle;
        else if (runtime_type == EPetalType::Tomato)
            slot_snap.visual_type = SOwnerPetalSlot::EVisualType::Size;
        // Lightning's contact-hit accumulator is internal combat state,
        // not a player-facing resource/charge count.
        else if (runtime_type == EPetalType::Battery || runtime_type == ToPetalType(EModPetalType::Linker))
            slot_snap.visual_type = SOwnerPetalSlot::EVisualType::Counter;
        else
            slot_snap.visual_type = SOwnerPetalSlot::EVisualType::None;

        const float tomato_max_radius_multiplier =
            std::max(1.f, game_config::default_tomato_max_radius_multiplier);
        const uint16_t tomato_spawn_size = static_cast<uint16_t>(std::round(
            std::clamp(1.f / tomato_max_radius_multiplier, 0.f, 1.f) * static_cast<float>(UINT16_MAX)));
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
                copy_snap.progress = PackPercent(max_health > 0.f ? petal->Health() / max_health : 0.f);
                if (slot_snap.visual_type == SOwnerPetalSlot::EVisualType::Angle)
                {
                    copy_snap.visual_value = static_cast<uint16_t>(PackAngle(petal->FacingAngle()));
                } else if (slot_snap.visual_type == SOwnerPetalSlot::EVisualType::Size)
                {
                    const float base_radius =
                        petal->m_growth_base_radius > game_config::entity_collision_epsilon
                            ? petal->m_growth_base_radius
                            : petal->m_final_petal_stats.radius;
                    const float max_radius = base_radius * tomato_max_radius_multiplier;
                    const float size_ratio = max_radius > game_config::entity_collision_epsilon
                                                 ? petal->Radius() / max_radius
                                                 : 1.f;
                    copy_snap.visual_value = static_cast<uint16_t>(std::round(
                        std::clamp(size_ratio, 0.f, 1.f) * static_cast<float>(UINT16_MAX)));
                } else if (slot_snap.visual_type == SOwnerPetalSlot::EVisualType::Counter)
                {
                    int count = 0;
                    if (!slot.m_banned && !petal->IsMarkedForDes() && petal->Health() > 0.f)
                    {
                        if (const auto* battery = dynamic_cast<const CBatteryPetal*>(petal))
                            count = battery->m_charges_remaining;
                        else if (const auto* linker = dynamic_cast<const CLinkerPetal*>(petal))
                            count = linker->m_charges_remaining;
                    }
                    copy_snap.visual_value = static_cast<uint16_t>(std::clamp(count, 0, static_cast<int>(UINT16_MAX)));
                }
            } else
            {
                copy_snap.state = SOwnerPetalSlot::ECopyState::Loading;
                copy_snap.progress = PackPercent(slot.GetLoadProgress(static_cast<int>(copy_index)));
                if (slot_snap.visual_type == SOwnerPetalSlot::EVisualType::Size)
                    copy_snap.visual_value = tomato_spawn_size;
            }
            slot_snap.copies.push_back(copy_snap);
        }
    }
}

void PopulateOwnerHallucinations(const CEntity& owner, ServerEntitySnap& snap)
{
    const auto* mob = dynamic_cast<const CMobBase*>(&owner);
    if (!mob) return;

    snap.hallucinations.clear();
    for (const auto& state : mob->GetStates())
    {
        const auto* hallucinogenic = dynamic_cast<const CHallucinogenicState*>(state.get());
        if (!hallucinogenic) continue;

        CHallucinogenicState::parameter_array parameters{};
        if (hallucinogenic->ComputeEffectiveParameters(*mob, parameters))
            snap.hallucinations.push_back(parameters);
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
    std::string drop_owner_account;
    std::string drop_owner_progress_account;
    snapshot_pool pool = snapshot_pool::Misc;
    bool is_private_drop = false;
    bool player_entity = false;
    bool attached = false;
};

struct snapshot_world_cache
{
    std::uint32_t world_id = 0;
    std::uint64_t world_instance_generation = 0;
    bool used_in_frame = false;
    std::unordered_map<const CEntity*, cached_snapshot_entity> entities;
};

struct snapshot_frame_cache
{
    std::uint32_t snapshot_id = std::numeric_limits<std::uint32_t>::max();
    std::vector<snapshot_world_cache> worlds;
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
        cached.drop_owner_account = drop->GetOwnerAccount();
        cached.drop_owner_progress_account = drop->GetOwnerProgressAccount();
    }

    if (entity.IsEntityType(EEntityType::Mob)) cached.pool = snapshot_pool::Mob;
    else if (entity.IsEntityType(EEntityType::Projectile) || entity.IsEntityType(EEntityType::StateZone))
        cached.pool = snapshot_pool::Projectile;

    if (entity.IsProjectileType(EProjectileType::Missile))
        cached.attached = static_cast<const CMissile&>(entity).IsAttachedToOwner();
    return cached;
}

const cached_snapshot_entity& CachedSnapshotEntity(snapshot_frame_cache& frame, const CSnapshotService& service,
                                                   const CGameWorld& world, const CEntity& entity,
                                                   std::uint32_t snapshot_id)
{
    if (frame.snapshot_id != snapshot_id)
    {
        frame.snapshot_id = snapshot_id;
        for (snapshot_world_cache& cache : frame.worlds)
        {
            cache.used_in_frame = false;
            cache.entities.clear();
        }
    }

    auto cache_it = std::find_if(frame.worlds.begin(), frame.worlds.end(),
                                 [&](const snapshot_world_cache& cache) {
                                     return cache.world_id == world.GetId() &&
                                            cache.world_instance_generation == world.GetInstanceGeneration();
                                 });
    if (cache_it == frame.worlds.end())
    {
        cache_it = std::find_if(frame.worlds.begin(), frame.worlds.end(),
                                [](const snapshot_world_cache& cache) { return !cache.used_in_frame; });
        if (cache_it == frame.worlds.end())
        {
            frame.worlds.emplace_back();
            cache_it = std::prev(frame.worlds.end());
        }
        cache_it->entities.clear();
        cache_it->world_id = world.GetId();
        cache_it->world_instance_generation = world.GetInstanceGeneration();
    }
    cache_it->used_in_frame = true;
    snapshot_world_cache& world_cache = *cache_it;
    if (world_cache.entities.empty()) world_cache.entities.reserve(1024);
    auto [it, inserted] = world_cache.entities.try_emplace(&entity);
    if (inserted || it->second.snap.entity_id != static_cast<net_entity_id>(entity.Id()) ||
        it->second.snap.generation != entity.Generation())
        it->second = BuildCachedSnapshotEntity(service, entity);
    return it->second;
}

bool CanSnapshotEntityForPlayer(const cached_snapshot_entity& cached, const CPlayer& player)
{
    return !cached.is_private_drop || (player.IsAuthenticated() && !cached.drop_owner_account.empty() &&
        cached.drop_owner_account == player.GetAccountName() &&
        (cached.drop_owner_progress_account.empty() ? !player.HasIsolatedProgress() :
            cached.drop_owner_progress_account == player.GetProgressAccountName()));
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
    candidate.same_team = CheckTeam(cached.entity->Team(), owner.entity->Team());
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
    if (lhs.dist_sq != rhs.dist_sq) return lhs.dist_sq < rhs.dist_sq;

    // Spatial-grid iteration order is not stable. Use the entity identity as a final tie-breaker so
    // an nth_element cutoff does not randomly replace equally ranked entities between snapshots.
    const CEntity* lhs_entity = lhs.cached ? lhs.cached->entity : nullptr;
    const CEntity* rhs_entity = rhs.cached ? rhs.cached->entity : nullptr;
    if (lhs_entity && rhs_entity)
    {
        if (lhs_entity->Id() != rhs_entity->Id()) return lhs_entity->Id() < rhs_entity->Id();
        if (lhs_entity->Generation() != rhs_entity->Generation())
            return lhs_entity->Generation() < rhs_entity->Generation();
    }
    return lhs_entity != nullptr;
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

struct CSnapshotService::CBuildBatch::SImpl
{
    snapshot_frame_cache frame;
    snapshot_scratch scratch;
};

CSnapshotService::CBuildBatch::CBuildBatch() : m_impl(std::make_unique<SImpl>()) {}
CSnapshotService::CBuildBatch::~CBuildBatch() = default;

bool CSnapshotService::BuildSnapshot(CPlayer& player, std::uint32_t snapshot_id, SBuildResult& out) const
{
    CBuildBatch batch;
    return BuildSnapshot(player, snapshot_id, out, batch);
}

bool CSnapshotService::BuildSnapshot(CPlayer& player, std::uint32_t snapshot_id, SBuildResult& out,
                                     CBuildBatch& batch) const
{
    out = {};

    CEntity* owner = player.GetEntity();
    // A dead player flower remains visible for death/revival presentation.
    // Removal, not IsDead(), is the boundary for publishing its owner snap.
    if (!owner || owner->IsMarkedForDes() || !owner->GameWorld()) return false;
    auto* owner_mob = owner->IsEntityType(EEntityType::Mob) ? static_cast<CMobBase*>(owner) : nullptr;
    const SMobStats* owner_stats = owner_mob ? owner_mob->GetFinalStats() : nullptr;
    if (!owner_stats) return false;
    auto& frame = batch.m_impl->frame;
    auto& scratch = batch.m_impl->scratch;

    float view_radius = owner_stats->horizon;
    float snap_view_radius = view_radius * game_config::network_snapshot_view_radius_multiplier;

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
    const cached_snapshot_entity& owner_cached = CachedSnapshotEntity(frame, *this, *owner->GameWorld(), *owner, snapshot_id);

    auto& mandatory_squad_entities = scratch.mandatory_squad_entities;
    mandatory_squad_entities.clear();
    mandatory_squad_entities.reserve(game_config::open_controller_max_squad_size);
    if (const auto* squads = owner->GameWorld()->Extensions().Find<ISquadView>())
    {
        for (CPlayer* squad_member : squads->Members(player))
        {
            CEntity* squad_entity = squad_member ? squad_member->GetEntity() : nullptr;
            if (!squad_entity || squad_entity == owner || squad_entity->GameWorld() != owner->GameWorld() ||
                squad_entity->IsMarkedForDes() || !squad_entity->IsVisible())
                continue;
            if (std::find(mandatory_squad_entities.begin(), mandatory_squad_entities.end(), squad_entity) ==
                mandatory_squad_entities.end())
                mandatory_squad_entities.push_back(squad_entity);
        }
    }
    CEntity* mandatory_titan_entity = owner->GameWorld()->FindClosestEntityByEdge(
        owner->Pos(), game_config::titan_forge_range, [&](const CEntity* entity) {
            if (!entity || entity == owner || entity->IsMarkedForDes() || !entity->IsEntityType(EEntityType::Mob) ||
                std::find(mandatory_squad_entities.begin(), mandatory_squad_entities.end(), entity) !=
                    mandatory_squad_entities.end()) return false;
            const auto* mob = static_cast<const CMobBase*>(entity);
            return mob->GetMobType() == EMobType::Titan && mob->IsVisible();
        });

    const size_t mandatory_fixed_count = 1 + (mandatory_titan_entity ? 1 : 0);
    const size_t max_mandatory_squad = CSnapshotService::entity_budget - mandatory_fixed_count;
    if (mandatory_squad_entities.size() > max_mandatory_squad)
        mandatory_squad_entities.resize(max_mandatory_squad);

    const size_t mandatory_entity_count = mandatory_squad_entities.size() + mandatory_fixed_count;
    const size_t effective_entity_budget =
        std::min(CSnapshotService::entity_budget, std::max(configured_entity_budget, mandatory_entity_count));
    const size_t regular_entity_budget =
        effective_entity_budget > mandatory_entity_count ? effective_entity_budget - mandatory_entity_count : 0;
    auto& mob_entities = scratch.mobs;
    auto& projectile_entities = scratch.projectiles;
    auto& misc_entities = scratch.misc;
    mob_entities.clear();
    projectile_entities.clear();
    misc_entities.clear();
    mob_entities.reserve(std::min(configured_mob_budget, game_config::network_snapshot_mob_reserve_cap));
    projectile_entities.reserve(
        std::min(configured_projectile_budget, game_config::network_snapshot_projectile_reserve_cap));
    misc_entities.reserve(std::min(configured_misc_budget, game_config::network_snapshot_misc_reserve_cap));

    size_t visible_count = mandatory_entity_count;

    owner->GameWorld()->ForEachEntityInEdgeRange(owner->Pos(), snap_view_radius, [&](CEntity* entity) {
        if (!entity || entity->IsMarkedForDes() || !entity->IsVisible() || entity == owner) return;
        if (entity == mandatory_titan_entity) return;
        if (std::find(mandatory_squad_entities.begin(), mandatory_squad_entities.end(), entity) !=
            mandatory_squad_entities.end())
            return;
        const cached_snapshot_entity& cached = CachedSnapshotEntity(frame, *this, *owner->GameWorld(), *entity, snapshot_id);
        if (!CanSnapshotEntityForPlayer(cached, player)) return;
        float dist_sq = DistanceSq(owner->Pos(), entity->Pos());
        ++visible_count;

        visible_candidate candidate = MakeVisibleCandidate(cached, owner_cached, dist_sq);
        if (cached.pool == snapshot_pool::Mob) mob_entities.push_back(candidate);
        else if (cached.pool == snapshot_pool::Projectile) projectile_entities.push_back(candidate);
        else misc_entities.push_back(candidate);
    });

    // One extra cull pass after the default collection: the world controller
    // may hide entities from this player (fog of war, ...) before the budget
    // trimming below, so hidden entities never consume snapshot budget.
    if (IGameController* controller = owner->GameWorld()->GetController())
    {
        const auto hidden = [&](const visible_candidate& candidate) {
            const CEntity* entity = candidate.cached ? candidate.cached->entity : nullptr;
            return !entity || !controller->SnapshotEntityVisibleToPlayer(*owner->GameWorld(), player, *entity);
        };
        mob_entities.erase(std::remove_if(mob_entities.begin(), mob_entities.end(), hidden), mob_entities.end());
        projectile_entities.erase(std::remove_if(projectile_entities.begin(), projectile_entities.end(), hidden),
                                  projectile_entities.end());
        misc_entities.erase(std::remove_if(misc_entities.begin(), misc_entities.end(), hidden), misc_entities.end());
    }
    visible_count = mandatory_entity_count + mob_entities.size() + projectile_entities.size() + misc_entities.size();

    TrimSnapshotPool(mob_entities, configured_mob_budget);
    TrimSnapshotPool(projectile_entities, configured_projectile_budget);
    TrimSnapshotPool(misc_entities, configured_misc_budget);

    auto& visible_entities = scratch.visible;
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
    msg.owner_entity_id = static_cast<net_entity_id>(owner->Id());
    msg.view_radius = view_radius;
    msg.entities.reserve(std::min(visible_entities.size() + mandatory_entity_count, effective_entity_budget));

    ServerEntitySnap owner_snap = owner_cached.snap;
    owner_snap.flags |= static_cast<std::uint16_t>(ServerEntityFlag::Owner);
    PopulateOwnerPetalRuntime(*owner, owner_snap);
    PopulateOwnerHallucinations(*owner, owner_snap);
    size_t packed_size = snapshot_header_size + ServerEntitySnap::GetPackedSize(owner_snap);
    msg.entities.push_back(std::move(owner_snap));
    florrbt::Vector2f snapshot_origin = msg.entities.front().pos;
    net_entity_id owner_id = msg.owner_entity_id.value_or(0);

    for (const CEntity* squad_entity : mandatory_squad_entities)
    {
        const cached_snapshot_entity& cached =
            CachedSnapshotEntity(frame, *this, *owner->GameWorld(), *squad_entity, snapshot_id);
        ServerEntitySnap snap = cached.snap;
        packed_size += ServerEntitySnap::GetPackedSize(snap, snapshot_origin, owner_id, true);
        msg.entities.push_back(std::move(snap));
    }
    if (mandatory_titan_entity)
    {
        const cached_snapshot_entity& cached =
            CachedSnapshotEntity(frame, *this, *owner->GameWorld(), *mandatory_titan_entity, snapshot_id);
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
    // Weather intensities and wind ride along with every snapshot.
    out.packed_size = packed_size + server_environment_weather_count + server_environment_wind_size;
    return true;
}

std::vector<SOwnerPetalSlot> CSnapshotService::BuildPrimarySlots(const CFlower& flower)
{
    std::vector<SOwnerPetalSlot> result;
    const auto& slots = flower.GetSlots();
    const IGameController* controller = flower.GameWorld() ? flower.GameWorld()->GetController() : nullptr;
    const CEouSlotProxy* proxy = controller ? controller->SlotProxy(flower.Team()) : nullptr;
    int proxy_index = 0;
    result.reserve(slots.size());
    for (const CPetalSlot& slot : slots)
    {
        SOwnerPetalSlot output;
        if (slot.m_p_proto)
        {
            const EPetalType visual = slot.m_p_proto->m_type == ToPetalType(EModPetalType::EouPheromone)
                                          ? EPetalType::Honey : slot.m_p_proto->m_type;
            output.petal_type = static_cast<uint8_t>(visual);
            output.rarity = static_cast<uint8_t>(slot.m_stored_rarity);
        }
        else if (proxy && proxy_index < proxy->GetSlotCount())
            output = proxy->MakeOwnerSlot(proxy_index++);
        result.push_back(std::move(output));
    }
    return result;
}

ServerEntitySnap CSnapshotService::BuildEntitySnap(const CEntity& entity, const CEntity& owner) const
{
    ServerEntitySnap snap;
    snap.entity_id = static_cast<net_entity_id>(entity.Id());
    snap.generation = entity.Generation();
    snap.entity_type = entity.GetNetworkType();
    // Mod mobs may disguise themselves as a vanilla mob type on the wire so
    // clients render the vanilla sprite without client-side mod support.
    if (const auto* mob = dynamic_cast<const CMobBase*>(&entity))
    {
        if (const CMobPrototype* proto = FindMobPrototype(mob->GetMobType());
            proto && proto->m_visual_proxy_type != EMobType::None)
            snap.entity_type = static_cast<std::uint8_t>(proto->m_visual_proxy_type);
    }
    snap.team = static_cast<uint8_t>(std::clamp(entity.Team(), 0, static_cast<int>(UINT8_MAX)));
    snap.pos = entity.Pos();
    snap.radius = entity.Radius();
    snap.hp_percent = GetHealthPercent(entity);
    if (const auto* body = dynamic_cast<const IMultiSegmentBodyMob*>(&entity))
        if (const CMobBase* root = ResolveMultiSegmentRoot(entity.GameWorld(), body->RootHandle());
            root && SharesMultiSegmentRootHealth(*root))
            snap.hp_percent = GetHealthPercent(*root);
    snap.name = GetEntityName(entity);
    snap.angle = PackAngle(entity.FacingAngle());

    if (const auto* mob = dynamic_cast<const CMobBase*>(&entity)) snap.rarity = static_cast<uint8_t>(mob->GetRarity());
    if (const auto* body = dynamic_cast<const IMultiSegmentBodyMob*>(&entity))
    {
        std::uint8_t variant = static_cast<std::uint8_t>(std::clamp(body->SegmentIndex(), 0, 7));
        if (const CMobBase* root = ResolveMultiSegmentRoot(entity.GameWorld(), body->RootHandle()))
        {
            if (const CMobPrototype* prototype = FindMobPrototype(root->GetMobType());
                prototype && prototype->m_segment_visual_variant_factory)
            {
                const int body_count = prototype->ResolveSegmentCount(root->GetRarity());
                variant = static_cast<std::uint8_t>(std::clamp<int>(
                    prototype->m_segment_visual_variant_factory(body->SegmentIndex(), body_count), 0, 7));
            }
        }
        snap.flags |= PackServerEntityVisualVariant(variant);
    }
    if (const auto* player_flower = dynamic_cast<const CPlayerFlower*>(&entity))
    {
        snap.rarity = static_cast<uint8_t>(std::clamp(player_flower->m_level, 0, static_cast<int>(UINT8_MAX)));
        snap.primary_slots = BuildPrimarySlots(*player_flower);
    }
    if (const auto* drop = dynamic_cast<const CDrop*>(&entity))
    {
        snap.rarity = static_cast<uint8_t>(drop->GetRarity());
        // Drops have no gameplay team. Reuse that already-present wire byte as
        // their petal subtype so the id is no longer encoded into entity_type.
        snap.team = static_cast<uint8_t>(drop->GetType());
    }
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
        if (&entity == &owner)
        {
            snap.mana = std::max(0.f, flower->Mana());
            snap.max_mana = std::max(0.f, flower->GetMaxMana());
        }
        if (HasAvailablePetal(*flower, EPetalType::Antennae))
            snap.flags |= static_cast<uint16_t>(ServerEntityFlag::Antennae);
    }
    if (const auto* drop = dynamic_cast<const CDrop*>(&entity))
    {
        // Stacked drops carry their count so the client can render the same
        // "xN" badge the inventory cards use.
        if (drop->GetStackNum() > 1)
        {
            SEntityStateSnap state_snap{ EStateType::DropStack, 0 };
            state_snap.value =
                static_cast<uint8_t>(std::min<uint32_t>(drop->GetStackNum(), UINT8_MAX));
            snap.states.push_back(state_snap);
        }
    }
    if (const auto* mob = dynamic_cast<const CMobBase*>(&entity))
    {
        if (mob->IsInShortcut()) snap.flags |= static_cast<uint16_t>(ServerEntityFlag::Shortcut);
        const auto& states = mob->GetStates();
        snap.states.reserve(states.size());
        for (const auto& state : states)
        {
            if (!state || !IsKnownStateType(state->GetType())) continue;
            SEntityStateSnap state_snap{
                state->GetType(),
                static_cast<uint8_t>(std::clamp(static_cast<int>(state->m_rarity), 0,
                                                static_cast<int>(UINT8_MAX))),
            };
            if (const auto* ban_slot = dynamic_cast<const CBanSlotState*>(state.get()))
                state_snap.value = static_cast<uint8_t>(
                    std::clamp(ban_slot->GetSlotIndex(), 0, static_cast<int>(UINT8_MAX)));
            else if (const auto* hallucinogenic = dynamic_cast<const CHallucinogenicState*>(state.get()))
                state_snap.value = static_cast<uint8_t>(
                    std::clamp(hallucinogenic->GetParameterCount(), 0, static_cast<int>(UINT8_MAX)));
            snap.states.push_back(state_snap);
        }

        if (dynamic_cast<const CSummonedMeleeController*>(mob->GetController()))
            snap.flags |= static_cast<uint16_t>(ServerEntityFlag::Summoned);
        if (const auto* pickup = mob->GetPickupController();
            pickup && pickup->GetCarried(mob))
            snap.flags |= static_cast<uint16_t>(ServerEntityFlag::CarryingLeafPiece);
    }
    return snap;
}
