#include "gameworld.h"
#include <limits>

CGameWorld::~CGameWorld()
{
    for (CEntity* entity : m_pEntities)
    {
        if (entity != nullptr)
            delete entity;
    }
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

void CGameWorld::InsertEntity(CEntity* entity)
{
    int id = GetNewID();

    if (id >= static_cast<int>(m_pEntities.size()))
        m_pEntities.resize(id + 1, nullptr);

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

    m_pEntities[id] = entity;

    entity->m_ID = id;
}

void CGameWorld::RemoveEntity(int id)
{
    if (id < 0 || id >= static_cast<int>(m_pEntities.size()))
    {
        LOG_FATAL("gameworld", "Entity invalid ID " + std::to_string(id));
        return;
    }
    if (m_pEntities[id] == nullptr)
    {
        LOG_ERROR("gameworld", "Entity ID " + std::to_string(id) + " is already null");
        return;
    }

    m_pEntities[id]->m_IsMarkedForDes = true;
}

void CGameWorld::Tick(float dt) // 注意：有待完善
{
    std::vector<CEntity*> entities = m_pEntities; // 防止迭代器失效
    for (CEntity* entity : entities)
    {
        if (entity != nullptr && !entity->m_SkipWorldTick)
            entity->Tick(dt);
    }

    m_SpatialGrid.Rebuild(entities);

    Cleanup();
}

CEntity* CGameWorld::GetEntity(int id) const
{
    if (id < 0 || id >= static_cast<int>(m_pEntities.size()))
        return nullptr;
    return m_pEntities[id];
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
        if (m_pEntities[i] != nullptr && m_pEntities[i]->m_IsMarkedForDes)
        {
            FreeID(m_pEntities[i]->m_ID);
            delete m_pEntities[i];
            m_pEntities[i] = nullptr;
        }
    }
}
