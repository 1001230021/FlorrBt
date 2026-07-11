#include "gameworld.h"
#include "controllers/melee_controller.h"
#include "entities/mob.h"
#include "entities/petals/petal.h"
#include "entities/projectile.h"
#include "states/states.h"
#include "../../Shared/game_config.h"
#include <limits>

namespace
{
constexpr const char* default_map_path = "data/maps/training_grounds.tmj";

struct summon_owner_link
{
    int direct_owner_id = -1;
    int root_owner_id = -1;
};

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

void DamageYggdrasilOnEnemyContact(CPetal* petal, CMobBase* mob)
{
    if (!petal || !mob || petal->m_type != EPetalType::Yggdrasil) return;
    const SMobStats* stats = mob->GetFinalStats();
    if (!stats) return;
    petal->TakeDamage(stats->damage, mob, EDamageType::Normal);
}

bool IsRoseTeamHealCollision(const CEntity* lhs, const CEntity* rhs)
{
    if (!lhs || !rhs || !CheckTeam(lhs->m_team, rhs->m_team)) return false;

    const auto* lhs_petal = dynamic_cast<const CPetal*>(lhs);
    const auto* rhs_petal = dynamic_cast<const CPetal*>(rhs);
    const auto* lhs_mob = dynamic_cast<const CMobBase*>(lhs);
    const auto* rhs_mob = dynamic_cast<const CMobBase*>(rhs);

    if (lhs_petal && lhs_petal->m_type == EPetalType::Rose && rhs_mob &&
        lhs_petal->m_target_entity_id == rhs->m_id)
        return true;
    if (rhs_petal && rhs_petal->m_type == EPetalType::Rose && lhs_mob &&
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
        int id = *m_free_ids.begin();
        m_free_ids.erase(m_free_ids.begin());
        return id;
    }
    return m_next_id++;
}

void CGameWorld::FreeID(int id)
{
    if (m_free_ids.find(id) != m_free_ids.end())
    {
        LOG_FATAL("gameworld", "Free ID " + std::to_string(id) + " twice");
        return;
    }

    m_free_ids.insert(id);
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
    entity->m_id = id;
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
    entity->m_id = id;
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

    for (CEntity* entity : GetAllEntities())
    {
        auto* projectile = dynamic_cast<CProjectile*>(entity);
        if (projectile && projectile->m_owner_id == owner_id) projectile->m_is_marked_for_des = true;
    }
}

void CGameWorld::Tick(float dt)
{
    std::vector<CEntity*> entities = GetAllEntities();
    m_spatial_grid.Rebuild(entities);

    for (CEntity* entity : entities)
    {
        if (entity) entity->m_prev_pos = entity->m_pos;
        if (entity && !entity->m_skip_world_tick) entity->Tick(dt);
    }

    if (m_p_controller)
        m_p_controller->OnTick(*this, dt);

    ResolveWallCollisions(entities);
    m_spatial_grid.Rebuild(entities);
    ResolveCollisions(entities);
    Cleanup();
}

CEntity* CGameWorld::GetEntity(int id) const
{
    if (id < 0) return nullptr;

    if (id < static_cast<int>(m_p_entities.size()) && m_p_entities[id]) return m_p_entities[id].get();
    if (id < static_cast<int>(m_p_entity_refs.size())) return m_p_entity_refs[id];
    return nullptr;
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
    for (CEntity* candidate : GetAllEntities())
    {
        if (!candidate) continue;
        if (filter && !filter(candidate)) continue;
        float edge_distance = std::max(0.f, Distance(center, candidate->m_pos) - candidate->m_radius);
        if (edge_distance <= best_edge_distance)
        {
            best_edge_distance = edge_distance;
            target = candidate;
        }
    }
    return target;
}

std::vector<CEntity*> CGameWorld::GetAllEntities() const
{
    std::vector<CEntity*> result;
    result.reserve(m_p_entities.size() + m_p_entity_refs.size());
    for (const auto& entity : m_p_entities)
    {
        if (entity) result.push_back(entity.get());
    }
    for (CEntity* entity : m_p_entity_refs)
    {
        if (entity) result.push_back(entity);
    }
    return result;
}

FlorrBtMap::Wall* CGameWorld::GetWall(int id) const
{
    if (!m_map || id < 0 || id >= static_cast<int>(m_map->walls.size())) return nullptr;
    return const_cast<FlorrBtMap::Wall*>(&m_map->walls[id]);
}

void CGameWorld::BuildWallGrid()
{
    m_wall_grid.Clear();
    m_max_wall_query_radius = 0.f;
    if (!m_map) return;

    std::vector<FlorrBtMap::Wall*> walls;
    walls.reserve(m_map->walls.size());
    for (FlorrBtMap::Wall& wall : m_map->walls)
    {
        walls.push_back(&wall);
        m_max_wall_query_radius = std::max(m_max_wall_query_radius, wall.query_radius);
    }
    m_wall_grid.Rebuild(walls);
}

void CGameWorld::ResolveWallCollisions(const std::vector<CEntity*>& entities)
{
    if (!m_map || m_map->walls.empty()) return;

    for (CEntity* entity : entities)
    {
        if (!entity || entity->m_is_marked_for_des || !entity->CanCollide()) continue;

        sf::Vector2f start = entity->m_prev_pos;
        sf::Vector2f end = entity->m_pos;
        sf::Vector2f center = (start + end) * 0.5f;
        float travel = Distance(start, end);
        float query_radius = travel * 0.5f + entity->m_radius + m_max_wall_query_radius + 1.f;
        auto candidates = m_wall_grid.QueryRange(center, query_radius);

        bool swept_hit = false;
        const FlorrBtMap::Wall* blocking_wall = nullptr;
        int blocking_segment = -1;
        for (const FlorrBtMap::Wall* wall : candidates)
        {
            if (!wall) continue;
            if (travel > game_config::entity_collision_epsilon &&
                SweptCircleHitsWall(start, end, entity->m_radius, *wall))
            {
                swept_hit = true;
                blocking_wall = wall;
                blocking_segment = FindBlockingWallSegment(start, end, entity->m_radius, *wall);
                break;
            }
        }

        if (swept_hit)
        {
            entity->m_pos = start;
            if (blocking_wall)
            {
                sf::Vector2f slide_vel = ProjectMobVelocityAlongWall(*entity, *blocking_wall, blocking_segment);
                if (LengthSq(slide_vel) > game_config::entity_collision_epsilon)
                    entity->m_pos += slide_vel * game_config::server_fixed_dt;
            }
        }

        for (const FlorrBtMap::Wall* wall : candidates)
        {
            if (!wall) continue;
            PushEntityOutOfWall(*entity, *wall);
        }
    }
}

void CGameWorld::ResolveCollisions(const std::vector<CEntity*>& entities)
{
    float max_radius = 0.f;
    for (const CEntity* entity : entities)
    {
        if (entity && !entity->m_is_marked_for_des && entity->CanCollide()) max_radius = std::max(max_radius, entity->m_radius);
    }

    for (CEntity* entity : entities)
    {
        if (!entity || entity->m_is_marked_for_des || !entity->CanCollide()) continue;

        auto candidates = m_spatial_grid.QueryRange(entity->m_pos, entity->m_radius + max_radius,
                                                    [entity](const CEntity* other) -> bool
                                                    {
                                                        if (!other || other->m_is_marked_for_des) return false;
                                                        if (!other->CanCollide()) return false;
                                                        if (other == entity) return false;
                                                        return entity->m_id < other->m_id;
                                                    });

        for (CEntity* other : candidates)
        {
            if (!other || other->m_is_marked_for_des) continue;
            bool rose_team_heal_collision = IsRoseTeamHealCollision(entity, other);
            if (const auto* projectile = dynamic_cast<const CProjectile*>(entity);
                projectile && projectile->GetOwner() == other && !rose_team_heal_collision)
                continue;
            if (const auto* projectile = dynamic_cast<const CProjectile*>(other);
                projectile && projectile->GetOwner() == entity && !rose_team_heal_collision)
                continue;
            if (ShouldSkipSummonCollision(entity, other) && !rose_team_heal_collision) continue;
            if (IsSameTeamSummonCollision(entity, other) && !rose_team_heal_collision) continue;
            if (IsSameRootOwnerSummonCollision(entity, other) && !rose_team_heal_collision) continue;
            if (IsSameTeamPetalFlowerCollision(entity, other) && !rose_team_heal_collision) continue;
            if (BlocksNullifiedInteraction(entity, other) && !rose_team_heal_collision) continue;
            auto* entity_petal = dynamic_cast<CPetal*>(entity);
            auto* other_petal = dynamic_cast<CPetal*>(other);
            if (entity_petal && other_petal) continue;
            if (!entity->IsCollision(*other)) continue;

            if (!rose_team_heal_collision)
            {
                entity->OnCollision(other);
                other->OnCollision(entity);
            }

            auto* entity_mob = dynamic_cast<CMobBase*>(entity);
            auto* other_mob = dynamic_cast<CMobBase*>(other);

            bool same_team = CheckTeam(entity->m_team, other->m_team);
            if (same_team && !rose_team_heal_collision) continue;

            if (entity_petal && other_mob)
            {
                if (entity_petal->m_type == EPetalType::Yggdrasil)
                {
                    DamageYggdrasilOnEnemyContact(entity_petal, other_mob);
                    continue;
                }

                float damage = entity_petal->m_final_petal_stats.damage;
                if (const CPetalPrototype* proto = FindPetalPrototype(entity_petal->m_type);
                    proto && proto->m_p_behavior)
                    proto->m_p_behavior->OnPetalHit(entity_petal, entity_petal->m_rarity, other, damage);
                other->TakeDamage(damage, entity_petal->GetOwner(), EDamageType::Normal);
                continue;
            }

            if (other_petal && entity_mob)
            {
                if (other_petal->m_type == EPetalType::Yggdrasil)
                {
                    DamageYggdrasilOnEnemyContact(other_petal, entity_mob);
                    continue;
                }

                float damage = other_petal->m_final_petal_stats.damage;
                if (const CPetalPrototype* proto = FindPetalPrototype(other_petal->m_type);
                    proto && proto->m_p_behavior)
                    proto->m_p_behavior->OnPetalHit(other_petal, other_petal->m_rarity, entity, damage);
                entity->TakeDamage(damage, other_petal->GetOwner(), EDamageType::Normal);
                continue;
            }

            if (auto* mob = entity_mob)
            {
                if (!other_petal)
                {
                    if (const SMobStats* stats = mob->GetFinalStats()) other->TakeDamage(stats->damage, entity, EDamageType::Normal);
                }
            }
            if (auto* mob = other_mob)
            {
                if (!entity_petal)
                {
                    if (const SMobStats* stats = mob->GetFinalStats()) entity->TakeDamage(stats->damage, other, EDamageType::Normal);
                }
            }
        }
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
