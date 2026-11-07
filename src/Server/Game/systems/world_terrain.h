#pragma once
#include "../wall_board.h"
#include "../../../Shared/math_types.h"
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

class CGameWorld;
class CPlayerFlower;
class CSnapshotWriter;
class CSnapshotReader;
class IPlayerSceneWriter;

// 地形与修改历史属于世界；玩法切换只改变编辑权限，不更换地形实例。
class CWorldTerrain
{
  public:
    explicit CWorldTerrain(CGameWorld& world) : m_world(world) {}
    void ResetForMap();
    void EnsureLoaded();
    void TickTools(float dt);
    int PaintTerrain(florrbt::Vector2f center, float size_tiles = 2.f);
    int EraseTerrain(florrbt::Vector2f center, float size_tiles = 2.f);
    int DeleteTile(int row, int col);
    bool WritePlayerScene(IPlayerSceneWriter& writer) const;
    void CaptureSnapshot(CSnapshotWriter& writer) const;
    bool RestoreSnapshot(const CSnapshotReader& reader, std::uint32_t version, std::string& error);
    bool ResolveReferences(std::string& error);
    void EnsureDigBoard();
    std::unique_ptr<CWall> PrepareDigBoard();
    void InstallDigBoard(std::unique_ptr<CWall> board) noexcept;
    CWall& WallBoard() { return m_wall_board; }
    const CWall& WallBoard() const { return m_wall_board; }
    float TileSize() const { return m_tile_size; }
    std::uint64_t Revision() const { return m_revision; }
    void GeometryChanged();
    void RecordGeometryRemoval(florrbt::Vector2f min, florrbt::Vector2f max);
    bool HasHistory() const { return !m_dirty.empty() || !m_removed_wall_rects.empty() || m_wall_board.IsLoaded(); }

  private:
    void LoadTerrainData(CGameWorld& world);
    bool FindBrushBlock(florrbt::Vector2f pos, int size_tiles, int& anchor_x, int& anchor_y) const;
    bool BlockClearOfWalls(CGameWorld& world, int anchor_x, int anchor_y, int size_tiles) const;
    void PaintBrush(CGameWorld& world, CPlayerFlower& flower);
    void EraseBrush(CGameWorld& world, CPlayerFlower& flower);
    bool SetTile(CGameWorld& world, int row, int col, std::uint32_t gid);
    void NormalizeFloodFill(CGameWorld& world, int row, int col);
    void NormalizeRegion(CGameWorld& world, int min_row, int min_col, int max_row, int max_col);
    std::uint32_t DirtGidForMask(int mask) const;
    int CurrentDirtMask(int row, int col) const;
    std::uint32_t TileAt(int row, int col) const;

    CGameWorld& m_world;
    CWall m_wall_board;
    std::uint64_t m_revision = 0;
    std::uint64_t m_definition_signature = 0;
    bool m_replaying = false;
    bool m_has_saved_board = false;
    bool m_geometry_dirty = false;
    int m_dirt_layer_index = -1, m_width = 0, m_height = 0;
    float m_tile_size = 512.f;
    bool m_terrain_loaded = false, m_terrain_tried = false;
    std::vector<std::uint32_t> m_tiles;
    std::unordered_map<std::uint32_t, std::uint32_t> m_dirty;
    std::unordered_map<std::uint32_t, int> m_dirt_bits_by_gid;
    std::vector<std::pair<florrbt::Vector2f, florrbt::Vector2f>> m_removed_wall_rects;
    struct SPaintCooldown { std::uint64_t session = 0; float remaining = 0.f; };
    std::unordered_map<std::uint32_t, SPaintCooldown> m_paint_cooldowns;
    std::unordered_map<int, std::uint32_t> m_dirt_gid_by_bits, m_dirt_rep_by_mask;
};
