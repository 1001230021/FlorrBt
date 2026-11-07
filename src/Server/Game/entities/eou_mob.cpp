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

// Bresenham 线段途经格必须可通行，起终点由调用方单独判定。
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
}

bool eou_has_line_of_sight(const CTileBoard& board, florrbt::Vector2f from, florrbt::Vector2f to)
{
    const float tile_size = board.TileSize();
    if (tile_size <= 0.f) return true;
    const int c0 = static_cast<int>(std::floor(from.x / tile_size));
    const int r0 = static_cast<int>(std::floor(from.y / tile_size));
    const int c1 = static_cast<int>(std::floor(to.x / tile_size));
    const int r1 = static_cast<int>(std::floor(to.y / tile_size));
    if (r0 == r1 && c0 == c1) return true;
    // 挖掘终点本身就是墙，视线仅检查中间格。
    const auto walkable = [&board](int row, int col) {
        return board.InBounds(row, col) ? !board.TileSolid(row, col) : false;
    };
    // 按实际世界坐标线段穿格，过角时要求两侧相邻格都可通行。
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
        // 仅选择可达目标，避免工蚁围绕不可达墙体反复重规划。
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
    // 已清空房间内优先直达，避免为短途采集分配整张地图的 BFS 缓冲。
    if (eou_has_line_of_sight(board, start, goal)) return { start, goal };

    // 起终点允许位于墙格内，例如挖掘墙体的目标格。
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
            // 禁止从对角狭缝穿角。
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

    std::vector<std::pair<int, int>> cells;
    cells.reserve(64);
    for (int cur = goal_cell; cur != start_cell; cur = prev[static_cast<size_t>(cur)])
    {
        cells.emplace_back(cur / width, cur % width);
    }
    cells.emplace_back(start_row, start_col);
    std::reverse(cells.begin(), cells.end());

    // 对最短路径贪心跳过可直达的中间点，压缩路径点数量并保留可通行性。
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
        if (next == current) next = current + 1; // 保证路径索引向前推进。
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
    // 保留末格中心作为安全路径点，最终落点使用食物或巢穴的实际坐标。
    points.push_back(goal);
    return points;
}


namespace
{
constexpr float kEouFoodSearchMaxRange = 320.f;
}

template <>
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
    // 限制失败寻路频率，避免封墙后的食物使工蚁每帧重复计算。
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

void CEouSoldierAnt::OnTickIntent(float dt)
{
    if (IsPlayerControlled()) { CMob<SMobStats>::OnTickIntent(dt); return; }
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
        // 隔墙时按扫描周期沿通道寻路，视线恢复后转为直接追击。
        if (target && m_board && !eou_has_line_of_sight(*m_board, Pos(), target->Pos()))
            SetTarget(target->Pos());
    }

    if (target)
    {
        m_engaged = true;
        m_target_pos = target->Pos();
        if (!m_board || eou_has_line_of_sight(*m_board, Pos(), m_target_pos))
        {
            // 直接追击绕过路径点，接触伤害仍由正常碰撞规则处理。
            ClearEouWaypoints();
            CMob<SMobStats>::OnTickIntent(dt);
            MoveTowards(m_target_pos, dt);
            return;
        }
        CEOUMob<SMobStats>::OnTickIntent(dt);
        return;
    }

    m_engaged = false;
    CEOUMob<SMobStats>::OnTickIntent(dt);
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
        // 仅追击敌方生物或花朵，不选花瓣、投射物和 team 0 的中立实体。
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
    // 墙格会挡住工蚁，因此挖掘范围按接近墙边判定，不能要求站到墙内。
    const float dig_range = tile_size * std::max(0.1f, game_config::eou_worker_dig_range_tiles);
    if (EouDistance(Pos(), *m_goal) > dig_range) return;

    m_vel *= std::max(0.f, 1.f - 4.f * dt);
    m_dig_progress += dt;
    if (m_dig_progress < std::max(0.1f, game_config::eou_worker_dig_time)) return;

    const florrbt::Vector2f goal = *m_goal;
    m_goal.reset();
    m_dig_progress = 0.f;
    m_scan_timer = 0.f; // 立即扫描下一个标记，无标记时恢复觅食。

    CGameWorld* world = this->GameWorld();
    auto* controller = world ? dynamic_cast<CAntKingdomController*>(world->GetController()) : nullptr;
    if (controller) controller->WorkerDigWall(*world, Team(), goal);
}
