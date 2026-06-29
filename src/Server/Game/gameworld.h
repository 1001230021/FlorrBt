#pragma once
#include <vector>
#include <set>
#include "entity.h"
#include "../../Engine/logger.h"
#include "../../Shared/shared.h"
#include "../../Shared/spatial_hash_grid.h"

class CGameWorld {
public:
	CGameWorld() = default;
	~CGameWorld();

	int GetNewID();
	void FreeID(int id);

	void InsertEntity(CEntity* entity);
	void RemoveEntity(int id);

	void Tick(float dt);

	CEntity* GetEntity(int id) const;
	CEntity* FindClosestEntity(const sf::Vector2f& center, float maxRange,
		std::function<bool(const CEntity*)> filter = nullptr) const;
	const std::vector<CEntity*>& GetAllEntities() const { return m_pEntities; }

private:
	void Cleanup();

	std::set<int> m_FreeIDs;
	int m_NextID = 0;

	CSpatialHashGrid m_SpatialGrid{ 200.f };

	std::vector<CEntity*> m_pEntities;
};