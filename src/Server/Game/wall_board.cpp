#include "wall_board.h"
#include "../../Engine/map_tools.h"
#include "../HotReload/snapshot_archive.h"
#include "../Module/network_module.h"
#include "gamecontext.h"
#include "gameworld.h"
#include <algorithm>

std::uint32_t CWall::CellIndex(int row, int col) const
{
    return static_cast<std::uint32_t>(row) * static_cast<std::uint32_t>(std::max(1, m_width)) +
           static_cast<std::uint32_t>(col);
}

bool CWall::HasAliveWallOnCell(std::uint32_t cell) const
{
    const auto it = m_cells.find(cell);
    if (it == m_cells.end()) return false;
    for (const int id : it->second)
        if (m_alive[static_cast<size_t>(id)]) return true;
    return false;
}

bool CWall::TileSolid(int row, int col) const
{
    if (!m_loaded || !InBounds(row, col)) return false;
    return HasAliveWallOnCell(CellIndex(row, col));
}

bool CWall::HasTile(int row, int col) const
{
    if (!m_loaded || !InBounds(row, col)) return false;
    const auto it = m_cells.find(CellIndex(row, col));
    return it != m_cells.end() && !it->second.empty();
}

bool CWall::HasWall(int row, int col) const { return HasTile(row, col); }

void CWall::BuildFromMap(const CGameWorld& world)
{
    const FlorrBtMap* map = world.GetMap();
    m_width = map ? map->width : 0;
    m_height = map ? map->height : 0;
    m_tile_size = map ? static_cast<float>(map->tile_width) : 512.f;
    m_walls.clear();
    m_alive.clear();
    m_cells.clear();

    if (!map) return;

    const int max_row = std::max(0, m_height - 1);
    const int max_col = std::max(0, m_width - 1);
    for (const FlorrBtMap::Wall& wall : map->walls)
    {
        const int id = static_cast<int>(m_walls.size());
        SWallRecord record;
        record.min_pos = { wall.min_x, wall.min_y };
        record.max_pos = { wall.max_x, wall.max_y };
        // Primary cell: the cell under the wall's center. Partial/spanning
        // walls get a single deterministic owner cell this way.
        const int primary_row = std::clamp(static_cast<int>(wall.center.y / m_tile_size), 0, max_row);
        const int primary_col = std::clamp(static_cast<int>(wall.center.x / m_tile_size), 0, max_col);
        record.primary_cell = CellIndex(primary_row, primary_col);
        m_walls.push_back(record);
        m_alive.push_back(true);

        // Register every cell the wall's AABB touches.
        const int min_row = std::clamp(static_cast<int>(wall.min_y / m_tile_size), 0, max_row);
        const int max_row_cell = std::clamp(static_cast<int>(wall.max_y / m_tile_size), 0, max_row);
        const int min_col = std::clamp(static_cast<int>(wall.min_x / m_tile_size), 0, max_col);
        const int max_col_cell = std::clamp(static_cast<int>(wall.max_x / m_tile_size), 0, max_col);
        for (int row = min_row; row <= max_row_cell; ++row)
            for (int col = min_col; col <= max_col_cell; ++col)
                m_cells[CellIndex(row, col)].push_back(id);
    }
    m_loaded = true;
}

void CWall::ReplayInsertedCells()
{
    for (const std::uint32_t cell : m_inserted_cells)
    {
        // Skip when the cell already carries a cell wall (idempotent replay).
        bool exists = false;
        for (const SWallRecord& record : m_walls)
            if (!record.from_map && record.primary_cell == cell)
            {
                exists = true;
                break;
            }
        if (exists) continue;

        SWallRecord record;
        record.primary_cell = cell;
        record.from_map = false;
        const int row = static_cast<int>(cell / static_cast<std::uint32_t>(std::max(1, m_width)));
        const int col = static_cast<int>(cell % static_cast<std::uint32_t>(std::max(1, m_width)));
        record.min_pos = { col * m_tile_size, row * m_tile_size };
        record.max_pos = { (col + 1) * m_tile_size, (row + 1) * m_tile_size };
        m_cells[cell].push_back(static_cast<int>(m_walls.size()));
        m_walls.push_back(record);
        m_alive.push_back(true);
    }
}

void CWall::RemoveMapWallFromWorld(CGameWorld& world, const SWallRecord& record) const
{
    // Remove by the wall's own AABB, shrunk by an epsilon: RemoveWallsInRect
    // matches on inclusive intersection, so an unshrunk rect would also take
    // out flush neighbours sharing an edge with this wall.
    const florrbt::Vector2f shrunk_min{ record.min_pos.x + kWallRemoveEpsilon, record.min_pos.y + kWallRemoveEpsilon };
    const florrbt::Vector2f shrunk_max{ record.max_pos.x - kWallRemoveEpsilon, record.max_pos.y - kWallRemoveEpsilon };
    if (shrunk_max.x <= shrunk_min.x || shrunk_max.y <= shrunk_min.y) return;
    world.RemoveWallsInRect(shrunk_min, shrunk_max);
}

void CWall::ReplayRecordedRemovals(CGameWorld& world)
{
    for (const std::uint32_t cell : m_removed_primary_cells)
    {
        for (size_t id = 0; id < m_walls.size(); ++id)
        {
            if (!m_alive[id] || m_walls[id].primary_cell != cell) continue;
            if (m_walls[id].from_map) RemoveMapWallFromWorld(world, m_walls[id]);
            m_alive[id] = false;
        }
    }
}

void CWall::Reload(CGameWorld& world)
{
    m_world = &world;
    BuildFromMap(world);
    ReplayInsertedCells();
    ReplayRecordedRemovals(world);
}

void CWall::BroadcastWallRemovals(CGameWorld& world, const std::vector<int>& wall_ids) const
{
    CGameContext* context = world.GameContext();
    auto* network = context ? dynamic_cast<INetworkModule*>(&context->Network()) : nullptr;
    if (!network) return;

    for (const int id : wall_ids)
    {
        const SWallRecord& record = m_walls[static_cast<size_t>(id)];
        for (const auto& player : context->Players())
        {
            if (!player) continue;
            network->SendTerrainWallRemove(*player, record.min_pos, record.max_pos);
        }
    }
}

bool CWall::InsertCell(int row, int col)
{
    if (!m_loaded || !InBounds(row, col)) return false;
    const std::uint32_t cell = CellIndex(row, col);

    for (const SWallRecord& record : m_walls)
        if (!record.from_map && record.primary_cell == cell) return false;

    SWallRecord record;
    record.primary_cell = cell;
    record.from_map = false;
    record.min_pos = { col * m_tile_size, row * m_tile_size };
    record.max_pos = { (col + 1) * m_tile_size, (row + 1) * m_tile_size };
    m_cells[cell].push_back(static_cast<int>(m_walls.size()));
    m_walls.push_back(record);
    m_alive.push_back(true);
    m_inserted_cells.push_back(cell);
    return true;
}

bool CWall::CommitRemove(int row, int col, int team)
{
    if (!m_loaded || !m_world || !InBounds(row, col)) return false;
    const std::uint32_t cell = CellIndex(row, col);

    std::vector<int> ids;
    for (size_t id = 0; id < m_walls.size(); ++id)
    {
        if (!m_alive[id] || m_walls[id].primary_cell != cell) continue;
        ids.push_back(static_cast<int>(id));
    }
    if (ids.empty()) return false;

    for (const int id : ids)
    {
        if (m_walls[static_cast<size_t>(id)].from_map)
            RemoveMapWallFromWorld(*m_world, m_walls[static_cast<size_t>(id)]);
        m_alive[static_cast<size_t>(id)] = false;
    }
    m_removed_primary_cells.push_back(cell);
    // Mark the dug cell for the digging team (any team may mark it too).
    if (team >= 1 && team <= 32) m_dig_claims[cell].team_flags |= (1u << static_cast<unsigned>(team - 1));
    BroadcastWallRemovals(*m_world, ids);
    return true;
}

bool CWall::IsDugByTeam(int row, int col, int team) const
{
    if (team < 1 || team > 32 || !m_loaded || !InBounds(row, col)) return false;
    const auto it = m_dig_claims.find(CellIndex(row, col));
    return it != m_dig_claims.end() && (it->second.team_flags & (1u << static_cast<unsigned>(team - 1))) != 0;
}

bool CWall::IsDug(int row, int col) const
{
    if (!m_loaded || !InBounds(row, col)) return false;
    const auto it = m_dig_claims.find(CellIndex(row, col));
    return it != m_dig_claims.end() && it->second.team_flags != 0;
}

std::uint32_t CWall::DigFlags(int row, int col) const
{
    if (!InBounds(row, col)) return 0;
    const auto it = m_dig_claims.find(CellIndex(row, col));
    return it == m_dig_claims.end() ? 0 : it->second.team_flags;
}

std::vector<std::pair<int, int>> CWall::TeamFlaggedWallCells(int team) const
{
    std::vector<std::pair<int, int>> cells;
    if (!m_loaded || team < 1 || team > 32) return cells;
    const std::uint32_t bit = (1u << static_cast<unsigned>(team - 1));
    const int width = std::max(1, m_width);
    for (const auto& [cell, claim] : m_dig_claims)
    {
        if ((claim.team_flags & bit) == 0) continue;
        if (!HasAliveWallOnCell(cell)) continue;
        cells.emplace_back(static_cast<int>(cell / static_cast<std::uint32_t>(width)),
                           static_cast<int>(cell % static_cast<std::uint32_t>(width)));
    }
    return cells;
}

bool CWall::SetTeamFlag(int row, int col, int team)
{
    if (!m_loaded || !InBounds(row, col) || team < 1 || team > 32) return false;
    m_dig_claims[CellIndex(row, col)].team_flags |= (1u << static_cast<unsigned>(team - 1));
    return true;
}

bool CWall::ClearTeamFlag(int row, int col, int team)
{
    if (!m_loaded || !InBounds(row, col) || team < 1 || team > 32) return false;
    const std::uint32_t cell = CellIndex(row, col);
    const auto it = m_dig_claims.find(cell);
    if (it == m_dig_claims.end()) return false;
    it->second.team_flags &= ~(1u << static_cast<unsigned>(team - 1));
    if (it->second.team_flags == 0) m_dig_claims.erase(it);
    return true;
}

bool CWall::CommitPlace(int row, int col, std::uint32_t gid)
{
    (void)row;
    (void)col;
    (void)gid;
    return false;
}

void CWall::CaptureSnapshot(CSnapshotWriter& writer) const
{
    writer.Field("wall_width", static_cast<std::uint32_t>(m_width));
    writer.Field("wall_height", static_cast<std::uint32_t>(m_height));
    writer.Field("wall_tile_size", m_tile_size);
    writer.Field("wall_inserted_count", static_cast<std::uint32_t>(m_inserted_cells.size()));
    for (std::uint32_t i = 0; i < m_inserted_cells.size(); ++i)
        writer.Field("wall_inserted_cell_" + std::to_string(i), m_inserted_cells[i]);
    writer.Field("wall_removed_count", static_cast<std::uint32_t>(m_removed_primary_cells.size()));
    for (std::uint32_t i = 0; i < m_removed_primary_cells.size(); ++i)
        writer.Field("wall_removed_cell_" + std::to_string(i), m_removed_primary_cells[i]);
    writer.Field("wall_dig_claim_count", static_cast<std::uint32_t>(m_dig_claims.size()));
    std::uint32_t claim_index = 0;
    for (const auto& [cell, claim] : m_dig_claims)
    {
        writer.Field("wall_dig_claim_cell_" + std::to_string(claim_index), cell);
        writer.Field("wall_dig_claim_flags_" + std::to_string(claim_index), claim.team_flags);
        ++claim_index;
    }
}

bool CWall::RestoreSnapshot(const CSnapshotReader& reader, std::uint32_t version, std::string& error)
{
    if (version > 1)
    {
        error = "Unsupported wall board snapshot version " + std::to_string(version);
        return false;
    }

    m_width = static_cast<int>(reader.UInt32("wall_width", 0));
    m_height = static_cast<int>(reader.UInt32("wall_height", 0));
    m_tile_size = reader.Float("wall_tile_size", 512.f);
    m_inserted_cells.clear();
    const std::uint32_t inserted_count = reader.UInt32("wall_inserted_count", 0);
    for (std::uint32_t i = 0; i < inserted_count; ++i)
        m_inserted_cells.push_back(reader.UInt32("wall_inserted_cell_" + std::to_string(i), 0));
    m_removed_primary_cells.clear();
    const std::uint32_t count = reader.UInt32("wall_removed_count", 0);
    for (std::uint32_t i = 0; i < count; ++i)
        m_removed_primary_cells.push_back(reader.UInt32("wall_removed_cell_" + std::to_string(i), 0));
    m_dig_claims.clear();
    const std::uint32_t claim_count = reader.UInt32("wall_dig_claim_count", 0);
    for (std::uint32_t i = 0; i < claim_count; ++i)
    {
        const std::uint32_t cell = reader.UInt32("wall_dig_claim_cell_" + std::to_string(i), 0);
        const std::uint32_t flags = reader.UInt32("wall_dig_claim_flags_" + std::to_string(i), 0);
        if (flags != 0) m_dig_claims[cell].team_flags = flags;
    }
    m_loaded = false;
    return true;
}