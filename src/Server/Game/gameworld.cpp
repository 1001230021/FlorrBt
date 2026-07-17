#include "gameworld.h"
#include "controllers/melee_controller.h"
#include "entities/drop.h"
#include "entities/flower.h"
#include "entities/mob.h"
#include "entities/petals/petal.h"
#include "entities/portal.h"
#include "entities/projectile.h"
#include "gamecontext.h"
#include "player.h"
#include "states/states.h"
#include "zone_mob_tools.h"
#include "../../Engine/logger.h"
#include "../Module/network_module.h"
#include "../../Shared/game_config.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cctype>
#include <filesystem>
#include <limits>
#include <random>
#include <utility>

namespace
{
constexpr const char* default_map_path = "data/maps/garden.tmj";
constexpr const char* new_player_checkpoint_name = "new_players";
constexpr int transfer_spawn_position_attempts = 32;

struct summon_owner_link
{
    int direct_owner_id = -1;
    int root_owner_id = -1;
};

struct active_tick_view
{
    sf::Vector2f center;
    float radius = 0.f;
};

float Dot(sf::Vector2f a, sf::Vector2f b);

std::string WarpKey(std::string text)
{
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char ch)
    {
        return static_cast<char>(std::tolower(ch));
    });
    return text;
}

bool RandomPointInTransferCheckpoint(const FlorrBtMap::Checkpoint& checkpoint, sf::Vector2f& out_pos)
{
    if (checkpoint.w <= 0.f || checkpoint.h <= 0.f) return false;

    std::uniform_real_distribution<float> random_x(checkpoint.x, checkpoint.x + checkpoint.w);
    std::uniform_real_distribution<float> random_y(checkpoint.y, checkpoint.y + checkpoint.h);
    out_pos = {random_x(GetRng()), random_y(GetRng())};
    return true;
}

bool RandomPointInTransferZone(const FlorrBtMap::Zone& zone, sf::Vector2f& out_pos)
{
    if (zone.vertices.size() < 3) return false;

    float min_x = std::numeric_limits<float>::max();
    float min_y = std::numeric_limits<float>::max();
    float max_x = std::numeric_limits<float>::lowest();
    float max_y = std::numeric_limits<float>::lowest();
    for (const auto& vertex : zone.vertices)
    {
        min_x = std::min(min_x, vertex.x);
        min_y = std::min(min_y, vertex.y);
        max_x = std::max(max_x, vertex.x);
        max_y = std::max(max_y, vertex.y);
    }
    if (min_x >= max_x || min_y >= max_y) return false;

    std::uniform_real_distribution<float> random_x(min_x, max_x);
    std::uniform_real_distribution<float> random_y(min_y, max_y);
    for (int attempt = 0; attempt < transfer_spawn_position_attempts; ++attempt)
    {
        sf::Vector2f candidate = {random_x(GetRng()), random_y(GetRng())};
        if (!IsPointInZone(zone, candidate)) continue;
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
            if (checkpoint.name == new_player_checkpoint_name) continue;
            if (checkpoint.w <= 0.f || checkpoint.h <= 0.f) continue;
            checkpoints.push_back(&checkpoint);
        }
        if (!checkpoints.empty())
        {
            std::uniform_int_distribution<size_t> dist(0, checkpoints.size() - 1);
            sf::Vector2f pos;
            if (RandomPointInTransferCheckpoint(*checkpoints[dist(GetRng())], pos)) return pos;
        }

        std::vector<const FlorrBtMap::Zone*> zones;
        for (const FlorrBtMap::Zone& zone : map->zones)
        {
            if (zone.vertices.size() >= 3) zones.push_back(&zone);
        }
        if (!zones.empty())
        {
            std::uniform_int_distribution<size_t> dist(0, zones.size() - 1);
            for (size_t attempt = 0; attempt < zones.size(); ++attempt)
            {
                sf::Vector2f pos;
                if (RandomPointInTransferZone(*zones[dist(GetRng())], pos)) return pos;
            }
        }
    }

    return {game_config::player_respawn_x, game_config::player_respawn_y};
}

summon_owner_link GetSummonOwnerLink(const CEntity* entity)
{
    summon_owner_link link;
    if (!entity) return link;

    auto* mob = dynamic_cast<const CMobBase*>(entity);
    if (!mob) return link;

    auto* controller = dynamic_cast<CSummonedMeleeController*>(const_cast<CMobBase*>(mob)->GetController());
    if (!controller) return link;

    link.direct_owner_id = controller->GetOwnerId();
    link.root_owner_id = link.direct_owner_id;

    CGameWorld* world = const_cast<CMobBase*>(mob)->GameWorld();
    CEntity* owner = world ? world->GetEntity(link.direct_owner_id) : nullptr;
    auto* owner_projectile = dynamic_cast<CProjectile*>(owner);
    if (owner_projectile) link.root_owner_id = owner_projectile->m_owner_id;
    return link;
}

bool HasSummonOwnerLink(const summon_owner_link& link)
{
    return link.direct_owner_id >= 0;
}

bool IsPetalOwnedBy(const CEntity* entity, int owner_id)
{
    auto* projectile = dynamic_cast<const CProjectile*>(entity);
    return projectile && projectile->m_owner_id == owner_id;
}

bool IsSummonOwnerLink(const summon_owner_link& link, const CEntity* other)
{
    if (!HasSummonOwnerLink(link) || !other) return false;
    if (other->m_id == link.direct_owner_id || other->m_id == link.root_owner_id) return true;
    return IsPetalOwnedBy(other, link.root_owner_id);
}

bool ShouldSkipSummonCollision(const CEntity* lhs, const CEntity* rhs)
{
    return IsSummonOwnerLink(GetSummonOwnerLink(lhs), rhs) || IsSummonOwnerLink(GetSummonOwnerLink(rhs), lhs);
}

bool IsSameTeamSummonCollision(const CEntity* lhs, const CEntity* rhs)
{
    summon_owner_link lhs_link = GetSummonOwnerLink(lhs);
    summon_owner_link rhs_link = GetSummonOwnerLink(rhs);
    return HasSummonOwnerLink(lhs_link) && HasSummonOwnerLink(rhs_link) && CheckTeam(lhs->m_team, rhs->m_team);
}

bool IsSameRootOwnerSummonCollision(const CEntity* lhs, const CEntity* rhs)
{
    summon_owner_link lhs_link = GetSummonOwnerLink(lhs);
    summon_owner_link rhs_link = GetSummonOwnerLink(rhs);
    return HasSummonOwnerLink(lhs_link) && HasSummonOwnerLink(rhs_link) &&
           lhs_link.root_owner_id >= 0 && lhs_link.root_owner_id == rhs_link.root_owner_id;
}

bool ApplyWeakSummonCollision(CEntity* lhs, CEntity* rhs)
{
    if (!lhs || !rhs || !lhs->IsCollision(*rhs)) return false;

    sf::Vector2f delta = rhs->m_pos - lhs->m_pos;
    float dist_sq = LengthSq(delta);
    float dist = std::sqrt(std::max(0.f, dist_sq));
    sf::Vector2f normal;
    if (dist > game_config::entity_collision_epsilon)
    {
        normal = delta / dist;
    } else {
        uint32_t seed = static_cast<uint32_t>(lhs->m_id) * 1103515245u +
                        static_cast<uint32_t>(rhs->m_id) * 2654435761u;
        float angle = static_cast<float>(seed % 6283u) / 1000.f;
        normal = {std::cos(angle), std::sin(angle)};
        dist = 0.f;
    }

    float overlap = lhs->m_radius + rhs->m_radius - dist;
    if (overlap <= 0.f) return true;

    float lhs_inv_mass = lhs->m_mass > game_config::entity_collision_epsilon ? 1.f / lhs->m_mass : 1.f;
    float rhs_inv_mass = rhs->m_mass > game_config::entity_collision_epsilon ? 1.f / rhs->m_mass : 1.f;
    float inv_total = std::max(game_config::entity_collision_epsilon, lhs_inv_mass + rhs_inv_mass);
    constexpr float weak_push = 0.32f;
    lhs->m_pos -= normal * (overlap * weak_push * (lhs_inv_mass / inv_total));
    rhs->m_pos += normal * (overlap * weak_push * (rhs_inv_mass / inv_total));

    if (auto* mob = dynamic_cast<CMobBase*>(lhs))
    {
        float into_other = Dot(mob->m_vel, normal);
        if (into_other > 0.f) mob->m_vel -= normal * (into_other * 0.35f);
    }
    if (auto* mob = dynamic_cast<CMobBase*>(rhs))
    {
        float into_other = Dot(mob->m_vel, -normal);
        if (into_other > 0.f) mob->m_vel += normal * (into_other * 0.35f);
    }
    return true;
}

bool IsSameTeamSandstormMobCollision(const CMobBase* lhs, const CMobBase* rhs)
{
    if (!lhs || !rhs) return false;
    if (lhs->m_mob_type != EMobType::Sandstorm && rhs->m_mob_type != EMobType::Sandstorm) return false;
    return CheckTeam(lhs->m_team, rhs->m_team);
}

std::vector<active_tick_view> BuildActiveTickViews(CGameWorld& world)
{
    std::vector<active_tick_view> views;
    CGameContext* context = world.GameContext();
    if (!context) return views;

    const auto& players = context->Players();
    views.reserve(players.size());
    for (const auto& player : players)
    {
        if (!player || !player->IsConnected() || !player->IsAuthenticated()) continue;

        CEntity* owner = player->GetEntity();
        auto* owner_mob = dynamic_cast<CMobBase*>(owner);
        const SMobStats* stats = owner_mob ? owner_mob->GetFinalStats() : nullptr;
        if (owner && owner->GameWorld() != &world) continue;
        if (!owner || owner->m_is_marked_for_des || !stats) continue;

        float radius = std::max(0.f, stats->horizon) * 2.f;
        if (game_config::simulation_active_view_radius_cap > 0.f)
            radius = std::min(radius, game_config::simulation_active_view_radius_cap);
        views.push_back({owner->m_pos, radius});
    }
    return views;
}

bool IsSameTeamPetalFlowerCollision(const CEntity* lhs, const CEntity* rhs)
{
    const auto* lhs_petal = dynamic_cast<const CPetal*>(lhs);
    const auto* rhs_petal = dynamic_cast<const CPetal*>(rhs);
    const auto* lhs_flower = dynamic_cast<const CFlower*>(lhs);
    const auto* rhs_flower = dynamic_cast<const CFlower*>(rhs);
    if (lhs_petal && rhs_flower && lhs_petal->GetOwner() != rhs && CheckTeam(lhs_petal->m_team, rhs_flower->m_team))
        return true;
    if (rhs_petal && lhs_flower && rhs_petal->GetOwner() != lhs && CheckTeam(rhs_petal->m_team, lhs_flower->m_team))
        return true;
    return false;
}

bool IsWaxPetal(const CEntity* entity)
{
    const auto* petal = dynamic_cast<const CPetal*>(entity);
    return petal && petal->m_type == EPetalType::Wax;
}

bool IsWaxPair(const CEntity* lhs, const CEntity* rhs)
{
    return IsWaxPetal(lhs) || IsWaxPetal(rhs);
}

bool IsSameTeamPetalWaxPair(const CEntity* lhs, const CEntity* rhs)
{
    if (!IsWaxPair(lhs, rhs)) return false;
    const auto* lhs_petal = dynamic_cast<const CPetal*>(lhs);
    const auto* rhs_petal = dynamic_cast<const CPetal*>(rhs);
    return lhs_petal && rhs_petal && CheckTeam(lhs->m_team, rhs->m_team);
}

bool IsSameTeamSummonWaxPair(const CEntity* lhs, const CEntity* rhs)
{
    if (!IsWaxPair(lhs, rhs) || !CheckTeam(lhs->m_team, rhs->m_team)) return false;

    const CEntity* other = IsWaxPetal(lhs) ? rhs : lhs;
    return HasSummonOwnerLink(GetSummonOwnerLink(other));
}

void DamageYggdrasilOnEnemyContact(CPetal* petal, CMobBase* mob)
{
    if (!petal || !mob || petal->m_type != EPetalType::Yggdrasil) return;
    const SMobStats* stats = mob->GetFinalStats();
    if (!stats) return;
    petal->TakeDamage(stats->damage, mob, EDamageType::Normal);
}

bool IsHealPetalType(EPetalType type)
{
    return type == EPetalType::Rose || type == EPetalType::Dahlia;
}

bool IsRoseTeamHealCollision(const CEntity* lhs, const CEntity* rhs)
{
    if (!lhs || !rhs || !CheckTeam(lhs->m_team, rhs->m_team)) return false;

    const auto* lhs_petal = dynamic_cast<const CPetal*>(lhs);
    const auto* rhs_petal = dynamic_cast<const CPetal*>(rhs);
    const auto* lhs_mob = dynamic_cast<const CMobBase*>(lhs);
    const auto* rhs_mob = dynamic_cast<const CMobBase*>(rhs);

    if (lhs_petal && IsHealPetalType(lhs_petal->m_type) && rhs_mob &&
        lhs_petal->m_target_entity_id == rhs->m_id)
        return true;
    if (rhs_petal && IsHealPetalType(rhs_petal->m_type) && lhs_mob &&
        rhs_petal->m_target_entity_id == lhs->m_id)
        return true;
    return false;
}

sf::Vector2f WallPoint(const FlorrBtMap::Point& point)
{
    return {point.x, point.y};
}

float Dot(sf::Vector2f a, sf::Vector2f b)
{
    return a.x * b.x + a.y * b.y;
}

float Cross(sf::Vector2f a, sf::Vector2f b)
{
    return a.x * b.y - a.y * b.x;
}

sf::Vector2f NormalizeOrZero(sf::Vector2f value)
{
    float len = Length(value);
    if (len <= game_config::entity_collision_epsilon) return {0.f, 0.f};
    return value / len;
}

bool BounceFiredCarrotFromEntity(CPetal* petal, const CEntity* target)
{
    auto* missile = dynamic_cast<CMissilePetal*>(petal);
    if (!missile || !target || missile->m_type != EPetalType::Carrot || !missile->m_fired) return false;

    sf::Vector2f normal = NormalizeOrZero(missile->m_pos - target->m_pos);
    if (LengthSq(normal) <= game_config::entity_collision_epsilon)
        normal = NormalizeOrZero(-missile->m_vel);
    if (LengthSq(normal) <= game_config::entity_collision_epsilon)
        normal = {1.f, 0.f};

    if (Dot(missile->m_vel, normal) > 0.f)
        normal = -normal;

    sf::Vector2f old_vel = missile->m_vel;
    float speed = Length(old_vel);
    float into_entity = Dot(old_vel, normal);
    missile->m_pos = target->m_pos + normal * (target->m_radius + missile->m_radius + 0.02f);
    missile->m_vel = old_vel - normal * (2.f * into_entity);
    if (LengthSq(missile->m_vel) <= game_config::entity_collision_epsilon && speed > game_config::entity_collision_epsilon)
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
    if (const auto* missile = dynamic_cast<const CMissilePetal*>(petal);
        missile && missile->m_fired)
        return 1;

    float reload = EffectivePetalReload(petal);
    if (reload <= game_config::entity_collision_epsilon || reload >= dt) return 1;

    constexpr int max_hits_per_tick = 64;
    float& credit = petal->m_hit_credits[target->m_id];
    float total = credit + dt / reload;
    int hits = static_cast<int>(std::floor(total));
    credit = total - static_cast<float>(hits);
    return std::clamp(hits, 1, max_hits_per_tick);
}

bool PetalUsesPhysicalHitCompensation(const CPetal* petal)
{
    if (!petal) return false;

    switch (petal->m_type)
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
    float hit_spacing = std::max(1.f, std::min(attacker->m_radius, target->m_radius) * 0.5f);

    constexpr int max_physical_hits_per_tick = 8;
    int hits = 1 + static_cast<int>(std::floor(relative_sweep / hit_spacing));
    return std::clamp(hits, 1, max_physical_hits_per_tick);
}

int PetalPhysicalHitCountForTick(CPetal* petal, const CEntity* target, float dt)
{
    int hit_count = PetalHitCountForTick(petal, target, dt);
    if (PetalUsesPhysicalHitCompensation(petal))
        hit_count = std::max(hit_count, PhysicalHitCountForTick(petal, target));
    return hit_count;
}

void ApplyPhysicalDamageHits(CEntity* target, float damage, CEntity* attacker, int hit_count)
{
    if (!target || damage <= 0.f) return;

    for (int hit = 0; hit < hit_count; ++hit)
    {
        if (target->m_is_marked_for_des || target->IsDead() || target->m_health <= 0.f) break;
        target->TakeDamage(damage, attacker, EDamageType::Normal);
    }
}

void ApplyPetalHits(CPetal* petal, CMobBase* target, float dt)
{
    if (!petal || !target) return;

    int hit_count = PetalPhysicalHitCountForTick(petal, target, dt);
    const CPetalPrototype* proto = FindPetalPrototype(petal->m_type);
    for (int hit = 0; hit < hit_count; ++hit)
    {
        if (hit > 0 && (petal->m_is_marked_for_des || petal->m_health <= 0.f)) break;
        if (target->m_is_marked_for_des) break;

        float damage = petal->m_final_petal_stats.damage;
        if (proto && proto->m_p_behavior)
            proto->m_p_behavior->OnPetalHit(petal, petal->m_rarity, target, damage);
        if (damage <= 0.f) break;
        if (damage > 0.f)
            target->TakeDamage(damage, petal->GetOwner(), EDamageType::Normal);
    }
}

void ApplyMobHitToPetal(CMobBase* mob, CPetal* petal)
{
    if (!mob || !petal || mob->m_is_marked_for_des || petal->m_is_marked_for_des) return;

    const SMobStats* stats = mob->GetFinalStats();
    if (!stats || stats->damage <= 0.f) return;

    ApplyPhysicalDamageHits(petal, stats->damage, mob, PhysicalHitCountForTick(mob, petal));
}

bool DamageAttachedMissile(CMissile* missile, CEntity* target, float dt)
{
    if (!missile || !target || !missile->IsAttachedToOwner() || target == missile->GetOwner()) return false;

    if (auto* petal = dynamic_cast<CPetal*>(target))
    {
        int hit_count = PetalPhysicalHitCountForTick(petal, missile, dt);
        for (int hit = 0; hit < hit_count; ++hit)
        {
            if (missile->m_is_marked_for_des || missile->m_health <= 0.f) break;
            float damage = petal->m_final_petal_stats.damage;
            if (damage <= 0.f) break;
            missile->TakeDamage(damage, petal->GetOwner(), EDamageType::Normal);
        }
        return true;
    }

    if (auto* mob = dynamic_cast<CMobBase*>(target))
    {
        if (const SMobStats* stats = mob->GetFinalStats())
        {
            if (stats->damage > 0.f)
                ApplyPhysicalDamageHits(missile, stats->damage, mob,
                                        PhysicalHitCountForTick(mob, missile));
        }
        return true;
    }

    return false;
}

bool ApplyMissileHit(CMissile* missile, CEntity* target, float dt)
{
    if (!missile || !target || dynamic_cast<CDrop*>(target)) return false;
    if (!dynamic_cast<CMobBase*>(target) && !dynamic_cast<CPetal*>(target)) return false;
    if (missile->IsAttachedToOwner()) return DamageAttachedMissile(missile, target, dt);
    return missile->ApplyHit(target);
}

bool ApplyPollenHit(CPollenProjectile* pollen, CEntity* target)
{
    if (!pollen || !target || dynamic_cast<CDrop*>(target)) return false;
    auto* mob = dynamic_cast<CMobBase*>(target);
    auto* petal = dynamic_cast<CPetal*>(target);
    if (!mob && !petal) return false;

    if (!pollen->ApplyHit(target)) return false;

    float retaliation = 0.f;
    CEntity* retaliation_owner = target;
    if (mob)
    {
        if (const SMobStats* stats = mob->GetFinalStats()) retaliation = stats->damage;
    } else if (petal) {
        retaliation = petal->m_final_petal_stats.damage;
        retaliation_owner = petal->GetOwner();
    }
    if (retaliation > 0.f)
        pollen->TakeDamage(retaliation, retaliation_owner, EDamageType::Normal);
    return true;
}

void ApplyBodyPoison(CMobBase* attacker, CEntity* target)
{
    auto* target_mob = dynamic_cast<CMobBase*>(target);
    if (!attacker || !target_mob || target_mob == attacker) return;

    if (attacker->m_mob_type == EMobType::Spider)
    {
        const int level = GetLevel(attacker->GetRarity());
        const float duration = game_config::mob_spider_poison_duration;
        const float total_poison =
            game_config::mob_spider_poison_total * std::pow(3.f, static_cast<float>(level - 1));
        if (duration > 0.f && total_poison > 0.f)
        {
            auto poison = std::make_unique<CPoisonState>(target_mob, duration, total_poison / duration,
                                                         attacker->GetRarity(), attacker);
            if (poison->IsValid()) target_mob->AddState(std::move(poison));
        }
    }

    auto* flower = dynamic_cast<CFlower*>(attacker);
    if (!flower) return;

    const SFlowerStats* stats = flower->GetFinalStats();
    if (!stats || stats->body_poison_damage_multiplier <= 0.f || stats->body_poison_duration <= 0.f) return;

    float total_poison = stats->damage * stats->body_poison_damage_multiplier;
    float poison_per_second = total_poison / stats->body_poison_duration;
    auto poison = std::make_unique<CPoisonState>(target_mob, stats->body_poison_duration, poison_per_second,
                                                 flower->GetRarity(), flower);
    if (poison->IsValid()) target_mob->AddState(std::move(poison));
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
        if (rr <= game_config::entity_collision_epsilon) return DistanceSq(a, c) <= game_config::entity_collision_epsilon;
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
        bool intersects = ((a.y > point.y) != (b.y > point.y)) &&
                          (point.x < (b.x - a.x) * (point.y - a.y) / (b.y - a.y + 0.000001f) + a.x);
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
    sf::Vector2f min_pos{std::min(start.x, end.x), std::min(start.y, end.y)};
    sf::Vector2f max_pos{std::max(start.x, end.x), std::max(start.y, end.y)};
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

sf::Vector2f WallSegmentTangent(const FlorrBtMap::Wall& wall, int segment_index)
{
    if (segment_index < 0 || wall.vertices.empty()) return {0.f, 0.f};
    sf::Vector2f a = WallPoint(wall.vertices[segment_index]);
    sf::Vector2f b = WallPoint(wall.vertices[(segment_index + 1) % wall.vertices.size()]);
    return NormalizeOrZero(b - a);
}

sf::Vector2f ProjectMobVelocityAlongWall(CEntity& entity, const FlorrBtMap::Wall& wall, int segment_index)
{
    auto* mob = dynamic_cast<CMobBase*>(&entity);
    if (!mob) return {0.f, 0.f};

    sf::Vector2f tangent = WallSegmentTangent(wall, segment_index);
    if (LengthSq(tangent) <= game_config::entity_collision_epsilon) return {0.f, 0.f};

    mob->m_vel = tangent * Dot(mob->m_vel, tangent);
    return mob->m_vel;
}

sf::Vector2f ChooseWallNormal(const FlorrBtMap::Wall& wall, int segment_index, sf::Vector2f point)
{
    sf::Vector2f a = WallPoint(wall.vertices[segment_index]);
    sf::Vector2f b = WallPoint(wall.vertices[(segment_index + 1) % wall.vertices.size()]);
    sf::Vector2f edge = b - a;
    sf::Vector2f n1 = NormalizeOrZero({-edge.y, edge.x});
    if (LengthSq(n1) <= game_config::entity_collision_epsilon) return {1.f, 0.f};
    sf::Vector2f n2 = -n1;

    if (wall.closed)
    {
        bool n1_outside = !PointInWallPolygon(point + n1 * 1.0f, wall);
        bool n2_outside = !PointInWallPolygon(point + n2 * 1.0f, wall);
        if (n1_outside != n2_outside) return n1_outside ? n1 : n2;
    }

    float side = Cross(edge, point - a);
    if (std::abs(side) <= game_config::entity_collision_epsilon) return CheckChance(0.5f) ? n1 : n2;
    return side > 0.f ? n1 : n2;
}

bool HandleFiredMissileWallHit(CEntity& entity, const FlorrBtMap::Wall& wall, int segment_index,
                               sf::Vector2f start, sf::Vector2f end)
{
    if (auto* raw_missile = dynamic_cast<CMissile*>(&entity))
    {
        if (raw_missile->IsAttachedToOwner()) return true;
        raw_missile->m_is_marked_for_des = true;
        return true;
    }

    if (auto* missile = dynamic_cast<CMissilePetal*>(&entity);
        missile && missile->m_fired)
    {
        if (missile->m_type != EPetalType::Carrot)
        {
            missile->m_is_marked_for_des = true;
            return true;
        }

        if (segment_index < 0)
            segment_index = FindBlockingWallSegment(start, end, entity.m_radius, wall);

        sf::Vector2f normal = segment_index >= 0 ? ChooseWallNormal(wall, segment_index, start) :
                                                  NormalizeOrZero(start - end);
        if (LengthSq(normal) <= game_config::entity_collision_epsilon)
            normal = NormalizeOrZero(-missile->m_vel);
        if (LengthSq(normal) <= game_config::entity_collision_epsilon)
            normal = {1.f, 0.f};

        if (Dot(missile->m_vel, normal) > 0.f)
            normal = -normal;

        sf::Vector2f old_vel = missile->m_vel;
        float speed = Length(old_vel);
        float into_wall = Dot(old_vel, normal);
        missile->m_pos = start + normal * 0.02f;
        missile->m_vel = old_vel - normal * (2.f * into_wall);
        if (LengthSq(missile->m_vel) <= game_config::entity_collision_epsilon && speed > game_config::entity_collision_epsilon)
            missile->m_vel = normal * speed;
        if (LengthSq(missile->m_vel) > game_config::entity_collision_epsilon)
        {
            missile->m_facing_angle = std::atan2(missile->m_vel.y, missile->m_vel.x);
            missile->m_has_facing = true;
        }
        return true;
    }

    if (auto* thrown = dynamic_cast<CThrownPetal*>(&entity);
        thrown && thrown->m_thrown)
    {
        thrown->StopThrow(thrown->m_destroy_when_stopped);
        return true;
    }

    return false;
}

bool HandleBumbleBeeWallHit(CEntity& entity, const FlorrBtMap::Wall& wall, int segment_index,
                            sf::Vector2f start, sf::Vector2f end)
{
    auto* mob = dynamic_cast<CMobBase*>(&entity);
    if (!mob || mob->m_mob_type != EMobType::BumbleBee) return false;

    if (segment_index < 0)
        segment_index = FindBlockingWallSegment(start, end, entity.m_radius, wall);

    sf::Vector2f normal = segment_index >= 0 ? ChooseWallNormal(wall, segment_index, start) :
                                              NormalizeOrZero(start - end);
    if (LengthSq(normal) <= game_config::entity_collision_epsilon)
        normal = NormalizeOrZero(-mob->m_vel);
    if (LengthSq(normal) <= game_config::entity_collision_epsilon)
        normal = {1.f, 0.f};

    if (Dot(mob->m_vel, normal) > 0.f)
        normal = -normal;

    sf::Vector2f old_vel = mob->m_vel;
    float speed = Length(old_vel);
    if (speed <= game_config::entity_collision_epsilon)
    {
        const SMobStats* stats = mob->GetFinalStats();
        speed = stats ? stats->max_velocity : game_config::default_max_velocity;
        old_vel = -normal * speed;
    }

    float into_wall = Dot(old_vel, normal);
    mob->m_pos = start + normal * (entity.m_radius * 0.05f + 0.02f);
    mob->m_vel = old_vel - normal * (2.f * into_wall);
    if (LengthSq(mob->m_vel) <= game_config::entity_collision_epsilon)
        mob->m_vel = normal * speed;

    mob->m_facing_angle = std::atan2(mob->m_vel.y, mob->m_vel.x);
    mob->m_has_facing = true;
    return true;
}

bool PushEntityOutOfWall(CEntity& entity, const FlorrBtMap::Wall& wall)
{
    if (!ExpandedWallBoundsMayTouch(wall, entity.m_pos, entity.m_pos, entity.m_radius)) return false;

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
    if (!inside && dist >= entity.m_radius) return false;

    sf::Vector2f normal = inside ? ChooseWallNormal(wall, best_segment, best_point) : NormalizeOrZero(entity.m_pos - best_point);
    if (LengthSq(normal) <= game_config::entity_collision_epsilon)
        normal = ChooseWallNormal(wall, best_segment, entity.m_pos);

    float penetration = inside ? entity.m_radius + dist : entity.m_radius - dist;
    entity.m_pos += normal * (penetration + 0.01f);

    if (auto* mob = dynamic_cast<CMobBase*>(&entity))
    {
        float into_wall = Dot(mob->m_vel, normal);
        if (into_wall < 0.f) mob->m_vel -= normal * into_wall;
        if (LengthSq(mob->m_vel) <= game_config::entity_collision_epsilon && penetration > 0.f)
            mob->m_vel = normal * 0.01f;
    }
    return true;
}

}

CGameWorld::CGameWorld()
    : CGameWorld(default_map_path)
{
}

CGameWorld::CGameWorld(const std::string& path)
    : m_map(LoadMapFromTmj(path)),
      m_map_path(std::filesystem::path(path).generic_string()),
      m_spatial_grid(
          game_config::spatial_grid_cell_size, [](const CEntity& entity) { return entity.m_pos; },
          [](const CEntity& entity) { return entity.m_id; },
          [this](int id) { return GetEntity(id); },
          [](const CEntity& entity) { return entity.m_id >= 0 && !entity.m_is_marked_for_des; }),
      m_wall_grid(
          game_config::spatial_grid_cell_size, [](const FlorrBtMap::Wall& wall) { return sf::Vector2f{wall.center.x, wall.center.y}; },
          [](const FlorrBtMap::Wall& wall) { return wall.id; },
          [this](int id) { return GetWall(id); },
          [](const FlorrBtMap::Wall& wall) { return wall.id >= 0; })
{
    if (!m_map)
        LOG_ERROR("gameworld", "Failed to load map: " + path);
    BuildWallGrid();
    SpawnMapPortals();
}

CGameWorld::~CGameWorld()
{
    m_p_entities.clear();
    m_p_entity_refs.clear();
}

int CGameWorld::GetNewID()
{
    if (!m_free_ids.empty())
    {
        int id = m_free_ids.back();
        m_free_ids.pop_back();
        return id;
    }
    return m_next_id++;
}

void CGameWorld::FreeID(int id)
{
    if (std::find(m_free_ids.begin(), m_free_ids.end(), id) != m_free_ids.end())
    {
        LOG_FATAL("gameworld", "Free ID " + std::to_string(id) + " twice");
        return;
    }

    m_free_ids.push_back(id);
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
    return raw_entity;
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

    entity->m_is_marked_for_des = true;
}

void CGameWorld::DestroyProjectilesOwnedBy(int owner_id)
{
    if (owner_id < 0) return;

    auto destroy_if_owned = [owner_id](CEntity* entity)
    {
        auto* projectile = dynamic_cast<CProjectile*>(entity);
        if (projectile && projectile->m_owner_id == owner_id) projectile->m_is_marked_for_des = true;
    };

    for (const auto& entity : m_p_entities)
        if (entity) destroy_if_owned(entity.get());
    for (CEntity* entity : m_p_entity_refs)
        if (entity) destroy_if_owned(entity);
}

void CGameWorld::SpawnMapPortals()
{
    if (!m_map) return;

    for (const FlorrBtMap::Warp& warp : m_map->warps)
    {
        if (warp.goal.empty()) continue;
        float radius = warp.radius > 0.f ? warp.radius : game_config::portal_radius;
        auto portal = std::make_unique<CPortal>(this, sf::Vector2f{warp.x, warp.y}, radius, warp.goal);
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
        if (std::optional<sf::Vector2f> warp_pos = target_world.FindWarpPoint(*from))
            target_pos = warp_pos;
    }
    if (!target_pos)
        target_pos = PickDefaultTransferSpawnPosition(target_world);

    CEntity* entity = player.GetEntity();
    if (!entity)
    {
        LOG_WARN("gameworld", "Transfer failed: player " + std::to_string(player.GetId()) + " has no entity");
        return nullptr;
    }
    if (entity->GameWorld() != this)
    {
        LOG_WARN("gameworld", "Transfer failed: player " + std::to_string(player.GetId()) +
                              " is not controlled by this world");
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
        LOG_WARN("gameworld", "Transfer failed: entity " + std::to_string(old_id) +
                              " is not owned by this world");
        return nullptr;
    }

    if (auto* flower = dynamic_cast<CFlower*>(entity))
        flower->DestroyPetalEntities();
    DestroyProjectilesOwnedBy(old_id);

    std::unique_ptr<CEntity> moved_entity = std::move(m_p_entities[old_id]);

    const int new_id = target_world.GetNewID();
    if (new_id >= static_cast<int>(target_world.m_p_entities.size()))
        target_world.m_p_entities.resize(new_id + 1);
    if (target_world.m_p_entities[new_id] != nullptr)
    {
        target_world.FreeID(new_id);
        moved_entity->m_id = old_id;
        moved_entity->m_generation = old_generation;
        moved_entity->SetGameWorld(this);
        m_p_entities[old_id] = std::move(moved_entity);
        player.SetOwnedEntity(m_p_entities[old_id].get());
        LOG_ERROR("gameworld", "Transfer failed: target ID " + std::to_string(new_id) + " is occupied");
        return nullptr;
    }

    moved_entity->m_id = new_id;
    moved_entity->m_generation = target_world.m_next_generation++;
    if (target_world.m_next_generation == 0) target_world.m_next_generation = 1;
    moved_entity->SetGameWorld(&target_world);
    if (target_pos)
        moved_entity->m_pos = *target_pos;
    moved_entity->m_prev_pos = moved_entity->m_pos;

    CEntity* transferred = moved_entity.get();
    target_world.m_p_entities[new_id] = std::move(moved_entity);
    FreeID(old_id);
    m_spatial_grid.Clear();

    player.SetOwnedEntity(transferred);
    player.m_logged_missing_entity = false;
    if (CGameContext* context = target_world.GameContext())
        context->Network().NotifyPlayerWorldChanged(player);
    LOG_INFO("gameworld", "Transferred player " + std::to_string(player.GetId()) + " entity " +
                          std::to_string(old_id) + " -> " + std::to_string(transferred->m_id));
    return transferred;
}

CEntity* CGameWorld::TransferPlayerEntityToWorld(CPlayer& player, CGameWorld& target_world, const std::string& from)
{
    return TransferPlayerEntityToWorld(player, target_world, std::nullopt, from);
}

void CGameWorld::Tick(float dt)
{
    m_tick_entities.clear();
    CollectEntities(m_tick_entities);

    std::vector<CEntity*>& entities = m_tick_entities;
    m_cached_max_entity_radius = 0.f;
    for (const CEntity* entity : entities)
    {
        if (entity && !entity->m_is_marked_for_des)
            m_cached_max_entity_radius = std::max(m_cached_max_entity_radius, std::max(0.f, entity->m_radius));
    }
    m_spatial_grid.Rebuild(entities);

    std::vector<active_tick_view> active_tick_views = BuildActiveTickViews(*this);
    m_active_entities.clear();
    std::vector<CEntity*>& active_entities = m_active_entities;
    active_entities.reserve(entities.size());

    ++m_active_tick_marker;
    if (m_active_tick_marker == 0)
    {
        m_active_tick_marker = 1;
        for (CEntity* entity : entities)
            if (entity) entity->m_active_tick_marker = 0;
    }

    auto add_active_entity = [this, &active_entities](CEntity* entity)
    {
        if (!entity || entity->m_is_marked_for_des) return;
        if (entity->m_active_tick_marker == m_active_tick_marker) return;
        entity->m_active_tick_marker = m_active_tick_marker;
        active_entities.push_back(entity);
    };

    for (CEntity* entity : entities)
    {
        if (!entity) continue;
        entity->m_prev_pos = entity->m_pos;
        if (!entity->m_allow_skip_tick) add_active_entity(entity);
    }

    for (const active_tick_view& view : active_tick_views)
    {
        float query_radius = view.radius + std::max(m_cached_max_entity_radius, game_config::default_flower_radius);
        m_spatial_grid.ForEachInRange(view.center, query_radius, [&](CEntity* entity)
        {
            if (!entity || !entity->m_allow_skip_tick) return;
            float radius = view.radius + std::max(0.f, entity->m_radius);
            if (DistanceSq(entity->m_pos, view.center) > radius * radius) return;
            add_active_entity(entity);
        });
    }

    for (CEntity* entity : active_entities)
    {
        if (!entity || entity->m_is_marked_for_des) continue;
        if (entity->GameWorld() != this) continue;
        if (!entity->m_skip_world_tick) entity->Tick(dt);
    }

    if (m_p_controller)
        m_p_controller->OnTick(*this, dt);

    auto remove_transferred = [this](CEntity* entity)
    {
        return !entity || entity->GameWorld() != this;
    };
    active_entities.erase(std::remove_if(active_entities.begin(), active_entities.end(), remove_transferred),
                          active_entities.end());
    entities.erase(std::remove_if(entities.begin(), entities.end(), remove_transferred), entities.end());

    ResolveWallCollisions(active_entities);

    m_spatial_grid.Rebuild(entities);

    ResolveCollisions(active_entities, dt);

    Cleanup();
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
    float query_radius = max_edge_range + std::max(m_cached_max_entity_radius, game_config::default_flower_radius);
    m_spatial_grid.ForEachInRange(center, query_radius, [&](CEntity* candidate)
    {
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
    result.clear();
    result.reserve(m_p_entities.size() + m_p_entity_refs.size());
    for (const auto& entity : m_p_entities)
    {
        if (entity) result.push_back(entity.get());
    }
    for (CEntity* entity : m_p_entity_refs)
    {
        if (entity) result.push_back(entity);
    }
}

std::vector<CEntity*> CGameWorld::GetAllEntities() const
{
    std::vector<CEntity*> result;
    CollectEntities(result);
    return result;
}

std::string CGameWorld::GetMapName() const
{
    return std::filesystem::path(m_map_path).stem().generic_string();
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
    return sf::Vector2f{warp->x, warp->y};
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

bool CGameWorld::SegmentBlockedByWall(sf::Vector2f start, sf::Vector2f end) const
{
    if (!m_map || m_map->walls.empty()) return false;

    sf::Vector2f center = (start + end) * 0.5f;
    float travel = Distance(start, end);
    float query_radius = travel * 0.5f + 1.f;
    bool blocked = false;
    m_wall_grid.ForEachInRangeBroadphase(center, query_radius, [&](FlorrBtMap::Wall* wall)
    {
        if (blocked || !wall) return;
        if (SweptCircleHitsWall(start, end, 0.f, *wall)) blocked = true;
    });
    return blocked;
}

void CGameWorld::ResolveWallCollisions(const std::vector<CEntity*>& entities)
{
    if (!m_map || m_map->walls.empty()) return;

    constexpr int max_wall_push_passes = 4;
    constexpr float wall_query_slop = 2.f;

    auto find_swept_wall_hit = [this](sf::Vector2f start, sf::Vector2f end, float radius,
                                      const FlorrBtMap::Wall*& out_wall, int& out_segment)
    {
        out_wall = nullptr;
        out_segment = -1;

        sf::Vector2f center = (start + end) * 0.5f;
        float travel = Distance(start, end);
        float query_radius = travel * 0.5f + std::max(0.f, radius) + wall_query_slop;
        sf::Vector2f min_pos{std::min(start.x, end.x), std::min(start.y, end.y)};
        sf::Vector2f max_pos{std::max(start.x, end.x), std::max(start.y, end.y)};

        bool swept_hit = false;
        m_wall_grid.ForEachInRangeBroadphase(center, query_radius, m_wall_query_visited, [&](FlorrBtMap::Wall* wall)
        {
            if (swept_hit || !wall) return;
            if (!ExpandedWallBoundsMayTouch(*wall, min_pos, max_pos, radius + wall_query_slop)) return;
            if (!SweptCircleHitsWall(start, end, radius, *wall)) return;

            swept_hit = true;
            out_wall = wall;
            out_segment = FindBlockingWallSegment(start, end, radius, *wall);
        });
        return swept_hit;
    };

    auto push_out_of_current_walls = [this](CEntity& entity)
    {
        bool destroyed_by_wall = false;
        for (int pass = 0; pass < max_wall_push_passes; ++pass)
        {
            bool pushed_any = false;
            sf::Vector2f center = entity.m_pos;
            float query_radius = std::max(1.f, entity.m_radius + wall_query_slop);

            m_wall_grid.ForEachInRangeBroadphase(center, query_radius, m_wall_query_visited, [&](FlorrBtMap::Wall* wall)
            {
                if (destroyed_by_wall || !wall) return;
                if (!ExpandedWallBoundsMayTouch(*wall, entity.m_pos, entity.m_pos,
                                                entity.m_radius + wall_query_slop))
                    return;

                bool pushed = PushEntityOutOfWall(entity, *wall);
                if (!pushed) return;

                pushed_any = true;
                int contact_segment = FindBlockingWallSegment(entity.m_pos, entity.m_pos, entity.m_radius, *wall);
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
        if (!entity || entity->GameWorld() != this || entity->m_is_marked_for_des || !entity->CanCollide()) continue;
        if (auto* raw_missile = dynamic_cast<CMissile*>(entity);
            raw_missile && raw_missile->IsAttachedToOwner())
            continue;

        sf::Vector2f start = entity->m_prev_pos;
        sf::Vector2f end = entity->m_pos;
        float travel = Distance(start, end);

        bool swept_hit = false;
        const FlorrBtMap::Wall* blocking_wall = nullptr;
        int blocking_segment = -1;
        if (travel > game_config::entity_collision_epsilon)
            swept_hit = find_swept_wall_hit(start, end, entity->m_radius, blocking_wall, blocking_segment);

        if (swept_hit)
        {
            if (blocking_wall && HandleBumbleBeeWallHit(*entity, *blocking_wall, blocking_segment, start, end))
            {
                continue;
            } else if (blocking_wall && HandleFiredMissileWallHit(*entity, *blocking_wall, blocking_segment, start, end))
            {
                if (entity->m_is_marked_for_des) continue;
            } else {
                entity->m_pos = start;
                if (blocking_wall)
                {
                    sf::Vector2f slide_vel = ProjectMobVelocityAlongWall(*entity, *blocking_wall, blocking_segment);
                    if (LengthSq(slide_vel) > game_config::entity_collision_epsilon)
                    {
                        sf::Vector2f slide_end = start + slide_vel * game_config::server_fixed_dt;
                        const FlorrBtMap::Wall* slide_wall = nullptr;
                        int slide_segment = -1;
                        if (!find_swept_wall_hit(start, slide_end, entity->m_radius, slide_wall, slide_segment))
                        {
                            entity->m_pos = slide_end;
                        } else if (auto* mob = dynamic_cast<CMobBase*>(entity))
                        {
                            mob->m_vel = {0.f, 0.f};
                        }
                    }
                }
            }
        }

        if (entity->m_is_marked_for_des) continue;
        push_out_of_current_walls(*entity);
    }
}

void CGameWorld::ResolveCollisions(const std::vector<CEntity*>& entities, float dt)
{
    float normal_max_radius = 0.f;
    float large_max_radius = 0.f;
    const float large_radius_threshold = std::max(game_config::spatial_grid_cell_size * 0.5f, game_config::default_horizon * 0.25f);
    std::vector<CEntity*> normal_entities;
    std::vector<std::pair<float, CEntity*>> large_entities;
    normal_entities.reserve(entities.size());

    for (const CEntity* entity : entities)
    {
        if (!entity || entity->GameWorld() != this || entity->m_is_marked_for_des || !entity->CanCollide()) continue;

        if (entity->m_radius > large_radius_threshold)
        {
            large_max_radius = std::max(large_max_radius, entity->m_radius);
            large_entities.emplace_back(entity->m_pos.x, const_cast<CEntity*>(entity));
        } else {
            normal_max_radius = std::max(normal_max_radius, entity->m_radius);
            normal_entities.push_back(const_cast<CEntity*>(entity));
        }
    }
    std::sort(large_entities.begin(), large_entities.end(),
              [](const auto& lhs, const auto& rhs) { return lhs.first < rhs.first; });

    auto process_pair = [dt](CEntity* entity, CEntity* other)
    {
        if (!other || other->GameWorld() != entity->GameWorld() || other->m_is_marked_for_des) return;
        if (!other->CanCollide() || other == entity) return;
        if (!entity->IsCollision(*other)) return;

        bool rose_team_heal_collision = IsRoseTeamHealCollision(entity, other);
        if (const auto* projectile = dynamic_cast<const CProjectile*>(entity);
            projectile && projectile->GetOwner() == other && !rose_team_heal_collision && !IsWaxPetal(projectile))
            return;
        if (const auto* projectile = dynamic_cast<const CProjectile*>(other);
            projectile && projectile->GetOwner() == entity && !rose_team_heal_collision && !IsWaxPetal(projectile))
            return;
        auto* entity_petal = dynamic_cast<CPetal*>(entity);
        auto* other_petal = dynamic_cast<CPetal*>(other);
        const bool wax_pair = IsWaxPair(entity, other);

        if (wax_pair)
        {
            if (IsSameTeamPetalWaxPair(entity, other) || IsSameTeamSummonWaxPair(entity, other))
                return;
        } else {
            if (ShouldSkipSummonCollision(entity, other) && !rose_team_heal_collision) return;
            if ((IsSameTeamSummonCollision(entity, other) || IsSameRootOwnerSummonCollision(entity, other)) &&
                !rose_team_heal_collision)
            {
                ApplyWeakSummonCollision(entity, other);
                return;
            }
            if (IsSameTeamPetalFlowerCollision(entity, other) && !rose_team_heal_collision) return;
        }

        if (BlocksNullifiedInteraction(entity, other) && !rose_team_heal_collision) return;
        auto* entity_missile = dynamic_cast<CMissile*>(entity);
        auto* other_missile = dynamic_cast<CMissile*>(other);
        auto* entity_pollen = dynamic_cast<CPollenProjectile*>(entity);
        auto* other_pollen = dynamic_cast<CPollenProjectile*>(other);
        if (entity_petal && other_petal && !wax_pair) return;
        auto* entity_mob = dynamic_cast<CMobBase*>(entity);
        auto* other_mob = dynamic_cast<CMobBase*>(other);
        if (IsSameTeamSandstormMobCollision(entity_mob, other_mob) && !rose_team_heal_collision) return;

        if (!rose_team_heal_collision && ((!entity_missile && !other_missile) || wax_pair))
        {
            entity->OnCollision(other);
            other->OnCollision(entity);
        }

        bool same_team = CheckTeam(entity->m_team, other->m_team);
        if (same_team && !rose_team_heal_collision) return;

        if (entity_missile)
        {
            ApplyMissileHit(entity_missile, other, dt);
            return;
        }

        if (other_missile)
        {
            ApplyMissileHit(other_missile, entity, dt);
            return;
        }

        if (entity_pollen)
        {
            ApplyPollenHit(entity_pollen, other);
            return;
        }

        if (other_pollen)
        {
            ApplyPollenHit(other_pollen, entity);
            return;
        }

        if (entity_petal && other_mob)
        {
            if (entity_petal->m_type == EPetalType::Yggdrasil)
            {
                DamageYggdrasilOnEnemyContact(entity_petal, other_mob);
                return;
            }

            ApplyPetalHits(entity_petal, other_mob, dt);
            ApplyMobHitToPetal(other_mob, entity_petal);
            BounceFiredCarrotFromEntity(entity_petal, other);
            return;
        }

        if (other_petal && entity_mob)
        {
            if (other_petal->m_type == EPetalType::Yggdrasil)
            {
                DamageYggdrasilOnEnemyContact(other_petal, entity_mob);
                return;
            }

            ApplyPetalHits(other_petal, entity_mob, dt);
            ApplyMobHitToPetal(entity_mob, other_petal);
            BounceFiredCarrotFromEntity(other_petal, entity);
            return;
        }

        if (auto* mob = entity_mob)
        {
            if (!other_petal)
            {
                if (const SMobStats* stats = mob->GetFinalStats())
                {
                    ApplyPhysicalDamageHits(other, stats->damage, entity,
                                            PhysicalHitCountForTick(entity, other));
                    ApplyBodyPoison(mob, other);
                }
            }
        }
        if (auto* mob = other_mob)
        {
            if (!entity_petal)
            {
                if (const SMobStats* stats = mob->GetFinalStats())
                {
                    ApplyPhysicalDamageHits(entity, stats->damage, other,
                                            PhysicalHitCountForTick(other, entity));
                    ApplyBodyPoison(mob, entity);
                }
            }
        }
    };

    auto process_large_candidates = [&](CEntity* entity, bool require_id_order)
    {
        if (!entity || large_entities.empty() || large_max_radius <= 0.f) return;

        const float min_x = entity->m_pos.x - entity->m_radius - large_max_radius;
        const float max_x = entity->m_pos.x + entity->m_radius + large_max_radius;
        auto it = std::lower_bound(
            large_entities.begin(), large_entities.end(), min_x,
            [](const auto& entry, float value) { return entry.first < value; });
        for (; it != large_entities.end() && it->first <= max_x; ++it)
        {
            CEntity* large = it->second;
            if (!large || large == entity) continue;
            if (require_id_order && entity->m_id >= large->m_id) continue;

            const float reach = entity->m_radius + large->m_radius;
            if (std::abs(entity->m_pos.y - large->m_pos.y) > reach) continue;
            process_pair(entity, large);
        }
    };

    for (CEntity* entity : normal_entities)
    {
        m_spatial_grid.ForEachInRange(entity->m_pos, entity->m_radius + normal_max_radius, [&](CEntity* other)
        {
            if (!other || other->m_radius > large_radius_threshold) return;
            if (entity->m_id >= other->m_id) return;
            process_pair(entity, other);
        });
        process_large_candidates(entity, false);
    }

    for (const auto& entry : large_entities)
    {
        process_large_candidates(entry.second, true);
    }
}

void CGameWorld::Cleanup()
{
    for (size_t i = 0; i < m_p_entities.size(); ++i)
    {
        if (m_p_entities[i] && m_p_entities[i]->m_is_marked_for_des)
        {
            if (m_p_controller) m_p_controller->OnEntityDie(*this, m_p_entities[i].get());
            DestroyProjectilesOwnedBy(m_p_entities[i]->m_id);
            FreeID(m_p_entities[i]->m_id);
            m_p_entities[i].reset();
        }
    }

    for (CEntity*& entity : m_p_entity_refs)
    {
        if (entity && entity->m_is_marked_for_des)
        {
            if (m_p_controller) m_p_controller->OnEntityDie(*this, entity);
            DestroyProjectilesOwnedBy(entity->m_id);
            FreeID(entity->m_id);
            entity = nullptr;
        }
    }
}
