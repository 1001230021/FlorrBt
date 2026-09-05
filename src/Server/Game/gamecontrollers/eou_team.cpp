#include "eou_team.h"
#include "../../../Shared/game_config.h"
#include "../../../Shared/tools.h"
#include "../../HotReload/snapshot_archive.h"
#include "../entities/eou_mob.h"
#include "../gameworld.h"
#include <algorithm>
#include <string>

void CEouPheromoneGroup::SetPheromoneTarget(florrbt::Vector2f target)
{
    m_pheromone_target = target;
    m_unupdated.clear();
    m_unupdated.reserve(m_members.size());
    for (std::size_t i = 0; i < m_members.size(); ++i) m_unupdated.push_back(i);
}

bool CEouPheromoneGroup::AddMember(CEntity* entity)
{
    auto* eou = entity ? dynamic_cast<IEouMob*>(entity) : nullptr;
    if (!eou) return false;
    for (const SEouEntityHandle& handle : m_members)
        if (handle.id == entity->Id() && handle.generation == entity->Generation()) return false;
    eou->SetBoard(&m_board);
    eou->SetEouGroupId(m_group_id);
    m_members.push_back({ entity->Id(), entity->Generation() });
    m_unupdated.push_back(m_members.size() - 1);
    return true;
}

bool CEouPheromoneGroup::RemoveMember(int id, std::uint64_t generation)
{
    const auto it = std::find_if(m_members.begin(), m_members.end(),
                                 [id, generation](const SEouEntityHandle& handle) {
                                     return handle.id == id && handle.generation == generation;
                                 });
    if (it == m_members.end()) return false;
    m_members.erase(it);
    RebuildUnupdated();
    return true;
}

void CEouPheromoneGroup::RebuildUnupdated()
{
    m_unupdated.clear();
    m_unupdated.reserve(m_members.size());
    for (std::size_t i = 0; i < m_members.size(); ++i) m_unupdated.push_back(i);
}

void CEouPheromoneGroup::OnTick(CGameWorld& world, float dt)
{
    // Prune dead, removed or stale members.
    bool pruned = false;
    for (std::size_t i = 0; i < m_members.size();)
    {
        const SEouEntityHandle& handle = m_members[i];
        CEntity* entity = world.GetEntity(handle.id, handle.generation);
        auto* eou = entity ? dynamic_cast<IEouMob*>(entity) : nullptr;
        if (!eou || entity->IsMarkedForDes() || entity->IsDead())
        {
            m_members[i] = m_members.back();
            m_members.pop_back();
            pruned = true;
            continue;
        }
        eou->SetBoard(&m_board);
        ++i;
    }
    if (pruned) RebuildUnupdated();

    // Time-sliced reroute: pending members are processed at a rate that lets
    // a full group finish rerouting within eou_group_reroute_window seconds
    // regardless of size, capped per tick to bound the pathfinding cost.
    if (!m_unupdated.empty())
    {
        constexpr int kMaxReroutesPerTick = 8;
        const float window = std::max(0.1f, game_config::eou_group_reroute_window);
        m_reroute_budget += static_cast<float>(m_members.size()) * dt / window;
        int reroutes = static_cast<int>(m_reroute_budget);
        m_reroute_budget -= static_cast<float>(reroutes);
        reroutes = std::min(reroutes, std::min(static_cast<int>(m_unupdated.size()), kMaxReroutesPerTick));
        for (int i = 0; i < reroutes; ++i)
        {
            const std::size_t idx = m_unupdated.back();
            m_unupdated.pop_back();
            if (idx >= m_members.size()) continue;
            if (CEntity* entity = world.GetEntity(m_members[idx].id, m_members[idx].generation))
                if (auto* eou = dynamic_cast<IEouMob*>(entity))
                    eou->SetTarget(eou->EouResolveGoal(m_pheromone_target));
        }
    }
    else
    {
        m_reroute_budget = 0.f;
    }

    // Member movement runs inside each mob's own Tick (world-driven).
}

void CEouPheromoneGroup::CaptureSnapshot(CSnapshotWriter& writer) const
{
    writer.Field("eou_group_team_id", m_team_id);
    writer.Field("eou_group_id", m_group_id);
    writer.Field("eou_group_persistent", m_persistent);
    writer.Field("eou_group_target_x", m_pheromone_target.x);
    writer.Field("eou_group_target_y", m_pheromone_target.y);
    writer.Field("eou_group_member_count", static_cast<std::uint32_t>(m_members.size()));
    for (std::uint32_t i = 0; i < m_members.size(); ++i)
    {
        writer.Field("eou_group_member_id_" + std::to_string(i), m_members[i].id);
        writer.UInt64("eou_group_member_gen_" + std::to_string(i), m_members[i].generation);
    }
}

bool CEouPheromoneGroup::RestoreSnapshot(const CSnapshotReader& reader, std::uint32_t version,
                                         std::string& error)
{
    if (version > 1)
    {
        error = "Unsupported eou pheromone group snapshot version " + std::to_string(version);
        return false;
    }
    m_team_id = reader.Int("eou_group_team_id", m_team_id);
    m_group_id = reader.Int("eou_group_id", m_group_id);
    m_persistent = reader.Bool("eou_group_persistent", false);
    m_pheromone_target.x = reader.Float("eou_group_target_x", 0.f);
    m_pheromone_target.y = reader.Float("eou_group_target_y", 0.f);
    m_members.clear();
    const std::uint32_t count = reader.UInt32("eou_group_member_count", 0);
    for (std::uint32_t i = 0; i < count; ++i)
    {
        const int id = reader.Int("eou_group_member_id_" + std::to_string(i), 0);
        const std::uint64_t generation = reader.UInt64("eou_group_member_gen_" + std::to_string(i), 0);
        if (id > 0) m_members.push_back({ id, generation });
    }
    RebuildUnupdated();
    return true;
}

CEouPheromoneGroup* CTeam::CreateGroup(florrbt::Vector2f target)
{
    m_groups.push_back(std::make_unique<CEouPheromoneGroup>(m_team_id, m_next_group_id, target, m_board));
    CEouPheromoneGroup* group = m_groups.back().get();
    ++m_next_group_id;
    return group;
}

CEouPheromoneGroup* CTeam::EnsureGroup(int group_id, florrbt::Vector2f target)
{
    if (CEouPheromoneGroup* existing = FindGroup(group_id)) return existing;
    m_groups.push_back(std::make_unique<CEouPheromoneGroup>(m_team_id, group_id, target, m_board));
    m_groups.back()->SetPersistent(true);
    m_next_group_id = std::max(m_next_group_id, group_id + 1);
    return m_groups.back().get();
}

bool CTeam::RemoveGroup(int group_id)
{
    const auto it = std::find_if(m_groups.begin(), m_groups.end(),
                                 [group_id](const std::unique_ptr<CEouPheromoneGroup>& group) {
                                     return group->GroupId() == group_id;
                                 });
    if (it == m_groups.end()) return false;
    m_groups.erase(it);
    return true;
}

CEouPheromoneGroup* CTeam::FindGroup(int group_id)
{
    for (const auto& group : m_groups)
        if (group->GroupId() == group_id) return group.get();
    return nullptr;
}

const CEouPheromoneGroup* CTeam::FindGroup(int group_id) const
{
    for (const auto& group : m_groups)
        if (group->GroupId() == group_id) return group.get();
    return nullptr;
}

std::size_t CTeam::MemberCount() const
{
    std::size_t count = 0;
    for (const auto& group : m_groups) count += group->MemberCount();
    return count;
}

void CTeam::OnTick(CGameWorld& world, float dt)
{
    for (std::size_t i = 0; i < m_groups.size();)
    {
        m_groups[i]->OnTick(world, dt);
        if (m_groups[i]->MemberCount() == 0 && !m_groups[i]->Persistent() && m_groups.size() > 1)
        {
            // Keep at least one group per team (the primary group); empty
            // transient groups are pruned. Persistent (slot-bound pheromone)
            // groups always survive.
            m_groups.erase(m_groups.begin() + static_cast<std::ptrdiff_t>(i));
            continue;
        }
        ++i;
    }
}

void CTeam::Dissolve(CGameWorld& world)
{
    for (const auto& group : m_groups)
    {
        for (const SEouEntityHandle& handle : group->Members())
        {
            if (CEntity* entity = world.GetEntity(handle.id, handle.generation))
                entity->MarkForDestroy();
        }
    }
    m_groups.clear();
    if (CEntity* queen = world.GetEntity(m_queen.id, m_queen.generation)) queen->MarkForDestroy();
    m_queen = {};
}

void CTeam::CaptureSnapshot(CSnapshotWriter& writer) const
{
    writer.Field("eou_team_id", m_team_id);
    writer.Field("eou_team_next_group_id", m_next_group_id);
    writer.Field("eou_team_home_x", m_home_pos.x);
    writer.Field("eou_team_home_y", m_home_pos.y);
    writer.Field("eou_team_has_home", m_has_home);
    writer.Field("eou_team_food_stash", m_food_stash);
    writer.Field("eou_team_queen_id", m_queen.id);
    writer.UInt64("eou_team_queen_gen", m_queen.generation);
    writer.Field("eou_team_group_count", static_cast<std::uint32_t>(m_groups.size()));
    for (std::uint32_t i = 0; i < m_groups.size(); ++i)
    {
        CJsonOwner group_object = MakeJsonObject();
        CSnapshotWriter group_writer(group_object.get());
        m_groups[i]->CaptureSnapshot(group_writer);
        writer.Node("eou_team_group_" + std::to_string(i), group_object.release());
    }
}

bool CTeam::RestoreSnapshot(const CSnapshotReader& reader, std::uint32_t version, std::string& error)
{
    if (version > 1)
    {
        error = "Unsupported eou team snapshot version " + std::to_string(version);
        return false;
    }
    m_team_id = reader.Int("eou_team_id", m_team_id);
    m_next_group_id = std::max(1, reader.Int("eou_team_next_group_id", 1));
    m_home_pos = { reader.Float("eou_team_home_x", m_home_pos.x), reader.Float("eou_team_home_y", m_home_pos.y) };
    m_has_home = reader.Bool("eou_team_has_home", m_has_home);
    m_food_stash = std::max(0.f, reader.Float("eou_team_food_stash", m_food_stash));
    m_queen.id = reader.Int("eou_team_queen_id", 0);
    m_queen.generation = reader.UInt64("eou_team_queen_gen", 0);
    m_groups.clear();
    const std::uint32_t count = reader.UInt32("eou_team_group_count", 0);
    for (std::uint32_t i = 0; i < count; ++i)
    {
        const json_t* node = reader.Node("eou_team_group_" + std::to_string(i));
        if (!node || !json_is_object(node)) continue;
        auto group = std::make_unique<CEouPheromoneGroup>(m_team_id, 0, florrbt::Vector2f{}, m_board);
        CSnapshotReader group_reader(node);
        if (!group->RestoreSnapshot(group_reader, version, error)) return false;
        m_next_group_id = std::max(m_next_group_id, group->GroupId() + 1);
        m_groups.push_back(std::move(group));
    }
    return true;
}


// ============ CCellGroupSet ============

std::pair<int, int> CCellGroupSet::CellKeyOf(CGameWorld& world, SEouEntityHandle handle) const
{
    CEntity* entity = world.GetEntity(handle.id, handle.generation);
    if (!entity) return { -1, -1 };
    const float tile = m_tile_size > 0.f ? m_tile_size : 32.f;
    return { static_cast<int>(std::floor(entity->Pos().y / tile)), static_cast<int>(std::floor(entity->Pos().x / tile)) };
}

std::vector<SEouEntityHandle> CCellGroupSet::NeighboursOf(CGameWorld& world, SEouEntityHandle handle,
                                                          int cell_type) const
{
    std::vector<SEouEntityHandle> result;
    const auto [row, col] = CellKeyOf(world, handle);
    if (row < 0 || col < 0) return result;

    for (const SEouEntityHandle& candidate : m_cells)
    {
        if (candidate.id == handle.id && candidate.generation == handle.generation) continue;
        CEntity* entity = world.GetEntity(candidate.id, candidate.generation);
        auto* cell = dynamic_cast<CEouCellMob*>(entity);
        if (!cell || static_cast<int>(cell->CellType()) != cell_type) continue;
        const auto [c_row, c_col] = CellKeyOf(world, candidate);
        if (c_row < 0 || c_col < 0) continue;
        const bool adjacent = (row == c_row && std::abs(col - c_col) == 1) ||
                              (col == c_col && std::abs(row - c_row) == 1);
        if (adjacent) result.push_back(candidate);
    }
    return result;
}

std::vector<SEouEntityHandle> CCellGroupSet::AllCells() const
{
    std::vector<SEouEntityHandle> result;
    result.reserve(m_cells.size());
    for (const SEouEntityHandle& handle : m_cells) result.push_back(handle);
    return result;
}

void CCellGroupSet::RebuildGroups(CGameWorld& world)
{
    // Rebuild the union partition from scratch (cell counts stay small):
    // same-type, 4-neighbour connected components become one group each.
    m_groups.clear();
    m_next_group_id = 1;

    std::vector<SEouEntityHandle> remaining = m_cells;
    while (!remaining.empty())
    {
        const SEouEntityHandle seed = remaining.back();
        remaining.pop_back();

        CEntity* seed_entity = world.GetEntity(seed.id, seed.generation);
        auto* seed_cell = dynamic_cast<CEouCellMob*>(seed_entity);
        if (!seed_cell)
        {
            continue;
        }
        const int type = static_cast<int>(seed_cell->CellType());

        // BFS over same-type 4-neighbours.
        CCellGroup group(m_next_group_id);
        group.AddMember(seed);
        seed_cell->SetGroupId(m_next_group_id);

        std::vector<SEouEntityHandle> frontier;
        frontier.push_back(seed);
        while (!frontier.empty())
        {
            const SEouEntityHandle current = frontier.back();
            frontier.pop_back();
            std::vector<SEouEntityHandle> neighbours = NeighboursOf(world, current, type);
            for (SEouEntityHandle& neighbour : neighbours)
            {
                CEntity* entity = world.GetEntity(neighbour.id, neighbour.generation);
                auto* cell = dynamic_cast<CEouCellMob*>(entity);
                if (!cell) continue;
                if (cell->GroupId() == m_next_group_id) continue;
                cell->SetGroupId(m_next_group_id);
                group.AddMember(neighbour);
                frontier.push_back(neighbour);
                remaining.erase(std::remove_if(remaining.begin(), remaining.end(),
                                               [&neighbour](const SEouEntityHandle& handle) {
                                                   return handle.id == neighbour.id &&
                                                          handle.generation == neighbour.generation;
                                               }),
                                remaining.end());
            }
        }

        m_groups.push_back(std::move(group));
        ++m_next_group_id;
    }
}

int CCellGroupSet::AddCell(CGameWorld& world, SEouEntityHandle handle, int cell_type)
{
    (void)cell_type;
    m_cells.push_back(handle);
    RebuildGroups(world);
    for (const CCellGroup& group : m_groups)
    {
        for (const SEouEntityHandle& member : group.Members())
        {
            if (member.id == handle.id && member.generation == handle.generation) return group.GroupId();
        }
    }
    return 0;
}

void CCellGroupSet::RemoveCell(SEouEntityHandle handle)
{
    m_cells.erase(std::remove_if(m_cells.begin(), m_cells.end(),
                                 [&handle](const SEouEntityHandle& candidate) {
                                     return candidate.id == handle.id && candidate.generation == handle.generation;
                                 }),
                  m_cells.end());
    // Groups are rebuilt lazily by the controller on the next add; the
    // removed cell is simply gone from the set (stale handles resolve null
    // during the rebuild).
}

void CCellGroupSet::CaptureSnapshot(CSnapshotWriter& writer) const
{
    writer.Field("cell_group_count", static_cast<std::uint32_t>(m_cells.size()));
    for (std::uint32_t i = 0; i < m_cells.size(); ++i)
    {
        writer.Field("cell_group_member_" + std::to_string(i) + "_id", m_cells[i].id);
        writer.Field("cell_group_member_" + std::to_string(i) + "_gen", static_cast<int>(m_cells[i].generation));
    }
}

bool CCellGroupSet::RestoreSnapshot(const CSnapshotReader& reader, std::uint32_t, std::string&)
{
    m_cells.clear();
    const std::uint32_t count = reader.UInt32("cell_group_count", 0);
    for (std::uint32_t i = 0; i < count; ++i)
    {
        SEouEntityHandle handle;
        handle.id = reader.Int("cell_group_member_" + std::to_string(i) + "_id", -1);
        handle.generation = reader.UInt64("cell_group_member_" + std::to_string(i) + "_gen", 0);
        if (handle.id >= 0) m_cells.push_back(handle);
    }
    m_groups.clear();
    return true;
}

// ============ CTeam cells/food ============

void CTeam::AddCell(CGameWorld& world, CEouCellMob& cell)
{
    cell.SetTeamId(m_team_id);
    SEouEntityHandle handle{ cell.Id(), cell.Generation() };
    const int group_id = m_cells.AddCell(world, handle, static_cast<int>(cell.CellType()));
    cell.SetGroupId(group_id);
}

void CTeam::RemoveCell(CGameWorld& world, CEouCellMob& cell)
{
    m_cells.RemoveCell({ cell.Id(), cell.Generation() });
    cell.SetGroupId(0);
    (void)world;
}



float CTeam::StorageFoodTotal(CGameWorld& world) const
{
    float total = 0.f;
    for (const SEouEntityHandle& handle : m_cells.AllCells())
    {
        CEntity* entity = world.GetEntity(handle.id, handle.generation);
        auto* cell = dynamic_cast<CEouCellMob*>(entity);
        if (!cell || cell->CellType() != ECouCellType::Storage) continue;
        total += cell->StoredFood();
    }
    return total + m_food_stash;
}

bool CTeam::TryConsumeFood(CGameWorld& world, float amount)
{
    if (amount <= 0.f) return true;
    float remaining = amount;
    // Drain the stash first, then the storage cells.
    if (m_food_stash > 0.f)
    {
        const float used = std::min(m_food_stash, remaining);
        m_food_stash -= used;
        remaining -= used;
        if (remaining <= 0.f) return true;
    }
    for (const SEouEntityHandle& handle : m_cells.AllCells())
    {
        if (remaining <= 0.f) break;
        CEntity* entity = world.GetEntity(handle.id, handle.generation);
        auto* cell = dynamic_cast<CEouCellMob*>(entity);
        if (!cell || cell->CellType() != ECouCellType::Storage) continue;
        remaining -= cell->TakeFood(remaining);
    }
    return remaining <= 0.f;
}

float CTeam::DepositToNearestStorage(CGameWorld& world, florrbt::Vector2f pos, float amount)
{
    if (amount <= 0.f) return 0.f;
    CEouCellMob* best = nullptr;
    float best_dist_sq = std::numeric_limits<float>::max();
    for (const SEouEntityHandle& handle : m_cells.AllCells())
    {
        CEntity* entity = world.GetEntity(handle.id, handle.generation);
        auto* cell = dynamic_cast<CEouCellMob*>(entity);
        if (!cell || cell->CellType() != ECouCellType::Storage) continue;
        const float dist_sq = EouDistance(cell->Pos(), pos);
        if (dist_sq < best_dist_sq)
        {
            best_dist_sq = dist_sq;
            best = cell;
        }
    }
    if (!best) return 0.f; // no storage cell: the food is dropped
    return best->StoreFood(amount);
}

CEouCellMob* CTeam::GetCellAt(CGameWorld& world, florrbt::Vector2f pos)
{
    const float tile = m_cells.TileSize() > 0.f ? m_cells.TileSize() : 32.f;
    const int row = static_cast<int>(std::floor(pos.y / tile));
    const int col = static_cast<int>(std::floor(pos.x / tile));
    for (const SEouEntityHandle& handle : m_cells.AllCells())
    {
        CEntity* entity = world.GetEntity(handle.id, handle.generation);
        auto* cell = dynamic_cast<CEouCellMob*>(entity);
        if (!cell) continue;
        const int cell_row = static_cast<int>(std::floor(cell->Pos().y / tile));
        const int cell_col = static_cast<int>(std::floor(cell->Pos().x / tile));
        if (cell_row == row && cell_col == col) return cell;
    }
    return nullptr;
}

const CEouCellMob* CTeam::GetCellAt(CGameWorld& world, florrbt::Vector2f pos) const
{
    return const_cast<CTeam*>(this)->GetCellAt(world, pos);
}
