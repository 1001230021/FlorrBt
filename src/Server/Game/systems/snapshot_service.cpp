#include "snapshot_service.h"
#include "../controllers/melee_controller.h"
#include "../entities/blood_sacrifice_ritual.h"
#include "../entities/drop.h"
#include "../entities/flower.h"
#include "../entities/mob.h"
#include "../entities/petals/petal.h"
#include "../entities/portal.h"
#include "../entities/projectile.h"
#include "../gameworld.h"
#include "../player.h"
#include "../state_zone.h"
#include "../states/states.h"
#include "../../../Shared/game_config.h"
#include "../../../Shared/tools.h"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace
{
uint8_t GetEntityType(const CEntity& entity)
{
    if (dynamic_cast<const CPortal*>(&entity)) return server_portal_entity_type;
    if (dynamic_cast<const CDandelionMissile*>(&entity)) return server_dandelion_missile_entity_type;
    if (dynamic_cast<const CPollenProjectile*>(&entity)) return server_pollen_entity_type;
    if (dynamic_cast<const CSpiderWebZone*>(&entity)) return server_spider_web_entity_type;
    if (dynamic_cast<const CMissile*>(&entity)) return server_missile_entity_type;
    if (dynamic_cast<const CBloodSacrificeRitual*>(&entity)) return server_blood_sacrifice_entity_type;
    if (const auto* mob = dynamic_cast<const CMobBase*>(&entity)) return static_cast<uint8_t>(mob->m_mob_type);
    if (const auto* drop = dynamic_cast<const CDrop*>(&entity))
        return server_drop_entity_type_offset + static_cast<uint8_t>(drop->GetType());
    if (const auto* petal = dynamic_cast<const CPetal*>(&entity))
        return server_petal_entity_type_offset + static_cast<uint8_t>(petal->m_type);
    return 0;
}

std::string GetEntityName(const CEntity& entity)
{
    if (const auto* player_flower = dynamic_cast<const CPlayerFlower*>(&entity)) return player_flower->m_name;
    return "";
}

float GetHealthPercent(const CEntity& entity)
{
    if (const auto* ritual = dynamic_cast<const CBloodSacrificeRitual*>(&entity))
        return ritual->EffectProgress();
    if (dynamic_cast<const CPortal*>(&entity)) return 1.f;
    if (const auto* mob = dynamic_cast<const CMobBase*>(&entity))
    {
        const SMobStats* stats = mob->GetFinalStats();
        if (stats && stats->max_health > 0.f) return entity.m_health / stats->max_health;
    }
    return entity.m_health;
}

float GetPrimordialDefaultMobRadius()
{
    float base_radius = std::max({game_config::mob_beetle_radius, game_config::mob_normal_ladybug_radius,
                                  game_config::default_flower_radius, game_config::mob_player_flower_radius,
                                  game_config::mob_summoned_beetle_radius, game_config::mob_soldier_ant_radius,
                                  game_config::mob_soldier_fire_ant_radius, game_config::mob_soldier_termite_radius,
                                  game_config::mob_summoned_soldier_ant_radius, game_config::mob_bee_radius,
                                  game_config::mob_hornet_radius, game_config::mob_bumblebee_radius,
                                  game_config::mob_dandelion_radius,
                                  game_config::mob_ant_egg_radius,
                                  game_config::mob_rock_radius, game_config::mob_baby_ant_radius,
                                  game_config::mob_worker_ant_radius, game_config::mob_queen_ant_radius,
                                  game_config::mob_leaf_piece_radius *
                                      std::max(game_config::mob_leaf_piece_radius_scale_min,
                                               game_config::mob_leaf_piece_radius_scale_max),
                                  game_config::mob_soldier_ant_radius *
                                      game_config::mob_termite_overmind_radius_multiplier,
                                  game_config::mob_ant_hole_radius, game_config::mob_spider_radius,
                                  game_config::mob_soldier_ant_radius});
    return base_radius * game_config::MobRadiusScaleForLevel(GetLevel(ERarity::Primordial));
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

bool CanSnapshotEntityForPlayer(const CEntity& entity, const CPlayer& player)
{
    const auto* drop = dynamic_cast<const CDrop*>(&entity);
    if (!drop) return true;

    const int owner_id = drop->GetOwnerId();
    return owner_id == drop_owner_all || owner_id == static_cast<int>(player.GetId());
}

struct visible_candidate
{
    const CEntity* entity = nullptr;
    float dist_sq = 0.f;
    float rarity_rank = 0.f;
    bool owner_related = false;
    bool player_entity = false;
    bool same_team = false;
    bool attached = false;
};

float SnapshotRarityRank(const CEntity& entity)
{
    if (const auto* ritual = dynamic_cast<const CBloodSacrificeRitual*>(&entity))
        return GetRaritySortRank(ritual->GetRarity());
    if (const auto* mob = dynamic_cast<const CMobBase*>(&entity)) return GetRaritySortRank(mob->GetRarity());
    if (const auto* drop = dynamic_cast<const CDrop*>(&entity)) return GetRaritySortRank(drop->GetRarity());
    if (const auto* petal = dynamic_cast<const CPetal*>(&entity)) return GetRaritySortRank(petal->m_rarity);
    if (const auto* dandelion = dynamic_cast<const CDandelionMissile*>(&entity))
        return GetRaritySortRank(dandelion->GetRarity());
    return 0.f;
}

bool IsSnapshotProjectileLike(const CEntity& entity)
{
    return dynamic_cast<const CProjectile*>(&entity) || dynamic_cast<const CStateZone*>(&entity);
}

visible_candidate MakeVisibleCandidate(const CEntity& entity, const CEntity& owner, float dist_sq)
{
    const auto* missile = dynamic_cast<const CMissile*>(&entity);
    visible_candidate candidate;
    candidate.entity = &entity;
    candidate.dist_sq = dist_sq;
    candidate.rarity_rank = SnapshotRarityRank(entity);
    candidate.owner_related = ShareRootOwner(&entity, &owner);
    candidate.player_entity = dynamic_cast<const CPlayerFlower*>(&entity) != nullptr;
    candidate.same_team = CheckTeam(entity.m_team, owner.m_team);
    candidate.attached = missile && missile->IsAttachedToOwner();
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
    std::sort(pool.begin(), pool.end(), BetterSnapshotCandidate);
}
}

bool CSnapshotService::BuildSnapshot(CPlayer& player, std::uint32_t snapshot_id, SBuildResult& out) const
{
    out = {};

    CEntity* owner = player.GetEntity();
    auto* owner_mob = dynamic_cast<CMobBase*>(owner);
    const SMobStats* owner_stats = owner_mob ? owner_mob->GetFinalStats() : nullptr;
    if (!owner || !owner_stats || !owner->GameWorld()) return false;

    float view_radius = owner_stats->horizon;
    float snap_view_radius = view_radius * 2.f;
    if (game_config::network_snapshot_query_radius_cap > 0.f)
        snap_view_radius = std::min(snap_view_radius, game_config::network_snapshot_query_radius_cap);

    const size_t configured_entity_budget =
        std::clamp(game_config::network_snapshot_entity_budget, size_t{1}, CSnapshotService::entity_budget);
    const size_t configured_mob_budget =
        std::clamp(game_config::network_snapshot_mob_budget, size_t{0}, CSnapshotService::entity_budget);
    const size_t configured_projectile_budget =
        std::clamp(game_config::network_snapshot_projectile_budget, size_t{0}, CSnapshotService::entity_budget);
    const size_t configured_misc_budget =
        std::clamp(game_config::network_snapshot_misc_budget, size_t{0}, CSnapshotService::entity_budget);
    const size_t configured_packet_budget =
        std::clamp(game_config::network_snapshot_packet_budget, size_t{128}, CSnapshotService::packet_budget);
    float snap_radius = snap_view_radius + GetPrimordialDefaultMobRadius();
    std::vector<visible_candidate> mob_entities;
    std::vector<visible_candidate> projectile_entities;
    std::vector<visible_candidate> misc_entities;
    mob_entities.reserve(std::min<size_t>(configured_mob_budget, 128));
    projectile_entities.reserve(std::min<size_t>(configured_projectile_budget, 256));
    misc_entities.reserve(std::min<size_t>(configured_misc_budget, 64));

    size_t visible_count = 1;

    owner->GameWorld()->GetSpatialGrid().ForEachInRange(owner->m_pos, snap_radius, [&](CEntity* entity)
    {
        if (!entity || !entity->IsVisible() || entity == owner) return;
        if (!CanSnapshotEntityForPlayer(*entity, player)) return;
        float radius = snap_view_radius + std::max(0.f, entity->m_radius);
        float dist_sq = DistanceSq(owner->m_pos, entity->m_pos);
        if (dist_sq > radius * radius) return;
        ++visible_count;

        visible_candidate candidate = MakeVisibleCandidate(*entity, *owner, dist_sq);
        if (dynamic_cast<const CMobBase*>(entity))
            mob_entities.push_back(candidate);
        else if (IsSnapshotProjectileLike(*entity))
            projectile_entities.push_back(candidate);
        else
            misc_entities.push_back(candidate);
    });

    TrimSnapshotPool(mob_entities, configured_mob_budget);
    TrimSnapshotPool(projectile_entities, configured_projectile_budget);
    TrimSnapshotPool(misc_entities, configured_misc_budget);

    std::vector<visible_candidate> visible_entities;
    visible_entities.reserve(std::min(configured_entity_budget,
                                      mob_entities.size() + projectile_entities.size() + misc_entities.size() + 1));
    visible_entities.insert(visible_entities.end(), mob_entities.begin(), mob_entities.end());
    visible_entities.insert(visible_entities.end(), projectile_entities.begin(), projectile_entities.end());
    visible_entities.insert(visible_entities.end(), misc_entities.begin(), misc_entities.end());
    if (visible_entities.size() > configured_entity_budget - 1)
    {
        std::nth_element(visible_entities.begin(),
                         visible_entities.begin() + static_cast<std::ptrdiff_t>(configured_entity_budget - 1),
                         visible_entities.end(), BetterSnapshotCandidate);
        visible_entities.resize(configured_entity_budget - 1);
    }
    std::sort(visible_entities.begin(), visible_entities.end(), BetterSnapshotCandidate);

    ServerMessage msg;
    msg.type = ServerMessage::Type::Snapshot;
    msg.snapshot_id = snapshot_id;
    msg.owner_entity_id = static_cast<net_entity_id>(owner->m_id);
    msg.view_radius = view_radius;
    msg.entities.reserve(std::min(visible_entities.size() + 1, configured_entity_budget));

    ServerEntitySnap owner_snap = BuildEntitySnap(*owner, *owner);
    size_t packed_size = snapshot_header_size + ServerEntitySnap::GetPackedSize(owner_snap);
    msg.entities.push_back(std::move(owner_snap));
    sf::Vector2f snapshot_origin = msg.entities.front().pos;
    net_entity_id owner_id = msg.owner_entity_id.value_or(0);
    const size_t packet_budget_for_owner =
        std::max(configured_packet_budget, packed_size + server_entity_compact_size);

    for (const visible_candidate& candidate : visible_entities)
    {
        const CEntity* entity = candidate.entity;
        if (!entity || !entity->IsVisible()) continue;
        if (!CanSnapshotEntityForPlayer(*entity, player)) continue;
        if (msg.entities.size() >= configured_entity_budget) break;

        ServerEntitySnap snap = BuildEntitySnap(*entity, *owner);
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
    snap.entity_type = GetEntityType(entity);
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
    if (const auto* missile = dynamic_cast<const CDandelionMissile*>(&entity))
    {
        snap.rarity = static_cast<uint8_t>(missile->GetRarity());
        if (missile->IsAttachedToOwner()) snap.flags |= static_cast<uint16_t>(ServerEntityFlag::Attached);
    }
    else if (const auto* missile = dynamic_cast<const CMissile*>(&entity))
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
        if (FlowerHasAvailablePetal(*flower, EPetalType::Antennae))
            snap.flags |= static_cast<uint16_t>(ServerEntityFlag::Antennae);
    }
    if (const auto* mob = dynamic_cast<const CMobBase*>(&entity))
    {
        auto& mutable_mob = *const_cast<CMobBase*>(mob);
        if (dynamic_cast<CSummonedMeleeController*>(mutable_mob.GetController()))
            snap.flags |= static_cast<uint16_t>(ServerEntityFlag::Summoned);
        if (mob->HasState<CPsionicConnectionState>())
            snap.flags |= static_cast<uint16_t>(ServerEntityFlag::Relic);
        if (mob->HasState<CUndeadState>())
            snap.flags |= static_cast<uint16_t>(ServerEntityFlag::Undead);
        if (mob->HasState<CCorruptionState>())
            snap.flags |= static_cast<uint16_t>(ServerEntityFlag::Corrupted);
        if (mob->HasState<CPoisonState>())
            snap.flags |= static_cast<uint16_t>(ServerEntityFlag::Poisoned);
        if (mob->HasState<CDiggingState>())
            snap.flags |= static_cast<uint16_t>(ServerEntityFlag::Digging);
    }
    return snap;
}
