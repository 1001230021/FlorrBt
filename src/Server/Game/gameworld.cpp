#include "gameworld.h"
#include "entities/mob.h"

CGameWorld::CGameWorld()
    : m_spatial_grid(
          200.f, [](const CEntity& entity) { return entity.m_pos; }, [](const CEntity& entity) { return entity.m_id; },
          [this](int id) { return GetEntity(id); },
          [](const CEntity& entity) { return entity.m_id >= 0 && !entity.m_is_marked_for_des; })
{
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

void CGameWorld::Tick(float dt)
{
    std::vector<CEntity*> entities = GetAllEntities();
    m_spatial_grid.Rebuild(entities);

    for (CEntity* entity : entities)
    {
        if (entity && !entity->m_skip_world_tick) entity->Tick(dt);
    }

    if (m_p_controller)
        m_p_controller->OnTick(*this, dt);

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

void CGameWorld::ResolveCollisions(const std::vector<CEntity*>& entities)
{
    float max_radius = 0.f;
    for (const CEntity* entity : entities)
    {
        if (entity && !entity->m_is_marked_for_des) max_radius = std::max(max_radius, entity->m_radius);
    }

    for (CEntity* entity : entities)
    {
        if (!entity || entity->m_is_marked_for_des) continue;

        auto candidates = m_spatial_grid.QueryRange(entity->m_pos, entity->m_radius + max_radius,
                                                    [entity](const CEntity* other) -> bool
                                                    {
                                                        if (!other || other->m_is_marked_for_des) return false;
                                                        if (other == entity) return false;
                                                        return entity->m_id < other->m_id;
                                                    });

        for (CEntity* other : candidates)
        {
            if (!other || other->m_is_marked_for_des) continue;
            if (!entity->IsCollision(*other)) continue;

            entity->OnCollision(other);
            other->OnCollision(entity);

            bool same_team = CheckTeam(entity->m_team, other->m_team);
            if (same_team) continue;

            if (auto* mob = dynamic_cast<CMobBase*>(entity))
            {
                if (const SMobStats* stats = mob->GetFinalStats()) other->TakeDamage(stats->damage, entity, EDamageType::Normal);
            }
            if (auto* mob = dynamic_cast<CMobBase*>(other))
            {
                if (const SMobStats* stats = mob->GetFinalStats()) entity->TakeDamage(stats->damage, other, EDamageType::Normal);
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
            FreeID(m_p_entities[i]->m_id);
            m_p_entities[i].reset();
        }
    }

    for (CEntity*& entity : m_p_entity_refs)
    {
        if (entity && entity->m_is_marked_for_des)
        {
            FreeID(entity->m_id);
            entity = nullptr;
        }
    }
}
