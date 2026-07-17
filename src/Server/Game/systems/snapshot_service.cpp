#include "snapshot_service.h"
#include "../controllers/melee_controller.h"
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
#include <string>
#include <utility>
#include <vector>

namespace
{
uint8_t GetEntityType(const CEntity& entity)
{
    if (dynamic_cast<const CPortal*>(&entity)) return server_portal_entity_type;
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
    if (const auto* mob = dynamic_cast<const CMobBase*>(&entity)) return std::string(GetMobTypeName(mob->m_mob_type));
    if (const auto* drop = dynamic_cast<const CDrop*>(&entity)) return std::string(GetPetalTypeName(drop->GetType()));
    if (const auto* petal = dynamic_cast<const CPetal*>(&entity)) return std::string(GetPetalTypeName(petal->m_type));
    if (dynamic_cast<const CPortal*>(&entity)) return "Portal";
    if (dynamic_cast<const CSpiderWebZone*>(&entity)) return "SpiderWeb";
    if (dynamic_cast<const CPollenProjectile*>(&entity)) return "Pollen";
    if (dynamic_cast<const CMissile*>(&entity)) return "Missile";
    return "Entity";
}

float GetHealthPercent(const CEntity& entity)
{
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
                                  game_config::mob_rock_radius, game_config::mob_baby_ant_radius,
                                  game_config::mob_worker_ant_radius, game_config::mob_queen_ant_radius,
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

struct visible_candidate
{
    const CEntity* entity = nullptr;
    float dist_sq = 0.f;
};
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

    float snap_radius = snap_view_radius + GetPrimordialDefaultMobRadius();
    const size_t max_snapshot_candidates = entity_budget > 0 ? entity_budget - 1 : 0;
    std::vector<visible_candidate> visible_entities;
    visible_entities.reserve(std::min<size_t>(max_snapshot_candidates, 128));

    size_t visible_count = 1;
    auto farther_to_owner = [](const visible_candidate& lhs, const visible_candidate& rhs)
    {
        return lhs.dist_sq < rhs.dist_sq;
    };

    owner->GameWorld()->GetSpatialGrid().ForEachInRange(owner->m_pos, snap_radius, [&](CEntity* entity)
    {
        if (!entity || !entity->IsVisible() || entity == owner) return;
        float radius = snap_view_radius + std::max(0.f, entity->m_radius);
        float dist_sq = DistanceSq(owner->m_pos, entity->m_pos);
        if (dist_sq > radius * radius) return;
        ++visible_count;

        if (max_snapshot_candidates == 0) return;
        visible_candidate candidate{entity, dist_sq};
        if (visible_entities.size() < max_snapshot_candidates)
        {
            visible_entities.push_back(candidate);
            std::push_heap(visible_entities.begin(), visible_entities.end(), farther_to_owner);
            return;
        }
        if (candidate.dist_sq >= visible_entities.front().dist_sq) return;

        std::pop_heap(visible_entities.begin(), visible_entities.end(), farther_to_owner);
        visible_entities.back() = candidate;
        std::push_heap(visible_entities.begin(), visible_entities.end(), farther_to_owner);
    });

    std::sort(visible_entities.begin(), visible_entities.end(),
              [](const visible_candidate& lhs, const visible_candidate& rhs)
    {
        return lhs.dist_sq < rhs.dist_sq;
    });

    ServerMessage msg;
    msg.type = ServerMessage::Type::Snapshot;
    msg.snapshot_id = snapshot_id;
    msg.owner_entity_id = static_cast<net_entity_id>(owner->m_id);
    msg.view_radius = view_radius;
    msg.entities.reserve(std::min(visible_entities.size() + 1, entity_budget));

    ServerEntitySnap owner_snap = BuildEntitySnap(*owner, *owner);
    size_t packed_size = 14 + ServerEntitySnap::GetPackedSize(owner_snap);
    msg.entities.push_back(std::move(owner_snap));

    for (const visible_candidate& candidate : visible_entities)
    {
        const CEntity* entity = candidate.entity;
        if (!entity || !entity->IsVisible()) continue;
        if (msg.entities.size() >= entity_budget) break;

        ServerEntitySnap snap = BuildEntitySnap(*entity, *owner);
        size_t snap_size = ServerEntitySnap::GetPackedSize(snap);
        if (packed_size + snap_size > packet_budget) break;
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

    if (&entity == &owner) snap.flags |= static_cast<uint16_t>(ServerEntityFlag::Owner);
    if (entity.IsDead()) snap.flags |= static_cast<uint16_t>(ServerEntityFlag::Dead);

    if (const auto* attackable = dynamic_cast<const IAttackableMob*>(&entity))
    {
        if (attackable->IsAttacking()) snap.flags |= static_cast<uint16_t>(ServerEntityFlag::Attacking);
        if (attackable->IsDefending()) snap.flags |= static_cast<uint16_t>(ServerEntityFlag::Defending);
    }

    if (const auto* flower = dynamic_cast<const CFlower*>(&entity))
    {
        if (FlowerHasAvailablePetal(*flower, EPetalType::Relic))
            snap.flags |= static_cast<uint16_t>(ServerEntityFlag::Relic);
        if (FlowerHasAvailablePetal(*flower, EPetalType::Antennae))
            snap.flags |= static_cast<uint16_t>(ServerEntityFlag::Antennae);
    }
    if (const auto* mob = dynamic_cast<const CMobBase*>(&entity))
    {
        auto& mutable_mob = *const_cast<CMobBase*>(mob);
        if (dynamic_cast<CSummonedMeleeController*>(mutable_mob.GetController()))
            snap.flags |= static_cast<uint16_t>(ServerEntityFlag::Summoned);
        if (mob->HasState<CUndeadState>())
            snap.flags |= static_cast<uint16_t>(ServerEntityFlag::Undead);
        if (mob->HasState<CCorruptionState>())
            snap.flags |= static_cast<uint16_t>(ServerEntityFlag::Corrupted);
        if (mob->HasState<CPoisonState>())
            snap.flags |= static_cast<uint16_t>(ServerEntityFlag::Poisoned);
    }
    return snap;
}
