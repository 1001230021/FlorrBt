#pragma once
#include "../entities/eou_mob.h"
#include "../../../Shared/math_types.h"
#include <cstdint>
#include <memory>
#include <vector>

class CGameWorld;
class CSnapshotReader;
class CSnapshotWriter;
class CEouCellMob;

// EOU 复用统一的世界/代数句柄，仅保留接口别名。
using SEouEntityHandle = SEntityHandle;

// 同类型四邻域相连格子构成一个连通组，不合并对角或异类格子。
class CCellGroup
{
  public:
    explicit CCellGroup(int group_id) : m_group_id(group_id) {}

    int GroupId() const { return m_group_id; }
    std::vector<SEouEntityHandle>& Members() { return m_members; }
    const std::vector<SEouEntityHandle>& Members() const { return m_members; }
    std::size_t MemberCount() const { return m_members.size(); }

    void AddMember(SEouEntityHandle handle) { m_members.push_back(handle); }

  private:
    int m_group_id;
    std::vector<SEouEntityHandle> m_members;
};

// 增删格子后重建连通分组，保持同类型四邻域关系。
class CCellGroupSet
{
  public:
    void SetTileSize(float tile_size) { m_tile_size = tile_size; }
    float TileSize() const { return m_tile_size; }

    // 注册格子并重建分组，返回分组 ID；不属于该集合时返回 0。
    int AddCell(CGameWorld& world, SEouEntityHandle handle, int cell_type);
    void RemoveCell(CGameWorld& world, SEouEntityHandle handle);
    std::vector<SEouEntityHandle> NeighboursOf(CGameWorld& world, SEouEntityHandle handle, int cell_type) const;
    const std::vector<SEouEntityHandle>& AllCells() const { return m_cells; }

    const std::vector<CCellGroup>& Groups() const { return m_groups; }

    void CaptureSnapshot(CSnapshotWriter& writer) const;
    bool RestoreSnapshot(const CSnapshotReader& reader, std::uint32_t version, std::string& error);
    void ResolveReferences(CGameWorld& world, int team_id);

  private:
    std::pair<int, int> CellKeyOf(CGameWorld& world, SEouEntityHandle handle) const;
    void RebuildGroups(CGameWorld& world);

    float m_tile_size = 32.f;
    int m_next_group_id = 1;
    std::vector<SEouEntityHandle> m_cells;
    std::vector<CCellGroup> m_groups;
};

// 分组决定目标，生物负责移动；重规划按时间片限制 BFS 成本，并在更新时重新绑定地形板。
class CEouPheromoneGroup
{
  public:
    CEouPheromoneGroup(int team_id, int group_id, florrbt::Vector2f pheromone_target,
                       const CTileBoard& board)
        : m_team_id(team_id), m_group_id(group_id), m_pheromone_target(pheromone_target),
          m_board(board)
    {
    }

    int TeamId() const { return m_team_id; }
    int GroupId() const { return m_group_id; }
    const florrbt::Vector2f& PheromoneTarget() const { return m_pheromone_target; }
    // 槽位绑定的持久信息素分组不会因暂时无人而被删除。
    void SetPersistent(bool persistent) { m_persistent = persistent; }
    bool Persistent() const { return m_persistent; }

    // 将成员加入待更新队列，分批迁向新的信息素目标。
    void SetPheromoneTarget(florrbt::Vector2f target);

    bool AddMember(CEntity* entity);
    bool RemoveMember(const SEouEntityHandle& handle);
    std::size_t MemberCount() const { return m_members.size(); }
    const std::vector<SEouEntityHandle>& Members() const { return m_members; }

    void OnTick(CGameWorld& world, float dt);

    void CaptureSnapshot(CSnapshotWriter& writer) const;
    bool RestoreSnapshot(const CSnapshotReader& reader, std::uint32_t version, std::string& error);
    void ResolveReferences(CGameWorld& world);

  private:
    void RebuildUnupdated();

    int m_team_id;
    int m_group_id;
    florrbt::Vector2f m_pheromone_target;
    const CTileBoard& m_board;
    bool m_persistent = false;
    std::vector<SEouEntityHandle> m_members;
    std::vector<std::size_t> m_unupdated; // 待更新成员索引。
    float m_reroute_budget = 0.f; // 跨帧保留的不足一次重寻路预算。
};

// 控制器拥有团队，团队拥有信息素分组并决定目标。
class CTeam
{
  public:
    explicit CTeam(int team_id, const CTileBoard& board) : m_team_id(team_id), m_board(board)
    {
        m_cells.SetTileSize(board.TileSize());
    }

    int TeamId() const { return m_team_id; }
    // 巢穴位置用于工蚁回运，初始为本队首个玩家的出生点。
    void SetHomePos(florrbt::Vector2f pos)
    {
        m_home_pos = pos;
        m_has_home = true;
    }
    bool HasHomePos() const { return m_has_home; }
    florrbt::Vector2f HomePos() const { return m_home_pos; }
    // 储藏格无法容纳的食物进入团队库存，消费时优先扣此库存。
    float StashFood() const { return m_food_stash; }
    float DepositFood(float amount)
    {
        const float accepted = std::min(std::max(0.f, amount), std::max(0.f, game_config::eou_stash_capacity - m_food_stash));
        m_food_stash += accepted;
        return accepted;
    }
    void RefundFood(CGameWorld& world, float amount);
    float FoodCapacity(CGameWorld& world) const;

    // 蚁后使用完整句柄识别，控制器据此处理判负。
    void SetQueen(SEouEntityHandle handle) { m_queen = handle; }
    SEouEntityHandle Queen() const { return m_queen; }
    bool IsQueen(const SEouEntityHandle& handle) const
    {
        return m_queen.RefersTo(handle);
    }

    void AddCell(CGameWorld& world, CEouCellMob& cell);
    void RemoveCell(CGameWorld& world, CEouCellMob& cell);
    // 扣款失败不改变库存，只对已完成动作收费。
    float StorageFoodTotal(CGameWorld& world) const;
    bool TryConsumeFood(CGameWorld& world, float amount);
    float DepositToNearestStorage(CGameWorld& world, florrbt::Vector2f pos, float amount);
    // 按位置所在网格查找同队格子。
    CEouCellMob* GetCellAt(CGameWorld& world, florrbt::Vector2f pos);
    const CEouCellMob* GetCellAt(CGameWorld& world, florrbt::Vector2f pos) const;
    const CCellGroupSet& Cells() const { return m_cells; }

    CEouPheromoneGroup* CreateGroup(florrbt::Vector2f target);
    // 按需创建分组，槽位绑定信息素要求分组 ID 与槽位序号长期一致。
    CEouPheromoneGroup* EnsureGroup(int group_id, florrbt::Vector2f target);
    bool RemoveGroup(int group_id);
    CEouPheromoneGroup* FindGroup(int group_id);
    const CEouPheromoneGroup* FindGroup(int group_id) const;
    std::size_t GroupCount() const { return m_groups.size(); }
    std::size_t MemberCount() const;

    void OnTick(CGameWorld& world, float dt);

    // 团队解散时标记成员与蚁后移除，并释放全部分组。
    void Dissolve(CGameWorld& world);

    void CaptureSnapshot(CSnapshotWriter& writer) const;
    bool RestoreSnapshot(const CSnapshotReader& reader, std::uint32_t version, std::string& error);
    void ResolveReferences(CGameWorld& world);

  private:
    int m_team_id;
    int m_next_group_id = 1;
    const CTileBoard& m_board;
    std::vector<std::unique_ptr<CEouPheromoneGroup>> m_groups;
    florrbt::Vector2f m_home_pos{};
    bool m_has_home = false;
    float m_food_stash = 0.f;
    SEouEntityHandle m_queen;
    CCellGroupSet m_cells;
};
