#include "world_terrain.h"
#include "../../../Engine/logger.h"
#include "../../../Shared/game_config.h"
#include "../../HotReload/snapshot_archive.h"
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
#include <bit>
#include <type_traits>

static_assert(std::is_nothrow_move_assignable_v<CWall>);

namespace
{
std::uint64_t MapDefinitionSignature(const FlorrBtMap* map)
{
    if (!map) return 0;
    std::uint64_t hash = 14695981039346656037ull;
    const auto mix = [&](std::uint32_t value) {
        for (unsigned byte = 0; byte < 4; ++byte) { hash ^= (value >> (byte * 8)) & 255u; hash *= 1099511628211ull; }
    };
    mix(map->width); mix(map->height); mix(map->tile_width); mix(map->tile_height);
    for (const auto& [gid, image] : map->tile_images)
    {
        mix(gid);
        const auto name = std::filesystem::path(image).stem().string();
        mix(static_cast<std::uint32_t>(name.size()));
        for (unsigned char ch : name) mix(ch);
    }
    for (const auto& layer : map->tile_layers)
    {
        mix(layer.width); mix(layer.height);
        for (unsigned char ch : layer.name) mix(ch);
        for (int tile : layer.tiles) mix(static_cast<std::uint32_t>(tile));
    }
    for (const auto& wall : map->walls)
    {
        mix(static_cast<std::uint32_t>(wall.category)); mix(wall.closed ? 1u : 0u);
        for (const auto& point : wall.vertices) { mix(std::bit_cast<std::uint32_t>(point.x)); mix(std::bit_cast<std::uint32_t>(point.y)); }
    }
    return hash ? hash : 1;
}
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

bool IsAllowedDirtVariant(int bits)
{
    return bits == 1 || bits == 5 || bits == 13 || bits == 15;
}

std::string StripFrameSuffix(std::string name)
{
    if (name.size() > 2 && name.compare(name.size() - 2, 2, "_0") == 0) name.resize(name.size() - 2);
    return name;
}

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
}

void CWorldTerrain::EnsureLoaded()
{
    if (m_terrain_tried) return;
    CWorldTerrain prepared(m_world);
    prepared.LoadTerrainData(m_world);
    m_tiles = std::move(prepared.m_tiles);
    m_dirt_bits_by_gid = std::move(prepared.m_dirt_bits_by_gid);
    m_dirt_gid_by_bits = std::move(prepared.m_dirt_gid_by_bits);
    m_dirt_rep_by_mask = std::move(prepared.m_dirt_rep_by_mask);
    m_dirt_layer_index = prepared.m_dirt_layer_index; m_width = prepared.m_width; m_height = prepared.m_height;
    m_tile_size = prepared.m_tile_size; m_terrain_loaded = prepared.m_terrain_loaded; m_terrain_tried = true;
}

void CWorldTerrain::ResetForMap()
{
    m_wall_board.DetachWorld(m_world);
    m_wall_board = CWall{};
    m_tiles.clear(); m_dirty.clear(); m_removed_wall_rects.clear(); m_paint_cooldowns.clear();
    m_dirt_bits_by_gid.clear(); m_dirt_gid_by_bits.clear(); m_dirt_rep_by_mask.clear();
    m_terrain_loaded = m_terrain_tried = m_has_saved_board = m_geometry_dirty = false;
    m_dirt_layer_index = -1; m_width = m_height = 0;
    m_tile_size = m_world.GetMap() ? static_cast<float>(m_world.GetMap()->tile_width) : 512.f;
    m_revision = 0; m_definition_signature = MapDefinitionSignature(m_world.GetMap());
}

void CWorldTerrain::GeometryChanged()
{
    if (!m_replaying && m_revision != UINT64_MAX) ++m_revision;
}

void CWorldTerrain::RecordGeometryRemoval(florrbt::Vector2f min, florrbt::Vector2f max)
{
    if (m_replaying) return;
    m_removed_wall_rects.emplace_back(min, max);
    m_geometry_dirty = true;
    GeometryChanged();
}

void CWorldTerrain::EnsureDigBoard()
{
    EnsureLoaded();
    m_wall_board.BindWorld(m_world);
    if (m_wall_board.IsLoaded() && !m_geometry_dirty) return;
    if (!m_wall_board.IsLoaded() || m_geometry_dirty) m_wall_board.Reload(m_world);
    if (m_terrain_loaded)
        for (int row = 0; row < m_height; ++row)
            for (int col = 0; col < m_width; ++col)
                if (CurrentDirtMask(row, col) >= 0) m_wall_board.InsertCell(row, col);
    m_geometry_dirty = false;
}

std::unique_ptr<CWall> CWorldTerrain::PrepareDigBoard()
{
    EnsureLoaded();
    auto prepared = std::make_unique<CWall>(m_wall_board);
    if (!prepared->IsLoaded() || m_geometry_dirty) prepared->RefreshFromWorld(m_world);
    if (m_terrain_loaded)
        for (int row = 0; row < m_height; ++row)
            for (int col = 0; col < m_width; ++col)
                if (CurrentDirtMask(row, col) >= 0) prepared->InsertCell(row, col);
    return prepared;
}

void CWorldTerrain::InstallDigBoard(std::unique_ptr<CWall> board) noexcept
{
    if (!board) return;
    m_wall_board = std::move(*board);
    m_wall_board.BindWorld(m_world);
    m_geometry_dirty = false;
}

void CWorldTerrain::LoadTerrainData(CGameWorld& world)
{
    m_terrain_tried = true; m_terrain_loaded = false;
    m_dirt_layer_index = -1; m_width = m_height = 0;
    m_tiles.clear(); m_dirt_bits_by_gid.clear(); m_dirt_gid_by_bits.clear(); m_dirt_rep_by_mask.clear();
    const auto* map = world.GetMap();
    if (!map || map->tile_width <= 0) return;
    m_tile_size = static_cast<float>(map->tile_width);
    for (const auto& [gid, image] : map->tile_images)
    {
        const auto base = StripFrameSuffix(std::filesystem::path(image).stem().string());
        std::string prefix, variant;
        if (!SplitTerrainName(base, prefix, variant) || prefix != "dirt") continue;
        const int bits = VariantBits(variant);
        if (!IsAllowedDirtVariant(bits)) continue;
        m_dirt_bits_by_gid[gid] = bits;
        auto [it, inserted] = m_dirt_gid_by_bits.emplace(bits, gid);
        if (!inserted) it->second = std::min(it->second, gid);
    }
    // 固定变体和翻转顺序，避免哈希表遍历顺序改变绘制结果。
    for (int bits : {1, 5, 13, 15})
    {
        const auto found = m_dirt_gid_by_bits.find(bits);
        if (found == m_dirt_gid_by_bits.end()) continue;
        const auto gid = found->second;
        for (int flip = 0; flip < 8; ++flip)
        {
            const bool h = (flip & 1) != 0, v = (flip & 2) != 0, d = (flip & 4) != 0;
            m_dirt_rep_by_mask.emplace(ApplyFlips(bits, h, v, d),
                gid | (h ? kFlipH : 0) | (v ? kFlipV : 0) | (d ? kFlipD : 0));
        }
    }
    for (size_t index = 0; index < map->tile_layers.size() && index <= UINT8_MAX; ++index)
    {
        const auto& layer = map->tile_layers[index];
        if (layer.name != "dirt") continue;
        if (layer.width <= 0 || layer.height <= 0 || layer.width > UINT16_MAX || layer.height > UINT16_MAX ||
            layer.tiles.size() != static_cast<size_t>(layer.width) * layer.height) return;
        m_dirt_layer_index = static_cast<int>(index); m_width = layer.width; m_height = layer.height;
        m_tiles.assign(layer.tiles.begin(), layer.tiles.end());
        m_terrain_loaded = true;
        return;
    }
}

std::uint32_t CWorldTerrain::TileAt(int row, int col) const
{
    if (row < 0 || col < 0 || row >= m_height || col >= m_width) return 0;
    return m_tiles[static_cast<size_t>(row) * m_width + col];
}

int CWorldTerrain::CurrentDirtMask(int row, int col) const
{
    const std::uint32_t raw = TileAt(row, col);
    if (!raw) return -1;
    const auto bits_it = m_dirt_bits_by_gid.find(raw & kGidMask);
    if (bits_it == m_dirt_bits_by_gid.end()) return -1;
    return ApplyFlips(bits_it->second, (raw & kFlipH) != 0, (raw & kFlipV) != 0, (raw & kFlipD) != 0);
}

std::uint32_t CWorldTerrain::DirtGidForMask(int mask) const
{
    const auto it = m_dirt_rep_by_mask.find(mask);
    return it == m_dirt_rep_by_mask.end() ? 0 : it->second;
}

bool CWorldTerrain::SetTile(CGameWorld& world, int row, int col, std::uint32_t gid)
{
    if (row < 0 || col < 0 || row >= m_height || col >= m_width) return false;
    const size_t index = static_cast<size_t>(row) * m_width + col;
    if (m_tiles[index] == gid) return false;
    m_tiles[index] = gid;
    m_dirty[static_cast<std::uint32_t>(index)] = gid;
    GeometryChanged();

    CGameContext* context = world.GameContext();
    if (context)
        context->WorldEffects().BroadcastTerrainDelta(world, static_cast<std::uint8_t>(m_dirt_layer_index),
                                       static_cast<std::uint16_t>(row), static_cast<std::uint16_t>(col), gid);
    return true;
}

void CWorldTerrain::NormalizeRegion(CGameWorld& world, int min_row, int min_col, int max_row, int max_col)
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
            if (!next)
            {
                SetTile(world, row, col, 0);
                continue;
            }
            SetTile(world, row, col, next);
        }
    }
}

bool CWorldTerrain::FindBrushBlock(florrbt::Vector2f pos, int size_tiles, int& out_anchor_x,
                                                 int& out_anchor_y) const
{
    if (!m_terrain_loaded || size_tiles <= 0) return false;
    const int anchor_x = static_cast<int>(std::floor(pos.x / m_tile_size - (size_tiles - 1) * 0.5));
    const int anchor_y = static_cast<int>(std::floor(pos.y / m_tile_size - (size_tiles - 1) * 0.5));
    if (anchor_x < 0 || anchor_y < 0 || anchor_x + size_tiles > m_width || anchor_y + size_tiles > m_height)
        return false;
    out_anchor_x = anchor_x;
    out_anchor_y = anchor_y;
    return true;
}

bool CWorldTerrain::BlockClearOfWalls(CGameWorld& world, int anchor_x, int anchor_y, int size_tiles) const
{
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

int CWorldTerrain::PaintTerrain(florrbt::Vector2f center, float size_tiles)
{
    auto& world = m_world;
    EnsureLoaded();
    if (!std::isfinite(size_tiles) || size_tiles <= 0.f || size_tiles > 256.f ||
        !std::isfinite(center.x) || !std::isfinite(center.y)) return 0;
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

int CWorldTerrain::EraseTerrain(florrbt::Vector2f center, float size_tiles)
{
    auto& world = m_world;
    EnsureLoaded();
    if (!std::isfinite(size_tiles) || size_tiles <= 0.f || size_tiles > 256.f ||
        !std::isfinite(center.x) || !std::isfinite(center.y)) return 0;
    const int size = std::max(1, static_cast<int>(std::round(size_tiles)));
    int anchor_x = -1;
    int anchor_y = -1;
    if (!FindBrushBlock(center, size, anchor_x, anchor_y)) return 0;

    int changed = 0;
    for (int row = 0; row < size; ++row)
        for (int col = 0; col < size; ++col)
            if (SetTile(world, anchor_y + row, anchor_x + col, 0)) ++changed;

    constexpr float kEdgeEpsilon = 1.f;
    const florrbt::Vector2f block_min{ anchor_x * m_tile_size + kEdgeEpsilon, anchor_y * m_tile_size + kEdgeEpsilon };
    const florrbt::Vector2f block_max{ (anchor_x + size) * m_tile_size - kEdgeEpsilon,
                                  (anchor_y + size) * m_tile_size - kEdgeEpsilon };
    if (world.RemoveWallsInRect(block_min, block_max) > 0)
    {
        CGameContext* context = world.GameContext();
        if (context) context->WorldEffects().BroadcastTerrainWallRemove(world, block_min, block_max);
    }

    for (int row = anchor_y - 1; row <= anchor_y + size; ++row)
        for (int col = anchor_x - 1; col <= anchor_x + size; ++col)
            NormalizeFloodFill(world, row, col);
    return changed;
}

void CWorldTerrain::PaintBrush(CGameWorld& world, CPlayerFlower& flower)
{
    PaintTerrain(flower.Pos());
}

void CWorldTerrain::EraseBrush(CGameWorld& world, CPlayerFlower& flower)
{
    EraseTerrain(flower.Pos());
}

int CWorldTerrain::DeleteTile(int row, int col)
{
    auto& world = m_world;
    EnsureLoaded();
    if (!m_terrain_loaded || TileAt(row, col) == 0) return 0;

    SetTile(world, row, col, 0);
    for (int dr = -1; dr <= 1; ++dr)
        for (int dc = -1; dc <= 1; ++dc)
            NormalizeFloodFill(world, row + dr, col + dc);
    return 1;
}

void CWorldTerrain::NormalizeFloodFill(CGameWorld& world, int seed_row, int seed_col)
{
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

        if (CurrentDirtMask(row, col) == mask) continue;

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

void CWorldTerrain::TickTools(float dt)
{
    auto& world = m_world;

    EnsureLoaded();
    for (auto& [id, cooldown] : m_paint_cooldowns) cooldown.remaining = std::max(0.f, cooldown.remaining - dt);
    if (!m_terrain_loaded) return;

    CGameContext* context = world.GameContext();
    if (!context) return;

    for (CPlayer* player : world.Players())
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

        auto& cooldown = m_paint_cooldowns[player->GetId()];
        if (cooldown.session != player->SessionGeneration()) cooldown = {player->SessionGeneration(), 0.f};
        if (cooldown.remaining > 0.f) continue;
        cooldown.remaining = std::max(0.f, game_config::default_brush_reload);
        if (erase) EraseBrush(world, *flower);
        else PaintBrush(world, *flower);
    }
}

bool CWorldTerrain::WritePlayerScene(IPlayerSceneWriter& writer) const
{
    for (const auto& [index, gid] : m_dirty)
    {
        if (m_dirt_layer_index < 0 || m_width <= 0) return false;
        const int row = static_cast<int>(index / static_cast<std::uint32_t>(std::max(1, m_width)));
        const int col = static_cast<int>(index % static_cast<std::uint32_t>(std::max(1, m_width)));
        if (!writer.TerrainTile(static_cast<std::uint8_t>(m_dirt_layer_index),
                               static_cast<std::uint16_t>(row), static_cast<std::uint16_t>(col), gid)) return false;
    }
    for (const auto& [min_pos, max_pos] : m_removed_wall_rects)
        if (!writer.RemoveWall(min_pos, max_pos)) return false;
    return true;
}

void CWorldTerrain::CaptureSnapshot(CSnapshotWriter& writer) const
{
    writer.Field("version", 1);
    writer.UInt64("revision", m_revision);
    writer.UInt64("definition_signature", m_definition_signature);
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
    if (m_wall_board.IsLoaded())
    {
        auto board = MakeJsonObject(); CSnapshotWriter board_writer(board.get());
        m_wall_board.CaptureSnapshot(board_writer); writer.Node("wall_board", board.release());
    }
}

bool CWorldTerrain::RestoreSnapshot(const CSnapshotReader& reader, std::uint32_t version, std::string& error)
{
    if (!reader.IsValid() || version > 1) { error = "地形快照版本无效"; return false; }
    const auto signature = reader.UInt64("definition_signature", 0);
    if (signature && signature != m_definition_signature)
    { error = "地图资源与地形快照不兼容"; return false; }
    const int width = reader.Int("terrain_width", 0), height = reader.Int("terrain_height", 0);
    if (width < 0 || height < 0 || width > UINT16_MAX || height > UINT16_MAX)
    { error = "地形快照尺寸超出范围"; return false; }
    std::unordered_map<std::uint32_t, std::uint32_t> dirty;
    std::vector<std::pair<florrbt::Vector2f, florrbt::Vector2f>> removals;
    if (const auto* values = reader.Node("terrain_dirty"))
    {
        if (!json_is_array(values) || json_array_size(values) > 65536)
        { error = "地形修改列表无效"; return false; }
        for (size_t i = 0; i < json_array_size(values); ++i)
        {
            const auto* value = json_array_get(values, i);
            if (!json_is_array(value) || json_array_size(value) != 2 ||
                !json_is_integer(json_array_get(value, 0)) || !json_is_integer(json_array_get(value, 1)))
            { error = "地形修改条目无效"; return false; }
            const auto cell = json_integer_value(json_array_get(value, 0));
            const auto gid = json_integer_value(json_array_get(value, 1));
            if (cell < 0 || static_cast<std::uint64_t>(cell) >= static_cast<std::uint64_t>(width) * height ||
                gid < 0 || static_cast<std::uint64_t>(gid) > UINT32_MAX ||
                !dirty.emplace(static_cast<std::uint32_t>(cell), static_cast<std::uint32_t>(gid)).second)
            { error = "地形修改条目越界或重复"; return false; }
        }
    }
    if (const auto* values = reader.Node("terrain_wall_removes"))
    {
        if (!json_is_array(values) || json_array_size(values) > 65536)
        { error = "地形删墙列表无效"; return false; }
        for (size_t i = 0; i < json_array_size(values); ++i)
        {
            const auto* value = json_array_get(values, i);
            if (!json_is_array(value) || json_array_size(value) != 4)
            { error = "删墙区域无效"; return false; }
            float coords[4];
            for (size_t n = 0; n < 4; ++n)
            {
                if (!json_is_number(json_array_get(value, n))) { error = "删墙坐标无效"; return false; }
                coords[n] = static_cast<float>(json_number_value(json_array_get(value, n)));
                if (!std::isfinite(coords[n])) { error = "删墙坐标不是有限数"; return false; }
            }
            if (coords[0] > coords[2] || coords[1] > coords[3]) { error = "删墙区域反向"; return false; }
            removals.emplace_back(florrbt::Vector2f{coords[0], coords[1]}, florrbt::Vector2f{coords[2], coords[3]});
        }
    }
    CWall board;
    const bool has_board = reader.Has("wall_board") || reader.Has("wall_width");
    if (has_board && !board.RestoreSnapshot(reader.Has("wall_board") ? CSnapshotReader(reader.Node("wall_board")) : reader, 1, error))
        return false;
    m_dirty = std::move(dirty); m_removed_wall_rects = std::move(removals);
    m_wall_board = std::move(board); m_has_saved_board = has_board;
    m_revision = reader.UInt64("revision", 0);
    m_terrain_loaded = m_terrain_tried = false; m_tiles.clear();
    return true;
}

bool CWorldTerrain::ResolveReferences(std::string& error)
{
    auto& world = m_world;
    struct CReplay { bool& active; CReplay(bool& a) : active(a) { active = true; } ~CReplay() { active = false; } } replay(m_replaying);
    LoadTerrainData(world);
    if (!m_terrain_loaded && !m_dirty.empty())
    { error = "Restored terrain has no compatible map layer"; return false; }
    for (const auto& [index, gid] : m_dirty)
    {
        if (index >= m_tiles.size()) { error = "Restored terrain cell is outside its map"; return false; }
        if (gid && !m_dirt_bits_by_gid.contains(gid & kGidMask))
        { error = "地形快照引用了无效图块"; return false; }
        m_tiles[index] = gid;
    }
    for (const auto& [min, max] : m_removed_wall_rects) world.RemoveWallsInRect(min, max);
    // 旧版墙板回放的删墙动作一并迁入世界历史，后续切换玩法不再丢失。
    const auto revision = m_revision;
    m_replaying = false;
    if (m_has_saved_board) m_wall_board.Reload(world);
    m_revision = revision; m_geometry_dirty = false;
    return true;
}
