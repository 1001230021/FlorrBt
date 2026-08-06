#include "gameworld.h"
#include "../../Engine/logger.h"
#include "../../Shared/game_config.h"
#include "../Module/network_module.h"
#include "controllers/melee_controller.h"
#include "entities/drop.h"
#include "entities/flower.h"
#include "entities/mob.h"
#include "entities/petals/petal.h"
#include "entities/portal.h"
#include "entities/projectile.h"
#include "gamecontext.h"
#include "player.h"
#include "state_zone.h"
#include "states/states.h"
#include "zone_mob_tools.h"
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iomanip>
#include <limits>
#include <random>
#include <sstream>
#include <typeinfo>
#include <utility>

namespace
{
struct crash_entity_snapshot
{
    const std::type_info* concrete_type = nullptr;
    int id = -1;
    std::uint64_t generation = 0;
    int team = 0;
    float health = 0.f;
    float radius = 0.f;
    sf::Vector2f pos = { 0.f, 0.f };
    bool marked_for_destroy = false;
    bool valid = false;
};

struct collision_crash_context
{
    crash_entity_snapshot entity;
    crash_entity_snapshot other;
    bool valid = false;
};

thread_local collision_crash_context g_last_collision_crash_context;

struct summon_owner_link
{
    int direct_owner_id = -1;
    int root_owner_id = -1;
};

struct collision_entity_view
{
    CEntity* entity = nullptr;
    CProjectile* projectile = nullptr;
    CPetal* petal = nullptr;
    CMissile* missile = nullptr;
    CPollenProjectile* pollen = nullptr;
    CTrapProjectile* trap = nullptr;
    CMobBase* mob = nullptr;
    CFlower* flower = nullptr;
    CDrop* drop = nullptr;
    mutable const CMobBase* interaction_mob = nullptr;
    mutable bool interaction_mob_resolved = false;

    bool IsWax() const { return petal && petal->GetPetalType() == EPetalType::Wax; }
    bool IsGlass() const { return petal && petal->GetPetalType() == EPetalType::Glass; }
    bool IsTrapperPetal() const { return petal && petal->GetPetalType() == EPetalType::Trapper; }
    float ProjectileDamage() const
    {
        if (missile) return missile->m_damage;
        return pollen ? pollen->m_damage : 0.f;
    }
    const CMobBase* InteractionMob() const
    {
        if (!interaction_mob_resolved)
        {
            interaction_mob = ResolveInteractionMob(entity);
            interaction_mob_resolved = true;
        }
        return interaction_mob;
    }
};

collision_entity_view BuildCollisionEntityView(CEntity* entity)
{
    collision_entity_view view;
    view.entity = entity;
    if (!entity) return view;

    switch (entity->GetEntityType())
    {
    case EEntityType::Projectile:
        view.projectile = static_cast<CProjectile*>(entity);
        switch (entity->GetProjectileType())
        {
        case EProjectileType::Petal:
            view.petal = static_cast<CPetal*>(entity);
            break;
        case EProjectileType::Missile:
            view.missile = static_cast<CMissile*>(entity);
            break;
        case EProjectileType::Pollen:
            view.pollen = static_cast<CPollenProjectile*>(entity);
            break;
        case EProjectileType::Trap:
            view.pollen = static_cast<CPollenProjectile*>(entity);
            view.trap = static_cast<CTrapProjectile*>(entity);
            break;
        default:
            break;
        }
        return view;
    case EEntityType::Mob:
        view.mob = static_cast<CMobBase*>(entity);
        view.flower = dynamic_cast<CFlower*>(view.mob);
        break;
    case EEntityType::Drop:
        view.drop = static_cast<CDrop*>(entity);
        break;
    default:
        break;
    }
    return view;
}

using active_tick_view = CActiveTickView;
using player_tick_view = CPlayerTickView;

EEntityTickMode EffectiveTickMode(const CEntity* entity)
{
    if (!entity) return EEntityTickMode::Default;
    if (entity->IsEntityType(EEntityType::Mob))
    {
        const ERarity rarity = static_cast<const CMobBase*>(entity)->GetRarity();
        if (rarity == ERarity::Eternal || rarity == ERarity::Unique || rarity == ERarity::Primordial)
            return EEntityTickMode::Always;
    }
    if (entity->IsProjectileType(EProjectileType::Missile) || entity->IsProjectileType(EProjectileType::Pollen) ||
        entity->IsProjectileType(EProjectileType::Trap))
    {
        const auto* projectile = static_cast<const CProjectile*>(entity);
        const CEntity* owner = projectile->GetOwner();
        if (owner && owner->IsEntityType(EEntityType::Mob))
        {
            const ERarity rarity = static_cast<const CMobBase*>(owner)->GetRarity();
            if (rarity == ERarity::Eternal || rarity == ERarity::Unique || rarity == ERarity::Primordial)
                return EEntityTickMode::Always;
        }
    }
    return entity->m_tick_mode;
}

float Dot(sf::Vector2f a, sf::Vector2f b);
bool CollisionPairAlive(const CEntity* lhs, const CEntity* rhs);
void ApplyPhysicalDamageHits(CEntity* target, float damage, CEntity* attacker, int hit_count,
                             const CEntity* collision_source = nullptr);

bool TryMergeActiveTickViews(active_tick_view& base, const active_tick_view& incoming)
{
    if (incoming.radius <= 0.f) return true;
    if (base.radius <= 0.f)
    {
        base = incoming;
        return true;
    }

    sf::Vector2f delta = incoming.center - base.center;
    const float dist_sq = delta.x * delta.x + delta.y * delta.y;
    const float dist = std::sqrt(std::max(0.f, dist_sq));
    if (dist + incoming.radius <= base.radius) return true;
    if (dist + base.radius <= incoming.radius)
    {
        base = incoming;
        return true;
    }

    const float min_radius = std::min(base.radius, incoming.radius);
    if (dist > min_radius * game_config::drop_merge_distance_radius_multiplier) return false;
    if (dist <= game_config::entity_collision_epsilon)
    {
        base.radius = std::max(base.radius, incoming.radius);
        return true;
    }

    const float merged_radius = (dist + base.radius + incoming.radius) * 0.5f;
    const sf::Vector2f dir = delta / dist;
    base.center += dir * (merged_radius - base.radius);
    base.radius = merged_radius;
    return true;
}

void AddActiveTickView(std::vector<active_tick_view>& views, active_tick_view view)
{
    if (view.radius <= 0.f) return;

    for (size_t i = 0; i < views.size();)
    {
        active_tick_view merged = views[i];
        if (!TryMergeActiveTickViews(merged, view))
        {
            ++i;
            continue;
        }

        view = merged;
        views.erase(views.begin() + static_cast<std::ptrdiff_t>(i));
        i = 0;
    }

    views.push_back(view);
}

std::string WarpKey(std::string text)
{
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return text;
}

bool RandomPointInTransferCheckpoint(const CGameWorld& world, const FlorrBtMap::Checkpoint& checkpoint,
                                     sf::Vector2f& out_pos)
{
    if (checkpoint.w <= 0.f || checkpoint.h <= 0.f) return false;

    std::uniform_real_distribution<float> random_x(0.f, checkpoint.w);
    std::uniform_real_distribution<float> random_y(0.f, checkpoint.h);
    for (int attempt = 0; attempt < std::max(1, game_config::gameworld_transfer_spawn_position_attempts); ++attempt)
    {
        const FlorrBtMap::Point point = CheckpointLocalToWorldPoint(checkpoint, random_x(GetRng()), random_y(GetRng()));
        sf::Vector2f candidate = { point.x, point.y };
        if (world.CircleBlockedByWall(candidate, game_config::mob_player_flower_radius)) continue;
        out_pos = candidate;
        return true;
    }
    return false;
}

sf::Vector2f PickDefaultTransferSpawnPosition(const CGameWorld& world)
{
    const FlorrBtMap* map = world.GetMap();
    if (map)
    {
        std::vector<const FlorrBtMap::Checkpoint*> checkpoints;
        for (const FlorrBtMap::Checkpoint& checkpoint : map->checkpoints)
        {
            if (!checkpoint.is_respawn_area) continue;
            if (checkpoint.w <= 0.f || checkpoint.h <= 0.f) continue;
            checkpoints.push_back(&checkpoint);
        }
        if (!checkpoints.empty())
        {
            std::uniform_int_distribution<size_t> dist(0, checkpoints.size() - 1);
            sf::Vector2f pos;
            if (RandomPointInTransferCheckpoint(world, *checkpoints[dist(GetRng())], pos)) return pos;
        }
    }

    return { game_config::player_respawn_x, game_config::player_respawn_y };
}

summon_owner_link GetSummonOwnerLink(const collision_entity_view& entity)
{
    summon_owner_link link;
    const CMobBase* mob = entity.mob;
    if (!mob) return link;

    auto* controller = dynamic_cast<CSummonedMeleeController*>(const_cast<CMobBase*>(mob)->GetController());
    if (!controller) return link;

    link.direct_owner_id = controller->GetOwnerId();
    link.root_owner_id = link.direct_owner_id;

    CGameWorld* world = const_cast<CMobBase*>(mob)->GameWorld();
    CEntity* owner = controller->GetOwner(world);
    if (!owner)
    {
        link.direct_owner_id = -1;
        link.root_owner_id = -1;
        return link;
    }
    link.direct_owner_id = owner->m_id;
    link.root_owner_id = owner->m_id;
    auto* owner_projectile = dynamic_cast<CProjectile*>(owner);
    if (owner_projectile)
    {
        if (CEntity* root_owner = owner_projectile->GetOwner()) link.root_owner_id = root_owner->m_id;
    }
    return link;
}

bool HasSummonOwnerLink(const summon_owner_link& link) { return link.direct_owner_id >= 0; }

bool IsPetalOwnedBy(const collision_entity_view& entity, int owner_id)
{
    CEntity* owner = entity.projectile ? entity.projectile->GetOwner() : nullptr;
    return owner && owner->m_id == owner_id;
}

bool IsProjectileOwnerCollision(const CProjectile* projectile, const CEntity* target)
{
    if (!projectile || !target || projectile->m_owner_id < 0) return false;
    if (projectile->m_owner_id != target->m_id) return false;
    return projectile->m_owner_generation == 0 || projectile->m_owner_generation == target->m_generation ||
           projectile->GetOwner() == target;
}

bool IsSummonOwnerLink(const summon_owner_link& link, const collision_entity_view& other)
{
    if (!HasSummonOwnerLink(link) || !other.entity) return false;
    if (other.entity->m_id == link.direct_owner_id || other.entity->m_id == link.root_owner_id) return true;
    return IsPetalOwnedBy(other, link.root_owner_id);
}

bool ShouldSkipSummonCollision(const collision_entity_view& lhs, const collision_entity_view& rhs,
                               const summon_owner_link& lhs_link, const summon_owner_link& rhs_link)
{
    return IsSummonOwnerLink(lhs_link, rhs) || IsSummonOwnerLink(rhs_link, lhs);
}

bool AreBothSummons(const summon_owner_link& lhs_link, const summon_owner_link& rhs_link)
{
    return HasSummonOwnerLink(lhs_link) && HasSummonOwnerLink(rhs_link);
}

bool IsSameRootOwnerSummonCollision(const summon_owner_link& lhs_link, const summon_owner_link& rhs_link)
{
    return HasSummonOwnerLink(lhs_link) && HasSummonOwnerLink(rhs_link) && lhs_link.root_owner_id >= 0 &&
           lhs_link.root_owner_id == rhs_link.root_owner_id;
}

bool ApplyWeakOverlapCollision(const collision_entity_view& lhs_view, const collision_entity_view& rhs_view,
                               float push_multiplier)
{
    CEntity* lhs = lhs_view.entity;
    CEntity* rhs = rhs_view.entity;
    if (!lhs || !rhs || !lhs->IsCollision(*rhs)) return false;

    sf::Vector2f delta = rhs->m_pos - lhs->m_pos;
    float dist_sq = LengthSq(delta);
    float dist = std::sqrt(std::max(0.f, dist_sq));
    sf::Vector2f normal;
    if (dist > game_config::entity_collision_epsilon)
    {
        normal = delta / dist;
    } else
    {
        uint32_t seed = static_cast<uint32_t>(lhs->m_id) * 1103515245u + static_cast<uint32_t>(rhs->m_id) * 2654435761u;
        float angle = static_cast<float>(seed % 6283u) / 1000.f;
        normal = { std::cos(angle), std::sin(angle) };
        dist = 0.f;
    }

    float overlap = lhs->m_radius + rhs->m_radius - dist;
    if (overlap <= 0.f) return true;

    float lhs_inv_mass = lhs->m_mass > game_config::entity_collision_epsilon ? 1.f / lhs->m_mass : 1.f;
    float rhs_inv_mass = rhs->m_mass > game_config::entity_collision_epsilon ? 1.f / rhs->m_mass : 1.f;
    float inv_total = std::max(game_config::entity_collision_epsilon, lhs_inv_mass + rhs_inv_mass);
    const float push = std::max(0.f, push_multiplier);
    lhs->m_pos -= normal * (overlap * push * (lhs_inv_mass / inv_total));
    rhs->m_pos += normal * (overlap * push * (rhs_inv_mass / inv_total));

    if (CMobBase* mob = lhs_view.mob)
    {
        float into_other = Dot(mob->m_vel, normal);
        if (into_other > 0.f) mob->m_vel -= normal * (into_other * game_config::world_mob_overlap_velocity_retention);
    }
    if (CMobBase* mob = rhs_view.mob)
    {
        float into_other = Dot(mob->m_vel, -normal);
        if (into_other > 0.f) mob->m_vel += normal * (into_other * game_config::world_mob_overlap_velocity_retention);
    }
    return true;
}

crash_entity_snapshot CaptureEntityForCrash(const CEntity* entity)
{
    crash_entity_snapshot snapshot;
    if (!entity) return snapshot;

    snapshot.concrete_type = &typeid(*entity);
    snapshot.id = entity->m_id;
    snapshot.generation = entity->m_generation;
    snapshot.team = entity->m_team;
    snapshot.health = entity->m_health;
    snapshot.radius = entity->m_radius;
    snapshot.pos = entity->m_pos;
    snapshot.marked_for_destroy = entity->m_is_marked_for_des;
    snapshot.valid = true;
    return snapshot;
}

void WriteEntityForCrash(std::ostream& output, const crash_entity_snapshot& entity)
{
    if (!entity.valid)
    {
        output << "null";
        return;
    }

    output << entity.concrete_type->name() << "#" << entity.id << " gen=" << entity.generation
           << " team=" << entity.team << " hp=" << std::fixed << std::setprecision(2) << entity.health
           << " r=" << entity.radius << " pos=(" << entity.pos.x << "," << entity.pos.y << ")"
           << " marked=" << (entity.marked_for_destroy ? "true" : "false");
}

void SetLastCollisionCrashContext(const CEntity* entity, const CEntity* other)
{
    collision_crash_context context;
    context.entity = CaptureEntityForCrash(entity);
    context.other = CaptureEntityForCrash(other);
    context.valid = true;
    g_last_collision_crash_context = context;
}

bool IsCarriedLeafPiecePair(const collision_entity_view& lhs, const collision_entity_view& rhs)
{
    auto is_carried_pair = [](const collision_entity_view& soldier_entity, const collision_entity_view& leaf_piece) {
        const CMobBase* soldier = soldier_entity.mob;
        if (!soldier || soldier->GetMobType() != EMobType::LeafcutterSoldier) return false;
        const auto* controller = dynamic_cast<const CLeafcutterSoldierController*>(soldier->GetController());
        return controller && controller->IsCarryingLeafPiece(soldier, leaf_piece.entity);
    };
    return is_carried_pair(lhs, rhs) || is_carried_pair(rhs, lhs);
}

void ResolveLeafcutterCarryConstraints(const std::vector<CEntity*>& entities)
{
    for (CEntity* entity : entities)
    {
        auto* soldier = dynamic_cast<CMobBase*>(entity);
        if (!soldier || soldier->GetMobType() != EMobType::LeafcutterSoldier || soldier->m_is_marked_for_des ||
            soldier->IsDead())
            continue;

        auto* controller = dynamic_cast<CLeafcutterSoldierController*>(soldier->GetController());
        if (controller) controller->ResolveCarriedLeafPieceConstraint(soldier);
    }
}

void BuildActiveTickViews(CGameWorld& world, std::vector<active_tick_view>& views,
                          std::vector<player_tick_view>& player_views)
{
    views.clear();
    player_views.clear();
    CGameContext* context = world.GameContext();
    if (!context) return;

    const auto& players = context->Players();
    views.reserve(players.size());
    player_views.reserve(players.size());
    for (const auto& player : players)
    {
        if (!player || !player->IsConnected() || !player->IsAuthenticated()) continue;

        CEntity* owner = player->GetEntity();
        auto* owner_mob = dynamic_cast<CMobBase*>(owner);
        const SMobStats* stats = owner_mob ? owner_mob->GetFinalStats() : nullptr;
        if (owner && owner->GameWorld() != &world) continue;
        if (!owner || owner->m_is_marked_for_des || !stats) continue;

        const float horizon = std::max(0.f, stats->horizon);
        player_views.push_back({ owner->m_pos, horizon, std::max(0.f, owner->m_radius) });

        float radius = std::max(0.f, game_config::simulation_active_view_padding) + horizon;
        const float radius_cap = game_config::simulation_active_view_radius_cap;
        if (std::isfinite(radius_cap) && radius_cap > 0.f) radius = std::min(radius, radius_cap);
        AddActiveTickView(views, { owner->m_pos, radius });
    }
}

bool IsOwnedTrapTrapperPair(const collision_entity_view& lhs, const collision_entity_view& rhs)
{
    const auto matches = [](const collision_entity_view& trap_candidate,
                            const collision_entity_view& trapper_candidate) {
        const CTrapProjectile* trap = trap_candidate.trap;
        const CPetal* trapper = trapper_candidate.IsTrapperPetal() ? trapper_candidate.petal : nullptr;
        if (!trap || !trapper || trap->m_owner_id < 0 || trap->m_owner_id != trapper->m_owner_id) return false;

        return trap->m_owner_generation == 0 || trapper->m_owner_generation == 0 ||
               trap->m_owner_generation == trapper->m_owner_generation;
    };

    return matches(lhs, rhs) || matches(rhs, lhs);
}

bool IsFiredCarrotPetal(const CEntity* entity)
{
    const auto* missile = dynamic_cast<const CMissilePetal*>(entity);
    return missile && missile->GetPetalType() == EPetalType::Carrot && missile->m_fired;
}

int CollisionDamageHitCount(const CPetal* source_petal, int base_hit_count)
{
    const std::int64_t physical_hits = std::max(1, base_hit_count);
    const std::int64_t extra_hits =
        source_petal ? std::max<std::int64_t>(0, source_petal->GetEntityStats().extra_hit_num) : 0;
    const std::int64_t total_hits = physical_hits + extra_hits;
    return static_cast<int>(std::min(total_hits, static_cast<std::int64_t>(std::numeric_limits<int>::max())));
}

int CollisionDamageHitCount(const CEntity* source, int base_hit_count)
{
    return CollisionDamageHitCount(dynamic_cast<const CPetal*>(source), base_hit_count);
}

void DamageYggdrasilOnEnemyContact(CPetal* petal, CMobBase* mob)
{
    if (!petal || !mob || petal->GetPetalType() != EPetalType::Yggdrasil) return;
    const SMobStats* stats = mob->GetFinalStats();
    if (!stats) return;
    ApplyPhysicalDamageHits(petal, stats->damage, mob, 1);
}

bool IsHealPetalType(EPetalType type) { return type == EPetalType::Rose || type == EPetalType::Dahlia; }

bool EntityNeedsHealing(const CMobBase* mob)
{
    const SMobStats* stats = mob ? mob->GetFinalStats() : nullptr;
    return stats && stats->max_health > 0.f &&
           mob->m_health < stats->max_health - game_config::entity_collision_epsilon;
}

bool IsRoseTeamHealCollision(const collision_entity_view& lhs, const collision_entity_view& rhs)
{
    if (!lhs.entity || !rhs.entity) return false;

    if (lhs.petal && IsHealPetalType(lhs.petal->GetPetalType()) && rhs.mob &&
        lhs.petal->m_target_entity_id == rhs.entity->m_id &&
        (lhs.petal->m_target_entity_generation == 0 ||
         lhs.petal->m_target_entity_generation == rhs.entity->m_generation) &&
        EntityNeedsHealing(rhs.mob) &&
        CheckTeam(GetPreCorruptionTeam(lhs.entity, lhs.InteractionMob()),
                  GetPreCorruptionTeam(rhs.entity, rhs.InteractionMob())))
        return true;
    if (rhs.petal && IsHealPetalType(rhs.petal->GetPetalType()) && lhs.mob &&
        rhs.petal->m_target_entity_id == lhs.entity->m_id &&
        (rhs.petal->m_target_entity_generation == 0 ||
         rhs.petal->m_target_entity_generation == lhs.entity->m_generation) &&
        EntityNeedsHealing(lhs.mob) &&
        CheckTeam(GetPreCorruptionTeam(lhs.entity, lhs.InteractionMob()),
                  GetPreCorruptionTeam(rhs.entity, rhs.InteractionMob())))
        return true;
    return false;
}

struct collision_pair_context
{
    collision_entity_view lhs;
    collision_entity_view rhs;
    summon_owner_link lhs_summon;
    summon_owner_link rhs_summon;
    bool same_team = false;
    bool team_heal = false;

    bool HasWax() const { return lhs.IsWax() || rhs.IsWax(); }
    bool HasGlass() const { return lhs.IsGlass() || rhs.IsGlass(); }
    bool IsTrapPair() const { return lhs.trap && rhs.trap; }
};

collision_pair_context BuildCollisionPairContext(CEntity* lhs, CEntity* rhs)
{
    collision_pair_context pair;
    pair.lhs = BuildCollisionEntityView(lhs);
    pair.rhs = BuildCollisionEntityView(rhs);
    pair.lhs_summon = GetSummonOwnerLink(pair.lhs);
    pair.rhs_summon = GetSummonOwnerLink(pair.rhs);
    pair.same_team = CheckTeam(lhs->m_team, rhs->m_team);
    pair.team_heal = IsRoseTeamHealCollision(pair.lhs, pair.rhs);
    return pair;
}

bool ShouldIgnorePhaseCollision(const collision_pair_context& pair)
{
    if (ShouldSkipDiggingCollision(pair.lhs.entity, pair.rhs.entity)) return true;
    if (pair.team_heal) return false;
    return BlocksNullifiedMobInteraction(pair.lhs.InteractionMob(), pair.rhs.InteractionMob());
}

bool ShouldIgnoreConstraintCollision(const collision_pair_context& pair)
{
    return IsCarriedLeafPiecePair(pair.lhs, pair.rhs) || IsOwnedTrapTrapperPair(pair.lhs, pair.rhs);
}

bool ShouldIgnoreOwnershipCollision(const collision_pair_context& pair)
{
    if (pair.team_heal) return false;

    if (IsProjectileOwnerCollision(pair.lhs.projectile, pair.rhs.entity) && !pair.lhs.IsWax()) return true;
    if (IsProjectileOwnerCollision(pair.rhs.projectile, pair.lhs.entity) && !pair.rhs.IsWax()) return true;

    return !pair.HasWax() && ShouldSkipSummonCollision(pair.lhs, pair.rhs, pair.lhs_summon, pair.rhs_summon);
}

bool ShouldIgnoreFriendlyCollision(const collision_pair_context& pair)
{
    return false;
}

bool ShouldResolveWeakSummonCollision(const collision_pair_context& pair)
{
    if (pair.team_heal || pair.HasWax()) return false;
    return (pair.same_team && AreBothSummons(pair.lhs_summon, pair.rhs_summon)) ||
           IsSameRootOwnerSummonCollision(pair.lhs_summon, pair.rhs_summon);
}

bool ResolveWaxMobHardCollision(const collision_entity_view& lhs, const collision_entity_view& rhs);

bool CanResolvePhysicalCollision(const collision_pair_context& pair)
{
    if (pair.team_heal || pair.HasGlass()) return false;
    return pair.lhs.entity->CanPhysicallyCollideWith(pair.rhs.entity) &&
           pair.rhs.entity->CanPhysicallyCollideWith(pair.lhs.entity);
}

void ResolvePhysicalCollision(const collision_pair_context& pair)
{
    if (!CanResolvePhysicalCollision(pair)) return;
    if (pair.IsTrapPair())
    {
        ApplyWeakOverlapCollision(pair.lhs, pair.rhs, game_config::default_trapper_trap_collision_push);
        return;
    }

    // Wax-to-mob separation has its own immovable-wall response.
    if (ResolveWaxMobHardCollision(pair.lhs, pair.rhs)) return;

    if (pair.lhs.missile && pair.lhs.missile->IsAttachedToOwner()) pair.rhs.entity->OnCollision(pair.lhs.entity);
    else pair.lhs.entity->OnCollision(pair.rhs.entity);
}

bool CanResolveCollisionDamage(const collision_pair_context& pair) { return !pair.same_team || pair.team_heal; }

sf::Vector2f WallPoint(const FlorrBtMap::Point& point) { return { point.x, point.y }; }

float Dot(sf::Vector2f a, sf::Vector2f b) { return a.x * b.x + a.y * b.y; }

float Cross(sf::Vector2f a, sf::Vector2f b) { return a.x * b.y - a.y * b.x; }

sf::Vector2f NormalizeOrZero(sf::Vector2f value)
{
    float len = Length(value);
    if (len <= game_config::entity_collision_epsilon) return { 0.f, 0.f };
    return value / len;
}

sf::Vector2f GetEntityVelocity(CEntity& entity)
{
    if (auto* mob = dynamic_cast<CMobBase*>(&entity)) return mob->m_vel;
    if (auto* projectile = dynamic_cast<CProjectile*>(&entity)) return projectile->m_vel;
    return { 0.f, 0.f };
}

void SetEntityVelocity(CEntity& entity, sf::Vector2f velocity)
{
    if (auto* mob = dynamic_cast<CMobBase*>(&entity))
    {
        mob->m_vel = velocity;
        return;
    }
    if (auto* projectile = dynamic_cast<CProjectile*>(&entity)) projectile->m_vel = velocity;
}

bool ResolveWaxMobHardCollision(const collision_entity_view& lhs, const collision_entity_view& rhs)
{
    CEntity* wax = nullptr;
    CMobBase* mob = nullptr;
    CFlower* flower = nullptr;
    if (lhs.IsWax())
    {
        wax = lhs.entity;
        mob = rhs.mob;
        flower = rhs.flower;
    } else if (rhs.IsWax())
    {
        wax = rhs.entity;
        mob = lhs.mob;
        flower = lhs.flower;
    }

    if (!wax || !mob || flower) return false;

    sf::Vector2f delta = mob->m_pos - wax->m_pos;
    float distance = Length(delta);
    float overlap = mob->m_radius + wax->m_radius - distance;
    if (overlap <= 0.f) return true;

    sf::Vector2f normal = NormalizeOrZero(delta);
    if (LengthSq(normal) <= game_config::entity_collision_epsilon * game_config::entity_collision_epsilon)
        normal = NormalizeOrZero(mob->m_prev_pos - wax->m_pos);
    if (LengthSq(normal) <= game_config::entity_collision_epsilon * game_config::entity_collision_epsilon)
        normal = NormalizeOrZero(-mob->m_vel);
    if (LengthSq(normal) <= game_config::entity_collision_epsilon * game_config::entity_collision_epsilon)
        normal = { 1.f, 0.f };

    mob->m_pos += normal * (overlap + game_config::entity_collision_epsilon);
    float inward_speed = Dot(mob->m_vel, normal);
    if (inward_speed < 0.f) mob->m_vel -= normal * inward_speed;
    return true;
}

bool BounceFiredCarrotFromEntity(CPetal* petal, const CEntity* target)
{
    if (!petal || petal->GetPetalType() != EPetalType::Carrot) return false;
    auto* missile = dynamic_cast<CMissilePetal*>(petal);
    if (!missile || !target || !missile->m_fired) return false;

    sf::Vector2f normal = NormalizeOrZero(missile->m_pos - target->m_pos);
    if (LengthSq(normal) <= game_config::entity_collision_epsilon) normal = NormalizeOrZero(-missile->m_vel);
    if (LengthSq(normal) <= game_config::entity_collision_epsilon) normal = { 1.f, 0.f };

    if (Dot(missile->m_vel, normal) > 0.f) normal = -normal;

    sf::Vector2f old_vel = missile->m_vel;
    float speed = Length(old_vel);
    float into_entity = Dot(old_vel, normal);
    missile->m_pos =
        target->m_pos + normal * (target->m_radius + missile->m_radius + game_config::entity_projectile_contact_skin);
    missile->m_vel = old_vel - normal * (2.f * into_entity);
    if (LengthSq(missile->m_vel) <= game_config::entity_collision_epsilon &&
        speed > game_config::entity_collision_epsilon)
        missile->m_vel = normal * speed;
    if (LengthSq(missile->m_vel) > game_config::entity_collision_epsilon)
    {
        missile->m_facing_angle = std::atan2(missile->m_vel.y, missile->m_vel.x);
        missile->m_has_facing = true;
    }
    return true;
}

float EffectivePetalReload(const CPetal* petal)
{
    if (!petal) return 0.f;

    float reload = petal->m_final_petal_stats.reload;
    auto* flower = dynamic_cast<CFlower*>(petal->GetOwner());
    if (flower && flower->GetFinalStats())
    {
        float multiplier = std::max(game_config::default_petal_reload_multiplier_min,
                                    flower->GetFinalStats()->petal_reload_multiplier);
        reload = petal->m_base_petal_stats.reload * multiplier;
    }
    return std::max(0.f, reload);
}

int PetalHitCountForTick(CPetal* petal, const CEntity* target, float dt)
{
    if (!petal || !target || dt <= 0.f) return 1;
    if (petal->GetPetalType() == EPetalType::Carrot || petal->GetPetalType() == EPetalType::Missile)
    {
        if (const auto* missile = dynamic_cast<const CMissilePetal*>(petal); missile && missile->m_fired) return 1;
    }

    float reload = EffectivePetalReload(petal);
    if (reload <= game_config::entity_collision_epsilon || reload >= dt) return 1;

    float& credit = petal->m_hit_credits[target->m_id];
    float total = credit + dt / reload;
    int hits = static_cast<int>(std::floor(total));
    credit = total - static_cast<float>(hits);
    return std::clamp(hits, 1, std::max(1, game_config::entity_max_hits_per_tick));
}

bool PetalUsesPhysicalHitCompensation(const CPetal* petal)
{
    if (!petal) return false;

    switch (petal->GetPetalType())
    {
    case EPetalType::Basic:
    case EPetalType::Bone:
    case EPetalType::Cogwheel:
    case EPetalType::Dust:
    case EPetalType::Heavy:
    case EPetalType::Faster:
    case EPetalType::Pincer:
    case EPetalType::Stinger:
    case EPetalType::Wing:
    case EPetalType::Sawblade:
    case EPetalType::Light:
    case EPetalType::Leaf:
    case EPetalType::Rock:
    case EPetalType::Cactus:
    case EPetalType::Corn:
    case EPetalType::Dandelion:
    case EPetalType::Orange:
    case EPetalType::Rice:
    case EPetalType::Soil:
        return true;
    default:
        return false;
    }
}

int PhysicalHitCountForTick(const CEntity* attacker, const CEntity* target)
{
    if (!attacker || !target) return 1;

    sf::Vector2f attacker_delta = attacker->m_pos - attacker->m_prev_pos;
    sf::Vector2f target_delta = target->m_pos - target->m_prev_pos;
    float relative_sweep = Length(attacker_delta - target_delta);
    float hit_spacing =
        std::max(game_config::entity_hit_spacing_min,
                 std::min(attacker->m_radius, target->m_radius) * game_config::entity_hit_spacing_radius_multiplier);

    int hits = 1 + static_cast<int>(std::floor(relative_sweep / hit_spacing));
    return std::clamp(hits, 1, std::max(1, game_config::entity_max_physical_hits_per_tick));
}

int PetalPhysicalHitCountForTick(CPetal* petal, const CEntity* target, float dt)
{
    int hit_count = PetalHitCountForTick(petal, target, dt);
    if (PetalUsesPhysicalHitCompensation(petal))
        hit_count = std::max(hit_count, PhysicalHitCountForTick(petal, target));
    return hit_count;
}

void ApplyPhysicalDamageHits(CEntity* target, float damage, CEntity* attacker, int hit_count,
                             const CEntity* collision_source)
{
    if (!target || damage <= 0.f) return;

    const CEntity* source = collision_source ? collision_source : attacker;
    const int total_hit_count = CollisionDamageHitCount(source, hit_count);
    for (int hit = 0; hit < total_hit_count; ++hit)
    {
        if (target->IsDead() || target->m_health <= 0.f) break;
        target->TakeDamage(damage, attacker, EDamageType::Normal);
    }
}

float PetalContactDamage(CPetal* petal, CEntity* target)
{
    if (!petal || !target) return 0.f;

    float damage = petal->m_final_petal_stats.damage;
    const CPetalPrototype* proto = FindPetalPrototype(petal->GetPetalType());
    if (proto && proto->m_p_behavior) proto->m_p_behavior->OnPetalHit(petal, petal->m_rarity, target, damage);
    return damage;
}

void RefreshAttachedMissileTransforms(const std::vector<CEntity*>& entities)
{
    for (CEntity* entity : entities)
    {
        auto* missile = dynamic_cast<CMissile*>(entity);
        if (!missile || !missile->IsAttachedToOwner() || missile->m_is_marked_for_des) continue;
        if (!missile->RefreshAttachedTransform()) missile->MarkForDestroy(EEntityRemovalReason::OwnerRemoved);
    }
}

void ApplyPetalMobContact(CPetal* petal, CMobBase* mob, float dt)
{
    if (!petal || !mob) return;

    const int petal_hit_count =
        CollisionDamageHitCount(petal, PetalPhysicalHitCountForTick(petal, mob, dt));
    const int mob_hit_count = PhysicalHitCountForTick(mob, petal);
    const int exchange_count = std::max(petal_hit_count, mob_hit_count);
    const SMobStats* mob_stats = mob->GetFinalStats();
    bool petal_can_deal_damage = true;

    for (int hit = 0; hit < exchange_count; ++hit)
    {
        if (!CollisionPairAlive(petal, mob)) break;

        if (petal_can_deal_damage && hit < petal_hit_count)
        {
            const float damage = PetalContactDamage(petal, mob);
            if (damage > 0.f) mob->TakeDamage(damage, petal->GetOwner(), EDamageType::Normal);
            else petal_can_deal_damage = false;
        }
        if (!CollisionPairAlive(petal, mob)) break;

        if (mob_stats && mob_stats->damage > 0.f && hit < mob_hit_count)
            petal->TakeDamage(mob_stats->damage, mob, EDamageType::Normal);
        if (!CollisionPairAlive(petal, mob)) break;
    }
}

void ApplyPetalPetalContact(CPetal* lhs, CPetal* rhs, float dt)
{
    if (!lhs || !rhs) return;

    const int lhs_hit_count = CollisionDamageHitCount(lhs, PetalPhysicalHitCountForTick(lhs, rhs, dt));
    const int rhs_hit_count = CollisionDamageHitCount(rhs, PetalPhysicalHitCountForTick(rhs, lhs, dt));
    const int exchange_count = std::max(lhs_hit_count, rhs_hit_count);
    bool lhs_can_deal_damage = true;
    bool rhs_can_deal_damage = true;

    for (int hit = 0; hit < exchange_count; ++hit)
    {
        if (!CollisionPairAlive(lhs, rhs)) break;

        float lhs_damage = 0.f;
        if (lhs_can_deal_damage && hit < lhs_hit_count)
        {
            lhs_damage = PetalContactDamage(lhs, rhs);
            if (lhs_damage <= 0.f) lhs_can_deal_damage = false;
        }

        float rhs_damage = 0.f;
        if (rhs_can_deal_damage && hit < rhs_hit_count)
        {
            rhs_damage = PetalContactDamage(rhs, lhs);
            if (rhs_damage <= 0.f) rhs_can_deal_damage = false;
        }

        // Both attacks are decided before either side takes damage, so lethal contacts remain symmetric.
        if (lhs_damage > 0.f) rhs->TakeDamage(lhs_damage, lhs->GetOwner(), EDamageType::Normal);
        if (rhs_damage > 0.f) lhs->TakeDamage(rhs_damage, rhs->GetOwner(), EDamageType::Normal);
    }
}

void ApplyContactDamageToMissile(CMissile* missile, const collision_entity_view& target, float dt)
{
    if (!missile || !target.entity) return;

    if (CPetal* petal = target.petal)
    {
        const int hit_count = PetalPhysicalHitCountForTick(petal, missile, dt);
        ApplyPhysicalDamageHits(missile, PetalContactDamage(petal, missile), petal->GetOwner(), hit_count, petal);
        return;
    }

    if (CProjectile* projectile = target.projectile)
    {
        const float damage = target.ProjectileDamage();
        if (damage > 0.f) ApplyPhysicalDamageHits(missile, damage, projectile->GetOwner(), 1, projectile);
        return;
    }

    if (CMobBase* mob = target.mob)
    {
        if (const SMobStats* stats = mob->GetFinalStats())
        {
            if (stats->damage > 0.f)
                ApplyPhysicalDamageHits(missile, stats->damage, mob, PhysicalHitCountForTick(mob, missile));
        }
        return;
    }
}

bool ApplyAttachedMissileContact(CMissile* missile, const collision_entity_view& target, float dt)
{
    if (!missile || !target.entity || !missile->IsAttachedToOwner() ||
        IsProjectileOwnerCollision(missile, target.entity))
        return false;

    const int missile_hit_count = PhysicalHitCountForTick(missile, target.entity);
    if (missile->m_damage > 0.f)
        ApplyPhysicalDamageHits(target.entity, missile->m_damage, missile->GetOwner(), missile_hit_count, missile);

    if (!CollisionPairAlive(missile, target.entity)) return true;

    ApplyContactDamageToMissile(missile, target, dt);
    return true;
}

bool ApplyFiredMissileContact(CMissile* missile, const collision_entity_view& target, float dt)
{
    if (!missile || !target.entity || missile->IsAttachedToOwner()) return false;
    bool hit_applied = false;
    const int hit_count = CollisionDamageHitCount(missile, 1);
    for (int hit = 0; hit < hit_count; ++hit)
    {
        if (target.entity->IsDead() || target.entity->m_is_marked_for_des) break;
        if (!missile->ApplyHit(target.entity)) break;
        hit_applied = true;
    }
    if (!hit_applied) return false;
    if (!CollisionPairAlive(missile, target.entity)) return true;
    ApplyContactDamageToMissile(missile, target, dt);
    return true;
}

bool ApplyMissileHit(CMissile* missile, const collision_entity_view& target, float dt)
{
    if (!missile || !target.entity || target.drop) return false;
    if (IsProjectileOwnerCollision(missile, target.entity)) return false;
    if (!target.mob && !target.petal && !target.projectile)
        return false;
    if (auto* dandelion = dynamic_cast<CDandelionMissile*>(missile))
    {
        if (target.mob) ApplyDandelionAntiHeal(target.mob, dandelion->GetRarity());
    }
    if (missile->IsAttachedToOwner()) return ApplyAttachedMissileContact(missile, target, dt);
    return ApplyFiredMissileContact(missile, target, dt);
}

bool ApplyPollenHit(CPollenProjectile* pollen, const collision_entity_view& target)
{
    if (!pollen || !target.entity || target.drop) return false;
    CMobBase* mob = target.mob;
    CPetal* petal = target.petal;
    CProjectile* projectile = target.projectile;
    if (!mob && !petal && !projectile) return false;

    bool hit_applied = false;
    const int hit_count = CollisionDamageHitCount(pollen, 1);
    for (int hit = 0; hit < hit_count; ++hit)
    {
        if (target.entity->IsDead() || target.entity->m_is_marked_for_des) break;
        if (!pollen->ApplyHit(target.entity)) break;
        hit_applied = true;
    }
    if (!hit_applied) return false;

    float retaliation = 0.f;
    CEntity* retaliation_owner = target.entity;
    if (mob)
    {
        if (const SMobStats* stats = mob->GetFinalStats()) retaliation = stats->damage;
    } else if (petal)
    {
        retaliation = PetalContactDamage(petal, pollen);
        retaliation_owner = petal->GetOwner();
    } else if (projectile)
    {
        retaliation = target.ProjectileDamage();
        retaliation_owner = projectile->GetOwner();
    }
    if (!CollisionPairAlive(pollen, target.entity)) return true;
    if (retaliation > 0.f) ApplyPhysicalDamageHits(pollen, retaliation, retaliation_owner, 1, target.entity);
    return true;
}

bool CollisionPairAlive(const CEntity* lhs, const CEntity* rhs)
{
    return lhs && rhs && !lhs->m_is_marked_for_des && !rhs->m_is_marked_for_des && lhs->CanCollide() &&
           rhs->CanCollide() && lhs->GameWorld() && lhs->GameWorld() == rhs->GameWorld();
}

void ApplyBodyPoison(CMobBase* attacker, CFlower* attacker_flower, CMobBase* target)
{
    if (!attacker || !target || target == attacker) return;

    if (attacker->GetMobType() == EMobType::Spider)
    {
        const int level = GetLevel(attacker->GetRarity());
        const float duration = game_config::mob_spider_poison_duration;
        const float total_poison = game_config::mob_spider_poison_total * std::pow(3.f, static_cast<float>(level - 1));
        if (duration > 0.f && total_poison > 0.f)
        {
            auto poison = std::make_unique<CPoisonState>(target, duration, total_poison / duration,
                                                         attacker->GetRarity(), attacker);
            if (poison->IsValid()) target->AddState(std::move(poison));
        }
    }

    if (!attacker_flower) return;

    const SFlowerStats* stats = attacker_flower->GetFinalStats();
    if (!stats || stats->body_poison_damage_multiplier <= 0.f || stats->body_poison_duration <= 0.f) return;

    float total_poison = stats->damage * stats->body_poison_damage_multiplier;
    float poison_per_second = total_poison / stats->body_poison_duration;
    auto poison = std::make_unique<CPoisonState>(target, stats->body_poison_duration, poison_per_second,
                                                 attacker_flower->GetRarity(), attacker_flower);
    if (poison->IsValid()) target->AddState(std::move(poison));
}

void ResolveOverlappingEntityPair(CEntity* entity, CEntity* other, float dt, bool resolve_physical_collision)
{
    if (!CollisionPairAlive(entity, other) || entity == other) return;

    const collision_pair_context pair = BuildCollisionPairContext(entity, other);

    // 1. World-state filters run before every physical or damage response.
    if (ShouldIgnorePhaseCollision(pair)) return;

    // 2. Constraint and ownership filters: bound entities cannot collide with their anchors or owners.
    if (ShouldIgnoreConstraintCollision(pair) || ShouldIgnoreOwnershipCollision(pair)) return;

    // 3. Team filters: friendlies phase unless the pair is an explicit team-heal interaction.
    if (ShouldIgnoreFriendlyCollision(pair)) return;

    SetLastCollisionCrashContext(entity, other);
    if (ShouldResolveWeakSummonCollision(pair))
    {
        if (resolve_physical_collision)
            ApplyWeakOverlapCollision(pair.lhs, pair.rhs, game_config::world_mob_overlap_weak_push);
        return;
    }

    // 4. Physical response is independent from damage eligibility.
    if (resolve_physical_collision) ResolvePhysicalCollision(pair);

    // 5. Damage dispatch only runs for hostile pairs or an explicitly targeted team heal.
    if (!CanResolveCollisionDamage(pair)) return;

    CPetal* entity_petal = pair.lhs.petal;
    CPetal* other_petal = pair.rhs.petal;
    CMissile* entity_missile = pair.lhs.missile;
    CMissile* other_missile = pair.rhs.missile;
    CPollenProjectile* entity_pollen = pair.lhs.pollen;
    CPollenProjectile* other_pollen = pair.rhs.pollen;
    CMobBase* entity_mob = pair.lhs.mob;
    CMobBase* other_mob = pair.rhs.mob;

    if (entity_missile)
    {
        ApplyMissileHit(entity_missile, pair.rhs, dt);
        return;
    }
    if (other_missile)
    {
        ApplyMissileHit(other_missile, pair.lhs, dt);
        return;
    }
    if (entity_pollen)
    {
        ApplyPollenHit(entity_pollen, pair.rhs);
        return;
    }
    if (other_pollen)
    {
        ApplyPollenHit(other_pollen, pair.lhs);
        return;
    }

    if (entity_petal && other_petal)
    {
        BounceFiredCarrotFromEntity(entity_petal, other);
        BounceFiredCarrotFromEntity(other_petal, entity);
        ApplyPetalPetalContact(entity_petal, other_petal, dt);
        return;
    }

    if (entity_petal && other_mob)
    {
        BounceFiredCarrotFromEntity(entity_petal, other);
        if (entity_petal->GetPetalType() == EPetalType::Yggdrasil)
        {
            DamageYggdrasilOnEnemyContact(entity_petal, other_mob);
            return;
        }

        ApplyPetalMobContact(entity_petal, other_mob, dt);
        return;
    }

    if (other_petal && entity_mob)
    {
        BounceFiredCarrotFromEntity(other_petal, entity);
        if (other_petal->GetPetalType() == EPetalType::Yggdrasil)
        {
            DamageYggdrasilOnEnemyContact(other_petal, entity_mob);
            return;
        }

        ApplyPetalMobContact(other_petal, entity_mob, dt);
        return;
    }

    if (entity_mob && other_mob)
    {
        const SMobStats* entity_stats = entity_mob->GetFinalStats();
        const SMobStats* other_stats = other_mob->GetFinalStats();
        const int entity_hit_count = PhysicalHitCountForTick(entity, other);
        const int other_hit_count = PhysicalHitCountForTick(other, entity);

        // Snapshot both attacks first so ID/order cannot suppress a lethal counter-hit.
        if (entity_stats && entity_stats->damage > 0.f)
            ApplyPhysicalDamageHits(other, entity_stats->damage, entity, entity_hit_count);
        if (other_stats && other_stats->damage > 0.f)
            ApplyPhysicalDamageHits(entity, other_stats->damage, other, other_hit_count);

        if (!other->IsDead() && !other->m_is_marked_for_des) ApplyBodyPoison(entity_mob, pair.lhs.flower, other_mob);
        if (!entity->IsDead() && !entity->m_is_marked_for_des) ApplyBodyPoison(other_mob, pair.rhs.flower, entity_mob);
        return;
    }

    if (entity_mob && !other_petal)
    {
        if (const SMobStats* stats = entity_mob->GetFinalStats())
        {
            ApplyPhysicalDamageHits(other, stats->damage, entity, PhysicalHitCountForTick(entity, other));
            if (!CollisionPairAlive(entity, other)) return;
            ApplyBodyPoison(entity_mob, pair.lhs.flower, other_mob);
        }
    }
    if (other_mob && !entity_petal)
    {
        if (const SMobStats* stats = other_mob->GetFinalStats())
        {
            ApplyPhysicalDamageHits(entity, stats->damage, other, PhysicalHitCountForTick(other, entity));
            if (!CollisionPairAlive(entity, other)) return;
            ApplyBodyPoison(other_mob, pair.rhs.flower, entity_mob);
        }
    }
}

sf::Vector2f ClosestPointOnSegment(sf::Vector2f point, sf::Vector2f a, sf::Vector2f b)
{
    sf::Vector2f ab = b - a;
    float len_sq = LengthSq(ab);
    if (len_sq <= game_config::entity_collision_epsilon) return a;
    float t = std::clamp(Dot(point - a, ab) / len_sq, 0.f, 1.f);
    return a + ab * t;
}

bool SegmentIntersectsSegment(sf::Vector2f a, sf::Vector2f b, sf::Vector2f c, sf::Vector2f d)
{
    sf::Vector2f r = b - a;
    sf::Vector2f s = d - c;
    float denom = Cross(r, s);
    float numer = Cross(c - a, r);
    if (std::abs(denom) <= game_config::entity_collision_epsilon)
    {
        if (std::abs(numer) > game_config::entity_collision_epsilon) return false;
        float rr = LengthSq(r);
        if (rr <= game_config::entity_collision_epsilon)
            return DistanceSq(a, c) <= game_config::entity_collision_epsilon;
        float t0 = Dot(c - a, r) / rr;
        float t1 = Dot(d - a, r) / rr;
        if (t0 > t1) std::swap(t0, t1);
        return t0 <= 1.f && t1 >= 0.f;
    }

    float t = Cross(c - a, s) / denom;
    float u = Cross(c - a, r) / denom;
    return t >= 0.f && t <= 1.f && u >= 0.f && u <= 1.f;
}

float SegmentDistanceSq(sf::Vector2f a, sf::Vector2f b, sf::Vector2f c, sf::Vector2f d)
{
    if (SegmentIntersectsSegment(a, b, c, d)) return 0.f;
    float best = DistanceSq(a, ClosestPointOnSegment(a, c, d));
    best = std::min(best, DistanceSq(b, ClosestPointOnSegment(b, c, d)));
    best = std::min(best, DistanceSq(c, ClosestPointOnSegment(c, a, b)));
    best = std::min(best, DistanceSq(d, ClosestPointOnSegment(d, a, b)));
    return best;
}

bool PointInWallPolygon(sf::Vector2f point, const FlorrBtMap::Wall& wall)
{
    if (!wall.closed || wall.vertices.size() < 3) return false;

    bool inside = false;
    size_t count = wall.vertices.size();
    for (size_t i = 0, j = count - 1; i < count; j = i++)
    {
        sf::Vector2f a = WallPoint(wall.vertices[i]);
        sf::Vector2f b = WallPoint(wall.vertices[j]);
        bool intersects =
            ((a.y > point.y) != (b.y > point.y)) &&
            (point.x <
             (b.x - a.x) * (point.y - a.y) / (b.y - a.y + game_config::gameworld_wall_point_test_epsilon) + a.x);
        if (intersects) inside = !inside;
    }
    return inside;
}

int WallSegmentCount(const FlorrBtMap::Wall& wall)
{
    if (wall.vertices.size() < 2) return 0;
    return wall.closed ? static_cast<int>(wall.vertices.size()) : static_cast<int>(wall.vertices.size() - 1);
}

bool ExpandedWallBoundsMayTouch(const FlorrBtMap::Wall& wall, sf::Vector2f min_pos, sf::Vector2f max_pos, float padding)
{
    return max_pos.x + padding >= wall.min_x && min_pos.x - padding <= wall.max_x &&
           max_pos.y + padding >= wall.min_y && min_pos.y - padding <= wall.max_y;
}

bool SweptCircleHitsWall(sf::Vector2f start, sf::Vector2f end, float radius, const FlorrBtMap::Wall& wall)
{
    sf::Vector2f min_pos{ std::min(start.x, end.x), std::min(start.y, end.y) };
    sf::Vector2f max_pos{ std::max(start.x, end.x), std::max(start.y, end.y) };
    if (!ExpandedWallBoundsMayTouch(wall, min_pos, max_pos, radius)) return false;

    float radius_sq = radius * radius;
    int segment_count = WallSegmentCount(wall);
    for (int i = 0; i < segment_count; ++i)
    {
        sf::Vector2f a = WallPoint(wall.vertices[i]);
        sf::Vector2f b = WallPoint(wall.vertices[(i + 1) % wall.vertices.size()]);
        if (SegmentDistanceSq(start, end, a, b) <= radius_sq) return true;
    }

    return PointInWallPolygon(start, wall) || PointInWallPolygon(end, wall);
}

int FindBlockingWallSegment(sf::Vector2f start, sf::Vector2f end, float radius, const FlorrBtMap::Wall& wall)
{
    float radius_sq = radius * radius;
    int segment_count = WallSegmentCount(wall);
    int best_segment = -1;
    float best_dist_sq = std::numeric_limits<float>::max();
    for (int i = 0; i < segment_count; ++i)
    {
        sf::Vector2f a = WallPoint(wall.vertices[i]);
        sf::Vector2f b = WallPoint(wall.vertices[(i + 1) % wall.vertices.size()]);
        float dist_sq = SegmentDistanceSq(start, end, a, b);
        if (dist_sq <= radius_sq && dist_sq < best_dist_sq)
        {
            best_dist_sq = dist_sq;
            best_segment = i;
        }
    }
    return best_segment;
}

int FindClosestWallSegment(sf::Vector2f point, const FlorrBtMap::Wall& wall)
{
    int segment_count = WallSegmentCount(wall);
    int best_segment = -1;
    float best_dist_sq = std::numeric_limits<float>::max();
    for (int i = 0; i < segment_count; ++i)
    {
        sf::Vector2f a = WallPoint(wall.vertices[i]);
        sf::Vector2f b = WallPoint(wall.vertices[(i + 1) % wall.vertices.size()]);
        float dist_sq = DistanceSq(point, ClosestPointOnSegment(point, a, b));
        if (dist_sq < best_dist_sq)
        {
            best_dist_sq = dist_sq;
            best_segment = i;
        }
    }
    return best_segment;
}

sf::Vector2f WallSegmentTangent(const FlorrBtMap::Wall& wall, int segment_index)
{
    if (segment_index < 0 || wall.vertices.empty()) return { 0.f, 0.f };
    sf::Vector2f a = WallPoint(wall.vertices[segment_index]);
    sf::Vector2f b = WallPoint(wall.vertices[(segment_index + 1) % wall.vertices.size()]);
    return NormalizeOrZero(b - a);
}

sf::Vector2f ProjectEntityVelocityAlongWall(CEntity& entity, const FlorrBtMap::Wall& wall, int segment_index)
{
    sf::Vector2f tangent = WallSegmentTangent(wall, segment_index);
    if (LengthSq(tangent) <= game_config::entity_collision_epsilon) return { 0.f, 0.f };

    sf::Vector2f velocity = GetEntityVelocity(entity);
    sf::Vector2f projected = tangent * Dot(velocity, tangent);
    SetEntityVelocity(entity, projected);
    return projected;
}

sf::Vector2f ChooseWallNormal(const FlorrBtMap::Wall& wall, int segment_index, sf::Vector2f point,
                              sf::Vector2f motion = { 0.f, 0.f })
{
    sf::Vector2f a = WallPoint(wall.vertices[segment_index]);
    sf::Vector2f b = WallPoint(wall.vertices[(segment_index + 1) % wall.vertices.size()]);
    sf::Vector2f edge = b - a;
    sf::Vector2f n1 = NormalizeOrZero({ -edge.y, edge.x });
    if (LengthSq(n1) <= game_config::entity_collision_epsilon) return { 1.f, 0.f };
    sf::Vector2f n2 = -n1;

    if (wall.closed)
    {
        bool n1_outside = !PointInWallPolygon(point + n1 * game_config::gameworld_wall_normal_probe_distance, wall);
        bool n2_outside = !PointInWallPolygon(point + n2 * game_config::gameworld_wall_normal_probe_distance, wall);
        if (n1_outside != n2_outside) return n1_outside ? n1 : n2;
    }

    if (LengthSq(motion) > game_config::entity_collision_epsilon * game_config::entity_collision_epsilon)
    {
        float n1_dot = Dot(motion, n1);
        float n2_dot = Dot(motion, n2);
        if (std::abs(n1_dot - n2_dot) > game_config::entity_collision_epsilon) return n1_dot < n2_dot ? n1 : n2;
    }

    float side = Cross(edge, point - a);
    if (std::abs(side) <= game_config::entity_collision_epsilon) return n1;
    return side > 0.f ? n1 : n2;
}

bool HandleFiredMissileWallHit(CEntity& entity, const FlorrBtMap::Wall& wall, int segment_index, sf::Vector2f start,
                               sf::Vector2f end)
{
    if (auto* raw_missile = dynamic_cast<CMissile*>(&entity))
    {
        if (raw_missile->IsAttachedToOwner()) return true;
        raw_missile->MarkForDestroy();
        return true;
    }

    if (auto* missile = dynamic_cast<CMissilePetal*>(&entity); missile && missile->m_fired)
    {
        if (missile->GetPetalType() != EPetalType::Carrot)
        {
            missile->MarkForDestroy();
            return true;
        }

        if (segment_index < 0)
            segment_index = FindBlockingWallSegment(start, end, std::max(0.f, entity.WallCollisionRadius()), wall);
        if (segment_index < 0) segment_index = FindClosestWallSegment(start, wall);

        sf::Vector2f motion = end - start;
        if (LengthSq(motion) <= game_config::entity_collision_epsilon * game_config::entity_collision_epsilon)
            motion = missile->m_vel;
        sf::Vector2f normal =
            segment_index >= 0 ? ChooseWallNormal(wall, segment_index, start, motion) : NormalizeOrZero(start - end);
        if (LengthSq(normal) <= game_config::entity_collision_epsilon) normal = NormalizeOrZero(-missile->m_vel);
        if (LengthSq(normal) <= game_config::entity_collision_epsilon) normal = { 1.f, 0.f };

        if (Dot(missile->m_vel, normal) > 0.f) normal = -normal;

        sf::Vector2f old_vel = missile->m_vel;
        float speed = Length(old_vel);
        float into_wall = Dot(old_vel, normal);
        missile->m_pos = start + normal * game_config::gameworld_wall_contact_skin;
        missile->m_vel = old_vel - normal * (2.f * into_wall);
        if (LengthSq(missile->m_vel) <= game_config::entity_collision_epsilon &&
            speed > game_config::entity_collision_epsilon)
            missile->m_vel = normal * speed;
        if (LengthSq(missile->m_vel) > game_config::entity_collision_epsilon)
        {
            missile->m_facing_angle = std::atan2(missile->m_vel.y, missile->m_vel.x);
            missile->m_has_facing = true;
        }
        return true;
    }

    if (auto* thrown = dynamic_cast<CThrownPetal*>(&entity); thrown && thrown->m_thrown)
    {
        thrown->StopThrow(thrown->m_destroy_when_stopped);
        return true;
    }

    return false;
}

bool HandleBumbleBeeWallHit(CEntity& entity, const FlorrBtMap::Wall& wall, int segment_index, sf::Vector2f start,
                            sf::Vector2f end)
{
    auto* mob = dynamic_cast<CMobBase*>(&entity);
    if (!mob || mob->GetMobType() != EMobType::BumbleBee) return false;

    const float wall_radius = std::max(0.f, entity.WallCollisionRadius());
    if (segment_index < 0) segment_index = FindBlockingWallSegment(start, end, wall_radius, wall);

    sf::Vector2f motion = end - start;
    if (LengthSq(motion) <= game_config::entity_collision_epsilon * game_config::entity_collision_epsilon)
        motion = mob->m_vel;
    sf::Vector2f normal =
        segment_index >= 0 ? ChooseWallNormal(wall, segment_index, start, motion) : NormalizeOrZero(start - end);
    if (LengthSq(normal) <= game_config::entity_collision_epsilon) normal = NormalizeOrZero(-mob->m_vel);
    if (LengthSq(normal) <= game_config::entity_collision_epsilon) normal = { 1.f, 0.f };

    if (Dot(mob->m_vel, normal) > 0.f) normal = -normal;

    sf::Vector2f old_vel = mob->m_vel;
    float speed = Length(old_vel);
    if (speed <= game_config::entity_collision_epsilon)
    {
        const SMobStats* stats = mob->GetFinalStats();
        speed = stats ? stats->max_velocity : game_config::default_max_velocity;
        old_vel = -normal * speed;
    }

    float into_wall = Dot(old_vel, normal);
    mob->m_pos = start + normal * (wall_radius * game_config::gameworld_wall_contact_radius_multiplier +
                                   game_config::gameworld_wall_contact_skin);
    mob->m_vel = old_vel - normal * (2.f * into_wall);
    if (LengthSq(mob->m_vel) <= game_config::entity_collision_epsilon) mob->m_vel = normal * speed;

    mob->m_facing_angle = std::atan2(mob->m_vel.y, mob->m_vel.x);
    mob->m_has_facing = true;
    return true;
}

bool PushEntityOutOfWall(CEntity& entity, const FlorrBtMap::Wall& wall)
{
    const float wall_radius = std::max(0.f, entity.WallCollisionRadius());
    if (!ExpandedWallBoundsMayTouch(wall, entity.m_pos, entity.m_pos, wall_radius)) return false;

    float best_dist_sq = std::numeric_limits<float>::max();
    sf::Vector2f best_point = entity.m_pos;
    int best_segment = -1;
    int segment_count = WallSegmentCount(wall);
    for (int i = 0; i < segment_count; ++i)
    {
        sf::Vector2f a = WallPoint(wall.vertices[i]);
        sf::Vector2f b = WallPoint(wall.vertices[(i + 1) % wall.vertices.size()]);
        sf::Vector2f closest = ClosestPointOnSegment(entity.m_pos, a, b);
        float dist_sq = DistanceSq(entity.m_pos, closest);
        if (dist_sq < best_dist_sq)
        {
            best_dist_sq = dist_sq;
            best_point = closest;
            best_segment = i;
        }
    }
    if (best_segment < 0) return false;

    bool inside = PointInWallPolygon(entity.m_pos, wall);
    float dist = std::sqrt(std::max(0.f, best_dist_sq));
    if (!inside && dist >= wall_radius) return false;

    sf::Vector2f motion = entity.m_pos - entity.m_prev_pos;
    if (LengthSq(motion) <= game_config::entity_collision_epsilon * game_config::entity_collision_epsilon)
        motion = GetEntityVelocity(entity);
    sf::Vector2f normal =
        inside ? ChooseWallNormal(wall, best_segment, best_point, motion) : NormalizeOrZero(entity.m_pos - best_point);
    if (LengthSq(normal) <= game_config::entity_collision_epsilon)
        normal = ChooseWallNormal(wall, best_segment, entity.m_pos, motion);

    float penetration = inside ? wall_radius + dist : wall_radius - dist;
    entity.m_pos += normal * (penetration + game_config::gameworld_wall_contact_skin);

    sf::Vector2f velocity = GetEntityVelocity(entity);
    if (!IsFiredCarrotPetal(&entity) &&
        LengthSq(velocity) > game_config::entity_collision_epsilon * game_config::entity_collision_epsilon)
    {
        float into_wall = Dot(velocity, normal);
        if (into_wall < 0.f)
        {
            velocity -= normal * into_wall;
            SetEntityVelocity(entity, velocity);
        }
    }
    if (auto* mob = dynamic_cast<CMobBase*>(&entity))
    {
        if (LengthSq(mob->m_vel) <= game_config::entity_collision_epsilon && penetration > 0.f)
            mob->m_vel = normal * game_config::gameworld_wall_stuck_velocity;
    }
    return true;
}

} // namespace

std::string GetLastServerCrashContext()
{
    const collision_crash_context context = g_last_collision_crash_context;
    if (!context.valid) return "startup";

    std::ostringstream output;
    output << "collision ";
    WriteEntityForCrash(output, context.entity);
    output << " <-> ";
    WriteEntityForCrash(output, context.other);
    return output.str();
}

CGameWorld::CGameWorld() : CGameWorld(game_config::lobby_map_path, 0) {}

CGameWorld::CGameWorld(const std::string& path, std::uint32_t world_id)
    : m_world_id(world_id), m_map(LoadMapFromTmj(path)), m_map_path(std::filesystem::path(path).generic_string()),
      m_spatial_grid(
          game_config::spatial_grid_cell_size, [](const CEntity& entity) { return entity.m_pos; },
          [](const CEntity& entity) { return entity.m_id; }, [this](int id) { return GetEntity(id); },
          [](const CEntity& entity) { return entity.m_id >= 0; }),
      m_wall_grid(
          game_config::spatial_grid_cell_size,
          [](const FlorrBtMap::Wall& wall) { return sf::Vector2f{ wall.center.x, wall.center.y }; },
          [](const FlorrBtMap::Wall& wall) { return wall.id; }, [this](int id) { return GetWall(id); },
          [](const FlorrBtMap::Wall& wall) { return wall.id >= 0; })
{
    m_normal_entity_radius_limit =
        std::max(game_config::spatial_grid_cell_size * game_config::spatial_grid_large_radius_cell_multiplier,
                 game_config::default_horizon * game_config::spatial_grid_large_radius_horizon_multiplier);
    if (!m_map) LOG_ERROR("gameworld", "Failed to load map: " + path);
    BuildWallGrid();
    SpawnMapPortals();
}

CGameWorld::~CGameWorld()
{
    m_cleanup_entities.clear();
    m_always_tick_entities.clear();
    m_conditional_tick_entities.clear();
    m_large_entities.clear();
    m_live_entities.clear();
    m_p_entities.clear();
    m_p_entity_refs.clear();
}

void CGameWorld::SetController(std::unique_ptr<IGameController> controller)
{
    if (m_p_controller) m_p_controller->OnDeactivate(*this);
    m_p_controller = std::move(controller);
    if (m_p_controller) m_p_controller->OnActivate(*this);
}

int CGameWorld::GetNewID()
{
    if (!m_free_ids.empty())
    {
        int id = m_free_ids.back();
        m_free_ids.pop_back();
        if (id < 0 || id >= static_cast<int>(m_id_in_use.size()) || m_id_in_use[id])
        {
            LOG_FATAL("gameworld", "Corrupt free ID " + std::to_string(id));
            return GetNewID();
        }
        m_id_in_use[id] = 1;
        return id;
    }

    const int id = m_next_id++;
    if (id >= static_cast<int>(m_id_in_use.size())) m_id_in_use.resize(static_cast<size_t>(id) + 1, 0);
    m_id_in_use[id] = 1;
    return id;
}

void CGameWorld::FreeID(int id)
{
    if (id < 0 || id >= static_cast<int>(m_id_in_use.size()) || !m_id_in_use[id])
    {
        LOG_FATAL("gameworld", "Free ID " + std::to_string(id) + " twice");
        return;
    }

    m_id_in_use[id] = 0;
    m_free_ids.push_back(id);
}

void CGameWorld::RegisterLiveEntity(CEntity* entity)
{
    if (!entity) return;
    entity->m_live_index = m_live_entities.size();
    m_live_entities.push_back(entity);
    m_spatial_grid.InsertTracked(entity);
    if (entity->m_is_marked_for_des) QueueEntityForCleanup(entity);
    SyncAlwaysTickMembership(entity);
    SyncConditionalTickMembership(entity);
    SyncLargeEntityMembership(entity);
}

void CGameWorld::UnregisterLiveEntity(CEntity* entity)
{
    if (!entity) return;
    m_spatial_grid.RemoveTracked(entity->m_id);
    RemoveQueuedCleanupEntity(entity);
    RemoveAlwaysTickEntity(entity);
    RemoveConditionalTickEntity(entity);
    RemoveLargeEntity(entity);

    const size_t index = entity->m_live_index;
    if (index >= m_live_entities.size() || m_live_entities[index] != entity)
    {
        LOG_FATAL("gameworld", "Invalid live entity index for ID " + std::to_string(entity->m_id));
        return;
    }

    CEntity* moved = m_live_entities.back();
    m_live_entities[index] = moved;
    moved->m_live_index = index;
    m_live_entities.pop_back();
    entity->m_live_index = std::numeric_limits<size_t>::max();
}

void CGameWorld::QueueEntityForCleanup(CEntity* entity)
{
    if (!entity || entity->GameWorld() != this) return;

    const size_t index = entity->m_cleanup_index;
    if (index < m_cleanup_entities.size() && m_cleanup_entities[index] == entity) return;

    entity->m_cleanup_index = m_cleanup_entities.size();
    m_cleanup_entities.push_back(entity);
}

void CGameWorld::RemoveQueuedCleanupEntity(CEntity* entity)
{
    if (!entity) return;
    const size_t index = entity->m_cleanup_index;
    if (index >= m_cleanup_entities.size() || m_cleanup_entities[index] != entity)
    {
        entity->m_cleanup_index = std::numeric_limits<size_t>::max();
        return;
    }

    CEntity* moved = m_cleanup_entities.back();
    m_cleanup_entities[index] = moved;
    moved->m_cleanup_index = index;
    m_cleanup_entities.pop_back();
    entity->m_cleanup_index = std::numeric_limits<size_t>::max();
}

void CGameWorld::SyncAlwaysTickMembership(CEntity* entity)
{
    if (!entity) return;

    const EEntityTickMode tick_mode = EffectiveTickMode(entity);
    const bool should_tick_always = tick_mode == EEntityTickMode::Always ||
                                    (tick_mode == EEntityTickMode::Default && !entity->m_allow_skip_tick);
    const bool is_always_tick = entity->m_always_tick_index < m_always_tick_entities.size() &&
                                m_always_tick_entities[entity->m_always_tick_index] == entity;
    if (should_tick_always == is_always_tick) return;
    if (!should_tick_always)
    {
        RemoveAlwaysTickEntity(entity);
        return;
    }

    entity->m_always_tick_index = m_always_tick_entities.size();
    m_always_tick_entities.push_back(entity);
}

void CGameWorld::RemoveAlwaysTickEntity(CEntity* entity)
{
    if (!entity) return;
    const size_t index = entity->m_always_tick_index;
    if (index >= m_always_tick_entities.size() || m_always_tick_entities[index] != entity)
    {
        entity->m_always_tick_index = std::numeric_limits<size_t>::max();
        return;
    }

    CEntity* moved = m_always_tick_entities.back();
    m_always_tick_entities[index] = moved;
    moved->m_always_tick_index = index;
    m_always_tick_entities.pop_back();
    entity->m_always_tick_index = std::numeric_limits<size_t>::max();
}

void CGameWorld::SyncConditionalTickMembership(CEntity* entity)
{
    if (!entity) return;

    const bool should_tick_conditionally = EffectiveTickMode(entity) == EEntityTickMode::PlayerVisible;
    const bool is_conditional_tick = entity->m_conditional_tick_index < m_conditional_tick_entities.size() &&
                                     m_conditional_tick_entities[entity->m_conditional_tick_index] == entity;
    if (should_tick_conditionally == is_conditional_tick) return;
    if (!should_tick_conditionally)
    {
        RemoveConditionalTickEntity(entity);
        return;
    }

    entity->m_conditional_tick_index = m_conditional_tick_entities.size();
    m_conditional_tick_entities.push_back(entity);
}

void CGameWorld::RemoveConditionalTickEntity(CEntity* entity)
{
    if (!entity) return;
    const size_t index = entity->m_conditional_tick_index;
    if (index >= m_conditional_tick_entities.size() || m_conditional_tick_entities[index] != entity)
    {
        entity->m_conditional_tick_index = std::numeric_limits<size_t>::max();
        return;
    }

    CEntity* moved = m_conditional_tick_entities.back();
    m_conditional_tick_entities[index] = moved;
    moved->m_conditional_tick_index = index;
    m_conditional_tick_entities.pop_back();
    entity->m_conditional_tick_index = std::numeric_limits<size_t>::max();
}

void CGameWorld::SyncLargeEntityMembership(CEntity* entity)
{
    if (!entity) return;
    const bool should_be_large = entity->m_radius > m_normal_entity_radius_limit;
    const bool is_large = entity->m_large_spatial_index < m_large_entities.size() &&
                          m_large_entities[entity->m_large_spatial_index] == entity;
    if (should_be_large == is_large) return;

    if (!should_be_large)
    {
        RemoveLargeEntity(entity);
        return;
    }

    entity->m_large_spatial_index = m_large_entities.size();
    m_large_entities.push_back(entity);
}

void CGameWorld::RemoveLargeEntity(CEntity* entity)
{
    if (!entity) return;
    const size_t index = entity->m_large_spatial_index;
    if (index >= m_large_entities.size() || m_large_entities[index] != entity)
    {
        entity->m_large_spatial_index = std::numeric_limits<size_t>::max();
        return;
    }

    CEntity* moved = m_large_entities.back();
    m_large_entities[index] = moved;
    moved->m_large_spatial_index = index;
    m_large_entities.pop_back();
    entity->m_large_spatial_index = std::numeric_limits<size_t>::max();
}

CEntity* CGameWorld::InsertEntity(std::unique_ptr<CEntity> entity)
{
    if (!entity) return nullptr;
    if (entity->m_id != -1)
    {
        LOG_FATAL("gameworld", "The entity already has ID " + std::to_string(entity->m_id) + ".");
        return nullptr;
    }

    int id = GetNewID();
    if (id >= static_cast<int>(m_p_entities.size())) m_p_entities.resize(id + 1);

    if (m_p_entities[id] != nullptr)
    {
        LOG_FATAL("gameworld", "The ID " + std::to_string(id) + " is already occupied.");
        FreeID(id);
        return nullptr;
    }

    CEntity* raw_entity = entity.get();
    entity->SetGameWorld(this);
    entity->m_id = id;
    entity->m_generation = m_next_generation++;
    if (m_next_generation == 0) m_next_generation = 1;
    m_p_entities[id] = std::move(entity);
    RegisterLiveEntity(raw_entity);
    return raw_entity;
}

CEntity* CGameWorld::InsertEntityWithIdentity(std::unique_ptr<CEntity> entity, int id, std::uint64_t generation)
{
    if (!entity || id < 0 || generation == 0) return nullptr;
    if (entity->m_id != -1)
    {
        LOG_ERROR("gameworld", "Restored entity already has an ID");
        return nullptr;
    }

    if (id >= static_cast<int>(m_p_entities.size())) m_p_entities.resize(static_cast<size_t>(id) + 1);
    if (m_p_entities[id])
    {
        LOG_ERROR("gameworld", "Restored entity ID " + std::to_string(id) + " is already occupied");
        return nullptr;
    }
    if (id >= static_cast<int>(m_id_in_use.size())) m_id_in_use.resize(static_cast<size_t>(id) + 1, 0);
    if (m_id_in_use[id])
    {
        LOG_ERROR("gameworld", "Restored entity ID " + std::to_string(id) + " is already marked in use");
        return nullptr;
    }

    CEntity* raw_entity = entity.get();
    entity->SetGameWorld(this);
    entity->m_id = id;
    entity->m_generation = generation;
    m_p_entities[id] = std::move(entity);
    m_id_in_use[id] = 1;
    m_next_id = std::max(m_next_id, id + 1);
    if (generation >= m_next_generation)
    {
        m_next_generation = generation + 1;
        if (m_next_generation == 0) m_next_generation = 1;
    }
    RegisterLiveEntity(raw_entity);
    return raw_entity;
}

void CGameWorld::ClearEntitiesForRestore()
{
    m_cleanup_entities.clear();
    m_always_tick_entities.clear();
    m_conditional_tick_entities.clear();
    m_large_entities.clear();
    m_active_entities.clear();
    m_collision_normal_entities.clear();
    m_collision_large_entities.clear();
    m_collision_inactive_entities.clear();
    m_pending_psionic_damage.clear();
    m_live_entities.clear();
    m_spatial_grid.Clear();
    m_p_entity_refs.clear();
    m_p_entities.clear();
    m_free_ids.clear();
    m_id_in_use.clear();
    m_next_id = 0;
    m_next_generation = 1;
    m_active_tick_marker = 1;
}

void CGameWorld::QueuePsionicDamage(CMobBase* receiver, float damage, CEntity* attacker, EDamageType damage_type)
{
    if (!receiver || receiver->GameWorld() != this || damage <= 0.f) return;
    if (IsSharedDamageType(damage_type)) return;

    const SPsionicReceiverKey key{ receiver->m_id, receiver->m_generation };
    auto [it, inserted] = m_pending_psionic_damage.try_emplace(key);
    if (inserted)
    {
        it->second.origin = receiver->m_pos;
        it->second.team = receiver->m_team;
    }
    it->second.damage_by_source[{ attacker, BaseDamageType(damage_type) }] += damage;
}

void CGameWorld::FlushPendingPsionicDamage()
{
    if (m_pending_psionic_damage.empty()) return;

    auto pending_damage = std::move(m_pending_psionic_damage);
    m_pending_psionic_damage.clear();
    std::vector<SPsionicReceiverKey> anchor_keys;
    anchor_keys.reserve(pending_damage.size());
    for (const auto& [key, pending] : pending_damage)
    {
        (void)pending;
        anchor_keys.push_back(key);
    }
    std::sort(anchor_keys.begin(), anchor_keys.end(), [](const SPsionicReceiverKey& lhs, const SPsionicReceiverKey& rhs) {
        if (lhs.entity_id != rhs.entity_id) return lhs.entity_id < rhs.entity_id;
        return lhs.entity_generation < rhs.entity_generation;
    });

    std::unordered_map<SPsionicDamageKey, float, SPsionicDamageKeyHash> distributed_damage;
    std::unordered_set<SPsionicReceiverKey, SPsionicReceiverKeyHash> assigned_targets;
    std::vector<CMobBase*> candidates;
    std::unordered_map<SPsionicSourceKey, float, SPsionicSourceKeyHash> group_damage_by_source;
    const float link_range = std::max(0.f, game_config::psionic_connection_range);
    const float link_range_sq = link_range * link_range;
    for (const SPsionicReceiverKey& anchor_key : anchor_keys)
    {
        auto anchor_it = pending_damage.find(anchor_key);
        if (anchor_it == pending_damage.end()) continue;
        const SPendingPsionicDamage anchor = anchor_it->second;
        candidates.clear();
        m_spatial_grid.ForEachInRange(anchor.origin, link_range, [&](CEntity* entity) {
            if (!entity || entity->m_is_marked_for_des || entity->IsDead()) return;
            if (!entity->IsEntityType(EEntityType::Mob)) return;
            auto* mob = static_cast<CMobBase*>(entity);
            if (!mob || !CheckTeam(mob->m_team, anchor.team)) return;
            if (DistanceSq(mob->m_pos, anchor.origin) > link_range_sq) return;
            if (!HasActivePsionicConnection(mob)) return;
            const SPsionicReceiverKey candidate_key{ mob->m_id, mob->m_generation };
            if (!assigned_targets.insert(candidate_key).second) return;
            candidates.push_back(mob);
        });
        if (candidates.empty())
        {
            pending_damage.erase(anchor_it);
            continue;
        }
        std::sort(candidates.begin(), candidates.end(), [](const CMobBase* lhs, const CMobBase* rhs) {
            if (lhs->m_id != rhs->m_id) return lhs->m_id < rhs->m_id;
            return lhs->m_generation < rhs->m_generation;
        });

        group_damage_by_source.clear();
        for (CMobBase* mob : candidates)
        {
            const SPsionicReceiverKey key{ mob->m_id, mob->m_generation };
            auto pending_it = pending_damage.find(key);
            if (pending_it == pending_damage.end()) continue;
            for (const auto& [source, damage] : pending_it->second.damage_by_source)
                group_damage_by_source[source] += damage;
            pending_damage.erase(pending_it);
        }

        // The anchor can disappear between receiving damage and this flush, while nearby linked mobs remain valid.
        auto stale_anchor = pending_damage.find(anchor_key);
        if (stale_anchor != pending_damage.end())
        {
            for (const auto& [source, damage] : stale_anchor->second.damage_by_source)
                group_damage_by_source[source] += damage;
            pending_damage.erase(stale_anchor);
        }

        const float divisor = static_cast<float>(candidates.size());
        for (const auto& [source, damage] : group_damage_by_source)
        {
            if (damage <= 0.f) continue;
            const float shared_damage = damage / divisor;
            const EDamageType shared_type = ToSharedDamageType(source.damage_type);
            for (CMobBase* mob : candidates)
            {
                const SPsionicDamageKey target{ mob->m_id, mob->m_generation, source.attacker, shared_type };
                distributed_damage[target] += shared_damage;
            }
        }
    }

    for (const auto& [target, damage] : distributed_damage)
    {
        CEntity* entity = GetEntity(target.entity_id, target.entity_generation);
        if (!entity || !entity->IsEntityType(EEntityType::Mob)) continue;
        static_cast<CMobBase*>(entity)->TakeDamage(damage, target.attacker, target.damage_type);
    }
}

void CGameWorld::FinalizeEntityRestore()
{
    m_free_ids.clear();
    if (m_next_id > static_cast<int>(m_id_in_use.size())) m_id_in_use.resize(m_next_id, 0);
    for (int id = 0; id < m_next_id; ++id)
    {
        if (!m_id_in_use[id]) m_free_ids.push_back(id);
    }
}

CEntity* CGameWorld::InsertEntity(CEntity* entity)
{
    if (!entity) return nullptr;
    return InsertEntity(std::unique_ptr<CEntity>(entity));
}

CEntity* CGameWorld::InsertNonOwningEntity(CEntity* entity)
{
    if (!entity) return nullptr;
    if (entity->m_id != -1)
    {
        LOG_FATAL("gameworld", "The non-owning entity already has ID " + std::to_string(entity->m_id) + ".");
        return nullptr;
    }

    int id = GetNewID();
    if (id >= static_cast<int>(m_p_entity_refs.size())) m_p_entity_refs.resize(id + 1, nullptr);

    if (m_p_entity_refs[id] != nullptr)
    {
        LOG_FATAL("gameworld", "Non-owning entity slot " + std::to_string(id) + " is already occupied.");
        FreeID(id);
        return nullptr;
    }

    m_p_entity_refs[id] = entity;
    entity->SetGameWorld(this);
    entity->m_id = id;
    entity->m_generation = m_next_generation++;
    if (m_next_generation == 0) m_next_generation = 1;
    RegisterLiveEntity(entity);
    return entity;
}

void CGameWorld::RemoveEntity(int id)
{
    CEntity* entity = GetEntity(id);
    if (!entity)
    {
        LOG_ERROR("gameworld", "Entity ID " + std::to_string(id) + " is invalid or already null");
        return;
    }

    entity->MarkForDestroy();
}

void CGameWorld::DestroyProjectilesOwnedBy(int owner_id)
{
    if (owner_id < 0) return;

    auto destroy_if_owned = [owner_id](CEntity* entity) {
        auto* projectile = dynamic_cast<CProjectile*>(entity);
        if (projectile && projectile->m_owner_id == owner_id)
            projectile->MarkForDestroy(EEntityRemovalReason::OwnerRemoved);
    };

    ForEachEntity(destroy_if_owned);
}

void CGameWorld::DestroySummonedMobsOwnedBy(int owner_id, std::uint64_t owner_generation)
{
    if (owner_id < 0) return;

    auto destroy_if_owned = [owner_id, owner_generation](CEntity* entity) {
        auto* mob = dynamic_cast<CMobBase*>(entity);
        if (!mob) return;

        auto* controller = dynamic_cast<CSummonedMeleeController*>(mob->GetController());
        if (!controller || controller->GetOwnerId() != owner_id) return;

        const std::uint64_t summon_owner_generation = controller->GetOwnerGeneration();
        if (owner_generation != 0 && summon_owner_generation != 0 && summon_owner_generation != owner_generation)
            return;

        mob->MarkForDestroy(EEntityRemovalReason::OwnerRemoved);
    };

    ForEachEntity(destroy_if_owned);
}

void CGameWorld::SpawnMapPortals()
{
    if (!m_map) return;

    for (const FlorrBtMap::Warp& warp : m_map->warps)
    {
        if (warp.goal.empty()) continue;
        float radius = warp.radius > 0.f ? warp.radius : game_config::portal_radius;
        auto portal = std::make_unique<CPortal>(this, sf::Vector2f{ warp.x, warp.y }, radius, warp.goal);
        portal->SetFromPointName(warp.point.empty() ? warp.name : warp.point);
        InsertEntity(std::move(portal));
    }
}

CEntity* CGameWorld::TransferPlayerEntityToWorld(CPlayer& player, CGameWorld& target_world,
                                                 std::optional<sf::Vector2f> target_pos,
                                                 std::optional<std::string> from)
{
    if (from && !from->empty())
    {
        if (std::optional<sf::Vector2f> warp_pos = target_world.FindWarpPoint(*from)) target_pos = warp_pos;
    }
    if (!target_pos) target_pos = PickDefaultTransferSpawnPosition(target_world);

    CEntity* entity = player.GetEntity();
    if (!entity)
    {
        LOG_WARN("gameworld", "Transfer failed: player " + std::to_string(player.GetId()) + " has no entity");
        return nullptr;
    }
    if (entity->GameWorld() != this)
    {
        LOG_WARN("gameworld",
                 "Transfer failed: player " + std::to_string(player.GetId()) + " is not controlled by this world");
        return nullptr;
    }
    if (entity->m_is_marked_for_des)
    {
        LOG_WARN("gameworld", "Transfer failed: entity " + std::to_string(entity->m_id) + " is marked for destroy");
        return nullptr;
    }

    if (&target_world == this)
    {
        if (target_pos)
        {
            entity->m_pos = *target_pos;
            entity->m_prev_pos = entity->m_pos;
        }
        player.SetOwnedEntity(entity);
        player.m_logged_missing_entity = false;
        return entity;
    }

    const int old_id = entity->m_id;
    const std::uint64_t old_generation = entity->m_generation;
    if (old_id < 0 || old_id >= static_cast<int>(m_p_entities.size()) || m_p_entities[old_id].get() != entity)
    {
        LOG_WARN("gameworld", "Transfer failed: entity " + std::to_string(old_id) + " is not owned by this world");
        return nullptr;
    }

    const int new_id = target_world.GetNewID();
    if (new_id >= static_cast<int>(target_world.m_p_entities.size())) target_world.m_p_entities.resize(new_id + 1);
    if (target_world.m_p_entities[new_id] != nullptr)
    {
        target_world.FreeID(new_id);
        LOG_ERROR("gameworld", "Transfer failed: target ID " + std::to_string(new_id) + " is occupied");
        return nullptr;
    }

    if (m_p_controller) m_p_controller->OnPlayerLeftWorld(*this, player);
    if (auto* flower = dynamic_cast<CFlower*>(entity)) flower->DestroyPetalEntities();
    DestroyProjectilesOwnedBy(old_id);
    DestroySummonedMobsOwnedBy(old_id, old_generation);

    std::unique_ptr<CEntity> moved_entity = std::move(m_p_entities[old_id]);
    UnregisterLiveEntity(moved_entity.get());

    moved_entity->m_id = new_id;
    moved_entity->m_generation = target_world.m_next_generation++;
    if (target_world.m_next_generation == 0) target_world.m_next_generation = 1;
    moved_entity->SetGameWorld(&target_world);
    if (target_pos) moved_entity->m_pos = *target_pos;
    moved_entity->m_prev_pos = moved_entity->m_pos;

    CEntity* transferred = moved_entity.get();
    target_world.m_p_entities[new_id] = std::move(moved_entity);
    target_world.RegisterLiveEntity(transferred);
    FreeID(old_id);
    player.SetOwnedEntity(transferred);
    player.m_cp_stack.clear();
    player.m_cp_check_phase = static_cast<uint8_t>(player.GetId() & 0x07);
    player.m_logged_missing_entity = false;
    if (target_world.m_p_controller) target_world.m_p_controller->OnPlayerEnteredWorld(target_world, player);
    if (CGameContext* context = target_world.GameContext()) context->Network().NotifyPlayerWorldChanged(player);
    LOG_INFO("gameworld", "Transferred player " + std::to_string(player.GetId()) + " entity " + std::to_string(old_id) +
                              " -> " + std::to_string(transferred->m_id));
    return transferred;
}

CEntity* CGameWorld::TransferPlayerEntityToWorld(CPlayer& player, CGameWorld& target_world, const std::string& from)
{
    return TransferPlayerEntityToWorld(player, target_world, std::nullopt, from);
}

void CGameWorld::Tick(float dt)
{
    auto run_tick_phases = [this, dt](auto&& phase_done) {
        CollectActiveEntitiesForTick();
        phase_done(ETickPhase::ActiveCollection);

        TickActiveEntities(dt);
        FlushPendingPsionicDamage();
        phase_done(ETickPhase::EntityUpdate);

        SyncSpatialPositionsAndMemberships(m_active_entities);
        if (m_p_controller) m_p_controller->OnTick(*this, dt);
        RemoveTransferredActiveEntities();
        FlushPendingPsionicDamage();
        phase_done(ETickPhase::ControllerAndSync);

        ResolveWallCollisions(m_active_entities);
        ResolveLeafcutterCarryConstraints(m_active_entities);
        SyncSpatialPositions(m_active_entities);
        phase_done(ETickPhase::WallResolution);

        ResolveCollisions(m_active_entities, dt);
        FlushPendingPsionicDamage();
        ResolveLeafcutterCarryConstraints(m_active_entities);
        RefreshAttachedMissileTransforms(m_active_entities);
        SyncSpatialPositionsAndMemberships(m_active_entities);
        phase_done(ETickPhase::EntityCollision);

        Cleanup();
        phase_done(ETickPhase::Cleanup);
    };

    if (!game_config::world_tick_phase_profile_enabled)
    {
        run_tick_phases([](ETickPhase) {});
        return;
    }

    using tick_clock = std::chrono::steady_clock;
    std::array<double, tick_phase_count> sample_ms{};
    auto phase_started = tick_clock::now();
    run_tick_phases([&](ETickPhase phase) {
        const auto now = tick_clock::now();
        sample_ms[static_cast<std::size_t>(phase)] =
            std::chrono::duration<double, std::milli>(now - phase_started).count();
        phase_started = now;
    });

    ++m_tick_phase_telemetry.samples;
    m_tick_phase_telemetry.last_ms = sample_ms;
    for (std::size_t i = 0; i < tick_phase_count; ++i)
    {
        m_tick_phase_telemetry.total_ms[i] += sample_ms[i];
        m_tick_phase_telemetry.max_ms[i] = std::max(m_tick_phase_telemetry.max_ms[i], sample_ms[i]);
    }
}

void CGameWorld::CollectActiveEntitiesForTick()
{
    BuildActiveTickViews(*this, m_active_tick_views, m_player_tick_views);
    m_active_entities.clear();
    m_active_entities.reserve(m_always_tick_entities.size() + m_conditional_tick_entities.size());

    ++m_active_tick_marker;
    if (m_active_tick_marker == 0)
    {
        m_active_tick_marker = 1;
        for (CEntity* entity : m_live_entities)
            if (entity) entity->m_active_tick_marker = 0;
    }

    auto add_active_entity = [this](CEntity* entity) {
        if (!entity) return;
        if (entity->m_active_tick_marker == m_active_tick_marker) return;
        entity->m_active_tick_marker = m_active_tick_marker;
        entity->m_prev_pos = entity->m_pos;
        m_active_entities.push_back(entity);
    };

    for (CEntity* entity : m_always_tick_entities)
        add_active_entity(entity);

    for (const active_tick_view& view : m_active_tick_views)
    {
        ForEachEntityInEdgeRange(view.center, view.radius, [&](CEntity* entity) {
            if (!entity || !entity->m_allow_skip_tick) return;
            if (EffectiveTickMode(entity) == EEntityTickMode::PlayerVisible) return;
            add_active_entity(entity);
        });
    }

    for (CEntity* entity : m_conditional_tick_entities)
    {
        if (!entity || entity->m_is_marked_for_des || entity->GameWorld() != this) continue;
        const float entity_horizon = std::max(0.f, entity->TickHorizon());
        for (const player_tick_view& player_view : m_player_tick_views)
        {
            const float entity_sees_player = entity_horizon + player_view.radius;
            const float player_sees_entity = player_view.horizon + std::max(0.f, entity->m_radius);
            const float activation_radius = std::max(entity_sees_player, player_sees_entity);
            if (DistanceSq(entity->m_pos, player_view.center) > activation_radius * activation_radius) continue;
            add_active_entity(entity);
            break;
        }
    }
}

void CGameWorld::TickActiveEntities(float dt)
{
    for (CEntity* entity : m_active_entities)
    {
        if (!entity || entity->m_is_marked_for_des) continue;
        if (entity->GameWorld() != this) continue;
        if (!entity->m_skip_world_tick) entity->Tick(dt);
    }
}

void CGameWorld::RemoveTransferredActiveEntities()
{
    auto remove_transferred = [this](CEntity* entity) { return !entity || entity->GameWorld() != this; };
    m_active_entities.erase(std::remove_if(m_active_entities.begin(), m_active_entities.end(), remove_transferred),
                            m_active_entities.end());
}

CEntity* CGameWorld::GetEntity(int id) const
{
    if (id < 0) return nullptr;

    if (id < static_cast<int>(m_p_entities.size()) && m_p_entities[id]) return m_p_entities[id].get();
    if (id < static_cast<int>(m_p_entity_refs.size())) return m_p_entity_refs[id];
    return nullptr;
}

CEntity* CGameWorld::GetEntity(int id, std::uint64_t generation) const
{
    CEntity* entity = GetEntity(id);
    if (!entity || entity->m_generation != generation) return nullptr;
    return entity;
}

CEntity* CGameWorld::FindClosestEntity(const sf::Vector2f& center, float max_range,
                                       std::function<bool(const CEntity*)> filter) const
{
    return m_spatial_grid.FindClosest(center, max_range, filter);
}

CEntity* CGameWorld::FindClosestEntityByEdge(const sf::Vector2f& center, float max_edge_range,
                                             std::function<bool(const CEntity*)> filter) const
{
    if (max_edge_range <= 0.f) return nullptr;

    CEntity* target = nullptr;
    float best_edge_distance = max_edge_range;
    ForEachEntityInEdgeRange(center, max_edge_range, [&](CEntity* candidate) {
        if (!candidate) return;
        if (filter && !filter(candidate)) return;
        float edge_distance = std::max(0.f, Distance(center, candidate->m_pos) - candidate->m_radius);
        if (edge_distance <= best_edge_distance)
        {
            best_edge_distance = edge_distance;
            target = candidate;
        }
    });
    return target;
}

void CGameWorld::CollectEntities(std::vector<CEntity*>& result) const
{
    result.assign(m_live_entities.begin(), m_live_entities.end());
}

void CGameWorld::SyncSpatialPositions(const std::vector<CEntity*>& entities)
{
    for (CEntity* entity : entities)
    {
        if (!entity || entity->GameWorld() != this) continue;
        m_spatial_grid.UpdateTracked(entity);
    }
}

void CGameWorld::SyncSpatialPositionsAndMemberships(const std::vector<CEntity*>& entities)
{
    for (CEntity* entity : entities)
    {
        if (!entity || entity->GameWorld() != this) continue;
        m_spatial_grid.UpdateTracked(entity);
        SyncAlwaysTickMembership(entity);
        SyncConditionalTickMembership(entity);
        SyncLargeEntityMembership(entity);
    }
}

std::vector<CEntity*> CGameWorld::GetAllEntities() const
{
    std::vector<CEntity*> result;
    CollectEntities(result);
    return result;
}

std::string CGameWorld::GetMapName() const { return std::filesystem::path(m_map_path).stem().generic_string(); }

ERarity CGameWorld::GetSpawnZoneRarity(const sf::Vector2f& pos) const
{
    float max_difficulty = 0.f;
    if (m_map)
    {
        for (const FlorrBtMap::Zone& zone : m_map->zones)
        {
            if (!IsPointInZone(zone, pos)) continue;
            max_difficulty = std::max(max_difficulty, zone.difficulty);
        }
    }

    const int rarity = std::clamp(static_cast<int>(std::lround(max_difficulty / 10.f)),
                                  static_cast<int>(ERarity::Common), static_cast<int>(ERarity::Primordial));
    return static_cast<ERarity>(rarity);
}

std::size_t CGameWorld::GetPlayerCount() const
{
    if (!m_p_game_context) return 0;

    std::size_t count = 0;
    for (const auto& player : m_p_game_context->Players())
    {
        if (!player) continue;
        const CEntity* entity = player->GetEntity();
        if (entity && entity->GameWorld() == this && !entity->m_is_marked_for_des) ++count;
    }
    return count;
}

std::optional<sf::Vector2f> CGameWorld::FindWarpPoint(const std::string& from) const
{
    if (!m_map || from.empty()) return std::nullopt;

    const std::string key = WarpKey(from);
    std::vector<const FlorrBtMap::Warp*> matches;
    for (const FlorrBtMap::Warp& warp : m_map->warps)
    {
        if (WarpKey(warp.point) == key || WarpKey(warp.name) == key || WarpKey(warp.goal) == key)
            matches.push_back(&warp);
    }
    if (matches.empty()) return std::nullopt;

    const FlorrBtMap::Warp* warp = matches.front();
    if (matches.size() > 1)
    {
        std::uniform_int_distribution<size_t> dist(0, matches.size() - 1);
        warp = matches[dist(GetRng())];
    }
    return sf::Vector2f{ warp->x, warp->y };
}

FlorrBtMap::Wall* CGameWorld::GetWall(int id) const
{
    if (!m_map || id < 0 || id >= static_cast<int>(m_map->walls.size())) return nullptr;
    return const_cast<FlorrBtMap::Wall*>(&m_map->walls[id]);
}

void CGameWorld::BuildWallGrid()
{
    m_wall_grid.Clear();
    if (!m_map) return;

    for (FlorrBtMap::Wall& wall : m_map->walls)
    {
        m_wall_grid.Insert(&wall, std::max(1.f, wall.query_radius));
    }
}

bool CGameWorld::SweptCircleBlockedByWall(sf::Vector2f start, sf::Vector2f end, float radius) const
{
    if (!m_map || m_map->walls.empty()) return false;

    radius = std::max(0.f, radius);
    sf::Vector2f center = (start + end) * 0.5f;
    float travel = Distance(start, end);
    float query_radius =
        travel * 0.5f + radius + std::max(0.f, game_config::gameworld_wall_swept_query_padding);
    bool blocked = false;
    m_wall_grid.ForEachInRangeBroadphase(center, query_radius, [&](FlorrBtMap::Wall* wall) {
        if (blocked || !wall) return;
        if (SweptCircleHitsWall(start, end, radius, *wall)) blocked = true;
    });
    return blocked;
}

bool CGameWorld::SegmentBlockedByWall(sf::Vector2f start, sf::Vector2f end) const
{
    return SweptCircleBlockedByWall(start, end, 0.f);
}

bool CGameWorld::CircleBlockedByWall(sf::Vector2f center, float radius) const
{
    return SweptCircleBlockedByWall(center, center, radius);
}

void CGameWorld::ResolveWallCollisions(const std::vector<CEntity*>& entities)
{
    if (!m_map || m_map->walls.empty()) return;

    const int max_wall_push_passes = std::max(1, game_config::entity_wall_push_passes);
    const float wall_query_slop = std::max(0.f, game_config::entity_wall_query_slop);

    auto find_swept_wall_hit = [this, wall_query_slop](sf::Vector2f start, sf::Vector2f end, float radius,
                                                       const FlorrBtMap::Wall*& out_wall, int& out_segment) {
        out_wall = nullptr;
        out_segment = -1;

        sf::Vector2f center = (start + end) * 0.5f;
        float travel = Distance(start, end);
        float query_radius = travel * 0.5f + std::max(0.f, radius) + wall_query_slop;
        sf::Vector2f min_pos{ std::min(start.x, end.x), std::min(start.y, end.y) };
        sf::Vector2f max_pos{ std::max(start.x, end.x), std::max(start.y, end.y) };

        bool swept_hit = false;
        m_wall_grid.ForEachInRangeBroadphase(center, query_radius, m_wall_query_visited, [&](FlorrBtMap::Wall* wall) {
            if (swept_hit || !wall) return;
            if (!ExpandedWallBoundsMayTouch(*wall, min_pos, max_pos, radius + wall_query_slop)) return;
            if (!SweptCircleHitsWall(start, end, radius, *wall)) return;

            swept_hit = true;
            out_wall = wall;
            out_segment = FindBlockingWallSegment(start, end, radius, *wall);
        });
        return swept_hit;
    };

    auto push_out_of_current_walls = [this, max_wall_push_passes, wall_query_slop](CEntity& entity) {
        bool destroyed_by_wall = false;
        const float wall_radius = std::max(0.f, entity.WallCollisionRadius());
        for (int pass = 0; pass < max_wall_push_passes; ++pass)
        {
            bool pushed_any = false;
            sf::Vector2f center = entity.m_pos;
            float query_radius = std::max(1.f, wall_radius + wall_query_slop);

            m_wall_grid.ForEachInRangeBroadphase(
                center, query_radius, m_wall_query_visited, [&](FlorrBtMap::Wall* wall) {
                    if (destroyed_by_wall || !wall) return;
                    if (!ExpandedWallBoundsMayTouch(*wall, entity.m_pos, entity.m_pos,
                                                    wall_radius + wall_query_slop))
                        return;

                    bool pushed = PushEntityOutOfWall(entity, *wall);
                    if (!pushed) return;

                    pushed_any = true;
                    int contact_segment = FindBlockingWallSegment(entity.m_pos, entity.m_pos, wall_radius, *wall);
                    if (HandleFiredMissileWallHit(entity, *wall, contact_segment, entity.m_pos, entity.m_pos) &&
                        entity.m_is_marked_for_des)
                    {
                        destroyed_by_wall = true;
                        return;
                    }
                    HandleBumbleBeeWallHit(entity, *wall, contact_segment, entity.m_pos, entity.m_pos);
                });

            if (destroyed_by_wall || !pushed_any) break;
        }
        return destroyed_by_wall;
    };

    for (CEntity* entity : entities)
    {
        if (!entity || entity->GameWorld() != this || !entity->CanCollide()) continue;
        if (!entity->CollidesWithWalls()) continue;
        if (auto* raw_missile = dynamic_cast<CMissile*>(entity); raw_missile && raw_missile->IsAttachedToOwner())
            continue;

        sf::Vector2f start = entity->m_prev_pos;
        sf::Vector2f end = entity->m_pos;
        const float wall_radius = std::max(0.f, entity->WallCollisionRadius());
        float travel = Distance(start, end);

        bool swept_hit = false;
        const FlorrBtMap::Wall* blocking_wall = nullptr;
        int blocking_segment = -1;
        if (travel > game_config::entity_collision_epsilon)
            swept_hit = find_swept_wall_hit(start, end, wall_radius, blocking_wall, blocking_segment);

        if (swept_hit)
        {
            if (blocking_wall && HandleBumbleBeeWallHit(*entity, *blocking_wall, blocking_segment, start, end))
            {
                continue;
            } else if (blocking_wall &&
                       HandleFiredMissileWallHit(*entity, *blocking_wall, blocking_segment, start, end))
            {
                if (entity->m_is_marked_for_des || !entity->CanCollide()) continue;
            } else
            {
                sf::Vector2f motion = end - start;
                sf::Vector2f wall_normal = { 0.f, 0.f };
                if (blocking_wall && blocking_segment >= 0)
                    wall_normal = ChooseWallNormal(*blocking_wall, blocking_segment, start, motion);
                if (LengthSq(wall_normal) <=
                    game_config::entity_collision_epsilon * game_config::entity_collision_epsilon)
                    wall_normal = NormalizeOrZero(start - end);
                if (LengthSq(wall_normal) <=
                    game_config::entity_collision_epsilon * game_config::entity_collision_epsilon)
                    wall_normal = NormalizeOrZero(-GetEntityVelocity(*entity));
                if (LengthSq(wall_normal) <=
                    game_config::entity_collision_epsilon * game_config::entity_collision_epsilon)
                    wall_normal = { 1.f, 0.f };

                sf::Vector2f slide_start = start + wall_normal * game_config::gameworld_wall_contact_skin;
                entity->m_pos = slide_start;
                if (blocking_wall)
                {
                    sf::Vector2f slide_vel = ProjectEntityVelocityAlongWall(*entity, *blocking_wall, blocking_segment);
                    if (LengthSq(slide_vel) >
                        game_config::entity_collision_epsilon * game_config::entity_collision_epsilon)
                    {
                        sf::Vector2f slide_end = slide_start + slide_vel * game_config::server_fixed_dt;
                        const FlorrBtMap::Wall* slide_wall = nullptr;
                        int slide_segment = -1;
                        if (!find_swept_wall_hit(slide_start, slide_end, wall_radius, slide_wall, slide_segment))
                        {
                            entity->m_pos = slide_end;
                        } else
                        {
                            SetEntityVelocity(*entity, { 0.f, 0.f });
                        }
                    } else
                    {
                        SetEntityVelocity(*entity, { 0.f, 0.f });
                    }
                }
            }
        }

        if (!entity->CanCollide()) continue;
        push_out_of_current_walls(*entity);
    }
}

void CGameWorld::ResolveCollisions(const std::vector<CEntity*>& entities, float dt)
{
    float large_max_radius = 0.f;
    const float large_radius_threshold =
        std::max(game_config::spatial_grid_cell_size * game_config::spatial_grid_large_radius_cell_multiplier,
                 game_config::default_horizon * game_config::spatial_grid_large_radius_horizon_multiplier);
    std::vector<CEntity*>& normal_entities = m_collision_normal_entities;
    std::vector<std::pair<float, CEntity*>>& large_entities = m_collision_large_entities;
    std::vector<CEntity*>& inactive_collision_entities = m_collision_inactive_entities;
    normal_entities.clear();
    large_entities.clear();
    inactive_collision_entities.clear();
    normal_entities.reserve(entities.size());
    large_entities.reserve(m_large_entities.size());
    inactive_collision_entities.reserve(entities.size());

    for (const CEntity* entity : entities)
    {
        if (!entity || entity->GameWorld() != this || !entity->CanCollide()) continue;
        if (entity->m_radius <= large_radius_threshold)
            normal_entities.push_back(const_cast<CEntity*>(entity));
    }

    // Large candidates must include inactive entities; only active entities become pair sources below.
    for (CEntity* entity : m_large_entities)
    {
        if (!entity || entity->GameWorld() != this || !entity->CanCollide()) continue;
        large_max_radius = std::max(large_max_radius, entity->m_radius);
        large_entities.emplace_back(entity->m_pos.x, entity);
    }
    std::sort(large_entities.begin(), large_entities.end(),
              [](const auto& lhs, const auto& rhs) { return lhs.first < rhs.first; });

    auto is_active = [this](const CEntity* entity) {
        return entity && entity->m_active_tick_marker == m_active_tick_marker;
    };

    auto process_candidate = [&](CEntity* entity, CEntity* other) {
        if (!entity || !other || entity == other) return;
        if (is_active(other) && entity->m_id >= other->m_id) return;
        if (!is_active(other)) other->m_prev_pos = other->m_pos;

        const bool overlapping = entity->IsCollision(*other);
        if (!overlapping) return;
        const bool unloaded_mob = !is_active(other) && other->IsEntityType(EEntityType::Mob);
        ResolveOverlappingEntityPair(entity, other, dt, !unloaded_mob);
        if (overlapping && !is_active(other)) inactive_collision_entities.push_back(other);
    };

    auto process_normal_candidates = [&](CEntity* entity) {
        if (!entity) return;
        const float query_radius = entity->m_radius + large_radius_threshold;
        m_spatial_grid.ForEachInRange(entity->m_pos, query_radius, [&](CEntity* other) {
            if (!other || other->m_radius > large_radius_threshold) return;
            process_candidate(entity, other);
        });
    };

    auto process_large_candidates = [&](CEntity* entity) {
        if (!entity || large_entities.empty() || large_max_radius <= 0.f) return;

        const float min_x = entity->m_pos.x - entity->m_radius - large_max_radius;
        const float max_x = entity->m_pos.x + entity->m_radius + large_max_radius;
        auto it = std::lower_bound(large_entities.begin(), large_entities.end(), min_x,
                                   [](const auto& entry, float value) { return entry.first < value; });
        for (; it != large_entities.end() && it->first <= max_x; ++it)
        {
            CEntity* large = it->second;
            if (!large || large == entity) continue;

            const float reach = entity->m_radius + large->m_radius;
            if (std::abs(entity->m_pos.y - large->m_pos.y) > reach) continue;
            process_candidate(entity, large);
        }
    };

    for (CEntity* entity : normal_entities)
    {
        process_normal_candidates(entity);
        process_large_candidates(entity);
    }

    for (CEntity* entity : entities)
    {
        if (!entity || entity->GameWorld() != this || !entity->CanCollide() ||
            entity->m_radius <= large_radius_threshold)
            continue;
        process_normal_candidates(entity);
        process_large_candidates(entity);
    }

    std::sort(inactive_collision_entities.begin(), inactive_collision_entities.end(),
              [](const CEntity* lhs, const CEntity* rhs) { return lhs->m_id < rhs->m_id; });
    inactive_collision_entities.erase(
        std::unique(inactive_collision_entities.begin(), inactive_collision_entities.end()),
        inactive_collision_entities.end());
    for (CEntity* entity : inactive_collision_entities)
    {
        if (!entity || entity->GameWorld() != this || entity->m_is_marked_for_des) continue;
        m_spatial_grid.UpdateTracked(entity);
    }
}

void CGameWorld::Cleanup()
{
    auto& clear_owned_owner_keys = m_clear_owned_owner_keys;
    auto& clear_summon_owner_keys = m_clear_summon_owner_keys;
    clear_owned_owner_keys.clear();
    clear_summon_owner_keys.clear();

    auto collect_clear_owner = [&clear_owned_owner_keys, &clear_summon_owner_keys](const CEntity* entity) {
        if (entity && entity->HasTag(EEntityTag::ClearOwnedEntitiesOnDestroy))
            clear_owned_owner_keys.emplace_back(entity->m_id, entity->m_generation);
        if (entity && entity->HasTag(EEntityTag::ClearOwnedSummonsOnDestroy))
            clear_summon_owner_keys.emplace_back(entity->m_id, entity->m_generation);
    };

    while (!m_cleanup_entities.empty())
    {
        CEntity* entity = m_cleanup_entities.back();
        m_cleanup_entities.pop_back();
        if (entity) entity->m_cleanup_index = std::numeric_limits<size_t>::max();
        if (!entity || entity->GameWorld() != this || !entity->m_is_marked_for_des) continue;

        const EEntityRemovalReason removal_reason = entity->RemovalReason();
        entity->OnBeforeRemoved(removal_reason);
        if (entity->GameWorld() != this || !entity->m_is_marked_for_des) continue;
        if (m_p_controller)
        {
            if (removal_reason == EEntityRemovalReason::Defeated)
            {
                if (auto* mob = dynamic_cast<CMobBase*>(entity)) m_p_controller->OnMobDefeated(*this, *mob);
            }
            m_p_controller->OnEntityRemoved(*this, *entity, removal_reason);
        }
        if (entity->GameWorld() != this || !entity->m_is_marked_for_des) continue;
        collect_clear_owner(entity);

        const int id = entity->m_id;
        UnregisterLiveEntity(entity);
        FreeID(id);

        if (id >= 0 && id < static_cast<int>(m_p_entities.size()) && m_p_entities[id].get() == entity)
        {
            m_p_entities[id].reset();
        } else if (id >= 0 && id < static_cast<int>(m_p_entity_refs.size()) && m_p_entity_refs[id] == entity)
        {
            m_p_entity_refs[id] = nullptr;
        } else
        {
            LOG_FATAL("gameworld", "Destroyed entity ID " + std::to_string(id) + " has no storage slot");
        }
    }

    if (clear_owned_owner_keys.empty() && clear_summon_owner_keys.empty()) return;

    auto deduplicate = [](auto& keys) {
        std::sort(keys.begin(), keys.end());
        keys.erase(std::unique(keys.begin(), keys.end()), keys.end());
    };

    deduplicate(clear_owned_owner_keys);
    deduplicate(clear_summon_owner_keys);

    auto has_owner_key = [](const auto& keys, int owner_id, std::uint64_t owner_generation) {
        if (owner_id < 0) return false;
        return std::binary_search(keys.begin(), keys.end(), std::make_pair(owner_id, owner_generation));
    };

    auto destroy_if_owned = [&clear_owned_owner_keys, &clear_summon_owner_keys, &has_owner_key](CEntity* entity) {
        auto* projectile = dynamic_cast<CProjectile*>(entity);
        if (projectile && has_owner_key(clear_owned_owner_keys, projectile->m_owner_id, projectile->m_owner_generation))
            projectile->MarkForDestroy(EEntityRemovalReason::OwnerRemoved);

        auto* mob = dynamic_cast<CMobBase*>(entity);
        if (!mob) return;

        auto* controller = dynamic_cast<CSummonedMeleeController*>(mob->GetController());
        if (controller &&
            has_owner_key(clear_summon_owner_keys, controller->GetOwnerId(), controller->GetOwnerGeneration()))
            mob->MarkForDestroy(EEntityRemovalReason::OwnerRemoved);
    };

    ForEachEntity(destroy_if_owned);
}
