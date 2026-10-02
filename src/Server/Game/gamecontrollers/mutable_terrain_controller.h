#pragma once
#include "opencontroller.h"
#include "../../../Shared/math_types.h"
#include <cstdint>
#include <unordered_map>
#include <vector>

// Mutable-terrain world: no extra gameplay features. Every player flower is
// permanently under primordial nullification (phases through mobs and
// walls), and the Brush petal paints dirt tiles into the map's "dirt" layer
// while attacking. Every tile change is broadcast to the world's players as
// a TerrainDelta message so clients update their maps incrementally.
//
// Virtually inherits COpenController so a diamond child can combine this
// controller with another COpenController-derived base (e.g. a pvp base).
class CMutableTerrainController : virtual public COpenController
{
  public:
    bool ForcesPrimordialNullification() const override { return true; }
    void OnActivate(CGameWorld& world) override;
    void OnTick(CGameWorld& world, float dt) override;
    void OnPlayerEnteredWorld(CGameWorld& world, CPlayer& player) override;
    std::string_view SnapshotKey() const override { return "world_controller.mutable_terrain"; }
    std::uint32_t SnapshotVersion() const override { return 1; }
    void CaptureSnapshot(CSnapshotWriter& writer) const override;
    bool RestoreSnapshot(const CSnapshotReader& reader, std::uint32_t version, std::string& error) override;

    // Brush-like terrain edit: paints a dirt blob centered near `center`
    // (world units). `size_tiles` is the block size in tiles (2x2 default =
    // the rounded dirt circle of the four corner variants). Returns the
    // number of changed tiles, or 0 if the edit was rejected (out of bounds,
    // blocked by walls, no dirt layer...).
    int PaintTerrain(CGameWorld& world, florrbt::Vector2f center, float size_tiles = 2.f);

    // Eraser-like terrain edit: clears tiles and removes native map walls in
    // the block, then re-applies the terrain rule with a flood fill. Returns
    // the number of changed tiles (including removed walls' tile clears), or
    // 0 if rejected.
    int EraseTerrain(CGameWorld& world, florrbt::Vector2f center, float size_tiles = 2.f);

    // Deletes a single terrain tile (dirt layer only; walls are the CWall
    // board's business) and re-applies the terrain rule around the hole.
    // Returns 1 when a tile was cleared, 0 when the cell was already empty.
    int DeleteTile(CGameWorld& world, int row, int col);

  protected:
    // Exposed to derived controllers (e.g. the ant-kingdom controller scans
    // the dirt layer to bind a CWall to every in-rule tile).
    void LoadTerrainData(CGameWorld& world);
    bool FindBrushBlock(florrbt::Vector2f pos, int size_tiles, int& out_anchor_x, int& out_anchor_y) const;
    bool BlockClearOfWalls(CGameWorld& world, int anchor_x, int anchor_y, int size_tiles) const;
    void PaintBrush(CGameWorld& world, CPlayerFlower& flower);
    void EraseBrush(CGameWorld& world, CPlayerFlower& flower);
    bool SetTile(CGameWorld& world, int row, int col, std::uint32_t gid);
    void NormalizeFloodFill(CGameWorld& world, int seed_row, int seed_col);
    void NormalizeRegion(CGameWorld& world, int min_row, int min_col, int max_row, int max_col);
    std::uint32_t DirtGidForMask(int mask) const;
    int CurrentDirtMask(int row, int col) const;
    std::uint32_t TileAt(int row, int col) const;

    int m_dirt_layer_index = -1;
    int m_width = 0;
    int m_height = 0;
    float m_tile_size = 512.f;
    bool m_terrain_loaded = false;
    bool m_terrain_tried = false;
    std::vector<std::uint32_t> m_tiles;                       // raw gids (gid + flip bits)
    std::unordered_map<std::uint32_t, std::uint32_t> m_dirty; // cell index -> raw gid
    std::unordered_map<std::uint32_t, int> m_dirt_bits_by_gid;  // canonical gid -> variant bits

  private:
    std::vector<std::pair<florrbt::Vector2f, florrbt::Vector2f>> m_removed_wall_rects;
    std::unordered_map<std::uint32_t, float> m_paint_cooldowns;
    std::unordered_map<int, std::uint32_t> m_dirt_gid_by_bits;  // variant bits -> canonical gid
    std::unordered_map<int, std::uint32_t> m_dirt_rep_by_mask;  // corner mask -> gid | flip bits
};
