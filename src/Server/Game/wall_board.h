#pragma once
#include "tile_board.h"
#include "../../Shared/math_types.h"
#include <cstdint>
#include <unordered_map>
#include <utility>
#include <vector>

class CGameWorld;

// Wall overlay: marks which map walls still stand, cell by cell. Walls in
// FlorrBtMap are arbitrary polygons (Tiled collision objects) that may only
// cover part of a tile or span several tiles. Each wall is registered to
// every cell its AABB touches and owns exactly one "primary cell" (the cell
// under its center).
//
// CWall is a marker layer only - it never takes over collision. The world's
// m_map->walls + wall grid stay authoritative; CommitRemove collects the
// walls whose primary cell is being dug and removes them from the world by
// their recorded AABB (shrunk by an epsilon so flush neighbours survive).
// Ids into m_map->walls are useless for removal: the world compacts that
// vector on every removal, shifting all later indices.
//
// Besides map walls, "cell walls" can be inserted per tile (InsertCell).
// These are pure markers bound to one dirt cell (no map polygon behind
// them); committing a removal on them only marks them dead and broadcasts.
class CWall : public CTileBoard
{
  public:
    // Rebuilds registration from the world's map and replays recorded
    // removals. Safe to call again after a hot-reload map rebuild.
    void Reload(CGameWorld& world) override;

    int Width() const override { return m_width; }
    int Height() const override { return m_height; }
    float TileSize() const override { return m_tile_size; }

    bool TileSolid(int row, int col) const override;
    bool HasTile(int row, int col) const override;
    bool HasWall(int row, int col) const;

    // Binds a marker wall to one cell (whole-tile AABB, primary cell = the
    // cell). Used by the ant-kingdom controller to give every in-rule dirt
    // tile a diggable wall marker. Returns false when the cell already has
    // one or the cell is out of bounds.
    bool InsertCell(int row, int col);

    // Removes every alive wall whose primary cell is (row, col). Returns the
    // number of removed walls, or 0 when the cell is clear. When `team` is
    // positive the digging team's flag is set on the cell (any number of
    // teams may mark the same dug cell).
    bool CommitRemove(int row, int col, int team = 0) override;
    // Walls cannot be placed (yet): always rejected.
    bool CommitPlace(int row, int col, std::uint32_t gid) override;

    // Whether this cell has been dug open by `team` (bit per team).
    bool IsDugByTeam(int row, int col, int team) const;
    // Any team has dug this cell open.
    bool IsDug(int row, int col) const;
    // Raw per-team flag mask (bit team-1 set when that team dug the cell).
    std::uint32_t DigFlags(int row, int col) const;

    // Cells flagged by `team` that still have at least one alive (undug) wall:
    // the dig candidates an eou worker ant searches for. Returns {row, col}
    // pairs (empty when the team flagged nothing undigged).
    std::vector<std::pair<int, int>> TeamFlaggedWallCells(int team) const;
    // World-space center of a cell.
    florrbt::Vector2f CellCenter(int row, int col) const
    {
        return { (static_cast<float>(col) + 0.5f) * m_tile_size, (static_cast<float>(row) + 0.5f) * m_tile_size };
    }

    // Marks/unmarks a team flag on a cell (pure marker, no wall removal).
    // Both return false when the cell is out of bounds or the team is
    // outside 1..32.
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
        bool from_map = true; // map polygon (removed from the world) vs cell marker
    };

    // A dug-open cell can be marked by many teams: one bit per team (bit
    // team-1), so a compact uint32 flag is the right shape.
    struct SDigClaim
    {
        std::uint32_t team_flags = 0;
    };

    // Keeps flush neighbours out of rect-based world removals (see
    // RemoveMapWallFromWorld).
    static constexpr float kWallRemoveEpsilon = 1.f;

    std::uint32_t CellIndex(int row, int col) const;
    bool HasAliveWallOnCell(std::uint32_t cell) const;
    void BuildFromMap(const CGameWorld& world);
    void ReplayInsertedCells();
    void ReplayRecordedRemovals(CGameWorld& world);
    void RemoveMapWallFromWorld(CGameWorld& world, const SWallRecord& record) const;
    void BroadcastWallRemovals(CGameWorld& world, const std::vector<int>& wall_ids) const;
    int m_width = 0;
    int m_height = 0;
    float m_tile_size = 512.f;
    bool m_loaded = false;

    std::vector<SWallRecord> m_walls;       // map walls first, then cell walls
    std::vector<bool> m_alive;              // per wall id
    std::unordered_map<std::uint32_t, std::vector<int>> m_cells; // cell -> covering wall ids
    std::unordered_map<std::uint32_t, SDigClaim> m_dig_claims;   // cell -> digging team claim
    std::vector<std::uint32_t> m_inserted_cells;                 // recorded cell wall bindings
    std::vector<std::uint32_t> m_removed_primary_cells;          // recorded removals (snapshot/replay)
    CGameWorld* m_world = nullptr;                              // set by Reload
};
