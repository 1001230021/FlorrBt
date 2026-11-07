#pragma once
#include "tile_board.h"
#include "../../Shared/math_types.h"
#include <cstdint>
#include <unordered_map>
#include <utility>
#include <optional>
#include <vector>

class CGameWorld;
class IPlayerSceneWriter;

// 墙板只记录标记，碰撞以世界地图及墙网格为准。
// 多边形墙登记到包围盒覆盖的所有格，但只归属中心所在格。
// 移除时使用收缩后的包围盒；墙数组会压缩，不能保留其下标作为身份。
// 单格墙仅是挖掘标记，没有对应的地图多边形。
class CWall : public CTileBoard
{
  public:
    // 从世界地图重建登记并回放移除记录，可重复调用。
    void Reload(CGameWorld& world) override;
    void RefreshFromWorld(CGameWorld& world);
    void BindWorld(CGameWorld& world) noexcept { m_world = &world; }
    // 撤销借用的世界上下文，避免移除操作依赖析构顺序。
    void DetachWorld(CGameWorld& world);
    bool WritePlayerScene(IPlayerSceneWriter& writer) const;

    int Width() const override { return m_width; }
    int Height() const override { return m_height; }
    float TileSize() const override { return m_tile_size; }

    bool TileSolid(int row, int col) const override;
    bool HasTile(int row, int col) const override;
    bool HasWall(int row, int col) const;

    // 插入仅用于挖掘的单格墙标记；越界或已有标记时失败。
    bool InsertCell(int row, int col);

    // 移除归属该格的墙；team 为正时同时登记该队伍的挖掘标记。
    bool CommitRemove(int row, int col, int team = 0) override;
    bool CommitPlace(int row, int col, std::uint32_t gid) override;

    bool IsDugByTeam(int row, int col, int team) const;
    bool IsDug(int row, int col) const;
    // 每队一位，位序为 team - 1。
    std::uint32_t DigFlags(int row, int col) const;

    // 返回该队标记且仍有墙体的格坐标，供工蚁选择挖掘目标。
    std::vector<std::pair<int, int>> TeamFlaggedWallCells(int team) const;
    florrbt::Vector2f CellCenter(int row, int col) const
    {
        return { (static_cast<float>(col) + 0.5f) * m_tile_size, (static_cast<float>(row) + 0.5f) * m_tile_size };
    }

    // 只修改队伍标记；格越界或队号不在 1..32 时失败。
    bool SetTeamFlag(int row, int col, int team);
    bool ClearTeamFlag(int row, int col, int team);

    void CaptureSnapshot(CSnapshotWriter& writer) const override;
    bool RestoreSnapshot(const CSnapshotReader& reader, std::uint32_t version, std::string& error) override;

    bool IsLoaded() const { return m_loaded; }
    int WallCount() const { return static_cast<int>(m_walls.size()); }

  private:
    struct SWallRecord
    {
        std::uint32_t primary_cell = 0;
        florrbt::Vector2f min_pos;
        florrbt::Vector2f max_pos;
        bool from_map = true; // 区分真实地图多边形与单格标记。
    };

    // 同一格可由多队标记，每队占一位。
    struct SDigClaim
    {
        std::uint32_t team_flags = 0;
    };

    // 移除矩形内缩，保留共边的相邻墙。
    static constexpr float kWallRemoveEpsilon = 1.f;
    static std::optional<std::pair<florrbt::Vector2f, florrbt::Vector2f>> RemovalRect(const SWallRecord& record);

    std::uint32_t CellIndex(int row, int col) const;
    bool HasAliveWallOnCell(std::uint32_t cell) const;
    void BuildFromMap(const CGameWorld& world);
    void ReplayInsertedCells();
    void ReplayRecordedRemovals(CGameWorld* world);
    void RemoveMapWallFromWorld(CGameWorld& world, const SWallRecord& record) const;
    void BroadcastWallRemovals(CGameWorld& world, const std::vector<int>& wall_ids) const;
    int m_width = 0;
    int m_height = 0;
    float m_tile_size = 512.f;
    bool m_loaded = false;

    std::vector<SWallRecord> m_walls; // 地图墙在前，单格墙在后。
    std::vector<bool> m_alive;
    std::unordered_map<std::uint32_t, std::vector<int>> m_cells; // 格坐标到覆盖墙记录。
    std::unordered_map<std::uint32_t, SDigClaim> m_dig_claims; // 格坐标到队伍挖掘声明。
    std::vector<std::uint32_t> m_inserted_cells; // 记录新增单格墙。
    std::vector<std::uint32_t> m_removed_primary_cells; // 记录移除，供快照回放。
    CGameWorld* m_world = nullptr; // 由 Reload 绑定的借用指针。
};
