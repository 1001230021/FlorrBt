#include "mutable_terrain_controller.h"
#include "../../../Engine/json_value.h"
#include "../../../Engine/logger.h"
#include "../../../Shared/game_config.h"
#include "../../HotReload/snapshot_archive.h"
#include "../../Module/network_module.h"
#include "../entities/flower.h"
#include "../gamecontext.h"
#include "../gameworld.h"
#include "../player.h"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <limits>
#include <string>
#include <unordered_set>

namespace
{
constexpr std::uint32_t kFlipH = 0x80000000u;
constexpr std::uint32_t kFlipV = 0x40000000u;
constexpr std::uint32_t kFlipD = 0x20000000u;
constexpr std::uint32_t kGidMask = 0x1fffffff;

int VariantBits(std::string_view variant)
{
    if (variant == "c") return 15;
    if (variant == "t") return 3;
    if (variant == "b") return 12;
    if (variant == "l") return 5;
    if (variant == "r") return 10;
    if (variant == "tl") return 1;
    if (variant == "tr") return 2;
    if (variant == "bl") return 4;
    if (variant == "br") return 8;
    if (variant == "tli") return 14;
    if (variant == "tri") return 13;
    if (variant == "bli") return 11;
    if (variant == "bri") return 7;
    return -1;
}

// Mirrors the client renderer's flip order (D, then H, then V).
int ApplyFlips(int bits, bool flip_h, bool flip_v, bool flip_d)
{
    int out = 0;
    for (int oy = 0; oy <= 1; ++oy)
    {
        for (int ox = 0; ox <= 1; ++ox)
        {
            int x = ox;
            int y = oy;
            if (flip_v) y = 1 - y;
            if (flip_h) x = 1 - x;
            if (flip_d) std::swap(x, y);
            const int src = (x == 0 && y == 0) ? 0 : (x == 1 && y == 0) ? 1 : (x == 0 && y == 1) ? 2 : 3;
            if ((bits & (1 << src)) != 0) out |= 1 << (ox + oy * 2);
        }
    }
    return out;
}

// The terrain rule only ever uses these four dirt variants (dirt_l_0,
// dirt_tl_0, dirt_tri_0, dirt_c_0) plus their H/V/D flip transforms. Every
// other variant is ignored at load time and no fifth wall tile may appear in
// the rules below.
bool IsAllowedDirtVariant(int bits)
{
    return bits == 1 || bits == 5 || bits == 13 || bits == 15; // tl, l, tri, c
}

std::string StripFrameSuffix(std::string name)
{
    if (name.size() > 2 && name.compare(name.size() - 2, 2, "_0") == 0) name.resize(name.size() - 2);
    return name;
}

// "<prefix>_<variant>" -> {prefix, variant}
bool SplitTerrainName(std::string_view name, std::string& prefix, std::string& variant)
{
    static const char* kVariants[] = { "tli", "tri", "bli", "bri", "tl", "tr", "bl", "br", "c", "t", "b", "l", "r" };
    for (const char* candidate : kVariants)
    {
        const std::string suffix = std::string("_") + candidate;
        if (name.size() <= suffix.size() || name.compare(name.size() - suffix.size(), suffix.size(), suffix) != 0)
            continue;
        prefix = std::string(name.substr(0, name.size() - suffix.size()));
        variant = candidate;
        return true;
    }
    return false;
}
} // namespace

void CMutableTerrainController::OnActivate(CGameWorld& world)
{
    COpenController::OnActivate(world);
    LoadTerrainData(world);
}

void CMutableTerrainController::LoadTerrainData(CGameWorld& world)
{
    m_terrain_tried = true;
    m_terrain_loaded = false;
    m_dirt_layer_index = -1;
    m_width = 0;
    m_height = 0;
    m_dirt_bits_by_gid.clear();
    m_dirt_gid_by_bits.clear();
    m_dirt_rep_by_mask.clear();

    const std::string map_path = world.GetMapPath();
    if (map_path.empty()) return;
    std::filesystem::path map_file(map_path);
    const std::filesystem::path map_dir = map_file.parent_path();

    std::string error;
    std::optional<CJsonValue> tmj = CJsonValue::LoadFromFile(map_file, &error);
    if (!tmj) return;

    m_tile_size = static_cast<float>(tmj->Find("tilewidth") ? tmj->Find("tilewidth")->AsNumber(512.0) : 512.0);

    // Collect dirt tiles from every referenced tileset.
    if (const CJsonValue* tilesets = tmj->Find("tilesets"); tilesets && tilesets->IsArray())
    {
        for (const CJsonValue& tileset_ref : tilesets->AsArray())
        {
            const std::uint32_t firstgid = static_cast<std::uint32_t>(tileset_ref.Find("firstgid")->AsNumber(1.0));
            const std::string source = tileset_ref.Find("source")->AsString();
            if (source.empty()) continue;
            const std::filesystem::path tsj_path = (map_dir / std::filesystem::path(source)).lexically_normal();
            std::optional<CJsonValue> tsj = CJsonValue::LoadFromFile(tsj_path, &error);
            if (!tsj || !tsj->Find("tiles") || !tsj->Find("tiles")->IsArray()) continue;
            for (const CJsonValue& tile : tsj->Find("tiles")->AsArray())
            {
                const std::string image = tile.Find("image")->AsString();
                if (image.empty()) continue;
                const std::uint32_t gid = firstgid + static_cast<std::uint32_t>(tile.Find("id")->AsInt(0));
                const std::string base = StripFrameSuffix(std::filesystem::path(image).stem().string());
                std::string prefix;
                std::string variant;
                if (!SplitTerrainName(base, prefix, variant) || prefix != "dirt") continue;
                const int bits = VariantBits(variant);
                if (bits < 0 || !IsAllowedDirtVariant(bits)) continue;
                m_dirt_bits_by_gid[gid] = bits;
                m_dirt_gid_by_bits[bits] = gid;
            }
        }
    }

    // Build the mask -> representation table (variant + flips).
    for (const auto& [bits, gid] : m_dirt_gid_by_bits)
    {
        for (int flip_combo = 0; flip_combo < 8; ++flip_combo)
        {
            const bool flip_h = (flip_combo & 1) != 0;
            const bool flip_v = (flip_combo & 2) != 0;
            const bool flip_d = (flip_combo & 4) != 0;
            const int mask = ApplyFlips(bits, flip_h, flip_v, flip_d);
            const std::uint32_t raw = gid | (flip_h ? kFlipH : 0) | (flip_v ? kFlipV : 0) | (flip_d ? kFlipD : 0);
            m_dirt_rep_by_mask.emplace(mask, raw);
        }
    }

    // Find the "dirt" tile layer; the layer index counts tilelayers only,
    // matching the client's layer list.
    if (const CJsonValue* layers = tmj->Find("layers"); layers && layers->IsArray())
    {
        int tile_layer_index = 0;
        for (const CJsonValue& layer : layers->AsArray())
        {
            if (layer.Find("type")->AsString() != "tilelayer") continue;
            if (layer.Find("name")->AsString() == "dirt")
            {
                m_dirt_layer_index = tile_layer_index;
                m_width = layer.Find("width")->AsInt(0);
                m_height = layer.Find("height")->AsInt(0);
                const CJsonValue* data = layer.Find("data");
                if (data && data->IsArray() && m_width > 0 && m_height > 0)
                {
                    m_tiles.resize(static_cast<size_t>(m_width) * m_height, 0);
                    size_t index = 0;
                    for (const CJsonValue& value : data->AsArray())
                    {
                        if (index >= m_tiles.size()) break;
                        m_tiles[index++] = static_cast<std::uint32_t>(value.AsNumber(0.0));
                    }
                }
                break;
            }
            ++tile_layer_index;
        }
    }

    if (m_dirt_layer_index >= 0 && m_tiles.size() == static_cast<size_t>(m_width) * m_height)
    {
        m_terrain_loaded = true;
        LOG_INFO("mutable_terrain",
                 "terrain ready: map=" + map_path + " dirt_layer=" + std::to_string(m_dirt_layer_index) +
                     " size=" + std::to_string(m_width) + "x" + std::to_string(m_height) +
                     " variants=" + std::to_string(m_dirt_gid_by_bits.size()));
    } else
    {
        LOG_WARN("mutable_terrain", "map has no usable dirt tile layer: " + map_path);
    }
}

std::uint32_t CMutableTerrainController::TileAt(int row, int col) const
{
    if (row < 0 || col < 0 || row >= m_height || col >= m_width) return 0;
    return m_tiles[static_cast<size_t>(row) * m_width + col];
}

int CMutableTerrainController::CurrentDirtMask(int row, int col) const
{
    const std::uint32_t raw = TileAt(row, col);
    if (!raw) return -1;
    const auto bits_it = m_dirt_bits_by_gid.find(raw & kGidMask);
    if (bits_it == m_dirt_bits_by_gid.end()) return -1;
    return ApplyFlips(bits_it->second, (raw & kFlipH) != 0, (raw & kFlipV) != 0, (raw & kFlipD) != 0);
}

std::uint32_t CMutableTerrainController::DirtGidForMask(int mask) const
{
    const auto it = m_dirt_rep_by_mask.find(mask);
    return it == m_dirt_rep_by_mask.end() ? 0 : it->second;
}

bool CMutableTerrainController::SetTile(CGameWorld& world, int row, int col, std::uint32_t gid)
{
    if (row < 0 || col < 0 || row >= m_height || col >= m_width) return false;
    const size_t index = static_cast<size_t>(row) * m_width + col;
    if (m_tiles[index] == gid) return false;
    m_tiles[index] = gid;
    m_dirty[static_cast<std::uint32_t>(index)] = gid;

    CGameContext* context = world.GameContext();
    auto* network = context ? dynamic_cast<INetworkModule*>(&context->Network()) : nullptr;
    if (network)
        network->BroadcastTerrainDelta(world, static_cast<std::uint8_t>(m_dirt_layer_index),
                                       static_cast<std::uint16_t>(row), static_cast<std::uint16_t>(col), gid);
    return true;
}

void CMutableTerrainController::NormalizeRegion(CGameWorld& world, int min_row, int min_col, int max_row, int max_col)
{
    const auto same_group = [this](int row, int col) {
        const std::uint32_t raw = TileAt(row, col);
        return raw != 0 && m_dirt_bits_by_gid.count(raw & kGidMask) != 0;
    };

    for (int row = std::max(0, min_row); row <= std::min(m_height - 1, max_row); ++row)
    {
        for (int col = std::max(0, min_col); col <= std::min(m_width - 1, max_col); ++col)
        {
            const std::uint32_t raw = TileAt(row, col);
            if (!raw || m_dirt_bits_by_gid.count(raw & kGidMask) == 0) continue;

            const bool tl = same_group(row - 1, col - 1) && same_group(row - 1, col) && same_group(row, col - 1);
            const bool tr = same_group(row - 1, col + 1) && same_group(row - 1, col) && same_group(row, col + 1);
            const bool bl = same_group(row + 1, col - 1) && same_group(row + 1, col) && same_group(row, col - 1);
            const bool br = same_group(row + 1, col + 1) && same_group(row + 1, col) && same_group(row, col + 1);
            const int mask = (tl ? 1 : 0) | (tr ? 2 : 0) | (bl ? 4 : 0) | (br ? 8 : 0);

            if (CurrentDirtMask(row, col) == mask) continue;
            const std::uint32_t next = DirtGidForMask(mask);
            // The four-variant rule cannot express this mask: the tile is
            // cleared instead of falling back to a fifth wall tile.
            if (!next)
            {
                SetTile(world, row, col, 0);
                continue;
            }
            SetTile(world, row, col, next);
        }
    }
}

bool CMutableTerrainController::FindBrushBlock(florrbt::Vector2f pos, int size_tiles, int& out_anchor_x,
                                                 int& out_anchor_y) const
{
    if (!m_terrain_loaded || size_tiles <= 0) return false;
    // Anchor the block so its center lands nearest to `pos`: for size 2 this
    // picks the same "2x2 closest to the flower" block as before.
    const int anchor_x = static_cast<int>(std::floor(pos.x / m_tile_size - (size_tiles - 1) * 0.5));
    const int anchor_y = static_cast<int>(std::floor(pos.y / m_tile_size - (size_tiles - 1) * 0.5));
    if (anchor_x < 0 || anchor_y < 0 || anchor_x + size_tiles > m_width || anchor_y + size_tiles > m_height)
        return false;
    out_anchor_x = anchor_x;
    out_anchor_y = anchor_y;
    return true;
}

bool CMutableTerrainController::BlockClearOfWalls(CGameWorld& world, int anchor_x, int anchor_y, int size_tiles) const
{
    // Probe the block's corners and center so an edit never touches walls.
    const float probe_radius = m_tile_size * 0.5f;
    for (int py = 0; py <= size_tiles; ++py)
    {
        for (int px = 0; px <= size_tiles; ++px)
        {
            const florrbt::Vector2f probe{ (anchor_x + px) * m_tile_size, (anchor_y + py) * m_tile_size };
            if (world.CircleBlockedByWall(probe, probe_radius)) return false;
        }
    }
    return true;
}

int CMutableTerrainController::PaintTerrain(CGameWorld& world, florrbt::Vector2f center, float size_tiles)
{
    const int size = std::max(1, static_cast<int>(std::round(size_tiles)));
    int anchor_x = -1;
    int anchor_y = -1;
    if (!FindBrushBlock(center, size, anchor_x, anchor_y)) return 0;
    if (!BlockClearOfWalls(world, anchor_x, anchor_y, size)) return 0;
    const auto dirt_c = m_dirt_gid_by_bits.find(15);
    if (dirt_c == m_dirt_gid_by_bits.end()) return 0;

    int changed = 0;
    if (size == 2)
    {
        // Tiled "dirt" terrain rule: the four variants are corner tiles; TL
        // gets the V+H mirrored corner, TR the V mirror, BL the original, BR
        // the H mirror, so the 2x2 reads as a rounded dirt circle.
        const auto dirt_tl = m_dirt_gid_by_bits.find(1);
        if (dirt_tl == m_dirt_gid_by_bits.end()) return 0;
        const std::uint32_t base = dirt_tl->second;
        if (SetTile(world, anchor_y, anchor_x, base | kFlipV | kFlipH)) ++changed;
        if (SetTile(world, anchor_y, anchor_x + 1, base | kFlipV)) ++changed;
        if (SetTile(world, anchor_y + 1, anchor_x, base)) ++changed;
        if (SetTile(world, anchor_y + 1, anchor_x + 1, base | kFlipH)) ++changed;
    } else
    {
        for (int row = 0; row < size; ++row)
            for (int col = 0; col < size; ++col)
                if (SetTile(world, anchor_y + row, anchor_x + col, dirt_c->second)) ++changed;
    }

    NormalizeRegion(world, anchor_y - 1, anchor_x - 1, anchor_y + size, anchor_x + size);
    return changed;
}

int CMutableTerrainController::EraseTerrain(CGameWorld& world, florrbt::Vector2f center, float size_tiles)
{
    const int size = std::max(1, static_cast<int>(std::round(size_tiles)));
    int anchor_x = -1;
    int anchor_y = -1;
    if (!FindBrushBlock(center, size, anchor_x, anchor_y)) return 0;

    int changed = 0;
    for (int row = 0; row < size; ++row)
        for (int col = 0; col < size; ++col)
            if (SetTile(world, anchor_y + row, anchor_x + col, 0)) ++changed;

    // Native map walls overlapping the erased block are removed too, and the
    // removal is broadcast so clients drop the same walls locally. The rect
    // is shrunk by an epsilon: wall-rect intersection is inclusive, so an
    // unshrunk block would also take out flush walls of neighbouring tiles.
    constexpr float kEdgeEpsilon = 1.f;
    const florrbt::Vector2f block_min{ anchor_x * m_tile_size + kEdgeEpsilon, anchor_y * m_tile_size + kEdgeEpsilon };
    const florrbt::Vector2f block_max{ (anchor_x + size) * m_tile_size - kEdgeEpsilon,
                                  (anchor_y + size) * m_tile_size - kEdgeEpsilon };
    if (world.RemoveWallsInRect(block_min, block_max) > 0)
    {
        m_removed_wall_rects.emplace_back(block_min, block_max);
        CGameContext* context = world.GameContext();
        auto* network = context ? dynamic_cast<INetworkModule*>(&context->Network()) : nullptr;
        if (network) network->BroadcastTerrainWallRemove(world, block_min, block_max);
    }

    // Re-apply the terrain rule with a flood fill: the hole's edge is
    // normalized first, and corrections keep walking outward until every
    // reachable dirt tile conforms.
    for (int row = anchor_y - 1; row <= anchor_y + size; ++row)
        for (int col = anchor_x - 1; col <= anchor_x + size; ++col)
            NormalizeFloodFill(world, row, col);
    return changed;
}

void CMutableTerrainController::PaintBrush(CGameWorld& world, CPlayerFlower& flower)
{
    PaintTerrain(world, flower.Pos());
}

void CMutableTerrainController::EraseBrush(CGameWorld& world, CPlayerFlower& flower)
{
    EraseTerrain(world, flower.Pos());
}

int CMutableTerrainController::DeleteTile(CGameWorld& world, int row, int col)
{
    if (!m_terrain_loaded || TileAt(row, col) == 0) return 0;

    SetTile(world, row, col, 0);
    // Re-conform the hole's edge: flood fills seeded from the cleared cell
    // and its eight neighbors.
    for (int dr = -1; dr <= 1; ++dr)
        for (int dc = -1; dc <= 1; ++dc)
            NormalizeFloodFill(world, row + dr, col + dc);
    return 1;
}

void CMutableTerrainController::NormalizeFloodFill(CGameWorld& world, int seed_row, int seed_col)
{
    // DFS: fix any dirt tile that violates the terrain rule, then walk down
    // into its neighbors; a branch stops as soon as it meets a tile that
    // already conforms (or a non-dirt tile). The visited set bounds the walk.
    const auto same_group = [this](int row, int col) {
        const std::uint32_t raw = TileAt(row, col);
        return raw != 0 && m_dirt_bits_by_gid.count(raw & kGidMask) != 0;
    };

    std::vector<std::pair<int, int>> stack;
    std::unordered_set<std::uint64_t> visited;
    stack.emplace_back(seed_row, seed_col);
    while (!stack.empty())
    {
        const auto [row, col] = stack.back();
        stack.pop_back();
        if (row < 0 || col < 0 || row >= m_height || col >= m_width) continue;
        const std::uint64_t key = (static_cast<std::uint64_t>(row) << 32) |
                                  static_cast<std::uint32_t>(col);
        if (!visited.insert(key).second) continue;

        const std::uint32_t raw = TileAt(row, col);
        if (!raw || m_dirt_bits_by_gid.count(raw & kGidMask) == 0) continue;

        const bool tl = same_group(row - 1, col - 1) && same_group(row - 1, col) && same_group(row, col - 1);
        const bool tr = same_group(row - 1, col + 1) && same_group(row - 1, col) && same_group(row, col + 1);
        const bool bl = same_group(row + 1, col - 1) && same_group(row + 1, col) && same_group(row, col - 1);
        const bool br = same_group(row + 1, col + 1) && same_group(row + 1, col) && same_group(row, col + 1);
        const int mask = (tl ? 1 : 0) | (tr ? 2 : 0) | (bl ? 4 : 0) | (br ? 8 : 0);

        if (CurrentDirtMask(row, col) == mask) continue; // conforms: branch stops

        // Decisive cleanup: a dirt tile that violates the rule and has no dirt
        // neighbors left is removed outright instead of lingering as a stray
        // fragment.
        int dirt_neighbors = 0;
        for (int dy = -1; dy <= 1; ++dy)
            for (int dx = -1; dx <= 1; ++dx)
                if ((dx != 0 || dy != 0) && same_group(row + dy, col + dx)) ++dirt_neighbors;
        if (dirt_neighbors == 0)
        {
            SetTile(world, row, col, 0);
            continue;
        }

        const std::uint32_t next = DirtGidForMask(mask);
        if (!next)
        {
            // The four-variant rule cannot express this mask: clear the tile
            // (never a fifth wall tile) and keep walking so its neighbors
            // re-conform to the new edge.
            SetTile(world, row, col, 0);
            for (int dy = -1; dy <= 1; ++dy)
                for (int dx = -1; dx <= 1; ++dx)
                    if (dx != 0 || dy != 0) stack.emplace_back(row + dy, col + dx);
            continue;
        }
        SetTile(world, row, col, next);

        for (int dy = -1; dy <= 1; ++dy)
            for (int dx = -1; dx <= 1; ++dx)
                if (dx != 0 || dy != 0) stack.emplace_back(row + dy, col + dx);
    }
}

void CMutableTerrainController::OnTick(CGameWorld& world, float dt)
{
    COpenController::OnTick(world, dt);

    // After a hot-reload restore the tile array and wall grid are rebuilt from
    // the map file: reload the terrain, re-apply the recorded tile changes,
    // and re-remove the recorded native walls.
    if (!m_terrain_loaded && !m_terrain_tried)
    {
        LoadTerrainData(world);
        if (m_terrain_loaded)
        {
            for (const auto& [index, gid] : m_dirty)
            {
                if (index < m_tiles.size()) m_tiles[index] = gid;
            }
            for (const auto& [min_pos, max_pos] : m_removed_wall_rects)
                world.RemoveWallsInRect(min_pos, max_pos);
        }
    }
    if (!m_terrain_loaded) return;

    CGameContext* context = world.GameContext();
    if (!context) return;

    for (const auto& player : context->Players())
    {
        if (!player) continue;
        auto* flower = dynamic_cast<CPlayerFlower*>(player->GetEntity());
        if (!flower || flower->IsDead() || flower->IsMarkedForDes()) continue;
        const bool has_brush = flower->HasActivePetal(ToPetalType(EModPetalType::Brush));
        const bool has_eraser = flower->HasActivePetal(ToPetalType(EModPetalType::Eraser));
        if (!has_brush && !has_eraser) continue;
        const bool paint = has_brush && flower->m_attacking;
        const bool erase = has_eraser && flower->m_defending;
        if (!paint && !erase) continue;

        const std::uint32_t player_id = player->GetId();
        float& cooldown = m_paint_cooldowns[player_id];
        cooldown -= dt;
        if (cooldown > 0.f) continue;
        cooldown = game_config::default_brush_reload;
        if (erase) EraseBrush(world, *flower);
        else PaintBrush(world, *flower);
    }
}

void CMutableTerrainController::OnPlayerEnteredWorld(CGameWorld& world, CPlayer& player)
{
    COpenController::OnPlayerEnteredWorld(world, player);
    if (!m_terrain_loaded || m_dirty.empty()) return;
    CGameContext* context = world.GameContext();
    auto* network = context ? dynamic_cast<INetworkModule*>(&context->Network()) : nullptr;
    if (!network) return;
    for (const auto& [index, gid] : m_dirty)
    {
        const int row = static_cast<int>(index / static_cast<std::uint32_t>(std::max(1, m_width)));
        const int col = static_cast<int>(index % static_cast<std::uint32_t>(std::max(1, m_width)));
        network->SendTerrainDelta(player, static_cast<std::uint8_t>(m_dirt_layer_index),
                                  static_cast<std::uint16_t>(row), static_cast<std::uint16_t>(col), gid);
    }
    for (const auto& [min_pos, max_pos] : m_removed_wall_rects)
        network->SendTerrainWallRemove(player, min_pos, max_pos);
}

void CMutableTerrainController::CaptureSnapshot(CSnapshotWriter& writer) const
{
    COpenController::CaptureSnapshot(writer);
    writer.Field("terrain_layer", m_dirt_layer_index);
    writer.Field("terrain_width", m_width);
    writer.Field("terrain_height", m_height);
    writer.Field("terrain_tile_size", m_tile_size);
    CJsonOwner dirty = MakeJsonArray();
    for (const auto& [index, gid] : m_dirty)
    {
        CJsonOwner entry = MakeJsonArray();
        AppendJson(entry.get(), json_integer(static_cast<json_int_t>(index)));
        AppendJson(entry.get(), json_integer(static_cast<json_int_t>(gid)));
        AppendJson(dirty.get(), entry.release());
    }
    writer.Node("terrain_dirty", dirty.release());

    CJsonOwner walls = MakeJsonArray();
    for (const auto& [min_pos, max_pos] : m_removed_wall_rects)
    {
        CJsonOwner entry = MakeJsonArray();
        AppendJson(entry.get(), json_real(min_pos.x));
        AppendJson(entry.get(), json_real(min_pos.y));
        AppendJson(entry.get(), json_real(max_pos.x));
        AppendJson(entry.get(), json_real(max_pos.y));
        AppendJson(walls.get(), entry.release());
    }
    writer.Node("terrain_wall_removes", walls.release());
}

bool CMutableTerrainController::RestoreSnapshot(const CSnapshotReader& reader, std::uint32_t version, std::string& error)
{
    if (!COpenController::RestoreSnapshot(reader, version, error)) return false;
    if (version > SnapshotVersion())
    {
        error = "Unsupported mutable terrain snapshot version";
        return false;
    }

    m_dirt_layer_index = reader.Int("terrain_layer", -1);
    m_width = reader.Int("terrain_width", 0);
    m_height = reader.Int("terrain_height", 0);
    m_tile_size = reader.Float("terrain_tile_size", 512.f);
    m_dirty.clear();
    if (const json_t* dirty = reader.Node("terrain_dirty"); json_is_array(dirty))
    {
        size_t index = 0;
        json_t* entry = nullptr;
        json_array_foreach(dirty, index, entry)
        {
            if (!json_is_array(entry) || json_array_size(entry) < 2) continue;
            const std::uint32_t cell = static_cast<std::uint32_t>(json_integer_value(json_array_get(entry, 0)));
            const std::uint32_t gid = static_cast<std::uint32_t>(json_integer_value(json_array_get(entry, 1)));
            m_dirty[cell] = gid;
        }
    }
    m_removed_wall_rects.clear();
    if (const json_t* walls = reader.Node("terrain_wall_removes"); json_is_array(walls))
    {
        size_t index = 0;
        json_t* entry = nullptr;
        json_array_foreach(walls, index, entry)
        {
            if (!json_is_array(entry) || json_array_size(entry) < 4) continue;
            const florrbt::Vector2f min_pos{ static_cast<float>(json_real_value(json_array_get(entry, 0))),
                                        static_cast<float>(json_real_value(json_array_get(entry, 1))) };
            const florrbt::Vector2f max_pos{ static_cast<float>(json_real_value(json_array_get(entry, 2))),
                                        static_cast<float>(json_real_value(json_array_get(entry, 3))) };
            m_removed_wall_rects.emplace_back(min_pos, max_pos);
        }
    }
    return true;
}
