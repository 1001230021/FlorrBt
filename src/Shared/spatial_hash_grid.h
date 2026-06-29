#pragma once
#include <unordered_map>
#include <vector>
#include <functional>
#include <SFML/System/Vector2.hpp>
#include "../Server/Game/entity.h"

class CEntity;
class CSpatialHashGrid {
public:
    explicit CSpatialHashGrid(float cellSize);

    void Clear();
    void Insert(CEntity* entity);
    void Rebuild(const std::vector<CEntity*>& entities);

    CEntity* FindClosest(
        const sf::Vector2f& center,
        float maxRange,
        std::function<bool(const CEntity*)> filter = nullptr
    ) const;

private:
    int HashCell(int cx, int cy) const;
    int CellX(float worldX) const;
    int CellY(float worldY) const;

    float m_CellSize;
    std::unordered_map<int, std::vector<CEntity*>> m_Grid;

};
