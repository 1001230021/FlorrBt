#include "gameworld.h"
#include <limits>

CGameWorld::CGameWorld() : m_SpatialGrid(this, 200.f) {}

CGameWorld::~CGameWorld()
{
    m_pEntities.clear();
}

int CGameWorld::GetNewID()
{
    if (!m_FreeIDs.empty())
    {
        int id = *m_FreeIDs.begin();
        m_FreeIDs.erase(m_FreeIDs.begin());
        return id;
    }
    else
    {
        return m_NextID++;
    }
}

void CGameWorld::FreeID(int id)
{
    if (m_FreeIDs.find(id) != m_FreeIDs.end())
    {
        LOG_FATAL("gameworld", "Free ID " + std::to_string(id) + " twice");
        return;
    }

    m_FreeIDs.insert(id);
}

void CGameWorld::InsertEntity(std::unique_ptr<CEntity> entity)
{
    if (!entity)
        return;

    int id = GetNewID();

    if (id >= static_cast<int>(m_pEntities.size()))
        m_pEntities.resize(id + 1);

    if (m_pEntities[id] != nullptr)
    {
        LOG_FATAL("gameworld", "The ID " + std::to_string(id) + " already occupied.");
        return;
    }
    else if (entity->m_ID != -1)
    {
        LOG_FATAL("gameworld", "The entity has already had a ID " + std::to_string(id) + ".");
        return;
    }

    entity->m_ID = id;
    m_pEntities[id] = std::move(entity);
}

void CGameWorld::InsertEntity(CEntity* entity)
{
    if (!entity)
        return;
    InsertEntity(std::unique_ptr<CEntity>(entity));
}

void CGameWorld::InsertNonOwningEntity(CEntity* entity)
{
    if (!entity)
        return;

    int id = GetNewID();

    if (id >= static_cast<int>(m_pEntities.size()))
        m_pEntities.resize(id + 1);

    if (id >= static_cast<int>(m_pEntityRefs.size()))
        m_pEntityRefs.resize(id + 1, nullptr);

    if (m_pEntityRefs[id] != nullptr)
    {
        LOG_FATAL("gameworld", "Non-owning entity slot " + std::to_string(id) + " already occupied.");
        return;
    }

    m_pEntityRefs[id] = entity;
    entity->m_ID = id;
}

void CGameWorld::RemoveEntity(int id)
{
    if (id < 0 || id >= static_cast<int>(m_pEntities.size()))
    {
        LOG_FATAL("gameworld", "Entity invalid ID " + std::to_string(id));
        return;
    }
    if (!m_pEntities[id])
    {
        LOG_ERROR("gameworld", "Entity ID " + std::to_string(id) + " is already null");
        return;
    }

    m_pEntities[id]->m_IsMarkedForDes = true;
}

void CGameWorld::Tick(float dt)
{
    std::vector<CEntity*> entities;
    entities.reserve(m_pEntities.size() + m_pEntityRefs.size());
    for (const auto& p : m_pEntities)
        entities.push_back(p.get());
    for (const auto& r : m_pEntityRefs)
        entities.push_back(r);

    m_SpatialGrid.Rebuild(entities);

    for (CEntity* entity : entities)
    {
        if (entity != nullptr && !entity->m_SkipWorldTick)
            entity->Tick(dt);
    }

    Cleanup();
}

CEntity* CGameWorld::GetEntity(int id) const
{
    if (id < 0)
        return nullptr;

    if (id < static_cast<int>(m_pEntities.size()))
    {
        if (m_pEntities[id])
            return m_pEntities[id].get();
    }

    if (id < static_cast<int>(m_pEntityRefs.size()))
        return m_pEntityRefs[id];

    return nullptr;
}

CEntity* CGameWorld::FindClosestEntity(const sf::Vector2f& center, float maxRange,
                                       std::function<bool(const CEntity*)> filter) const
{
    return m_SpatialGrid.FindClosest(center, maxRange, filter);
}

void CGameWorld::Cleanup()
{
    for (size_t i = 0; i < m_pEntities.size(); ++i)
    {
        if (m_pEntities[i] && m_pEntities[i]->m_IsMarkedForDes)
        {
            FreeID(m_pEntities[i]->m_ID);
            m_pEntities[i].reset();
        }
    }

    for (auto it = m_pEntityRefs.begin(); it != m_pEntityRefs.end();)
    {
        if (*it && (*it)->m_IsMarkedForDes)
        {
            FreeID((*it)->m_ID);
            *it = nullptr;
            ++it;
        }
        else
            ++it;
    }
}
