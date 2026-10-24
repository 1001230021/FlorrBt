#include "gameworld.h"
#include "chain.h"
#include "../../Engine/logger.h"
#include "../../Shared/game_config.h"
#include "../../Shared/circle_contact.h"
#include "../../Shared/petal_motion.h"
#include "../../Shared/spawn_dominance.h"
#include "../Module/network_module.h"
#include "controllers/melee_controller.h"
#include "controllers/pickup_controller.h"
#include "entities/drop.h"
#include "entities/flower.h"
#include "entities/mob.h"
#include "entities/petals/petal.h"
#include "entities/portal.h"
#include "entities/projectile.h"
#include "gamecontext.h"
#include "gamecontrollers/world_controller_factory.h"
#include "player.h"
#include "state_zone.h"
#include "states/states.h"
#include "systems/lightning_humidity.h"
#include "extensions/weather_view.h"
#include "zone_mob_tools.h"
#include <algorithm>
#include <cctype>
#include <chrono>
#include <stdexcept>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iomanip>
#include <limits>
#include <random>
#include <sstream>
#include <typeinfo>
#include <unordered_set>
#include <utility>

namespace
{
constexpr float k_retire_sweep_interval = 0.5f;
constexpr int k_max_network_lightning_hops =
    static_cast<int>(std::numeric_limits<std::uint8_t>::max()) - 1;

struct crash_entity_snapshot
{
    const std::type_info* concrete_type = nullptr;
    int id = -1;
    std::uint64_t generation = 0;
    int team = 0;
    float health = 0.f;
    float radius = 0.f;
    florrbt::Vector2f pos = { 0.f, 0.f };
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
    SEntityHandle direct_owner_ref;
    SEntityHandle root_owner_ref;
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
    return entity->TickMode();
}

float Dot(florrbt::Vector2f a, florrbt::Vector2f b);
bool CollisionPairAlive(const CEntity* lhs, const CEntity* rhs);
bool CollisionPairUsableForPetalContact(const CEntity* lhs, const CEntity* rhs);
void ApplyPhysicalDamageHits(CEntity* target, float damage, CEntity* attacker, int hit_count,
                             const CEntity* collision_source = nullptr);

// A fast projectile can travel farther than its diameter in one server tick.
// Testing only the final positions then lets it tunnel through mobs. Compare
// the relative-motion segment against the combined radii so contact anywhere
// along the tick is still dispatched to the normal collision/damage path.
bool SweptProjectileTouches(const CEntity& lhs, const CEntity& rhs)
{
    if (!dynamic_cast<const CProjectile*>(&lhs) && !dynamic_cast<const CProjectile*>(&rhs)) return false;

    const florrbt::Vector2f start = lhs.PrevPos() - rhs.PrevPos();
    const florrbt::Vector2f motion = (lhs.Pos() - rhs.Pos()) - start;
    const float motion_sq = LengthSq(motion);
    float fraction = 0.f;
    if (motion_sq > game_config::entity_collision_epsilon * game_config::entity_collision_epsilon)
        fraction = std::clamp(-Dot(start, motion) / motion_sq, 0.f, 1.f);

    const florrbt::Vector2f closest = start + motion * fraction;
    const float radius = std::max(0.f, lhs.Radius()) + std::max(0.f, rhs.Radius());
    return LengthSq(closest) <= radius * radius;
}

using active_tick_view = CActiveTickView;
using player_tick_view = CPlayerTickView;

bool TryMergeActiveTickViews(active_tick_view& base, const active_tick_view& incoming)
{
    if (incoming.radius <= 0.f) return true;
    if (base.radius <= 0.f)
    {
        base = incoming;
        return true;
    }

    florrbt::Vector2f delta = incoming.center - base.center;
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
    const florrbt::Vector2f dir = delta / dist;
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
                                     florrbt::Vector2f& out_pos)
{
    if (checkpoint.w <= 0.f || checkpoint.h <= 0.f) return false;

    std::uniform_real_distribution<float> random_x(0.f, checkpoint.w);
    std::uniform_real_distribution<float> random_y(0.f, checkpoint.h);
    for (int attempt = 0; attempt < std::max(1, game_config::gameworld_transfer_spawn_position_attempts); ++attempt)
    {
        const FlorrBtMap::Point point = CheckpointLocalToWorldPoint(checkpoint, random_x(GetRng()), random_y(GetRng()));
        florrbt::Vector2f candidate = { point.x, point.y };
        if (world.CircleBlockedByWall(candidate, game_config::mob_player_flower_radius)) continue;
        out_pos = candidate;
        return true;
    }
    return false;
}

florrbt::Vector2f PickDefaultTransferSpawnPosition(const CGameWorld& world)
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
            florrbt::Vector2f pos;
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

    CGameWorld* world = const_cast<CMobBase*>(mob)->GameWorld();
    CEntity* owner = controller->GetOwner(world);
    if (!owner)
    {
        return link;
    }
    link.direct_owner_ref = owner->GetHandle();
    link.root_owner_ref = link.direct_owner_ref;
    auto* owner_projectile = dynamic_cast<CProjectile*>(owner);
    if (owner_projectile)
    {
        if (CEntity* root_owner = owner_projectile->GetOwner()) link.root_owner_ref = root_owner->GetHandle();
    }
    return link;
}

bool HasSummonOwnerLink(const summon_owner_link& link) { return link.direct_owner_ref.IsValid(); }

bool IsPetalOwnedBy(const collision_entity_view& entity, const SEntityHandle& owner_ref)
{
    CEntity* owner = entity.projectile ? entity.projectile->GetOwner() : nullptr;
    return owner && owner_ref.RefersTo(owner->GetHandle());
}

bool IsProjectileOwnerCollision(const CProjectile* projectile, const CEntity* target)
{
    if (!projectile || !target) return false;
    return projectile->GetOwner() == target;
}

bool IsSummonOwnerLink(const summon_owner_link& link, const collision_entity_view& other)
{
    if (!HasSummonOwnerLink(link) || !other.entity) return false;
    const SEntityHandle other_ref = other.entity->GetHandle();
    if (link.direct_owner_ref.RefersTo(other_ref) || link.root_owner_ref.RefersTo(other_ref)) return true;
    return IsPetalOwnedBy(other, link.root_owner_ref);
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
    return HasSummonOwnerLink(lhs_link) && HasSummonOwnerLink(rhs_link) &&
           lhs_link.root_owner_ref.RefersTo(rhs_link.root_owner_ref);
}

void ConstrainContactDisplacement(CEntity& entity, florrbt::Vector2f before)
{
    CGameWorld* world = entity.GameWorld();
    const FlorrBtMap* map = world ? world->GetMap() : nullptr;
    if (!map || map->walls.empty() || !entity.CollidesWithWalls() || entity.Pos() == before) return;
    // A huge body may separate a tiny mob by an entire corridor width. The
    // normal wall pass ran before mob contact, so don't let this push tunnel
    // through a wall between ticks. No damage/bounce callback is replayed.
    if (world->SweptCircleBlockedByWall(before, entity.Pos(), std::max(0.f, entity.WallCollisionRadius()),
                                      entity.WallCollisionMask()))
        entity.Pos() = before;
    if (auto* mob = dynamic_cast<CMobBase*>(&entity)) mob->SynchronizeEnvironment(before);
}

bool ResolveBodyContact(const collision_entity_view& lhs_view, const collision_entity_view& rhs_view,
                        float separation, float velocity_transfer = 1.f)
{
    CEntity* lhs = lhs_view.entity;
    CEntity* rhs = rhs_view.entity;
    if (!lhs || !rhs) return false;
    const auto weights = florrbt::ComputeCircleContactWeights(lhs->Mass(), rhs->Mass(),
        lhs->IsCollisionPositionLocked(), rhs->IsCollisionPositionLocked(), game_config::mob_contact_mass_exponent);
    florrbt::Vector2f lhs_static_velocity{}, rhs_static_velocity{};
    florrbt::Vector2f& lhs_velocity = lhs_view.mob ? lhs_view.mob->m_vel :
        lhs_view.projectile ? lhs_view.projectile->m_vel : lhs_static_velocity;
    florrbt::Vector2f& rhs_velocity = rhs_view.mob ? rhs_view.mob->m_vel :
        rhs_view.projectile ? rhs_view.projectile->m_vel : rhs_static_velocity;
    const florrbt::Vector2f lhs_before = lhs->Pos(), rhs_before = rhs->Pos();
    const bool contact = florrbt::ResolveCircleContact(lhs->Pos(), lhs_velocity, lhs->Radius(), weights.lhs,
        rhs->Pos(), rhs_velocity, rhs->Radius(), weights.rhs, separation, velocity_transfer);
    if (contact && separation > 0.f)
    {
        ConstrainContactDisplacement(*lhs, lhs_before);
        ConstrainContactDisplacement(*rhs, rhs_before);
    }
    return contact;
}

bool ApplyWeakOverlapCollision(const collision_entity_view& lhs_view, const collision_entity_view& rhs_view,
                               float push_multiplier)
{
    return ResolveBodyContact(lhs_view, rhs_view, push_multiplier, game_config::world_mob_overlap_velocity_retention);
}

crash_entity_snapshot CaptureEntityForCrash(const CEntity* entity)
{
    crash_entity_snapshot snapshot;
    if (!entity) return snapshot;

    snapshot.concrete_type = &typeid(*entity);
    snapshot.id = entity->Id();
    snapshot.generation = entity->Generation();
    snapshot.team = entity->Team();
    snapshot.health = entity->Health();
    snapshot.radius = entity->Radius();
    snapshot.pos = entity->Pos();
    snapshot.marked_for_destroy = entity->IsMarkedForDes();
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

bool IsCarriedEntityPair(const collision_entity_view& lhs, const collision_entity_view& rhs)
{
    auto is_carried_pair = [](const collision_entity_view& carrier_view, const collision_entity_view& carried_view) {
        const CMobBase* carrier = carrier_view.mob;
        if (!carrier) return false;
        const auto* pickup = carrier->GetPickupController();
        return pickup && pickup->IsCarrying(carrier, carried_view.entity);
    };
    return is_carried_pair(lhs, rhs) || is_carried_pair(rhs, lhs);
}

void ResolveCarryConstraints(const std::vector<CEntity*>& entities)
{
    for (CEntity* entity : entities)
    {
        auto* mob = dynamic_cast<CMobBase*>(entity);
        if (!mob || mob->IsMarkedForDes() || mob->IsDead()) continue;

        auto* pickup = mob->GetPickupController();
        if (pickup) pickup->ResolveCarriedConstraint(mob);
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
        if (!owner || owner->IsMarkedForDes() || !stats) continue;

const float horizon = std::max(0.f, stats->horizon);
        player_views.push_back({ owner->Pos(), horizon, std::max(0.f, owner->Radius()) });

        // Load range == the client's view circle: every mob inside the view is
        // activated, with no extra padding and no cap.
        AddActiveTickView(views, { owner->Pos(), horizon });
    }
}

bool IsOwnedTrapTrapperPair(const collision_entity_view& lhs, const collision_entity_view& rhs)
{
    const auto matches = [](const collision_entity_view& trap_candidate,
                            const collision_entity_view& trapper_candidate) {
        const CTrapProjectile* trap = trap_candidate.trap;
        const CPetal* trapper = trapper_candidate.IsTrapperPetal() ? trapper_candidate.petal : nullptr;
        if (!trap || !trapper) return false;
        CEntity* trap_owner = trap->GetOwner();
        CEntity* trapper_owner = trapper->GetOwner();
        return trap_owner && trap_owner == trapper_owner;
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

int ScaleCollisionHitCount(int hit_count, int multiplier)
{
    const std::int64_t hits = std::max(1, hit_count);
    const std::int64_t scale = std::max(1, multiplier);
    return static_cast<int>(
        std::min(hits * scale, static_cast<std::int64_t>(std::numeric_limits<int>::max())));
}

void DamageYggdrasilOnEnemyContact(CPetal* petal, CMobBase* mob)
{
    if (!petal || !mob || petal->GetPetalType() != EPetalType::Yggdrasil) return;
    const SMobStats* stats = mob->GetFinalStats();
    if (!stats) return;
    ApplyPhysicalDamageHits(petal, stats->damage, mob,
                            std::max(1, game_config::entity_petal_mob_hit_multiplier));
}

bool IsHealPetalType(EPetalType type) { return type == EPetalType::Rose || type == EPetalType::Dahlia; }

bool NeedsMedicRecovery(const CMobBase* mob)
{
    const SMobStats* stats = mob ? mob->GetFinalStats() : nullptr;
    if (!stats || stats->max_health <= 0.f) return false;
    if (mob->Health() < stats->max_health - game_config::entity_collision_epsilon) return true;

    const auto* flower = dynamic_cast<const CFlower*>(mob);
    const SFlowerStats* flower_stats = flower ? flower->GetFinalStats() : nullptr;
    return flower_stats && flower_stats->overheal_to_shield > game_config::entity_collision_epsilon &&
           flower->GetShield() < flower_stats->max_health - game_config::entity_collision_epsilon;
}

bool IsRoseTeamHealCollision(const collision_entity_view& lhs, const collision_entity_view& rhs)
{
    if (!lhs.entity || !rhs.entity) return false;

    if (lhs.petal && IsHealPetalType(lhs.petal->GetPetalType()) && rhs.mob &&
        lhs.petal->m_target_ref.RefersTo(rhs.entity->GetHandle()) &&
        NeedsMedicRecovery(rhs.mob) &&
        CheckTeam(GetPreCorruptionTeam(lhs.entity, lhs.InteractionMob()),
                  GetPreCorruptionTeam(rhs.entity, rhs.InteractionMob())))
        return true;
    if (rhs.petal && IsHealPetalType(rhs.petal->GetPetalType()) && lhs.mob &&
        rhs.petal->m_target_ref.RefersTo(lhs.entity->GetHandle()) &&
        NeedsMedicRecovery(lhs.mob) &&
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
    pair.same_team = CheckTeam(lhs->Team(), rhs->Team());
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
    return IsCarriedEntityPair(pair.lhs, pair.rhs) || IsOwnedTrapTrapperPair(pair.lhs, pair.rhs) ||
           ShareMultiSegmentAssembly(pair.lhs.entity, pair.rhs.entity);
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
    if (pair.team_heal) return false;
    if (!pair.same_team) return false;
    // Summons phase through same-team flowers: they only block enemies.
    if (pair.lhs.flower && HasSummonOwnerLink(pair.rhs_summon)) return true;
    if (pair.rhs.flower && HasSummonOwnerLink(pair.lhs_summon)) return true;
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
    // Wax is a physical obstacle: it collides with every flower regardless of team.
    if (pair.HasWax() && (pair.lhs.flower || pair.rhs.flower)) return true;
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

    // Wax uses mob mass weighting, with extra passive resistance. Flowers
    // retain their mass-independent right of way through Wax.
    if (ResolveWaxMobHardCollision(pair.lhs, pair.rhs)) return;

    // All mobs transfer relative normal momentum, rather than destroying each
    // body's inward speed regardless of mass. Swarms keep softer separation.
    if (pair.lhs.mob && pair.rhs.mob)
    {
        const bool nonflower_mob_pair = !pair.lhs.flower && !pair.rhs.flower;
        ResolveBodyContact(pair.lhs, pair.rhs, nonflower_mob_pair ?
            game_config::nonflower_mob_overlap_push_multiplier : game_config::mob_overlap_push_multiplier);
        return;
    }

    if (pair.lhs.missile && pair.lhs.missile->IsAttachedToOwner()) pair.rhs.entity->OnCollision(pair.lhs.entity);
    else pair.lhs.entity->OnCollision(pair.rhs.entity);
}

bool CanResolveCollisionDamage(const collision_pair_context& pair) { return !pair.same_team || pair.team_heal; }

florrbt::Vector2f WallPoint(const FlorrBtMap::Point& point) { return { point.x, point.y }; }

float Dot(florrbt::Vector2f a, florrbt::Vector2f b) { return a.x * b.x + a.y * b.y; }

float Cross(florrbt::Vector2f a, florrbt::Vector2f b) { return a.x * b.y - a.y * b.x; }

florrbt::Vector2f NormalizeOrZero(florrbt::Vector2f value)
{
    float len = Length(value);
    if (len <= game_config::entity_collision_epsilon) return { 0.f, 0.f };
    return value / len;
}

florrbt::Vector2f GetEntityVelocity(CEntity& entity)
{
    if (auto* mob = dynamic_cast<CMobBase*>(&entity)) return mob->m_vel;
    if (auto* projectile = dynamic_cast<CProjectile*>(&entity)) return projectile->m_vel;
    return { 0.f, 0.f };
}

void SetEntityVelocity(CEntity& entity, florrbt::Vector2f velocity)
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

    if (!wax || !mob) return false;

    florrbt::Vector2f delta = mob->Pos() - wax->Pos();
    float distance = Length(delta);
    float overlap = mob->Radius() + wax->Radius() - distance;
    if (overlap <= 0.f) return true;

    florrbt::Vector2f normal = NormalizeOrZero(delta);
    if (LengthSq(normal) <= game_config::entity_collision_epsilon * game_config::entity_collision_epsilon)
        normal = NormalizeOrZero(mob->PrevPos() - wax->Pos());
    if (LengthSq(normal) <= game_config::entity_collision_epsilon * game_config::entity_collision_epsilon)
        normal = NormalizeOrZero(-GetEntityVelocity(*mob));
    if (LengthSq(normal) <= game_config::entity_collision_epsilon * game_config::entity_collision_epsilon)
        normal = { 1.f, 0.f };

    if (flower)
    {
        // Flowers push wax: only the wax moves out of the way, the flower stays put.
        wax->Pos() -= normal * (overlap + game_config::entity_collision_epsilon);
        florrbt::Vector2f wax_vel = GetEntityVelocity(*wax);
        float wax_inward_speed = Dot(wax_vel, normal);
        if (wax_inward_speed > 0.f) SetEntityVelocity(*wax, wax_vel - normal * wax_inward_speed);
        return true;
    }

    const auto wax_velocity = GetEntityVelocity(*wax);
    const float wax_approach = Dot(wax_velocity, normal);
    const float mob_approach = Dot(mob->m_vel, -normal);
    // Wax-Mob contact keeps ordinary raw-mass weighting.  The stronger
    // mass-exponent processing is for regular mob contacts and must not make
    // a high-rarity Wax behave like an effectively immovable body here.
    // Whenever either body is driving into the contact, Wax gets priority:
    // Mob absorbs most of the separation and impulse, while Wax is moved only
    // by the small configured fraction.
    const auto weights = florrbt::ComputeWaxContactWeights(wax->Mass(), mob->Mass(),
        mob->IsCollisionPositionLocked(), 1.f,
        wax_approach > 0.f || mob_approach > 0.f, game_config::default_wax_passive_push_fraction);
    const auto wax_before = wax->Pos(), mob_before = mob->Pos();
    auto velocity = wax_velocity;
    florrbt::ResolveCircleContact(wax->Pos(), velocity, wax->Radius(), weights.lhs,
        mob->Pos(), mob->m_vel, mob->Radius(), weights.rhs);
    SetEntityVelocity(*wax, velocity);
    ConstrainContactDisplacement(*wax, wax_before);
    ConstrainContactDisplacement(*mob, mob_before);
    return true;
}

bool BounceFiredCarrotFromEntity(CPetal* petal, const CEntity* target)
{
    if (!petal || petal->GetPetalType() != EPetalType::Carrot) return false;
    auto* missile = dynamic_cast<CMissilePetal*>(petal);
    if (!missile || !target || !missile->m_fired) return false;

    florrbt::Vector2f normal = NormalizeOrZero(missile->Pos() - target->Pos());
    if (LengthSq(normal) <= game_config::entity_collision_epsilon) normal = NormalizeOrZero(-missile->m_vel);
    if (LengthSq(normal) <= game_config::entity_collision_epsilon) normal = { 1.f, 0.f };

    if (Dot(missile->m_vel, normal) > 0.f) normal = -normal;

    florrbt::Vector2f old_vel = missile->m_vel;
    float speed = Length(old_vel);
    float into_entity = Dot(old_vel, normal);
    missile->Pos() =
        target->Pos() + normal * (target->Radius() + missile->Radius() + game_config::entity_projectile_contact_skin);
    missile->m_vel = old_vel - normal * (2.f * into_entity);
    if (LengthSq(missile->m_vel) <= game_config::entity_collision_epsilon &&
        speed > game_config::entity_collision_epsilon)
        missile->m_vel = normal * speed;
    if (LengthSq(missile->m_vel) > game_config::entity_collision_epsilon)
    {
        missile->FacingAngle() = std::atan2(missile->m_vel.y, missile->m_vel.x);
        missile->HasFacing() = true;
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

int CalculatePetalHitCount(CPetal* petal, const CEntity* target, float dt)
{
    if (!petal || !target || dt <= 0.f) return 1;
    if (petal->GetPetalType() == EPetalType::Carrot || petal->GetPetalType() == EPetalType::Missile)
    {
        if (const auto* missile = dynamic_cast<const CMissilePetal*>(petal); missile && missile->m_fired) return 1;
    }

    float reload = EffectivePetalReload(petal);
    if (reload <= game_config::entity_collision_epsilon || reload >= dt) return 1;

    const SEntityHandle target_handle = target->GetHandle();
    if (!target_handle.IsValid()) return 1;

    float& credit = petal->m_hit_credits[target_handle];
    float total = credit + dt / reload;
    int hits = static_cast<int>(std::floor(total));
    credit = total - static_cast<float>(hits);
    return std::clamp(hits, 1, std::max(1, game_config::entity_max_hits_per_tick));
}

bool UsesPetalPhysicalHitCompensation(const CPetal* petal)
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

    florrbt::Vector2f attacker_delta = attacker->Pos() - attacker->PrevPos();
    florrbt::Vector2f target_delta = target->Pos() - target->PrevPos();
    float relative_sweep = Length(attacker_delta - target_delta);
    float hit_spacing =
        std::max(game_config::entity_hit_spacing_min,
                 std::min(attacker->Radius(), target->Radius()) * game_config::entity_hit_spacing_radius_multiplier);

    int hits = 1 + static_cast<int>(std::floor(relative_sweep / hit_spacing));
    return std::clamp(hits, 1, std::max(1, game_config::entity_max_physical_hits_per_tick));
}

int CalculatePetalPhysicalHitCount(CPetal* petal, const CEntity* target, float dt)
{
    int hit_count = CalculatePetalHitCount(petal, target, dt);
    if (UsesPetalPhysicalHitCompensation(petal))
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
        if (target->IsDead() || target->Health() <= 0.f) break;
        target->TakeDamage(damage, attacker, EDamageType::Normal);
    }
}

float CalculatePetalContactDamage(CPetal* petal, CEntity* target)
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
        if (!missile || !missile->IsAttachedToOwner() || missile->IsMarkedForDes()) continue;
        if (!missile->RefreshAttachedTransform()) missile->MarkForDestroy(EEntityRemovalReason::OwnerRemoved);
    }
}

void ApplyPetalMobContact(CPetal* petal, CMobBase* mob, float dt)
{
    if (!petal || !mob) return;

    const int hit_multiplier = std::max(1, game_config::entity_petal_mob_hit_multiplier);
    const int petal_hit_count = ScaleCollisionHitCount(
        CollisionDamageHitCount(petal, CalculatePetalPhysicalHitCount(petal, mob, dt)), hit_multiplier);
    const int mob_hit_count = ScaleCollisionHitCount(PhysicalHitCountForTick(mob, petal), hit_multiplier);
    const int exchange_count = std::max(petal_hit_count, mob_hit_count);
    const SMobStats* mob_stats = mob->GetFinalStats();
    bool petal_can_deal_damage = true;

    for (int hit = 0; hit < exchange_count; ++hit)
    {
        if (!CollisionPairUsableForPetalContact(petal, mob)) break;

        // Decide both sides of this contact before applying either side's
        // damage.  A lethal Stinger hit marks the mob for destruction
        // immediately; checking CollisionPairAlive between the two damage
        // calls used to skip the mob's retaliation and made the Stinger
        // survive some one-shot contacts for free.  MarkForDestroy prevents
        // subsequent contacts, but must not erase the current exchange.
        float petal_damage = 0.f;
        if (petal_can_deal_damage && hit < petal_hit_count)
        {
            petal_damage = CalculatePetalContactDamage(petal, mob);
            if (petal_damage <= 0.f) petal_can_deal_damage = false;
        }

        const float mob_damage = mob_stats && mob_stats->damage > 0.f && hit < mob_hit_count
                                     ? mob_stats->damage
                                     : 0.f;

        // Both values were decided while the pair was alive.  Apply both
        // sides even if the first TakeDamage call marks one endpoint for
        // removal; the pair is filtered out at the top of the next iteration.
        if (petal_damage > 0.f) mob->TakeDamage(petal_damage, petal->GetOwner(), EDamageType::Normal);
        if (mob_damage > 0.f) petal->TakeDamage(mob_damage, mob, EDamageType::Normal);
    }
}

void ApplyPetalPetalContact(CPetal* lhs, CPetal* rhs, float dt)
{
    if (!lhs || !rhs) return;

    const int lhs_hit_count = CollisionDamageHitCount(lhs, CalculatePetalPhysicalHitCount(lhs, rhs, dt));
    const int rhs_hit_count = CollisionDamageHitCount(rhs, CalculatePetalPhysicalHitCount(rhs, lhs, dt));
    const int exchange_count = std::max(lhs_hit_count, rhs_hit_count);
    bool lhs_can_deal_damage = true;
    bool rhs_can_deal_damage = true;

    for (int hit = 0; hit < exchange_count; ++hit)
    {
        if (!CollisionPairAlive(lhs, rhs)) break;

        float lhs_damage = 0.f;
        if (lhs_can_deal_damage && hit < lhs_hit_count)
        {
            lhs_damage = CalculatePetalContactDamage(lhs, rhs);
            if (lhs_damage <= 0.f) lhs_can_deal_damage = false;
        }

        float rhs_damage = 0.f;
        if (rhs_can_deal_damage && hit < rhs_hit_count)
        {
            rhs_damage = CalculatePetalContactDamage(rhs, lhs);
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
        const int hit_count = CalculatePetalPhysicalHitCount(petal, missile, dt);
        ApplyPhysicalDamageHits(missile, CalculatePetalContactDamage(petal, missile), petal->GetOwner(), hit_count, petal);
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
        if (target.entity->IsDead() || target.entity->IsMarkedForDes()) break;
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
        if (target.entity->IsDead() || target.entity->IsMarkedForDes()) break;
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
        retaliation = CalculatePetalContactDamage(petal, pollen);
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
    return lhs && rhs && !lhs->IsMarkedForDes() && !rhs->IsMarkedForDes() && lhs->CanCollide() &&
           rhs->CanCollide() && lhs->GameWorld() && lhs->GameWorld() == rhs->GameWorld();
}

bool IsPetalMobContactPair(const CEntity* lhs, const CEntity* rhs)
{
    return (dynamic_cast<const CPetal*>(lhs) && dynamic_cast<const CMobBase*>(rhs)) ||
           (dynamic_cast<const CMobBase*>(lhs) && dynamic_cast<const CPetal*>(rhs));
}

// A mob marked for removal is no longer a live physical participant, but it
// must still finish a flower-petal contact that was already detected this
// tick. Keep this exception narrow: marked entities do not re-enter normal
// mob collision, seeking, or physics.
bool CollisionPairUsableForPetalContact(const CEntity* lhs, const CEntity* rhs)
{
    if (!lhs || !rhs || lhs == rhs || !lhs->CanCollide() || !rhs->CanCollide() || !lhs->GameWorld() ||
        lhs->GameWorld() != rhs->GameWorld())
        return false;
    if ((lhs->IsMarkedForDes() || rhs->IsMarkedForDes()) && !IsPetalMobContactPair(lhs, rhs)) return false;
    return true;
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

void ResolveOverlappingEntityPair(CEntity* entity, CEntity* other, float dt, bool resolve_physical_collision,
                                  std::vector<std::pair<CMobBase*, CMobBase*>>* mob_contacts)
{
    if (!CollisionPairUsableForPetalContact(entity, other)) return;

    const collision_pair_context pair = BuildCollisionPairContext(entity, other);

    // 1. World-state filters run before every physical or damage response.
    if (ShouldIgnorePhaseCollision(pair)) return;

    // 2. Constraint and ownership filters: bound entities cannot collide with their anchors or owners.
    if (ShouldIgnoreConstraintCollision(pair) || ShouldIgnoreOwnershipCollision(pair)) return;

    // 3. Team filters: friendlies phase unless the pair is an explicit team-heal interaction.
    if (ShouldIgnoreFriendlyCollision(pair)) return;

    SetLastCollisionCrashContext(entity, other);
    // Bound only the extra pressure work; normal contact/damage still runs for
    // every pair if this budget is exhausted in an unusually dense scene.
    if (mob_contacts && mob_contacts->size() < 65536 && resolve_physical_collision && pair.lhs.mob && pair.rhs.mob &&
        CanResolvePhysicalCollision(pair))
        mob_contacts->emplace_back(pair.lhs.mob, pair.rhs.mob);
    if (ShouldResolveWeakSummonCollision(pair))
    {
        if (resolve_physical_collision)
            ApplyWeakOverlapCollision(pair.lhs, pair.rhs, game_config::world_mob_overlap_weak_push);
        return;
    }

    // 4. Physical response is independent from damage eligibility.
    // A marked mob may finish the current petal damage exchange, but it must
    // not be pushed or participate in another physical collision before
    // Cleanup removes it.
    if (resolve_physical_collision && !entity->IsMarkedForDes() && !other->IsMarkedForDes())
        ResolvePhysicalCollision(pair);

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
        const int mob_hits_per_tick = std::max(1, game_config::entity_mob_mob_hits_per_tick);
        const int entity_hit_count = std::max(PhysicalHitCountForTick(entity, other), mob_hits_per_tick);
        const int other_hit_count = std::max(PhysicalHitCountForTick(other, entity), mob_hits_per_tick);

        // Snapshot both attacks first so ID/order cannot suppress a lethal counter-hit.
        if (entity_stats && entity_stats->damage > 0.f)
            ApplyPhysicalDamageHits(other, entity_stats->damage, entity, entity_hit_count);
        if (other_stats && other_stats->damage > 0.f)
            ApplyPhysicalDamageHits(entity, other_stats->damage, other, other_hit_count);

        if (!other->IsDead() && !other->IsMarkedForDes()) ApplyBodyPoison(entity_mob, pair.lhs.flower, other_mob);
        if (!entity->IsDead() && !entity->IsMarkedForDes()) ApplyBodyPoison(other_mob, pair.rhs.flower, entity_mob);
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

florrbt::Vector2f ClosestPointOnSegment(florrbt::Vector2f point, florrbt::Vector2f a, florrbt::Vector2f b)
{
    florrbt::Vector2f ab = b - a;
    float len_sq = LengthSq(ab);
    if (len_sq <= game_config::entity_collision_epsilon) return a;
    float t = std::clamp(Dot(point - a, ab) / len_sq, 0.f, 1.f);
    return a + ab * t;
}

bool SegmentIntersectsSegment(florrbt::Vector2f a, florrbt::Vector2f b, florrbt::Vector2f c, florrbt::Vector2f d)
{
    florrbt::Vector2f r = b - a;
    florrbt::Vector2f s = d - c;
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

float SegmentDistanceSq(florrbt::Vector2f a, florrbt::Vector2f b, florrbt::Vector2f c, florrbt::Vector2f d)
{
    if (SegmentIntersectsSegment(a, b, c, d)) return 0.f;
    float best = DistanceSq(a, ClosestPointOnSegment(a, c, d));
    best = std::min(best, DistanceSq(b, ClosestPointOnSegment(b, c, d)));
    best = std::min(best, DistanceSq(c, ClosestPointOnSegment(c, a, b)));
    best = std::min(best, DistanceSq(d, ClosestPointOnSegment(d, a, b)));
    return best;
}

bool PointInWallPolygon(florrbt::Vector2f point, const FlorrBtMap::Wall& wall)
{
    if (!wall.closed || wall.vertices.size() < 3) return false;

    bool inside = false;
    size_t count = wall.vertices.size();
    for (size_t i = 0, j = count - 1; i < count; j = i++)
    {
        florrbt::Vector2f a = WallPoint(wall.vertices[i]);
        florrbt::Vector2f b = WallPoint(wall.vertices[j]);
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

bool ExpandedWallBoundsMayTouch(const FlorrBtMap::Wall& wall, florrbt::Vector2f min_pos, florrbt::Vector2f max_pos, float padding)
{
    return max_pos.x + padding >= wall.min_x && min_pos.x - padding <= wall.max_x &&
           max_pos.y + padding >= wall.min_y && min_pos.y - padding <= wall.max_y;
}

bool SweptCircleHitsWall(florrbt::Vector2f start, florrbt::Vector2f end, float radius, const FlorrBtMap::Wall& wall)
{
    florrbt::Vector2f min_pos{ std::min(start.x, end.x), std::min(start.y, end.y) };
    florrbt::Vector2f max_pos{ std::max(start.x, end.x), std::max(start.y, end.y) };
    if (!ExpandedWallBoundsMayTouch(wall, min_pos, max_pos, radius)) return false;

    float radius_sq = radius * radius;
    int segment_count = WallSegmentCount(wall);
    for (int i = 0; i < segment_count; ++i)
    {
        florrbt::Vector2f a = WallPoint(wall.vertices[i]);
        florrbt::Vector2f b = WallPoint(wall.vertices[(i + 1) % wall.vertices.size()]);
        if (SegmentDistanceSq(start, end, a, b) <= radius_sq) return true;
    }

    return PointInWallPolygon(start, wall) || PointInWallPolygon(end, wall);
}

int FindBlockingWallSegment(florrbt::Vector2f start, florrbt::Vector2f end, float radius, const FlorrBtMap::Wall& wall)
{
    float radius_sq = radius * radius;
    int segment_count = WallSegmentCount(wall);
    int best_segment = -1;
    float best_dist_sq = std::numeric_limits<float>::max();
    for (int i = 0; i < segment_count; ++i)
    {
        florrbt::Vector2f a = WallPoint(wall.vertices[i]);
        florrbt::Vector2f b = WallPoint(wall.vertices[(i + 1) % wall.vertices.size()]);
        float dist_sq = SegmentDistanceSq(start, end, a, b);
        if (dist_sq <= radius_sq && dist_sq < best_dist_sq)
        {
            best_dist_sq = dist_sq;
            best_segment = i;
        }
    }
    return best_segment;
}

int FindClosestWallSegment(florrbt::Vector2f point, const FlorrBtMap::Wall& wall)
{
    int segment_count = WallSegmentCount(wall);
    int best_segment = -1;
    float best_dist_sq = std::numeric_limits<float>::max();
    for (int i = 0; i < segment_count; ++i)
    {
        florrbt::Vector2f a = WallPoint(wall.vertices[i]);
        florrbt::Vector2f b = WallPoint(wall.vertices[(i + 1) % wall.vertices.size()]);
        float dist_sq = DistanceSq(point, ClosestPointOnSegment(point, a, b));
        if (dist_sq < best_dist_sq)
        {
            best_dist_sq = dist_sq;
            best_segment = i;
        }
    }
    return best_segment;
}

florrbt::Vector2f WallSegmentTangent(const FlorrBtMap::Wall& wall, int segment_index)
{
    if (segment_index < 0 || wall.vertices.empty()) return { 0.f, 0.f };
    florrbt::Vector2f a = WallPoint(wall.vertices[segment_index]);
    florrbt::Vector2f b = WallPoint(wall.vertices[(segment_index + 1) % wall.vertices.size()]);
    return NormalizeOrZero(b - a);
}

florrbt::Vector2f ProjectEntityVelocityAlongWall(CEntity& entity, const FlorrBtMap::Wall& wall, int segment_index)
{
    florrbt::Vector2f tangent = WallSegmentTangent(wall, segment_index);
    if (LengthSq(tangent) <= game_config::entity_collision_epsilon) return { 0.f, 0.f };

    florrbt::Vector2f velocity = GetEntityVelocity(entity);
    florrbt::Vector2f projected = tangent * Dot(velocity, tangent);
    SetEntityVelocity(entity, projected);
    return projected;
}

florrbt::Vector2f ChooseWallNormal(const FlorrBtMap::Wall& wall, int segment_index, florrbt::Vector2f point,
                              florrbt::Vector2f motion = { 0.f, 0.f })
{
    florrbt::Vector2f a = WallPoint(wall.vertices[segment_index]);
    florrbt::Vector2f b = WallPoint(wall.vertices[(segment_index + 1) % wall.vertices.size()]);
    florrbt::Vector2f edge = b - a;
    florrbt::Vector2f n1 = NormalizeOrZero({ -edge.y, edge.x });
    if (LengthSq(n1) <= game_config::entity_collision_epsilon) return { 1.f, 0.f };
    florrbt::Vector2f n2 = -n1;

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

bool HandleFiredMissileWallHit(CEntity& entity, const FlorrBtMap::Wall& wall, int segment_index, florrbt::Vector2f start,
                               florrbt::Vector2f end)
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

        florrbt::Vector2f motion = end - start;
        if (LengthSq(motion) <= game_config::entity_collision_epsilon * game_config::entity_collision_epsilon)
            motion = missile->m_vel;
        florrbt::Vector2f normal =
            segment_index >= 0 ? ChooseWallNormal(wall, segment_index, start, motion) : NormalizeOrZero(start - end);
        if (LengthSq(normal) <= game_config::entity_collision_epsilon) normal = NormalizeOrZero(-missile->m_vel);
        if (LengthSq(normal) <= game_config::entity_collision_epsilon) normal = { 1.f, 0.f };

        if (Dot(missile->m_vel, normal) > 0.f) normal = -normal;

        florrbt::Vector2f old_vel = missile->m_vel;
        float speed = Length(old_vel);
        float into_wall = Dot(old_vel, normal);
        missile->Pos() = start + normal * game_config::gameworld_wall_contact_skin;
        missile->m_vel = old_vel - normal * (2.f * into_wall);
        if (LengthSq(missile->m_vel) <= game_config::entity_collision_epsilon &&
            speed > game_config::entity_collision_epsilon)
            missile->m_vel = normal * speed;
        if (LengthSq(missile->m_vel) > game_config::entity_collision_epsilon)
        {
            missile->FacingAngle() = std::atan2(missile->m_vel.y, missile->m_vel.x);
            missile->HasFacing() = true;
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

bool HandleBumbleBeeWallHit(CEntity& entity, const FlorrBtMap::Wall& wall, int segment_index, florrbt::Vector2f start,
                            florrbt::Vector2f end)
{
    auto* mob = dynamic_cast<CMobBase*>(&entity);
    if (!mob || mob->GetMobType() != EMobType::BumbleBee) return false;

    const float wall_radius = std::max(0.f, entity.WallCollisionRadius());
    if (segment_index < 0) segment_index = FindBlockingWallSegment(start, end, wall_radius, wall);

    florrbt::Vector2f motion = end - start;
    if (LengthSq(motion) <= game_config::entity_collision_epsilon * game_config::entity_collision_epsilon)
        motion = mob->m_vel;
    florrbt::Vector2f normal =
        segment_index >= 0 ? ChooseWallNormal(wall, segment_index, start, motion) : NormalizeOrZero(start - end);
    if (LengthSq(normal) <= game_config::entity_collision_epsilon) normal = NormalizeOrZero(-mob->m_vel);
    if (LengthSq(normal) <= game_config::entity_collision_epsilon) normal = { 1.f, 0.f };

    if (Dot(mob->m_vel, normal) > 0.f) normal = -normal;

    florrbt::Vector2f old_vel = mob->m_vel;
    float speed = Length(old_vel);
    if (speed <= game_config::entity_collision_epsilon)
    {
        const SMobStats* stats = mob->GetFinalStats();
        speed = stats ? stats->max_velocity : game_config::default_max_velocity;
        old_vel = -normal * speed;
    }

    float into_wall = Dot(old_vel, normal);
    mob->Pos() = start + normal * (wall_radius * game_config::gameworld_wall_contact_radius_multiplier +
                                   game_config::gameworld_wall_contact_skin);
    mob->m_vel = old_vel - normal * (2.f * into_wall);
    if (LengthSq(mob->m_vel) <= game_config::entity_collision_epsilon) mob->m_vel = normal * speed;

    mob->FacingAngle() = std::atan2(mob->m_vel.y, mob->m_vel.x);
    mob->HasFacing() = true;
    return true;
}

bool PushEntityOutOfWall(CEntity& entity, const FlorrBtMap::Wall& wall)
{
    const float wall_radius = std::max(0.f, entity.WallCollisionRadius());
    if (!ExpandedWallBoundsMayTouch(wall, entity.Pos(), entity.Pos(), wall_radius)) return false;

    float best_dist_sq = std::numeric_limits<float>::max();
    florrbt::Vector2f best_point = entity.Pos();
    int best_segment = -1;
    int segment_count = WallSegmentCount(wall);
    for (int i = 0; i < segment_count; ++i)
    {
        florrbt::Vector2f a = WallPoint(wall.vertices[i]);
        florrbt::Vector2f b = WallPoint(wall.vertices[(i + 1) % wall.vertices.size()]);
        florrbt::Vector2f closest = ClosestPointOnSegment(entity.Pos(), a, b);
        float dist_sq = DistanceSq(entity.Pos(), closest);
        if (dist_sq < best_dist_sq)
        {
            best_dist_sq = dist_sq;
            best_point = closest;
            best_segment = i;
        }
    }
    if (best_segment < 0) return false;

    bool inside = PointInWallPolygon(entity.Pos(), wall);
    float dist = std::sqrt(std::max(0.f, best_dist_sq));
    if (!inside && dist >= wall_radius) return false;

    florrbt::Vector2f motion = entity.Pos() - entity.PrevPos();
    if (LengthSq(motion) <= game_config::entity_collision_epsilon * game_config::entity_collision_epsilon)
        motion = GetEntityVelocity(entity);
    florrbt::Vector2f normal =
        inside ? ChooseWallNormal(wall, best_segment, best_point, motion) : NormalizeOrZero(entity.Pos() - best_point);
    if (LengthSq(normal) <= game_config::entity_collision_epsilon)
        normal = ChooseWallNormal(wall, best_segment, entity.Pos(), motion);

    float penetration = inside ? wall_radius + dist : wall_radius - dist;
    entity.Pos() += normal * (penetration + game_config::gameworld_wall_contact_skin);

    florrbt::Vector2f velocity = GetEntityVelocity(entity);
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
          game_config::spatial_grid_cell_size, [](const CEntity& entity) { return entity.Pos(); },
          [](const CEntity& entity) { return entity.Id(); }, [this](int id) { return GetEntity(id); },
          [](const CEntity& entity) { return entity.Id() >= 0; }),
      m_wall_grid(
          game_config::spatial_grid_cell_size,
          [](const FlorrBtMap::Wall& wall) { return florrbt::Vector2f{ wall.center.x, wall.center.y }; },
          [](const FlorrBtMap::Wall& wall) { return wall.id; }, [this](int id) { return GetWall(id); },
          [](const FlorrBtMap::Wall& wall) { return wall.id >= 0; })
{
    m_normal_entity_radius_limit =
        std::max(game_config::spatial_grid_cell_size * game_config::spatial_grid_large_radius_cell_multiplier,
                 game_config::default_horizon * game_config::spatial_grid_large_radius_horizon_multiplier);
    if (!m_map) LOG_ERROR("gameworld", "Failed to load map: " + path);
    if (m_map) m_spawn_zone_bounds = florrbt::BuildSpawnZoneBounds(m_map->zones);
    BuildWallGrid();
    SpawnMapPortals();
    m_extension_config = map_declarations::ExtensionsForMap(m_map_path);
    SetController(CreateWorldController(map_declarations::ControllerKeyForMap(m_map_path)));
}

CGameWorld::~CGameWorld()
{
    // Controller teardown belongs to the world lifetime, not to whichever
    // module happened to erase the world. This keeps room destruction,
    // admin destruction and server shutdown on the same lifecycle path.
    if (m_p_controller)
    {
        m_p_controller->OnDeactivate(*this);
        m_p_controller.reset();
    }

    // A player stores the world used to resolve its owned entity.  Detach it
    // before this world frees its entities; otherwise the player can retain a
    // dangling CGameWorld pointer and the next network/talent tick may call
    // into freed world storage.
    if (m_p_game_context)
    {
        for (const auto& player : m_p_game_context->Players())
        {
            if (!player) continue;
            player->ClearOwnedEntityFromWorld(this);
        }
    }
    m_cleanup_entities.clear();
    m_always_tick_entities.clear();
    m_conditional_tick_entities.clear();
    m_large_entities.clear();
    m_live_entities.clear();
    m_p_entities.clear();
    if (m_p_game_context)
        m_p_game_context->Extensions().DetachWorld({GetId(), GetInstanceGeneration()});
}

void CGameWorld::SetGameContext(CGameContext* context)
{
    if (m_transition_active) return;
    if (m_p_game_context == context) return;
    if (m_p_game_context) m_p_game_context->Extensions().DetachWorld({GetId(), GetInstanceGeneration()});
    m_p_game_context = context;
    m_extensions = context ? context->Extensions().AttachWorld(*this, m_extension_config) : CExtensionView{};
}

void CGameWorld::ConfigureExtensions(SWorldExtensionConfig config)
{
    if (m_transition_active) return;
    if (m_p_game_context) m_extensions = m_p_game_context->Extensions().AttachWorld(*this, config);
    m_extension_config = config;
}

std::vector<CPlayer*> CGameWorld::Players() const
{
    std::vector<CPlayer*> result;
    if (m_p_game_context)
        for (const auto& player : m_p_game_context->Players())
            if (player && player->GetEntity() && player->GetEntity()->GameWorld() == this)
                result.push_back(player.get());
    return result;
}

void CGameWorld::SetController(std::unique_ptr<IGameController> controller, bool notify_players)
{
    if (m_transition_active) return;
    const auto players = notify_players ? Players() : std::vector<CPlayer*>{};
    std::vector<bool*> participants{&m_transition_active};
    participants.reserve(players.size() + 1);
    for (auto* player : players) participants.push_back(&player->m_transition_active);
    struct CGuard
    {
        const std::vector<bool*>& flags;
        CGuard(const std::vector<bool*>& values) : flags(values) { for (auto* value : flags) *value = true; }
        ~CGuard() { for (auto* value : flags) *value = false; }
    } guard(participants);
    if (m_p_controller)
        for (auto* player : players)
            m_p_controller->OnPlayerDepartureCommitted(*this, *player, player->GetEntity());
    if (m_p_controller) m_p_controller->OnDeactivate(*this);
    m_p_controller = std::move(controller);
    if (m_p_controller) m_p_controller->OnActivate(*this);
    for (auto* player : players)
    {
        if (!player->SynchronizeGameplayScope(*this))
            throw std::runtime_error("Cannot initialize controller progress scope");
        if (m_p_controller) m_p_controller->OnPlayerArrivalCommitted(*this, *player);
        if (m_p_game_context) m_p_game_context->Network().NotifyPlayerRulesChanged(*player);
    }
}

int CGameWorld::GetNewID()
{
    while (!m_free_ids.empty())
    {
        int id = m_free_ids.back();
        m_free_ids.pop_back();
        if (id < 0 || id >= static_cast<int>(m_id_in_use.size()) || m_id_in_use[id])
        {
            LOG_FATAL("gameworld", "Corrupt free ID " + std::to_string(id));
            continue;
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
    if (auto* chain = dynamic_cast<CChainEntity*>(entity)) m_chains.push_back(chain);
    entity->LiveIndex() = m_live_entities.size();
    m_live_entities.push_back(entity);
    m_spatial_grid.InsertTracked(entity);
    if (entity->IsMarkedForDes()) QueueEntityForCleanup(entity);
    SyncAlwaysTickMembership(entity);
    SyncConditionalTickMembership(entity);
    SyncLargeEntityMembership(entity);
}

void CGameWorld::UnregisterLiveEntity(CEntity* entity)
{
    if (!entity) return;
    if (auto* chain = dynamic_cast<CChainEntity*>(entity)) std::erase(m_chains, chain);
    m_spawn_habitat_cache.erase(entity->Id());
    m_spatial_grid.RemoveTracked(entity->Id());
    RemoveQueuedCleanupEntity(entity);
    RemoveAlwaysTickEntity(entity);
    RemoveConditionalTickEntity(entity);
    RemoveLargeEntity(entity);

    const size_t index = entity->LiveIndex();
    if (index >= m_live_entities.size() || m_live_entities[index] != entity)
    {
        LOG_FATAL("gameworld", "Invalid live entity index for ID " + std::to_string(entity->Id()));
        return;
    }

    CEntity* moved = m_live_entities.back();
    m_live_entities[index] = moved;
    moved->LiveIndex() = index;
    m_live_entities.pop_back();
    entity->LiveIndex() = std::numeric_limits<size_t>::max();
}

void CGameWorld::QueueEntityForCleanup(CEntity* entity)
{
    if (!entity || entity->GameWorld() != this) return;

    const size_t index = entity->CleanupIndex();
    if (index < m_cleanup_entities.size() && m_cleanup_entities[index] == entity) return;

    entity->CleanupIndex() = m_cleanup_entities.size();
    m_cleanup_entities.push_back(entity);
}

void CGameWorld::RemoveQueuedCleanupEntity(CEntity* entity)
{
    if (!entity) return;
    const size_t index = entity->CleanupIndex();
    if (index >= m_cleanup_entities.size() || m_cleanup_entities[index] != entity)
    {
        entity->CleanupIndex() = std::numeric_limits<size_t>::max();
        return;
    }

    CEntity* moved = m_cleanup_entities.back();
    m_cleanup_entities[index] = moved;
    moved->CleanupIndex() = index;
    m_cleanup_entities.pop_back();
    entity->CleanupIndex() = std::numeric_limits<size_t>::max();
}

void CGameWorld::SyncAlwaysTickMembership(CEntity* entity)
{
    if (!entity) return;

    const EEntityTickMode tick_mode = EffectiveTickMode(entity);
    const bool should_tick_always = tick_mode == EEntityTickMode::Always ||
                                    (tick_mode == EEntityTickMode::Default && !entity->AllowSkipTick());
    const bool is_always_tick = entity->AlwaysTickIndex() < m_always_tick_entities.size() &&
                                m_always_tick_entities[entity->AlwaysTickIndex()] == entity;
    if (should_tick_always == is_always_tick) return;
    if (!should_tick_always)
    {
        RemoveAlwaysTickEntity(entity);
        return;
    }

    entity->AlwaysTickIndex() = m_always_tick_entities.size();
    m_always_tick_entities.push_back(entity);
}

void CGameWorld::RemoveAlwaysTickEntity(CEntity* entity)
{
    if (!entity) return;
    const size_t index = entity->AlwaysTickIndex();
    if (index >= m_always_tick_entities.size() || m_always_tick_entities[index] != entity)
    {
        entity->AlwaysTickIndex() = std::numeric_limits<size_t>::max();
        return;
    }

    CEntity* moved = m_always_tick_entities.back();
    m_always_tick_entities[index] = moved;
    moved->AlwaysTickIndex() = index;
    m_always_tick_entities.pop_back();
    entity->AlwaysTickIndex() = std::numeric_limits<size_t>::max();
}

void CGameWorld::SyncConditionalTickMembership(CEntity* entity)
{
    if (!entity) return;

    const bool should_tick_conditionally = EffectiveTickMode(entity) == EEntityTickMode::PlayerVisible;
    const bool is_conditional_tick = entity->ConditionalTickIndex() < m_conditional_tick_entities.size() &&
                                     m_conditional_tick_entities[entity->ConditionalTickIndex()] == entity;
    if (should_tick_conditionally == is_conditional_tick) return;
    if (!should_tick_conditionally)
    {
        RemoveConditionalTickEntity(entity);
        return;
    }

    entity->ConditionalTickIndex() = m_conditional_tick_entities.size();
    m_conditional_tick_entities.push_back(entity);
}

void CGameWorld::RemoveConditionalTickEntity(CEntity* entity)
{
    if (!entity) return;
    const size_t index = entity->ConditionalTickIndex();
    if (index >= m_conditional_tick_entities.size() || m_conditional_tick_entities[index] != entity)
    {
        entity->ConditionalTickIndex() = std::numeric_limits<size_t>::max();
        return;
    }

    CEntity* moved = m_conditional_tick_entities.back();
    m_conditional_tick_entities[index] = moved;
    moved->ConditionalTickIndex() = index;
    m_conditional_tick_entities.pop_back();
    entity->ConditionalTickIndex() = std::numeric_limits<size_t>::max();
}

void CGameWorld::SyncLargeEntityMembership(CEntity* entity)
{
    if (!entity) return;
    const bool should_be_large = entity->Radius() > m_normal_entity_radius_limit;
    const bool is_large = entity->LargeSpatialIndex() < m_large_entities.size() &&
                          m_large_entities[entity->LargeSpatialIndex()] == entity;
    if (should_be_large == is_large) return;

    if (!should_be_large)
    {
        RemoveLargeEntity(entity);
        return;
    }

    entity->LargeSpatialIndex() = m_large_entities.size();
    m_large_entities.push_back(entity);
}

void CGameWorld::RemoveLargeEntity(CEntity* entity)
{
    if (!entity) return;
    const size_t index = entity->LargeSpatialIndex();
    if (index >= m_large_entities.size() || m_large_entities[index] != entity)
    {
        entity->LargeSpatialIndex() = std::numeric_limits<size_t>::max();
        return;
    }

    CEntity* moved = m_large_entities.back();
    m_large_entities[index] = moved;
    moved->LargeSpatialIndex() = index;
    m_large_entities.pop_back();
    entity->LargeSpatialIndex() = std::numeric_limits<size_t>::max();
}

CEntity* CGameWorld::InsertEntity(std::unique_ptr<CEntity> entity)
{
    if (!entity) return nullptr;
    if (entity->Id() != -1)
    {
        LOG_FATAL("gameworld", "The entity already has ID " + std::to_string(entity->Id()) + ".");
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
    entity->SetIdentity(id, m_next_generation++);
    if (m_next_generation == 0) m_next_generation = 1;
    m_p_entities[id] = std::move(entity);
    RegisterLiveEntity(raw_entity);
    // Multi-segment roots need a stable world-qualified handle before their
    // body entities can be linked.  Restore insertion intentionally skips this
    // hook; hot-reload restores all serialized entities first and resolves the
    // links in its dedicated reference phase.
    if (auto* mob = dynamic_cast<CMobBase*>(raw_entity)) AssembleMultiSegmentMob(*mob);
    return raw_entity;
}

bool CGameWorld::RestoreInstanceGeneration(std::uint64_t generation)
{
    if (m_transition_active) return false;
    if (!m_live_entities.empty() || generation == 0 || generation == std::numeric_limits<std::uint64_t>::max())
        return false;
    if (m_p_game_context) m_p_game_context->Extensions().DetachWorld({GetId(), GetInstanceGeneration()});
    m_instance_generation = generation;
    if (m_p_game_context) m_extensions = m_p_game_context->Extensions().AttachWorld(*this, m_extension_config);
    auto previous = s_next_instance_generation.load();
    while (previous < generation && !s_next_instance_generation.compare_exchange_weak(previous, generation)) {}
    return true;
}

bool CGameWorld::RestoreNextEntityGeneration(std::uint64_t generation)
{
    if (m_transition_active) return false;
    if (generation == 0 || generation == std::numeric_limits<std::uint64_t>::max()) return false;
    m_next_generation = std::max(m_next_generation, generation);
    return true;
}

CEntity* CGameWorld::InsertEntityWithIdentity(std::unique_ptr<CEntity> entity, int id, std::uint64_t generation)
{
    constexpr int max_restore_entity_id = 1000000;
    // UINT64_MAX cannot be incremented to produce the next generation.  Do
    // not admit it from a snapshot or a debug tool, otherwise the next ID
    // reuse could wrap back to generation 1 and resurrect a stale handle.
    if (!entity || id < 0 || id > max_restore_entity_id || generation == 0 ||
        generation == std::numeric_limits<std::uint64_t>::max())
        return nullptr;
    if (entity->Id() != -1)
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
    entity->SetIdentity(id, generation);
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

bool CGameWorld::ReloadMapForRestore(std::string& error)
{
    if (m_transition_active || !m_live_entities.empty()) { error = "Cannot reload an occupied world map"; return false; }
    auto map = LoadMapFromTmj(m_map_path);
    if (!map) { error = "Cannot reload snapshot map: " + m_map_path; return false; }
    m_map = std::move(map);
    m_spawn_zone_bounds = florrbt::BuildSpawnZoneBounds(m_map->zones);
    m_spawn_habitat_cache.clear();
    BuildWallGrid();
    return true;
}

void CGameWorld::ClearEntitiesForRestore()
{
    if (m_transition_active) throw std::logic_error("Cannot restore a world during a transition");
    // Restore replaces the owning entity array in-place. Detach every player
    // controlled by this world before destroying that array; otherwise the
    // player keeps a pointer to this world plus an old handle while the
    // world's entity storage is already being torn down. The subsequent
    // player restore phase will attach canonical handles again.
    if (m_p_game_context)
    {
        for (const auto& player : m_p_game_context->Players())
        {
            if (!player) continue;
            player->ClearOwnedEntityFromWorld(this);
        }
    }

    m_spawn_habitat_cache.clear();
    m_cleanup_entities.clear();
    m_always_tick_entities.clear();
    m_conditional_tick_entities.clear();
    m_large_entities.clear();
    m_active_entities.clear();
    m_active_tick_views.clear();
    m_player_tick_views.clear();
    m_contact_sweep.clear();
    m_collision_mob_contacts.clear();
    m_collision_inactive_entities.clear();
    m_clear_owned_owner_keys.clear();
    m_clear_summon_owner_keys.clear();
    m_pending_psionic_damage.clear();
    m_carried_entities.clear();
    m_wall_query_visited.clear();
    m_combat_feedback.clear();
    m_combat_feedback_indices.clear();
    m_tick_phase_telemetry = {};
    m_retire_sweep_timer = 0.f;
    m_live_entities.clear();
    m_spatial_grid.Clear();
    m_chains.clear();
    m_p_entities.clear();
    m_free_ids.clear();
    m_id_in_use.clear();
    m_next_id = 0;
    m_next_generation = 1;
    m_active_tick_marker = 1;
    m_collision_sync_marker = 1;
}

void CGameWorld::QueuePsionicDamage(CMobBase* receiver, float damage, CEntity* attacker, EDamageType damage_type)
{
    if (!receiver || receiver->GameWorld() != this || !std::isfinite(damage) || damage <= 0.f) return;
    if (IsSharedDamageType(damage_type)) return;

    const SPsionicReceiverKey key{ receiver->GetHandle() };
    auto [it, inserted] = m_pending_psionic_damage.try_emplace(key);
    if (inserted)
    {
        it->second.origin = receiver->Pos();
        it->second.team = receiver->Team();
    }
    it->second.damage_by_source[{ attacker ? attacker->GetHandle() : SEntityHandle{}, BaseDamageType(damage_type) }] +=
        damage;
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
        if (lhs.entity_ref.id != rhs.entity_ref.id) return lhs.entity_ref.id < rhs.entity_ref.id;
        return lhs.entity_ref.generation < rhs.entity_ref.generation;
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
            if (!entity || entity->IsMarkedForDes() || entity->IsDead()) return;
            if (!entity->IsEntityType(EEntityType::Mob)) return;
            auto* mob = static_cast<CMobBase*>(entity);
            if (!mob || !CheckTeam(mob->Team(), anchor.team)) return;
            if (DistanceSq(mob->Pos(), anchor.origin) > link_range_sq) return;
            if (!HasActivePsionicConnection(mob)) return;
            const SPsionicReceiverKey candidate_key{ mob->GetHandle() };
            if (!assigned_targets.insert(candidate_key).second) return;
            candidates.push_back(mob);
        });
        if (candidates.empty())
        {
            pending_damage.erase(anchor_it);
            continue;
        }
        std::sort(candidates.begin(), candidates.end(), [](const CMobBase* lhs, const CMobBase* rhs) {
            if (lhs->Id() != rhs->Id()) return lhs->Id() < rhs->Id();
            return lhs->Generation() < rhs->Generation();
        });

        group_damage_by_source.clear();
        for (CMobBase* mob : candidates)
        {
            const SPsionicReceiverKey key{ mob->GetHandle() };
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
                const SPsionicDamageKey target{ mob->GetHandle(), source.attacker_ref, shared_type };
                distributed_damage[target] += shared_damage;
            }
        }
    }

    for (const auto& [target, damage] : distributed_damage)
    {
        CEntity* entity = ResolveEntity(target.entity_ref);
        if (!entity || !entity->IsEntityType(EEntityType::Mob)) continue;
        CEntity* attacker = ResolveEntity(target.attacker_ref);
        static_cast<CMobBase*>(entity)->TakeDamage(damage, attacker, target.damage_type);
    }
}

void CGameWorld::FinalizeEntityRestore()
{
    ForEachEntity([](CEntity* entity) {
        if (auto* mob = dynamic_cast<CMobBase*>(entity)) mob->SynchronizeEnvironment();
        if (auto* flower = dynamic_cast<CFlower*>(entity)) flower->RefreshPetalSuppression();
    });
    m_free_ids.clear();
    if (m_next_id > static_cast<int>(m_id_in_use.size())) m_id_in_use.resize(m_next_id, 0);
    for (int id = 0; id < m_next_id; ++id)
    {
        if (!m_id_in_use[id]) m_free_ids.push_back(id);
    }

    // Carrying is a world-level invariant, not merely controller state.
    // Rebuild the registry after all entities and controller snapshots exist;
    // otherwise a restored leaf piece/hornet target is visible through the
    // controller but is not protected from a second carrier until the next
    // controller tick.
    m_carried_entities.clear();
    ForEachEntity([this](CEntity* entity) {
        auto* mob = dynamic_cast<CMobBase*>(entity);
        if (!mob || !mob->GetController()) return;
        auto* pickup = mob->GetPickupController();
        if (!pickup) return;
        CEntity* carried = pickup->GetCarried(mob);
        if (!carried) return;
        if (!TryRegisterCarriedEntity(*carried, *mob)) pickup->DropCarried(mob);
    });
}

CEntity* CGameWorld::InsertEntity(CEntity* entity)
{
    if (!entity) return nullptr;
    return InsertEntity(std::unique_ptr<CEntity>(entity));
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

void CGameWorld::DestroyProjectilesOwnedBy(const SEntityHandle& owner_ref)
{
    if (!owner_ref.IsValid()) return;

    auto destroy_if_owned = [owner_ref](CEntity* entity) {
        auto* projectile = dynamic_cast<CProjectile*>(entity);
        if (projectile && owner_ref.RefersTo(projectile->m_owner_ref))
            projectile->MarkForDestroy(EEntityRemovalReason::OwnerRemoved);
    };

    ForEachEntity(destroy_if_owned);
}

void CGameWorld::DestroySummonedMobsOwnedBy(const SEntityHandle& owner_ref)
{
    if (!owner_ref.IsValid()) return;

    auto destroy_if_owned = [owner_ref](CEntity* entity) {
        auto* mob = dynamic_cast<CMobBase*>(entity);
        if (!mob) return;

        auto* controller = dynamic_cast<CSummonedMeleeController*>(mob->GetController());
        if (!controller || !controller->GetOwnerRef().RefersTo(owner_ref)) return;

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
        auto portal = std::make_unique<CPortal>(this, florrbt::Vector2f{ warp.x, warp.y }, radius, warp.goal);
        portal->SetFromPointName(warp.point.empty() ? warp.name : warp.point);
        InsertEntity(std::move(portal));
    }
}

SPlayerTransitionResult CGameWorld::TransferPlayerEntityToWorld(CPlayer& player, CGameWorld& target_world,
    std::optional<florrbt::Vector2f> target_pos, std::optional<std::string> from)
{
    const auto rng_before = GetRng();
    if (from && !from->empty())
        if (auto point = target_world.FindWarpPoint(*from)) target_pos = point;
    if (!target_pos) target_pos = PickDefaultTransferSpawnPosition(target_world);
    auto result = CWorldTransfer::Move(player, *this, target_world, *target_pos);
    if (!result && !result.deferred) GetRng() = rng_before;
    return result;
}

SPlayerTransitionResult CGameWorld::TransferPlayerEntityToWorld(CPlayer& player, CGameWorld& target_world,
                                                               const std::string& from)
{
    return TransferPlayerEntityToWorld(player, target_world, std::nullopt, from);
}
namespace
{
float ChainDot(florrbt::Vector2f lhs, florrbt::Vector2f rhs) { return lhs.x * rhs.x + lhs.y * rhs.y; }

florrbt::Vector2f* ChainVelocity(CEntity* entity)
{
    if (auto* mob = dynamic_cast<CMobBase*>(entity)) return &mob->m_vel;
    if (auto* projectile = dynamic_cast<CProjectile*>(entity)) return &projectile->m_vel;
    return nullptr;
}

float ChainInverseMass(const CEntity* entity)
{
    if (!entity || entity->IsCollisionPositionLocked()) return 0.f;
    const float mass = entity->Mass();
    if (!std::isfinite(mass)) return 0.f;
    return 1.f / std::max(0.001f, mass);
}

double ChainPullMass(const CEntity* entity)
{
    constexpr double mass_floor = 0.001;
    constexpr double mass_cap = 1'000'000.0;
    if (!entity || entity->IsCollisionPositionLocked()) return mass_cap;
    const double mass = static_cast<double>(entity->Mass());
    if (!std::isfinite(mass)) return mass_cap;
    return std::clamp(mass, mass_floor, mass_cap);
}

float ChainMassPullMultiplier(const CEntity* first, const CEntity* second)
{
    const double first_mass = ChainPullMass(first);
    const double second_mass = ChainPullMass(second);
    // Twice the reduced mass is the harmonic mean. It depends on both ends,
    // grows when both are heavy, and remains limited by a very light endpoint.
    const double pair_mass = 2.0 / (1.0 / first_mass + 1.0 / second_mass);
    const double reference_mass = std::max(0.001, static_cast<double>(game_config::default_linker_mass));
    const double exponent = std::max(0.0, static_cast<double>(game_config::default_linker_chain_mass_pull_exponent));
    const double raw_multiplier = std::pow(std::max(0.001, pair_mass / reference_mass), exponent);
    const float minimum = std::max(0.f, game_config::default_linker_chain_mass_pull_min);
    const float maximum = std::max(minimum, game_config::default_linker_chain_mass_pull_max);
    if (!std::isfinite(raw_multiplier)) return maximum;
    return std::clamp(static_cast<float>(raw_multiplier), minimum, maximum);
}
} // namespace

CChainEntity::CChainEntity(CGameWorld* world, CEntity& first, CEntity& second, float max_length, float integrity,
                           float elasticity, CEntity* lease_owner)
    : CEntity(world, 0.f, 0.f, 0.f, MakeEntityType(EEntityType::Effect, server_chain_entity_type)),
      m_first(first.GetHandle()), m_second(second.GetHandle()),
      m_lease_ref(lease_owner ? lease_owner->GetHandle() : SChainEndpointRef{}),
      m_max_length(std::isfinite(max_length) ? std::max(0.f, max_length) : 0.f),
      m_max_integrity(std::isfinite(integrity) ? std::max(1.f, integrity) : 1.f),
      m_elasticity(std::isfinite(elasticity) ? elasticity : -1.f)
{
    Team() = 0;
    Mass() = std::numeric_limits<float>::max();
    Health() = m_max_integrity;
    TickMode() = EEntityTickMode::Always;
    AllowSkipTick() = false;
    UpdateGeometry(&first, &second);
}

CEntity* CChainEntity::Resolve(const SChainEndpointRef& ref) const
{
    if (!GameWorld() || !ref.IsValid()) return nullptr;
    CEntity* entity = GameWorld()->ResolveEntity(ref);
    if (!entity || entity->IsMarkedForDes() || entity->IsDead() || entity->GameWorld() != GameWorld()) return nullptr;
    return entity;
}

void CChainEntity::UpdateGeometry(const CEntity* first, const CEntity* second)
{
    if (!first || !second) return;
    const florrbt::Vector2f delta = second->Pos() - first->Pos();
    const float distance = Length(delta);
    PrevPos() = Pos();
    Pos() = (first->Pos() + second->Pos()) * 0.5f;
    Radius() = std::max(1.f, distance * 0.5f + std::max(8.f, m_max_length * 0.5f));
}

bool CChainEntity::HasEndpoint(const CEntity& entity) const
{
    const SEntityHandle ref = entity.GetHandle();
    return m_first.RefersTo(ref) || m_second.RefersTo(ref);
}

bool CChainEntity::HasLease(const CEntity& entity) const
{
    return m_lease_ref.RefersTo(entity.GetHandle());
}

bool CChainEntity::ConvertEndpoint(CEntity& from, CEntity& replacement)
{
    if (replacement.GameWorld() != GameWorld() || replacement.IsMarkedForDes()) return false;
    bool converted = false;
    if (m_first.RefersTo(from.GetHandle()))
    {
        m_first = replacement.GetHandle();
        converted = true;
    }
    if (m_second.RefersTo(from.GetHandle()))
    {
        m_second = replacement.GetHandle();
        converted = true;
    }
    return converted && !m_first.RefersTo(m_second);
}

void CChainEntity::NotifyLeaseOnce()
{
    if (m_lease_notified) return;
    m_lease_notified = true;
    if (CEntity* lease = Resolve(m_lease_ref))
        if (auto* callback = dynamic_cast<IChainLease*>(lease)) callback->OnChainBroken(*this);
    m_lease_ref = {};
}

void CChainEntity::Break()
{
    if (IsMarkedForDes()) return;
    MarkForDestroy(EEntityRemovalReason::Expired);
    NotifyLeaseOnce();
}

void CChainEntity::OnBeforeRemoved(EEntityRemovalReason)
{
    NotifyLeaseOnce();
}

void CChainEntity::ValidateReferences()
{
    if (IsMarkedForDes()) return;
    CEntity* first = Resolve(m_first);
    CEntity* second = Resolve(m_second);
    if (!first || !second || first == second || (m_lease_ref.IsValid() && !Resolve(m_lease_ref))) Break();
}

void CChainEntity::ApplyConstraint(float dt)
{
    if (IsMarkedForDes()) return;
    if (!std::isfinite(dt) || dt < 0.f)
    {
        Break();
        return;
    }
    CEntity* first = Resolve(m_first);
    CEntity* second = Resolve(m_second);
    if (!first || !second || first == second)
    {
        Break();
        return;
    }

    if (!std::isfinite(first->Pos().x) || !std::isfinite(first->Pos().y) ||
        !std::isfinite(second->Pos().x) || !std::isfinite(second->Pos().y) ||
        !std::isfinite(first->Radius()) || !std::isfinite(second->Radius()))
    {
        Break();
        return;
    }

    UpdateGeometry(first, second);
    const florrbt::Vector2f delta = second->Pos() - first->Pos();
    const float center_distance = Length(delta);
    // m_max_length is the visible/free span of the chain between the two
    // entity surfaces.  Its actual allowed center-to-center length therefore
    // includes both endpoint radii, evaluated every tick so resized entities
    // do not retain a stale limit.
    const float first_radius = std::max(0.f, first->Radius());
    const float second_radius = std::max(0.f, second->Radius());
    const float max_center_distance = m_max_length + first_radius + second_radius;
    const float length_error = center_distance - max_center_distance;
    const bool hard_constraint = std::abs(m_elasticity) <= game_config::entity_collision_epsilon;
    const bool spring_constraint = m_elasticity > game_config::entity_collision_epsilon;

    florrbt::Vector2f* first_velocity = ChainVelocity(first);
    florrbt::Vector2f* second_velocity = ChainVelocity(second);
    const florrbt::Vector2f first_v = first_velocity ? *first_velocity : florrbt::Vector2f{};
    const florrbt::Vector2f second_v = second_velocity ? *second_velocity : florrbt::Vector2f{};
    const float relative_speed = center_distance > game_config::entity_collision_epsilon
                                     ? ChainDot(second_v - first_v, delta / center_distance)
                                     : 0.f;

    if (!(center_distance > game_config::entity_collision_epsilon))
    {
        // A coincident pair has no geometric normal.  A hard chain still
        // needs to separate it, so use a deterministic fallback direction;
        // slack chains have no work to do in this state.
        if (!hard_constraint || max_center_distance <= game_config::entity_collision_epsilon)
        {
            m_tension = 0.f;
            return;
        }
    }

    const florrbt::Vector2f normal = center_distance > game_config::entity_collision_epsilon
                                         ? delta / center_distance
                                         : florrbt::Vector2f{1.f, 0.f};
    // A rope and a spring are one-sided constraints: they are slack below
    // their rest length.  Only the zero-elasticity mode also resists
    // compression, because it represents a rigid bar of exactly this length.
    if ((!hard_constraint && length_error <= game_config::entity_collision_epsilon) ||
        (hard_constraint && std::abs(length_error) <= game_config::entity_collision_epsilon &&
         std::abs(relative_speed) <= game_config::entity_collision_epsilon))
    {
        m_tension = 0.f;
        return;
    }

    const float violation = hard_constraint ? std::abs(length_error) : std::max(0.f, length_error);
    const float direction_sign = hard_constraint
                                     ? (std::abs(length_error) > game_config::entity_collision_epsilon
                                            ? (length_error < 0.f ? -1.f : 1.f)
                                            : (relative_speed < 0.f ? -1.f : 1.f))
                                     : 1.f;
    const float violation_speed = hard_constraint
                                      ? std::max(0.f, direction_sign * relative_speed)
                                      : std::max(0.f, relative_speed);
    const float mass_pull_multiplier = ChainMassPullMultiplier(first, second);
    const float base_stiffness = std::max(0.f, game_config::default_linker_chain_stiffness);
    const float base_damping = std::max(0.f, game_config::default_linker_chain_damping);
    // Positive elasticity is deliberately inverse to stiffness: larger
    // values make the chain softer.  It still grows its restoring force with
    // stretch, but never snaps the endpoints back with a positional teleport.
    const float spring_scale = spring_constraint ? 1.f / (1.f + std::max(0.f, m_elasticity)) : 1.f;
    const float hard_stiffness = std::max(1024.f, base_stiffness * 128.f);
    const float effective_stiffness = hard_constraint ? hard_stiffness : base_stiffness * spring_scale;
    const float effective_damping = hard_constraint ? base_damping * 8.f : base_damping * spring_scale;
    const float tension_force = (effective_stiffness * violation + effective_damping * violation_speed) *
                                mass_pull_multiplier;
    const float tension_scale = std::max(1.f, effective_stiffness * std::max(1.f, m_max_length));
    m_tension = hard_constraint ? 1.f : std::clamp(tension_force / tension_scale, 0.f, 1.f);

    const float inverse_first = ChainInverseMass(first);
    const float inverse_second = ChainInverseMass(second);
    const float inverse_total = inverse_first + inverse_second;
    if (inverse_total > 0.f && dt > 0.f)
    {
        const float impulse = std::min(tension_force * dt, hard_constraint ? 4000.f : 400.f);
        const florrbt::Vector2f constraint_normal = normal * direction_sign;
        if (first_velocity) *first_velocity += constraint_normal * (impulse * inverse_first / inverse_total);
        if (second_velocity) *second_velocity -= constraint_normal * (impulse * inverse_second / inverse_total);

        float correction = 0.f;
        if (hard_constraint)
        {
            // A hard chain is allowed to correct both sides of its target;
            // this keeps it close to exactly the configured length even when
            // one endpoint is stationary or very massive.
            correction = std::abs(length_error);
        }
        else if (!spring_constraint)
        {
            // Preserve the old rope behavior: remove a substantial part of
            // an over-stretch while leaving shorter distances untouched.
            correction = std::min(std::max(0.f, length_error) * 0.8f,
                                  std::max(0.f, game_config::default_linker_chain_max_correction));
        }
        if (correction > 0.f)
        {
            first->Pos() += constraint_normal * (correction * inverse_first / inverse_total);
            second->Pos() -= constraint_normal * (correction * inverse_second / inverse_total);
        }
    }

    // The chain is damaged by both endpoints trying to pull apart. It never
    // deals contact damage to either endpoint.
    if (dt > 0.f)
    {
        const double endpoint_stress = ChainPullMass(first) * std::max(0.f, -ChainDot(first_v, normal)) +
                                       ChainPullMass(second) * std::max(0.f, ChainDot(second_v, normal));
        const float stretch_tension = std::max(0.f, length_error) > game_config::entity_collision_epsilon
                                          ? m_tension
                                          : 0.f;
        const float damage = dt * std::max(0.f, game_config::default_linker_chain_damage_per_second) *
                             std::clamp(stretch_tension + static_cast<float>(endpoint_stress / 1000.0), 0.f, 2.f);
        Health() = std::max(0.f, Health() - damage);
        if (Health() <= 0.f) Break();
    }
}

std::string CChainEntity::SnapshotName() const
{
    std::ostringstream out;
    out << "chain:" << m_first.id << ':' << m_first.generation << ':' << m_second.id << ':' << m_second.generation
        << ':' << std::fixed << std::setprecision(3) << m_max_length << ':' << Health() << ':' << m_tension << ':'
        << m_elasticity;
    return out.str();
}

void CGameWorld::ApplyChainConstraints(float dt)
{
    for (CChainEntity* chain : m_chains)
    {
        if (!chain || chain->IsMarkedForDes()) continue;
        chain->ApplyConstraint(dt);
        m_spatial_grid.UpdateTracked(chain);
    }
}

void CGameWorld::BreakChainsForEndpoint(const CEntity& endpoint)
{
    for (CChainEntity* chain : m_chains)
        if (chain->HasEndpoint(endpoint) || chain->HasLease(endpoint)) chain->Break();
}

void CGameWorld::Tick(float dt)
{
    if (!std::isfinite(dt) || dt <= 0.f) return;
    if (m_tick_active) throw std::logic_error("Cannot recursively tick a world");
    struct STickScope
    {
        bool& active;
        explicit STickScope(bool& value) : active(value) { active = true; }
        ~STickScope() { active = false; }
    } tick_scope(m_tick_active);
auto run_tick_phases = [this, dt](auto&& phase_done) {
        CollectActiveEntitiesForTick();
        phase_done(ETickPhase::ActiveCollection);

        m_retire_sweep_timer += dt;
        if (m_retire_sweep_timer >= k_retire_sweep_interval)
        {
            RetireInactiveMobs(m_retire_sweep_timer);
            m_retire_sweep_timer = 0.f;
        }

        TickActiveEntities(dt);
        FlushPendingPsionicDamage();
        phase_done(ETickPhase::EntityUpdate);

        SyncSpatialPositionsAndLargeMemberships(m_active_entities);
        if (m_p_controller) m_p_controller->OnTick(*this, dt);
        RemoveTransferredActiveEntities();
        FlushPendingPsionicDamage();
        ApplyChainConstraints(dt);
        phase_done(ETickPhase::ControllerAndSync);

        ResolveWallCollisions(m_active_entities);
        ResolveCarryConstraints(m_active_entities);
        for (auto* entity : m_active_entities)
            if (auto* mob = dynamic_cast<CMobBase*>(entity); mob && mob->GameWorld() == this)
                mob->SynchronizeEnvironmentAfterDisplacement();
        SyncSpatialPositions(m_active_entities);
        phase_done(ETickPhase::WallResolution);

        ResolveCollisions(m_active_entities, dt);
        FlushPendingPsionicDamage();
        ResolveCarryConstraints(m_active_entities);
        RefreshAttachedMissileTransforms(m_active_entities);
        for (auto* entity : m_active_entities)
            if (auto* mob = dynamic_cast<CMobBase*>(entity); mob && mob->GameWorld() == this)
                mob->SynchronizeEnvironmentAfterDisplacement();
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

void CGameWorld::RecordCombatDamage(const CEntity& target, float amount, EDamageType damage_type, CEntity* source)
{
    if (!std::isfinite(amount) || amount <= 0.f || target.Id() < 0 || !target.IsVisible()) return;
    const uint8_t kind = static_cast<uint8_t>(BaseDamageType(damage_type));
    const uint64_t key = (static_cast<uint64_t>(target.Id()) << 8) | kind;
    // Only this unsent batch is retained. Bounded even in an empty world or
    // while every client is backed up, and cleared after the snapshot batch.
    auto found = m_combat_feedback_indices.find(key);
    if (found == m_combat_feedback_indices.end())
    {
        if (m_combat_feedback.size() >= 256) return;
        m_combat_feedback_indices.emplace(key, m_combat_feedback.size());
        m_combat_feedback.push_back({ static_cast<net_entity_id>(target.Id()), target.Pos(), target.Radius(), 0.f, kind });
        found = m_combat_feedback_indices.find(key);
    }
    auto& hit = m_combat_feedback[found->second];
    if (hit.target_generation != target.Generation()) hit.amount = 0.f;
    hit.target_generation = target.Generation();
    hit.radius = target.Radius();
    hit.amount = std::min(1.e30f, hit.amount + amount);
    hit.pos = target.Pos();
    for (int depth = 0; source && depth < 8; ++depth)
    {
        const auto* projectile = dynamic_cast<const CProjectile*>(source);
        if (!projectile || !projectile->GetOwner() || projectile->GetOwner() == source) break;
        source = projectile->GetOwner();
    }
    if (source)
    {
        hit.source_type = source->GetNetworkType();
        if (const auto* mob = dynamic_cast<const CMobBase*>(source))
        {
            hit.source_rarity = static_cast<uint8_t>(mob->GetRarity());
            if (const auto* prototype = FindMobPrototype(mob->GetMobType());
                prototype && prototype->m_visual_proxy_type != EMobType::None)
                hit.source_type = static_cast<uint8_t>(prototype->m_visual_proxy_type);
        }
    } else
    {
        hit.source_type = 0;
        hit.source_rarity = 0;
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
            if (entity) entity->ActiveTickMarker() = 0;
    }

    auto add_active_entity = [this](CEntity* entity) {
        if (!entity) return;
        if (entity->ActiveTickMarker() == m_active_tick_marker) return;
        entity->ActiveTickMarker() = m_active_tick_marker;
        entity->PrevPos() = entity->Pos();
        m_active_entities.push_back(entity);
    };

    for (CEntity* entity : m_always_tick_entities)
        add_active_entity(entity);

    for (const active_tick_view& view : m_active_tick_views)
    {
        ForEachEntityInEdgeRange(view.center, view.radius, [&](CEntity* entity) {
            if (!entity || !entity->AllowSkipTick()) return;
            if (EffectiveTickMode(entity) == EEntityTickMode::PlayerVisible) return;
            add_active_entity(entity);
        });
    }

    for (CEntity* entity : m_conditional_tick_entities)
    {
        if (!entity || entity->IsMarkedForDes() || entity->GameWorld() != this) continue;
        const float entity_horizon = std::max(0.f, entity->TickHorizon());
        for (const player_tick_view& player_view : m_player_tick_views)
        {
            const float entity_sees_player = entity_horizon + player_view.radius;
            const float player_sees_entity = player_view.horizon + std::max(0.f, entity->Radius());
            const float activation_radius = std::max(entity_sees_player, player_sees_entity);
            if (DistanceSq(entity->Pos(), player_view.center) > activation_radius * activation_radius) continue;
add_active_entity(entity);
            break;
        }
    }
}

void CGameWorld::RetireInactiveMobs(float elapsed)
{
    const float retire_seconds = std::max(0.f, game_config::mob_inactive_retire_seconds);
    for (CEntity* entity : m_live_entities)
    {
        if (!entity || entity->IsMarkedForDes() || entity->GameWorld() != this) continue;
        if (!entity->IsEntityType(EEntityType::Mob)) continue;
        CMobBase* mob = static_cast<CMobBase*>(entity);
        if (mob->ActiveTickMarker() == m_active_tick_marker || GetMobSpawnRarityExcess(*mob) >= 2)
        {
            mob->ResetInactiveSeconds();
            continue;
        }
        bool visible_to_player = false;
        for (const player_tick_view& player_view : m_player_tick_views)
        {
            const float view_radius = player_view.horizon + std::max(0.f, mob->Radius());
            if (DistanceSq(mob->Pos(), player_view.center) <= view_radius * view_radius)
            {
                visible_to_player = true;
                break;
            }
        }
        if (visible_to_player) continue;
        mob->AddInactiveSeconds(elapsed);
        if (retire_seconds > 0.f && mob->InactiveSeconds() >= retire_seconds)
            mob->MarkForDestroy(EEntityRemovalReason::Despawned);
    }
}

void CGameWorld::TickActiveEntities(float dt)
{
    for (CEntity* entity : m_active_entities)
    {
        if (!entity || entity->IsMarkedForDes()) continue;
        if (entity->GameWorld() != this) continue;
        if (!entity->SkipWorldTick()) entity->Tick(dt);
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
    return nullptr;
}

CEntity* CGameWorld::GetEntity(int id, std::uint64_t generation) const
{
    CEntity* entity = GetEntity(id);
    if (!entity || entity->Generation() != generation) return nullptr;
    return entity;
}

CEntity* CGameWorld::ResolveEntity(const SEntityHandle& handle) const
{
    if (!handle.IsValid()) return nullptr;
    if (handle.HasWorldIdentity() &&
        (handle.world_id != m_world_id || handle.world_instance_generation != m_instance_generation))
        return nullptr;
    return GetEntity(handle.id, handle.generation);
}

CEntity* CGameWorld::GetEntity(const SEntityHandle& handle) const
{
    return ResolveEntity(handle);
}

bool CGameWorld::TryRegisterCarriedEntity(const CEntity& carried, const CEntity& carrier)
{
    if (carried.GameWorld() != this || carrier.GameWorld() != this || carried.Id() < 0 || carrier.Id() < 0 ||
        carried.IsMarkedForDes() || carrier.IsMarkedForDes() || carried.IsDead() || carrier.IsDead())
        return false;

    const SEntityHandle carried_ref = carried.GetHandle();
    const SEntityHandle carrier_ref = carrier.GetHandle();
    if (!carried_ref.IsValid() || !carrier_ref.IsValid()) return false;

    auto existing = m_carried_entities.find(carried_ref);
    if (existing != m_carried_entities.end())
    {
        const SCarriedEntityRecord& record = existing->second;
        if (record.carrier == carrier_ref)
            return true;
        if (IsCarriedEntity(carried)) return false;
    }

    m_carried_entities[carried_ref] = { carrier_ref };
    return true;
}

void CGameWorld::UnregisterCarriedEntity(const SEntityHandle& carried, const SEntityHandle& carrier)
{
    auto existing = m_carried_entities.find(carried);
    if (existing == m_carried_entities.end()) return;

    const SCarriedEntityRecord& record = existing->second;
    if (!(record.carrier == carrier))
        return;
    m_carried_entities.erase(existing);
}

bool CGameWorld::IsCarriedEntity(const CEntity& carried)
{
    const SEntityHandle carried_ref = carried.GetHandle();
    if (!carried_ref.IsValid()) return false;
    auto existing = m_carried_entities.find(carried_ref);
    if (existing == m_carried_entities.end()) return false;

    const SCarriedEntityRecord& record = existing->second;
    if (carried.GameWorld() != this || !ResolveEntity(carried_ref))
    {
        m_carried_entities.erase(existing);
        return false;
    }

    const auto* carrier = dynamic_cast<const CMobBase*>(ResolveEntity(record.carrier));
    const auto* pickup = carrier ? carrier->GetPickupController() : nullptr;
    if (!carrier || carrier->IsMarkedForDes() || carrier->IsDead() || !pickup || !pickup->IsCarrying(carrier, &carried))
    {
        m_carried_entities.erase(existing);
        return false;
    }
    return true;
}

void CGameWorld::ReleaseCarriedRelations(CEntity& entity)
{
    if (auto* mob = dynamic_cast<CMobBase*>(&entity))
        if (auto* pickup = mob->GetPickupController()) pickup->DropCarried(mob);
    const auto held = m_carried_entities.find(entity.GetHandle());
    if (held == m_carried_entities.end()) return;
    auto* carrier = dynamic_cast<CMobBase*>(ResolveEntity(held->second.carrier));
    if (carrier && carrier->GetPickupController()) carrier->GetPickupController()->DropCarried(carrier);
    else m_carried_entities.erase(held);
}

CEntity* CGameWorld::FindClosestEntity(const florrbt::Vector2f& center, float max_range,
                                       std::function<bool(const CEntity*)> filter) const
{
    return m_spatial_grid.FindClosest(center, max_range, filter);
}

CEntity* CGameWorld::FindClosestEntityByEdge(const florrbt::Vector2f& center, float max_edge_range,
                                             std::function<bool(const CEntity*)> filter) const
{
    if (max_edge_range <= 0.f) return nullptr;

    CEntity* target = nullptr;
    float best_edge_distance = max_edge_range;
    ForEachEntityInEdgeRange(center, max_edge_range, [&](CEntity* candidate) {
        if (!candidate) return;
        if (filter && !filter(candidate)) return;
        float edge_distance = std::max(0.f, Distance(center, candidate->Pos()) - candidate->Radius());
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

void CGameWorld::SyncSpatialPositionsAndLargeMemberships(const std::vector<CEntity*>& entities)
{
    for (CEntity* entity : entities)
    {
        if (!entity || entity->GameWorld() != this) continue;
        m_spatial_grid.UpdateTracked(entity);
        SyncLargeEntityMembership(entity);
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

int CGameWorld::GetMobSpawnRarityExcess(const CMobBase& mob) const
{
    if (!m_map || mob.IsDead() || mob.IsMarkedForDes() || mob.GameWorld() != this ||
        mob.GetMobType() == EMobType::PlayerFlower || mob.GetMobType() == EMobType::SummonedBeetle ||
        mob.GetMobType() == EMobType::SummonedSoldierAnt || GetLevel(mob.GetRarity()) < 3 ||
        dynamic_cast<const CSummonedMeleeController*>(mob.GetController())) return 0;
    auto& cached = m_spawn_habitat_cache[mob.Id()];
    if (!cached.valid || cached.generation != mob.Generation() || cached.position != mob.Pos())
    {
        cached.valid = true;
        cached.generation = mob.Generation();
        cached.position = mob.Pos();
        cached.difficulty = florrbt::FindSpawnHabitatDifficulty(m_map->zones, mob.Pos(), &m_spawn_zone_bounds);
    }
    const auto difficulty = cached.difficulty;
    return difficulty ? florrbt::ComputeSpawnRarityExcess(GetLevel(mob.GetRarity()), *difficulty,
        game_config::open_rarity_difficulty_scale) : 0;
}

ERarity CGameWorld::GetSpawnZoneRarity(const florrbt::Vector2f& pos) const
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
        if (entity && entity->GameWorld() == this && !entity->IsMarkedForDes()) ++count;
    }
    return count;
}

std::optional<florrbt::Vector2f> CGameWorld::FindWarpPoint(const std::string& from) const
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
    return florrbt::Vector2f{ warp->x, warp->y };
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

int CGameWorld::RemoveWallsInRect(florrbt::Vector2f min_pos, florrbt::Vector2f max_pos)
{
    if (!m_map) return 0;
    const float min_x = std::min(min_pos.x, max_pos.x);
    const float min_y = std::min(min_pos.y, max_pos.y);
    const float max_x = std::max(min_pos.x, max_pos.x);
    const float max_y = std::max(min_pos.y, max_pos.y);

    size_t removed = 0;
    auto& walls = m_map->walls;
    for (size_t i = 0; i < walls.size();)
    {
        const FlorrBtMap::Wall& wall = walls[i];
        const bool intersects = wall.max_x >= min_x && wall.min_x <= max_x && wall.max_y >= min_y &&
                                wall.min_y <= max_y;
        if (!intersects)
        {
            ++i;
            continue;
        }
        walls[i] = walls.back();
        walls.pop_back();
        ++removed;
    }

    if (removed > 0)
    {
        for (size_t i = 0; i < walls.size(); ++i)
            walls[i].id = static_cast<int>(i);
        BuildWallGrid();
    }
    return static_cast<int>(removed);
}

int CGameWorld::RemoveWallsByIds(const std::vector<int>& ids)
{
    if (!m_map || ids.empty()) return 0;

    std::vector<bool> remove(m_map->walls.size(), false);
    for (const int id : ids)
        if (id >= 0 && id < static_cast<int>(remove.size())) remove[static_cast<size_t>(id)] = true;

    size_t removed = 0;
    auto& walls = m_map->walls;
    for (size_t i = 0; i < walls.size();)
    {
        if (!remove[i])
        {
            ++i;
            continue;
        }
        walls[i] = walls.back();
        walls.pop_back();
        remove[i] = remove.back();
        remove.pop_back();
        ++removed;
    }

    if (removed > 0)
    {
        for (size_t i = 0; i < walls.size(); ++i)
            walls[i].id = static_cast<int>(i);
        BuildWallGrid();
    }
    return static_cast<int>(removed);
}

bool CGameWorld::SweptCircleBlockedByWall(florrbt::Vector2f start, florrbt::Vector2f end, float radius, EWallQuery mask) const
{
    if (!m_map || m_map->walls.empty()) return false;

    radius = std::max(0.f, radius);
    florrbt::Vector2f center = (start + end) * 0.5f;
    float travel = Distance(start, end);
    float query_radius =
        travel * 0.5f + radius + std::max(0.f, game_config::gameworld_wall_swept_query_padding);
    bool blocked = false;
    m_wall_grid.ForEachInRangeBroadphase(center, query_radius, [&](FlorrBtMap::Wall* wall) {
        if (blocked || !wall || !IncludesWall(mask, wall->category)) return;
        if (SweptCircleHitsWall(start, end, radius, *wall)) blocked = true;
    });
    return blocked;
}

bool CGameWorld::SegmentBlockedByWall(florrbt::Vector2f start, florrbt::Vector2f end) const
{
    return SweptCircleBlockedByWall(start, end, 0.f);
}

bool CGameWorld::CircleBlockedByWall(florrbt::Vector2f center, float radius, EWallQuery mask) const
{
    return SweptCircleBlockedByWall(center, center, radius, mask);
}

std::optional<florrbt::Vector2f> CGameWorld::NearestUnblockedPosition(florrbt::Vector2f from, float radius) const
{
    if (!CircleBlockedByWall(from, radius)) return from;
    if (!m_map) return std::nullopt;
    std::optional<florrbt::Vector2f> best;
    float best_distance = std::numeric_limits<float>::max();
    const float padding = std::max(0.f, radius) + 1.f;
    // Rare control-loss path, not a per-frame scan. Candidate points lie just
    // outside polygon edges; test against the entire union, not one tile.
    for (const auto& wall : m_map->walls)
        for (int i = 0; i < WallSegmentCount(wall); ++i)
        {
            const auto a = WallPoint(wall.vertices[i]);
            const auto b = WallPoint(wall.vertices[(i + 1) % wall.vertices.size()]);
            const auto edge = b - a;
            const float length_sq = LengthSq(edge);
            if (length_sq <= 0.f) continue;
            const auto delta = from - a;
            const float t = std::clamp((delta.x * edge.x + delta.y * edge.y) / length_sq, 0.f, 1.f);
            const auto point = a + edge * t;
            const auto normal = florrbt::Vector2f{-edge.y, edge.x} / std::sqrt(length_sq);
            for (float sign : {-1.f, 1.f})
            {
                const auto candidate = point + normal * (padding * sign);
                const float distance = DistanceSq(from, candidate);
                if (distance >= best_distance || candidate.x < radius || candidate.y < radius ||
                    candidate.x > m_map->width * m_map->tile_width - radius ||
                    candidate.y > m_map->height * m_map->tile_height - radius ||
                    CircleBlockedByWall(candidate, radius)) continue;
                best = candidate; best_distance = distance;
            }
        }
    return best;
}

void CGameWorld::ConstrainControlledMotion(CMobBase& mob, florrbt::Vector2f start)
{
    const auto previous = mob.PrevPos();
    mob.PrevPos() = start;
    CEntity* entity = &mob;
    ResolveWallCollisions(std::span<CEntity* const>(&entity, 1));
    mob.PrevPos() = previous;
}

void CGameWorld::ResolveWallCollisions(std::span<CEntity* const> entities)
{
    if (!m_map || m_map->walls.empty()) return;

    const int max_wall_push_passes = std::max(1, game_config::entity_wall_push_passes);
    const float wall_query_slop = std::max(0.f, game_config::entity_wall_query_slop);

    auto find_swept_wall_hit = [this, wall_query_slop](florrbt::Vector2f start, florrbt::Vector2f end, float radius, EWallQuery mask,
                                                       const FlorrBtMap::Wall*& out_wall, int& out_segment) {
        out_wall = nullptr;
        out_segment = -1;

        florrbt::Vector2f center = (start + end) * 0.5f;
        float travel = Distance(start, end);
        float query_radius = travel * 0.5f + std::max(0.f, radius) + wall_query_slop;
        florrbt::Vector2f min_pos{ std::min(start.x, end.x), std::min(start.y, end.y) };
        florrbt::Vector2f max_pos{ std::max(start.x, end.x), std::max(start.y, end.y) };

        bool swept_hit = false;
        m_wall_grid.ForEachInRangeBroadphase(center, query_radius, m_wall_query_visited, [&](FlorrBtMap::Wall* wall) {
            if (swept_hit || !wall || !IncludesWall(mask, wall->category)) return;
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
            florrbt::Vector2f center = entity.Pos();
            float query_radius = std::max(1.f, wall_radius + wall_query_slop);

            m_wall_grid.ForEachInRangeBroadphase(
                center, query_radius, m_wall_query_visited, [&](FlorrBtMap::Wall* wall) {
                    if (destroyed_by_wall || !wall || !IncludesWall(entity.WallCollisionMask(), wall->category)) return;
                    if (!ExpandedWallBoundsMayTouch(*wall, entity.Pos(), entity.Pos(),
                                                    wall_radius + wall_query_slop))
                        return;

                    bool pushed = PushEntityOutOfWall(entity, *wall);
                    if (!pushed) return;

                    pushed_any = true;
                    int contact_segment = FindBlockingWallSegment(entity.Pos(), entity.Pos(), wall_radius, *wall);
                    if (HandleFiredMissileWallHit(entity, *wall, contact_segment, entity.Pos(), entity.Pos()) &&
                        entity.IsMarkedForDes())
                    {
                        destroyed_by_wall = true;
                        return;
                    }
                    HandleBumbleBeeWallHit(entity, *wall, contact_segment, entity.Pos(), entity.Pos());
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

        florrbt::Vector2f start = entity->PrevPos();
        florrbt::Vector2f end = entity->Pos();
        const float wall_radius = std::max(0.f, entity->WallCollisionRadius());
        float travel = Distance(start, end);

        bool swept_hit = false;
        const FlorrBtMap::Wall* blocking_wall = nullptr;
        int blocking_segment = -1;
        if (travel > game_config::entity_collision_epsilon)
            swept_hit = find_swept_wall_hit(start, end, wall_radius, entity->WallCollisionMask(), blocking_wall, blocking_segment);

        if (swept_hit)
        {
            if (blocking_wall && HandleBumbleBeeWallHit(*entity, *blocking_wall, blocking_segment, start, end))
            {
                continue;
            } else if (blocking_wall &&
                       HandleFiredMissileWallHit(*entity, *blocking_wall, blocking_segment, start, end))
            {
                if (entity->IsMarkedForDes() || !entity->CanCollide()) continue;
            } else
            {
                florrbt::Vector2f motion = end - start;
                florrbt::Vector2f wall_normal = { 0.f, 0.f };
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

                florrbt::Vector2f slide_start =
                    start + wall_normal * (wall_radius + game_config::gameworld_wall_contact_skin);
                entity->Pos() = slide_start;
                if (blocking_wall)
                {
                    florrbt::Vector2f slide_vel = ProjectEntityVelocityAlongWall(*entity, *blocking_wall, blocking_segment);
                    if (LengthSq(slide_vel) >
                        game_config::entity_collision_epsilon * game_config::entity_collision_epsilon)
                    {
                        florrbt::Vector2f slide_end = slide_start + slide_vel * game_config::server_fixed_dt;
                        const FlorrBtMap::Wall* slide_wall = nullptr;
                        int slide_segment = -1;
                        // The slide test runs on the circle-center path (radius
                        // 0): slide_start already rests the circle against the
                        // wall, so a full-radius test would always hit the wall
                        // it is sliding along and kill tangential motion.
                        if (!find_swept_wall_hit(slide_start, slide_end, 0.f, entity->WallCollisionMask(), slide_wall, slide_segment))
                        {
                            entity->Pos() = slide_end;
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
    const int velocity_passes = std::clamp(game_config::mob_contact_velocity_passes, 1, 8);
    auto& mob_contacts = m_collision_mob_contacts;
    mob_contacts.clear();
    if (velocity_passes > 1) mob_contacts.reserve(std::min<size_t>(entities.size() * 4, 65536));
    auto& inactive_collision_entities = m_collision_inactive_entities;
    inactive_collision_entities.clear();
    ++m_collision_sync_marker;
    if (m_collision_sync_marker == 0)
    {
        m_collision_sync_marker = 1;
        for (CEntity* entity : m_live_entities)
            if (entity) entity->CollisionSyncMarker() = 0;
    }

    // Build actual swept bounds once, instead of probing a fixed, oversized
    // endpoint circle per active entity. Both participants' motion matters.
    auto& sweep = m_contact_sweep;
    sweep.clear();
    sweep.reserve(m_live_entities.size());
    for (CEntity* entity : m_live_entities)
    {
        if (!entity || entity->GameWorld() != this || !entity->CanCollide()) continue;
        // Keep marked mobs in the contact broadphase so an already lethal
        // petal contact can still finish its pending retaliation. Other
        // marked entities remain excluded from collision processing.
        if (entity->IsMarkedForDes() && !dynamic_cast<CMobBase*>(entity)) continue;
        const bool active = entity->ActiveTickMarker() == m_active_tick_marker;
        const auto from = active ? entity->PrevPos() : entity->Pos();
        const auto bounds = florrbt::MakeSweptCircleBounds(from, entity->Pos(), entity->Radius());
        if (!std::isfinite(bounds.min.x) || !std::isfinite(bounds.min.y) ||
            !std::isfinite(bounds.max.x) || !std::isfinite(bounds.max.y)) continue;
        sweep.push_back({ bounds, entity, entity->Id(), active });
    }
    florrbt::VisitSweptCirclePairs(sweep, [&](const auto& first, const auto& second) {
        CEntity* entity = first.entity;
        CEntity* other = second.entity;
        bool other_active = second.active;
        if (!first.active || (second.active && entity->Id() > other->Id()))
        {
            std::swap(entity, other);
            other_active = first.active;
        }
        if (entity->GameWorld() != this || other->GameWorld() != this) return;
        if (!other_active) other->PrevPos() = other->Pos();
        const bool overlapping = entity->IsCollision(*other);
        const bool swept_contact = !overlapping && SweptProjectileTouches(*entity, *other);
        if (!overlapping && !swept_contact) return;
        const bool unloaded_mob = !other_active && other->IsEntityType(EEntityType::Mob);
        ResolveOverlappingEntityPair(entity, other, dt, !unloaded_mob, velocity_passes > 1 ? &mob_contacts : nullptr);
        if (!other_active && other->CollisionSyncMarker() != m_collision_sync_marker)
        {
            other->CollisionSyncMarker() = m_collision_sync_marker;
            inactive_collision_entities.push_back(other);
        }
    });

    // Reuse only the eligible contacts found by the broadphase. Alternate
    // sweep direction so pressure reaches through a crowd in either ID order.
    // Never re-run contact damage, poison, spawning or position correction.
    auto transfer_contact_velocity = [this](const auto& contact) {
        CMobBase* lhs = contact.first;
        CMobBase* rhs = contact.second;
        if (!CollisionPairAlive(lhs, rhs) || lhs->GameWorld() != this) return;
        const auto pair = BuildCollisionPairContext(lhs, rhs);
        if (ShouldIgnorePhaseCollision(pair) || ShouldIgnoreConstraintCollision(pair) ||
            ShouldIgnoreOwnershipCollision(pair) || ShouldIgnoreFriendlyCollision(pair) ||
            !CanResolvePhysicalCollision(pair)) return;
        const float transfer = ShouldResolveWeakSummonCollision(pair) ?
            game_config::world_mob_overlap_velocity_retention : 1.f;
        ResolveBodyContact(pair.lhs, pair.rhs, 0.f, transfer);
    };
    for (int pass = 1; pass < velocity_passes; ++pass)
    {
        if (pass % 2)
            for (auto it = mob_contacts.rbegin(); it != mob_contacts.rend(); ++it) transfer_contact_velocity(*it);
        else
            for (const auto& contact : mob_contacts) transfer_contact_velocity(contact);
    }

    for (CEntity* entity : inactive_collision_entities)
    {
        if (!entity || entity->GameWorld() != this || entity->IsMarkedForDes()) continue;
        m_spatial_grid.UpdateTracked(entity);
    }
}

void CGameWorld::Cleanup()
{
    // Removal hooks and owner cleanup can enqueue more entities. Drain the
    // whole dependency closure, including chains, before returning to callers.
    do
    {
        auto &clear_owned_owner_keys = m_clear_owned_owner_keys;
        auto &clear_summon_owner_keys = m_clear_summon_owner_keys;
        clear_owned_owner_keys.clear();
        clear_summon_owner_keys.clear();

        auto collect_clear_owner = [&clear_owned_owner_keys, &clear_summon_owner_keys](const CEntity *entity) {
            if (entity && entity->HasTag(EEntityTag::ClearOwnedEntitiesOnDestroy))
                clear_owned_owner_keys.emplace_back(entity->GetHandle());
            if (entity && entity->HasTag(EEntityTag::ClearOwnedSummonsOnDestroy))
                clear_summon_owner_keys.emplace_back(entity->GetHandle());
        };

        while (!m_cleanup_entities.empty())
        {
            CEntity *entity = m_cleanup_entities.back();
            m_cleanup_entities.pop_back();
            if (entity)
                entity->CleanupIndex() = std::numeric_limits<size_t>::max();
            if (!entity || entity->GameWorld() != this || !entity->IsMarkedForDes())
                continue;

            const EEntityRemovalReason removal_reason = entity->RemovalReason();
            entity->OnBeforeRemoved(removal_reason);
            if (entity->GameWorld() != this || !entity->IsMarkedForDes())
                continue;
            if (m_p_controller)
            {
                if (removal_reason == EEntityRemovalReason::Defeated)
                {
                    if (auto *mob = dynamic_cast<CMobBase *>(entity))
                        m_p_controller->OnMobDefeated(*this, *mob);
                }
                m_p_controller->OnEntityRemoved(*this, *entity, removal_reason);
            }
            if (entity->GameWorld() != this || !entity->IsMarkedForDes())
                continue;
            collect_clear_owner(entity);
            ReleaseCarriedRelations(*entity);

            const int id = entity->Id();
            UnregisterLiveEntity(entity);
            FreeID(id);

            if (id >= 0 && id < static_cast<int>(m_p_entities.size()) && m_p_entities[id].get() == entity)
            {
                m_p_entities[id].reset();
            }
            else
            {
                LOG_FATAL("gameworld", "Destroyed entity ID " + std::to_string(id) + " has no storage slot");
            }
        }

        auto handle_less = [](const SEntityHandle &lhs, const SEntityHandle &rhs) {
            if (lhs.world_id != rhs.world_id)
                return lhs.world_id < rhs.world_id;
            if (lhs.world_instance_generation != rhs.world_instance_generation)
                return lhs.world_instance_generation < rhs.world_instance_generation;
            if (lhs.id != rhs.id)
                return lhs.id < rhs.id;
            return lhs.generation < rhs.generation;
        };
        auto deduplicate = [&handle_less](auto &keys) {
            std::sort(keys.begin(), keys.end(), handle_less);
            keys.erase(std::unique(keys.begin(), keys.end()), keys.end());
        };

        deduplicate(clear_owned_owner_keys);
        deduplicate(clear_summon_owner_keys);

        auto has_owner_key = [&handle_less](const auto &keys, const SEntityHandle &owner_ref) {
            if (!owner_ref.IsValid())
                return false;
            return std::binary_search(keys.begin(), keys.end(), owner_ref, handle_less);
        };

        auto destroy_if_owned = [&clear_owned_owner_keys, &clear_summon_owner_keys, &has_owner_key](CEntity *entity) {
            auto *projectile = dynamic_cast<CProjectile *>(entity);
            if (projectile && has_owner_key(clear_owned_owner_keys, projectile->m_owner_ref))
                projectile->MarkForDestroy(EEntityRemovalReason::OwnerRemoved);

            auto *mob = dynamic_cast<CMobBase *>(entity);
            if (!mob)
                return;

            auto *controller = dynamic_cast<CSummonedMeleeController *>(mob->GetController());
            if (controller && has_owner_key(clear_summon_owner_keys, controller->GetOwnerRef()))
                mob->MarkForDestroy(EEntityRemovalReason::OwnerRemoved);
        };

        if (!clear_owned_owner_keys.empty() || !clear_summon_owner_keys.empty())
            ForEachEntity(destroy_if_owned);
        for (CChainEntity *chain : m_chains)
            chain->ValidateReferences();
    } while (!m_cleanup_entities.empty());
}

namespace
{
const CMobBase* ResolveLightningPulseHolder(const CEntity& origin, const CEntity* owner)
{
    const CEntity* current = owner ? owner : &origin;
    std::unordered_set<SEntityHandle, SEntityHandleHash> visited;
    for (int depth = 0; current && depth < 8; ++depth)
    {
        const SEntityHandle ref = current->GetHandle();
        if (ref.IsValid() && !visited.insert(ref).second) break;
        if (const auto* mob = dynamic_cast<const CMobBase*>(current)) return mob;
        const auto* projectile = dynamic_cast<const CProjectile*>(current);
        if (!projectile || !projectile->GetOwner() || projectile->GetOwner() == current) break;
        current = projectile->GetOwner();
    }
    return nullptr;
}

float LightningSourcePulseMultiplier(const CEntity& origin, const CEntity* owner)
{
    return GetPulseLightningDamageMultiplier(ResolveLightningPulseHolder(origin, owner));
}

bool DefaultMagicLightningBoostFilter(const CEntity& entity)
{
    if (dynamic_cast<const ILightningBoostSource*>(&entity)) return true;
    if (const auto* petal = dynamic_cast<const CPetal*>(&entity))
        return petal->GetPetalType() == EPetalType::Lightning || petal->GetPetalType() == EPetalType::Battery;
    return false;
}

float DefaultMagicLightningBoostReader(const CEntity& entity)
{
    float damage = 0.f;
    if (const auto* source = dynamic_cast<const ILightningBoostSource*>(&entity))
        damage = source->LightningBoostDamage();
    else if (const auto* petal = dynamic_cast<const CPetal*>(&entity))
    {
        if (petal->GetPetalType() == EPetalType::Lightning)
            damage = std::max(0.f, petal->m_final_petal_stats.damage);
        else if (petal->GetPetalType() == EPetalType::Battery)
            damage = std::max(0.f, game_config::default_battery_base_damage) *
                     std::pow(std::max(0.f, game_config::default_petal_pow),
                              static_cast<float>(std::max(1, GetLevel(petal->m_rarity)) - 1));
    }

    if (!std::isfinite(damage) || damage <= 0.f) return 0.f;
    return damage * LightningSourcePulseMultiplier(entity, nullptr);
}

ERarity ResolveMagicLightningSourceRarity(const CEntity& origin, const CEntity* owner)
{
    // A Magic Lightning petal is the origin, so its rarity must win over the
    // flower that owns it. For mob-triggered magic chains the mob is the
    // origin instead. The owner fallback covers projectiles and future chain
    // emitters without making the chain depend on a concrete caller type.
    if (const auto* mob = dynamic_cast<const CMobBase*>(&origin)) return mob->GetRarity();
    if (const auto* petal = dynamic_cast<const CPetal*>(&origin)) return petal->m_rarity;
    if (owner)
    {
        if (const auto* mob = dynamic_cast<const CMobBase*>(owner)) return mob->GetRarity();
        if (const auto* petal = dynamic_cast<const CPetal*>(owner)) return petal->m_rarity;
    }
    return ERarity::Null;
}

float MagicLightningBoostRarityScale(const CEntity& entity, ERarity rarity)
{
    if (rarity == ERarity::Null) return 0.f;
    if (const auto* petal = dynamic_cast<const CPetal*>(&entity))
    {
        if (petal->GetPetalType() == EPetalType::Lightning || petal->GetPetalType() == EPetalType::Battery)
            return std::pow(std::max(0.f, game_config::default_petal_pow),
                            static_cast<float>(std::max(0, GetLevel(rarity) - 1)));
    }
    return 1.f;
}

float LimitMagicLightningBoostToSourceRarity(const CEntity& target, float value, ERarity source_rarity)
{
    if (!std::isfinite(value) || value <= 0.f || source_rarity == ERarity::Null) return 0.f;

    ERarity target_rarity = ERarity::Null;
    if (const auto* mob = dynamic_cast<const CMobBase*>(&target)) target_rarity = mob->GetRarity();
    else if (const auto* petal = dynamic_cast<const CPetal*>(&target)) target_rarity = petal->m_rarity;

    if (target_rarity == ERarity::Null ||
        GetRaritySortRank(target_rarity) <= GetRaritySortRank(source_rarity))
        return value;

    if (const auto* source = dynamic_cast<const ILightningBoostSource*>(&target))
    {
        const float capped = source->LightningBoostDamageAtRarity(source_rarity);
        if (!std::isfinite(capped) || capped < 0.f) return value;
        return capped * LightningSourcePulseMultiplier(target, nullptr);
    }

    const float target_scale = MagicLightningBoostRarityScale(target, target_rarity);
    const float source_scale = MagicLightningBoostRarityScale(target, source_rarity);
    if (!std::isfinite(target_scale) || target_scale <= game_config::entity_collision_epsilon ||
        !std::isfinite(source_scale) || source_scale < 0.f)
        return value;
    return value * std::clamp(source_scale / target_scale, 0.f, 1.f);
}

florrbt::Vector2f RandomLightningPathPoint(const std::vector<florrbt::Vector2f>& points, float max_step)
{
    if (points.empty()) return {};
    max_step = std::max(game_config::entity_collision_epsilon, max_step);

    florrbt::Vector2f heading{};
    float average_step = 0.f;
    int segment_count = 0;
    const size_t first = points.size() > 4 ? points.size() - 4 : 1;
    for (size_t i = first; i < points.size(); ++i)
    {
        const florrbt::Vector2f segment = points[i] - points[i - 1];
        const float length = Length(segment);
        if (length <= game_config::entity_collision_epsilon) continue;
        const float weight = 1.f + static_cast<float>(i - first) * 0.35f;
        heading += segment / length * weight;
        average_step += length;
        ++segment_count;
    }

    if (LengthSq(heading) <= game_config::entity_collision_epsilon)
    {
        const float angle = GetLimitedRng(-game_config::pi, game_config::pi);
        heading = { std::cos(angle), std::sin(angle) };
    }
    else
        heading = NormalizeOrZero(heading);

    const float turn = GetLimitedRng(-game_config::pi * 0.42f, game_config::pi * 0.42f);
    heading = { heading.x * std::cos(turn) - heading.y * std::sin(turn),
                heading.x * std::sin(turn) + heading.y * std::cos(turn) };
    const float observed_step = segment_count > 0 ? average_step / static_cast<float>(segment_count)
                                                   : max_step * 0.72f;
    const float base_step = std::clamp(observed_step, max_step * 0.38f, max_step * 0.92f);
    const float step = base_step * GetLimitedRng(0.72f, 1.08f);
    return points.back() + heading * std::min(step, max_step);
}
} // namespace

void CGameWorld::ChainLightning(CEntity& origin, float damage, int bounces, CEntity* owner,
                                const std::function<bool(const CMobBase&)>& target_filter)
{
    if (bounces <= 0 || damage <= 0.f) return;

    damage *= LightningSourcePulseMultiplier(origin, owner);

    float humidity = 0.f;
    if (const CGameContext* context = GameContext())
        humidity = context->Extensions().Globals().Find<IWeatherDiurnalView>()->GetWeatherDiurnalSnapshot().meteorology.humidity;
    const float damage_multiplier = lightning_humidity::DamageMultiplier(humidity);
    const float range_multiplier = lightning_humidity::RangeMultiplier(humidity);
    bounces = lightning_humidity::ScaledBounces(bounces, humidity);

    std::vector<florrbt::Vector2f> chain;
    chain.push_back(origin.Pos());

    florrbt::Vector2f current_pos = origin.Pos();
    float current_radius = std::max(0.f, origin.Radius());
    std::unordered_set<SEntityHandle, SEntityHandleHash> struck;
    const SEntityHandle origin_ref = origin.GetHandle();
    const SEntityHandle owner_ref = owner ? owner->GetHandle() : SEntityHandle{};
    const float one_tile = game_config::WorldUnits(256.f); // one world tile

    bounces = std::min(bounces, k_max_network_lightning_hops);
    for (int bounce = 0; bounce < bounces; ++bounce)
    {
        const float query_radius = current_radius + one_tile * range_multiplier;
        CMobBase* best = nullptr;
        float best_dist_sq = std::numeric_limits<float>::max();

        ForEachEntityInEdgeRange(current_pos, query_radius, [&](const CEntity* entity) {
            const auto* mob = dynamic_cast<const CMobBase*>(entity);
            if (!mob || mob->IsMarkedForDes() || mob->IsDead()) return;
            if (origin_ref.RefersTo(mob->GetHandle())) return;
            if (owner_ref.RefersTo(mob->GetHandle())) return;
            if (struck.contains(mob->GetHandle())) return;
            if (mob->Team() == (owner ? owner->Team() : origin.Team())) return;
            if (mob->HasState<CNullificationState>()) return;
            if (target_filter && !target_filter(*mob)) return;

            const float dist_sq = DistanceSq(current_pos, mob->Pos());
            const float edge = std::sqrt(dist_sq) - mob->Radius() - current_radius;
            if (edge > one_tile * range_multiplier) return;

            if (dist_sq < best_dist_sq)
            {
                best_dist_sq = dist_sq;
                best = const_cast<CMobBase*>(mob);
            }
        });

        if (!best)
        {
            if (GetLimitedRng(0.f, 1.f) >= 0.5f) break;
            current_pos = RandomLightningPathPoint(chain, one_tile * range_multiplier);
            current_radius = 0.f;
            chain.push_back(current_pos);
            continue;
        }
        struck.insert(best->GetHandle());
        best->TakeDamage(damage * damage_multiplier, owner, EDamageType::Lighting);
        best->m_vel *= std::max(0.f, game_config::chain_lightning_speed_multiplier);
        chain.push_back(best->Pos());
        current_pos = best->Pos();
        current_radius = std::max(0.f, best->Radius());
    }

    if (chain.size() <= 1) return;
    CGameContext* context = GameContext();
    if (!context) return;
    auto* network = dynamic_cast<INetworkModule*>(&context->Network());
    if (!network) return;
    network->BroadcastLightningChain(*this, chain, 0);
}

void CGameWorld::MagicChainLightning(CEntity& origin, float damage, int boost_bounces, int damage_bounces,
                                     CEntity* owner, const std::function<bool(const CEntity&)>& boost_filter,
                                     const std::function<float(const CEntity&)>& boost_reader,
                                     const std::function<bool(const CMobBase&)>& damage_filter)
{
    boost_bounces = std::max(0, boost_bounces);
    damage_bounces = std::max(0, damage_bounces);
    if (boost_bounces <= 0 && damage_bounces <= 0) return;

    damage = std::isfinite(damage) ? std::max(0.f, damage) * LightningSourcePulseMultiplier(origin, owner) : 0.f;
    float humidity = 0.f;
    if (const CGameContext* context = GameContext())
        humidity = context->Extensions().Globals().Find<IWeatherDiurnalView>()->GetWeatherDiurnalSnapshot().meteorology.humidity;
    const float damage_multiplier = lightning_humidity::DamageMultiplier(humidity);
    const float range_multiplier = lightning_humidity::RangeMultiplier(humidity);
    boost_bounces = lightning_humidity::ScaledBounces(boost_bounces, humidity);
    damage_bounces = lightning_humidity::ScaledBounces(damage_bounces, humidity);

    const std::function<bool(const CEntity&)> accepts_boost =
        boost_filter ? boost_filter : std::function<bool(const CEntity&)>(DefaultMagicLightningBoostFilter);
    const std::function<float(const CEntity&)> read_boost =
        boost_reader ? boost_reader : std::function<float(const CEntity&)>(DefaultMagicLightningBoostReader);
    const ERarity source_rarity = ResolveMagicLightningSourceRarity(origin, owner);
    const SEntityHandle owner_ref = owner ? owner->GetHandle() : SEntityHandle{};
    const int owner_team = owner ? owner->Team() : origin.Team();
    const float one_tile = game_config::WorldUnits(256.f);

    std::vector<florrbt::Vector2f> chain;
    chain.push_back(origin.Pos());
    florrbt::Vector2f current_pos = origin.Pos();
    float current_radius = std::max(0.f, origin.Radius());
    std::unordered_set<SEntityHandle, SEntityHandleHash> visited;
    if (origin.GetHandle().IsValid()) visited.insert(origin.GetHandle());
    if (owner_ref.IsValid()) visited.insert(owner_ref);

    auto in_range = [&](const CEntity& candidate) {
        const float dist_sq = DistanceSq(current_pos, candidate.Pos());
        const float edge = std::sqrt(dist_sq) - candidate.Radius() - current_radius;
        return edge <= one_tile * range_multiplier;
    };

    auto find_boost_target = [&]() -> CEntity* {
        if (boost_bounces <= 0) return nullptr;
        CEntity* best = nullptr;
        float best_dist_sq = std::numeric_limits<float>::max();
        const float query_radius = current_radius + one_tile * range_multiplier;
        ForEachEntityInEdgeRange(current_pos, query_radius, [&](const CEntity* candidate) {
            if (!candidate || candidate->IsMarkedForDes() || candidate->IsDead()) return;
            if (!candidate->GetHandle().IsValid() || visited.contains(candidate->GetHandle())) return;
            if (!accepts_boost(*candidate) || !in_range(*candidate)) return;
            const float dist_sq = DistanceSq(current_pos, candidate->Pos());
            if (dist_sq < best_dist_sq)
            {
                best_dist_sq = dist_sq;
                best = const_cast<CEntity*>(candidate);
            }
        });
        return best;
    };

    auto find_damage_target = [&]() -> CMobBase* {
        if (damage_bounces <= 0) return nullptr;
        CMobBase* best = nullptr;
        float best_dist_sq = std::numeric_limits<float>::max();
        const float query_radius = current_radius + one_tile * range_multiplier;
        ForEachEntityInEdgeRange(current_pos, query_radius, [&](const CEntity* candidate) {
            const auto* mob = dynamic_cast<const CMobBase*>(candidate);
            if (!mob || mob->IsMarkedForDes() || mob->IsDead()) return;
            if (!mob->GetHandle().IsValid() || visited.contains(mob->GetHandle())) return;
            if (mob->Team() == owner_team || mob->HasState<CNullificationState>()) return;
            if (damage_filter && !damage_filter(*mob)) return;
            if (!in_range(*mob)) return;
            const float dist_sq = DistanceSq(current_pos, mob->Pos());
            if (dist_sq < best_dist_sq)
            {
                best_dist_sq = dist_sq;
                best = const_cast<CMobBase*>(mob);
            }
        });
        return best;
    };

    // Add in a wider type: both budgets are configurable and an overflowing
    // signed sum must never turn the loop bound negative. One byte is used by
    // the origin node, leaving this many network-safe hops.
    const std::int64_t requested_hops =
        static_cast<std::int64_t>(boost_bounces) + static_cast<std::int64_t>(damage_bounces);
    const int max_hops = static_cast<int>(
        std::min<std::int64_t>(k_max_network_lightning_hops, requested_hops));
    for (int hop = 0; hop < max_hops && (boost_bounces > 0 || damage_bounces > 0); ++hop)
    {
        CEntity* target = nullptr;
        bool is_boost = false;
        if (boost_bounces > 0)
        {
            target = find_boost_target();
            is_boost = target != nullptr;
            if (!target) target = find_damage_target();
        }
        else
        {
            target = find_damage_target();
            if (!target)
            {
                target = find_boost_target();
                is_boost = target != nullptr;
            }
        }
        if (!target)
        {
            if (GetLimitedRng(0.f, 1.f) >= 0.5f) break;
            current_pos = RandomLightningPathPoint(chain, one_tile * range_multiplier);
            current_radius = 0.f;
            chain.push_back(current_pos);
            if (boost_bounces > 0) --boost_bounces;
            else --damage_bounces;
            continue;
        }

        visited.insert(target->GetHandle());
        if (is_boost)
        {
            // Enemy boost sources are struck by the chain's accumulated
            // damage before their lightning value is read into the chain.
            // This ordering is observable when the hit destroys or otherwise
            // changes the source, so keep TakeDamage ahead of read_boost.
            if (!CheckTeam(owner_team, target->Team()) && damage > 0.f)
            {
                target->TakeDamage(damage * damage_multiplier, owner, EDamageType::Lighting);
                if (auto* mob = dynamic_cast<CMobBase*>(target))
                    mob->m_vel *= std::max(0.f, game_config::chain_lightning_speed_multiplier);
            }
            const float added = LimitMagicLightningBoostToSourceRarity(*target, read_boost(*target), source_rarity);
            if (std::isfinite(added) && added > 0.f) damage += added;
            --boost_bounces;
        }
        else
        {
            auto* mob = static_cast<CMobBase*>(target);
            if (damage > 0.f) mob->TakeDamage(damage * damage_multiplier, owner, EDamageType::Lighting);
            mob->m_vel *= std::max(0.f, game_config::chain_lightning_speed_multiplier);
            --damage_bounces;
        }
        chain.push_back(target->Pos());
        current_pos = target->Pos();
        current_radius = std::max(0.f, target->Radius());
    }

    if (chain.size() <= 1) return;
    CGameContext* context = GameContext();
    if (!context) return;
    auto* network = dynamic_cast<INetworkModule*>(&context->Network());
    if (!network) return;
    network->BroadcastLightningChain(*this, chain, 1);
}
