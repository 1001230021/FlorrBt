#pragma once
#include <cstdint>
#include <unordered_set>

class CGameWorld;
class CMobBase;
class IGameWorldResolver;

// 跨世界的自然稀有种群索引由单个游戏上下文持有，不能跨服务器实例共享。
class CNaturalSpawnPopulation
{
  public:
    explicit CNaturalSpawnPopulation(IGameWorldResolver& worlds) : m_worlds(worlds) {}
    void Collect(std::unordered_set<int>& result);
    void Remember(const CMobBase& mob);
    void Invalidate() { m_valid = false; }
    static void CollectWorld(const CGameWorld& world, std::unordered_set<int>& result);
  private:
    IGameWorldResolver& m_worlds;
    std::unordered_set<int> m_types;
    std::uint64_t m_tick = 0;
    bool m_valid = false;
};
