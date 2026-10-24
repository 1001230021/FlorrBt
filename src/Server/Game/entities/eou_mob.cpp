#include "eou_mob.h"
#include "../wall_board.h"
#include "../gameworld.h"
#include "../gamecontrollers/ant_kingdom_controller.h"
#include <functional>
#include <limits>
#include <queue>
#include <utility>

CEouFoodMob* ResolveEouFoodTarget(CGameWorld* world, const SEntityHandle& handle)
{
    if (!world) return nullptr;
    return dynamic_cast<CEouFoodMob*>(world->ResolveEntity(handle));
}

namespace
{
constexpr int kDir8Row[8] = { 0, -1, 0, 1, -1, 1, 1, -1 };
constexpr int kDir8Col[8] = { -1, 0, 1, 0, -1, -1, 1, 1 };

// All cells along the Bresenham line from (r0,c0) to (r1,c1) - excluding the
// endpoints - must be walkable. Endpoints are handled by the caller's
// walkable() (start/goal cells are always passable).
bool LineClear(int r0, int c0, int r1, int c1, const std::function<bool(int, int)>& walkable)
{
    const int dr = std::abs(r1 - r0);
    const int dc = std::abs(c1 - c0);
    const int sr = r0 < r1 ? 1 : -1;
    const int sc = c0 < c1 ? 1 : -1;
    int err = (dc > dr ? dc : -dr) / 2;
    int r = r0;
    int c = c0;
    for (;;)
    {
        if (r == r1 && c == c1) break;
        const int old_r = r;
        const int old_c = c;
        const int e2 = err;
        if (e2 > -dc)
        {
            err -= dc;
            r += sr;
        }
        if (e2 < dr)
        {
            err += dr;
            c += sc;
        }
        if (r != old_r && c != old_c && (!walkable(r, old_c) || !walkable(old_r, c))) return false;
        if (r == r1 && c == c1) break;
        if (!walkable(r, c)) return false;
    }
    return true;
}
} // namespace

bool eou_has_line_of_sight(const CTileBoard& board, florrbt::Vector2f from, florrbt::Vector2f to)
{
    const float tile_size = board.TileSize();
    if (tile_size <= 0.f) return true;
    const int c0 = static_cast<int>(std::floor(from.x / tile_size));
    const int r0 = static_cast<int>(std::floor(from.y / tile_size));
    const int c1 = static_cast<int>(std::floor(to.x / tile_size));
    const int r1 = static_cast<int>(std::floor(to.y / tile_size));
    if (r0 == r1 && c0 == c1) return true;
    // The target wall cell is solid by definition: only the cells in between
    // may block the view (endpoints excluded).
    const auto walkable = [&board](int row, int col) {
        return board.InBounds(row, col) ? !board.TileSolid(row, col) : false;
    };
    // Traverse the actual world-space segment (not just the line between
    // cell centres). Crossing a corner requires both neighbouring cells.
    const float dx = to.x - from.x;
    const float dy = to.y - from.y;
    const int sx = dx > 0.f ? 1 : -1;
    const int sy = dy > 0.f ? 1 : -1;
    const float inf = std::numeric_limits<float>::infinity();
    const float step_x = dx != 0.f ? tile_size / std::abs(dx) : inf;
    const float step_y = dy != 0.f ? tile_size / std::abs(dy) : inf;
    float tx = dx != 0.f ? ((c0 + (sx > 0 ? 1 : 0)) * tile_size - from.x) / dx : inf;
    float ty = dy != 0.f ? ((r0 + (sy > 0 ? 1 : 0)) * tile_size - from.y) / dy : inf;
    int row = r0, col = c0;
    const int limit = std::abs(r1 - r0) + std::abs(c1 - c0) + 1;
    for (int i = 0; i < limit && (row != r1 || col != c1); ++i)
    {
        if (std::abs(tx - ty) < 0.000001f)
        {
            if (!walkable(row + sy, col) || !walkable(row, col + sx)) return false;
            row += sy; col += sx; tx += step_x; ty += step_y;
        }
        else if (tx < ty) { col += sx; tx += step_x; }
        else { row += sy; ty += step_y; }
        if (row == r1 && col == c1) return true;
        if (!walkable(row, col)) return false;
    }
    return row == r1 && col == c1;
}

std::optional<florrbt::Vector2f> CEouWorkerAnt::FindBestMarkedWall()
{
    const CWall* wall = dynamic_cast<const CWall*>(m_board);
    if (!wall || !wall->IsLoaded()) return std::nullopt;

    const int team = Team();
    const float vision = std::max(0.f, this->GetFinalStats()->horizon);
    const florrbt::Vector2f pos = this->Pos();

    florrbt::Vector2f best;
    float best_dist = std::numeric_limits<float>::max();
    bool found = false;
    for (const auto& [row, col] : wall->TeamFlaggedWallCells(team))
    {
        const florrbt::Vector2f center = wall->CellCenter(row, col);
        const float dist = EouDistance(pos, center);
        if (dist > vision || dist >= best_dist) continue;
        if (!eou_has_line_of_sight(*m_board, pos, center)) continue;
        // Only reachable targets: an unreachable wall would spin the worker
        // (SetTarget keeps failing and the reroute keeps retrying).
        if (eou_find_path(*m_board, pos, center).empty()) continue;
        best = center;
        best_dist = dist;
        found = true;
    }
    return found ? std::optional<florrbt::Vector2f>(best) : std::nullopt;
}

std::vector<florrbt::Vector2f> eou_find_path(const CTileBoard& board, florrbt::Vector2f start, florrbt::Vector2f goal)
{
    const int width = board.Width();
    const int height = board.Height();
    const float tile_size = board.TileSize();
    if (width <= 0 || height <= 0 || tile_size <= 0.f) return {};

    const int start_col = static_cast<int>(std::floor(start.x / tile_size));
    const int start_row = static_cast<int>(std::floor(start.y / tile_size));
    const int goal_col = static_cast<int>(std::floor(goal.x / tile_size));
    const int goal_row = static_cast<int>(std::floor(goal.y / tile_size));
    if (!board.InBounds(start_row, start_col) || !board.InBounds(goal_row, goal_col)) return {};
    // Most foraging routes stay within a cleared chamber. Avoid allocating
    // board-sized BFS buffers for these routine trips.
    if (eou_has_line_of_sight(board, start, goal)) return { start, goal };

    // The start and goal cells are always walkable even when marked solid
    // (e.g. digging a wall tile).
    const auto walkable = [&](int row, int col) {
        if (!board.InBounds(row, col)) return false;
        if ((row == start_row && col == start_col) || (row == goal_row && col == goal_col)) return true;
        return !board.TileSolid(row, col);
    };

    const int cell_count = width * height;
    const auto cell_index = [width](int row, int col) { return row * width + col; };

    std::vector<int> prev(static_cast<size_t>(cell_count), -1);
    std::queue<int> queue;
    const int start_cell = cell_index(start_row, start_col);
    const int goal_cell = cell_index(goal_row, goal_col);
    prev[static_cast<size_t>(start_cell)] = start_cell;
    queue.push(start_cell);

    while (!queue.empty())
    {
        const int current = queue.front();
        queue.pop();
        if (current == goal_cell) break;

        const int row = current / width;
        const int col = current % width;
        for (int i = 0; i < 8; ++i)
        {
            const int nrow = row + kDir8Row[i];
            const int ncol = col + kDir8Col[i];
            if (!board.InBounds(nrow, ncol)) continue;
            // No corner cutting through a diagonal gap.
            if (kDir8Row[i] != 0 && kDir8Col[i] != 0)
                if (!walkable(nrow, col) || !walkable(row, ncol)) continue;
            if (!walkable(nrow, ncol)) continue;

            const int next = cell_index(nrow, ncol);
            if (prev[static_cast<size_t>(next)] != -1) continue;
            prev[static_cast<size_t>(next)] = current;
            queue.push(next);
        }
    }

    if (prev[static_cast<size_t>(goal_cell)] == -1) return {};

    // Rebuild the cell chain (goal -> start).
    std::vector<std::pair<int, int>> cells;
    cells.reserve(64);
    for (int cur = goal_cell; cur != start_cell; cur = prev[static_cast<size_t>(cur)])
    {
        cells.emplace_back(cur / width, cur % width);
    }
    cells.emplace_back(start_row, start_col);
    std::reverse(cells.begin(), cells.end());

    // Greedy visibility compression on the shortest path: from each kept
    // waypoint, jump as far as possible to a later cell whose straight line
    // is fully walkable (Bresenham-checked). The result is a minimum waypoint
    // chain that is still guaranteed passable.
    std::vector<std::pair<int, int>> waypoints;
    waypoints.reserve(cells.size());
    waypoints.push_back(cells.front());
    size_t current = 0;
    while (current + 1 < cells.size())
    {
        size_t next = current;
        for (size_t j = cells.size() - 1; j > current; --j)
        {
            if (LineClear(cells[current].first, cells[current].second, cells[j].first, cells[j].second, walkable))
            {
                next = j;
                break;
            }
        }
        if (next == current) next = current + 1; // unreachable safety
        waypoints.push_back(cells[next]);
        current = next;
    }

    std::vector<florrbt::Vector2f> points;
    points.reserve(waypoints.size());
    for (const auto& [row, col] : waypoints)
    {
        points.emplace_back((static_cast<float>(col) + 0.5f) * tile_size,
                            (static_cast<float>(row) + 0.5f) * tile_size);
    }
    // Keep the last cell centre as a safe waypoint but finish at the actual
    // food/home coordinate, which need not be in the centre of its tile.
    points.push_back(goal);
    return points;
}


namespace
{
constexpr float kEouFoodSearchMaxRange = 320.f;
} // namespace

CEOUMob<SMobStats>::SFoodTarget CEOUMob<SMobStats>::FindEouFoodTarget(CGameWorld* world)
{
    SFoodTarget result;
    if (!world) return result;

    const float vision = std::max(0.f, this->GetFinalStats()->horizon);
    const float max_range = std::max(vision, kEouFoodSearchMaxRange);

    std::vector<std::pair<float, const CEouFoodMob*>> candidates;

    world->GetSpatialGrid().ForEachInRange(this->Pos(), max_range, [&](const CEntity* entity) {
        const auto* food = dynamic_cast<const CEouFoodMob*>(entity);
        if (!food || food->IsMarkedForDes() || food->IsDead() || food->RemainingFood() <= 0.f) return;
        const float dist = EouDistance(this->Pos(), food->Pos());
        if (dist > max_range) return;
        candidates.emplace_back(dist, food);
    });
    std::sort(candidates.begin(), candidates.end(), [](const auto& a, const auto& b) {
        return a.first != b.first ? a.first < b.first : a.second->Id() < b.second->Id();
    });
    // Bound failed path searches. A corpse behind a sealed wall must not
    // monopolise the worker or trigger a pathfind every frame.
    const size_t attempts = std::min<size_t>(candidates.size(), 4);
    if (candidates.empty()) { m_food_search_cursor = 0; return result; }
    const size_t first = m_food_search_cursor % candidates.size();
    for (size_t i = 0; i < attempts; ++i)
    {
        const auto* food = candidates[(first + i) % candidates.size()].second;
        if (!m_board || eou_find_path(*m_board, this->Pos(), food->Pos()).empty()) continue;
        result.world = world;
        result.ref = food->GetHandle();
        result.pos = food->Pos();
        m_food_search_cursor = 0;
        return result;
    }
    m_food_search_cursor = (first + attempts) % candidates.size();
    return result;
}

void CEouSoldierAnt::Tick(float dt)
{
    m_combat_scan_timer -= dt;
    CEntity* target = ResolveCombatTarget();
    if (m_combat_scan_timer <= 0.f)
    {
        m_combat_scan_timer = std::max(0.05f, game_config::eou_soldier_scan_interval);
        if (!target)
        {
            ScanForEnemy();
            target = ResolveCombatTarget();
        }
        // Blocked view: path through the tunnels on the scan cadence (the
        // direct chase below takes over as soon as the line of sight clears).
        if (target && m_board && !eou_has_line_of_sight(*m_board, Pos(), target->Pos()))
            SetTarget(target->Pos());
    }

    if (target)
    {
        m_engaged = true;
        m_target_pos = target->Pos();
        if (!m_board || eou_has_line_of_sight(*m_board, Pos(), m_target_pos))
        {
            // Direct chase: bypass the waypoint walk; contact damage comes
            // from the normal collision rules once the bodies touch.
            ClearEouWaypoints();
            CMob<SMobStats>::Tick(dt);
            MoveTowards(m_target_pos, dt);
            return;
        }
        // Path-following chase (waypoints refreshed on the scan cadence).
        CEOUMob<SMobStats>::Tick(dt);
        return;
    }

    m_engaged = false;
    CEOUMob<SMobStats>::Tick(dt);
}

CEntity* CEouSoldierAnt::ResolveCombatTarget()
{
    if (!m_target_ref.IsValid()) return nullptr;
    CGameWorld* world = this->GameWorld();
    if (!world)
    {
        m_target_ref = {};
        return nullptr;
    }
    CEntity* entity = world->ResolveEntity(m_target_ref);
    auto* mob = dynamic_cast<CMobBase*>(entity);
    const float leash =
        std::max(game_config::eou_soldier_leash_range, game_config::eou_soldier_aggro_range);
    if (!mob || mob->IsMarkedForDes() || mob->IsDead() || EouDistance(Pos(), mob->Pos()) > leash)
    {
        m_target_ref = {};
        return nullptr;
    }
    return entity;
}

void CEouSoldierAnt::ScanForEnemy()
{
    CGameWorld* world = this->GameWorld();
    if (!world) return;
    const int my_team = Team();
    if (my_team <= 0) return;
    const float aggro = std::max(0.f, game_config::eou_soldier_aggro_range);
    if (aggro <= 0.f) return;

    CEntity* best = nullptr;
    float best_dist = std::numeric_limits<float>::max();
    world->GetSpatialGrid().ForEachInRange(Pos(), aggro, [&](const CEntity* entity) {
        if (entity == this) return;
        // Enemy bodies only: mobs and flowers share CMobBase; petals and
        // projectiles do not. Team 0 is neutral (food, wild props).
        const auto* mob = dynamic_cast<const CMobBase*>(entity);
        if (!mob || mob->IsMarkedForDes() || mob->IsDead()) return;
        const int team = mob->Team();
        if (team <= 0 || team == my_team) return;
        const float dist = EouDistance(Pos(), mob->Pos());
        if (dist > aggro || dist >= best_dist) return;
        best_dist = dist;
        best = const_cast<CEntity*>(entity);
    });

    if (!best) return;
    m_target_ref = best->GetHandle();
}

float CEouWorkerAnt::DepositFood(float amount)
{
    if (amount <= 0.f) return 0.f;
    CGameWorld* world = this->GameWorld();
    if (!world) return 0.f;
    auto* controller = dynamic_cast<CAntKingdomController*>(world->GetController());
    return controller ? controller->DepositWorkerFood(*world, Team(), Pos(), amount) : 0.f;
}

void CEouWorkerAnt::TickDig(float dt)
{
    if (!m_goal || !m_board) return;
    const float tile_size = m_board->TileSize();
    if (tile_size <= 0.f) return;
    // The wall cell itself is solid, so the ant is stopped at its edge; "in
    // range" therefore means roughly touching the wall, not standing on it.
    const float dig_range = tile_size * std::max(0.1f, game_config::eou_worker_dig_range_tiles);
    if (EouDistance(Pos(), *m_goal) > dig_range) return;

    // Digging: hold position and chip away.
    m_vel *= std::max(0.f, 1.f - 4.f * dt);
    m_dig_progress += dt;
    if (m_dig_progress < std::max(0.1f, game_config::eou_worker_dig_time)) return;

    const florrbt::Vector2f goal = *m_goal;
    m_goal.reset();
    m_dig_progress = 0.f;
    m_scan_timer = 0.f; // rescan right away: next flag, or fall back to foraging

    CGameWorld* world = this->GameWorld();
    auto* controller = world ? dynamic_cast<CAntKingdomController*>(world->GetController()) : nullptr;
    if (controller) controller->WorkerDigWall(*world, Team(), goal);
}
