#pragma once
#include "../Server/Game/entity.h"
#include <SFML/System/Vector2.hpp>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <unordered_map>
#include <vector>

class CEntity;
class CGameWorld;

class CSpatialHashGrid
{
  public:
    explicit CSpatialHashGrid(CGameWorld* world, float cell_size);

    void Clear();
    void Insert(CEntity* entity);
    void Rebuild(const std::vector<CEntity*>& entities);

    CEntity* FindClosest(const sf::Vector2f& center, float max_range,
                         std::function<bool(const CEntity*)> filter = nullptr) const;

    std::vector<CEntity*> QueryRange(const sf::Vector2f& center, float radius,
                                     std::function<bool(const CEntity*)> filter = nullptr) const;

  private:
    std::uint64_t HashCell(int cell_x, int cell_y) const;
    int CellX(float world_x) const;
    int CellY(float world_y) const;

    float m_cell_size = 1.f;
    std::unordered_map<std::uint64_t, std::vector<int>> m_grid;
    CGameWorld* m_p_world = nullptr;
};