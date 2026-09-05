#pragma once
#include "../entities/eou_mob.h"
#include <SFML/System/Vector2.hpp>
#include <cstdint>
#include <memory>
#include <vector>

class CGameWorld;
class CSnapshotReader;
class CSnapshotWriter;
class CEouCellMob;

// Stable entity handle: {id, generation} resolves through
// world.GetEntity(id, generation), so stale handles (dead or reused ids)
// safely fail instead of dangling.
struct SEouEntityHandle
{
    int id = 0;
    std::uint64_t generation = 0;
};

// One connected block of same-type cells (a "union-find group": merging two
// groups collapses them into one block). Cells are adjacent on the 4-neighbour
// grid (not diagonals, and only same-type cells are united).
class CCellGroup
{
  public:
    explicit CCellGroup(int group_id) : m_group_id(group_id) {}

    int GroupId() const { return m_group_id; }
    std::vector<SEouEntityHandle>& Members() { return m_members; }
    const std::vector<SEouEntityHandle>& Members() const { return m_members; }
    std::size_t MemberCount() const { return m_members.size(); }

    void AddMember(SEouEntityHandle handle) { m_members.push_back(handle); }

  private:
    int m_group_id;
    std::vector<SEouEntityHandle> m_members;
};

// The team's cell set: a vector of union groups. Adding/removing a cell
// rebuilds the connectivity (4-neighbour, same-type) so groups stay valid.
class CCellGroupSet
{
  public:
    void SetTileSize(float tile_size) { m_tile_size = tile_size; }
    float TileSize() const { return m_tile_size; }

    // Registers a cell handle and rebuilds the group partition. Returns the
    // new cell's group id (0 when the cell does not belong).
    int AddCell(CGameWorld& world, SEouEntityHandle handle, int cell_type);
    // Deregisters a cell (it was destroyed) and rebuilds the groups.
    void RemoveCell(SEouEntityHandle handle);
    // Finds the immediate neighbours of this cell (same type, 4-dir grid).
    std::vector<SEouEntityHandle> NeighboursOf(CGameWorld& world, SEouEntityHandle handle, int cell_type) const;
    // All cells of all groups.
    std::vector<SEouEntityHandle> AllCells() const;

    const std::vector<CCellGroup>& Groups() const { return m_groups; }

    void CaptureSnapshot(CSnapshotWriter& writer) const;
    bool RestoreSnapshot(const CSnapshotReader& reader, std::uint32_t version, std::string& error);

  private:
    std::pair<int, int> CellKeyOf(CGameWorld& world, SEouEntityHandle handle) const;
    void RebuildGroups(CGameWorld& world);

    float m_tile_size = 32.f;
    int m_next_group_id = 1;
    std::vector<SEouEntityHandle> m_cells;
    std::vector<CCellGroup> m_groups;
};

// Pheromone group: a shared pheromone target and the mobs assigned to it.
// The group decides; the mobs own their own movement. Reroutes after a
// pheromone change are time-sliced (the m_unupdated list drains so a full
// group finishes within eou_group_reroute_window seconds, capped per tick)
// because SetTarget runs a BFS pathfind. The board pointer is re-injected
// every tick so hot-reload restores stay wired.
class CEouPheromoneGroup
{
  public:
    CEouPheromoneGroup(int team_id, int group_id, florrbt::Vector2f pheromone_target,
                       const CTileBoard& board)
        : m_team_id(team_id), m_group_id(group_id), m_pheromone_target(pheromone_target),
          m_board(board)
    {
    }

    int TeamId() const { return m_team_id; }
    int GroupId() const { return m_group_id; }
    const florrbt::Vector2f& PheromoneTarget() const { return m_pheromone_target; }
    // Persistent groups (the slot-bound pheromone groups) survive the empty
    // group pruning in CTeam::OnTick.
    void SetPersistent(bool persistent) { m_persistent = persistent; }
    bool Persistent() const { return m_persistent; }

    // Marks every member as pending so the time-sliced reroute walks them
    // over to the new pheromone target.
    void SetPheromoneTarget(florrbt::Vector2f target);

    bool AddMember(CEntity* entity);
    bool RemoveMember(int id, std::uint64_t generation);
    std::size_t MemberCount() const { return m_members.size(); }
    const std::vector<SEouEntityHandle>& Members() const { return m_members; }

    // Prunes dead/stale members, drains the time-sliced reroute queue and
    // re-injects the board.
    void OnTick(CGameWorld& world, float dt);

    void CaptureSnapshot(CSnapshotWriter& writer) const;
    bool RestoreSnapshot(const CSnapshotReader& reader, std::uint32_t version, std::string& error);

  private:
    void RebuildUnupdated();

    int m_team_id;
    int m_group_id;
    florrbt::Vector2f m_pheromone_target;
    const CTileBoard& m_board;
    bool m_persistent = false;
    std::vector<SEouEntityHandle> m_members;
    std::vector<std::size_t> m_unupdated; // pending member indices (space-for-time)
    float m_reroute_budget = 0.f;         // fractional reroutes carried between ticks
};

// Team: owns the pheromone groups (and therefore decides the mobs' goals).
// The controller owns the teams.
class CTeam
{
  public:
    explicit CTeam(int team_id, const CTileBoard& board) : m_team_id(team_id), m_board(board) {}

    int TeamId() const { return m_team_id; }
    // Home: the point workers ferry gathered food back to (the queen's
    // position - the first player's spawn of the team).
    void SetHomePos(florrbt::Vector2f pos)
    {
        m_home_pos = pos;
        m_has_home = true;
    }
    bool HasHomePos() const { return m_has_home; }
    florrbt::Vector2f HomePos() const { return m_home_pos; }
    // Stash: the queen's own food pile. Receives worker deposits that no
    // storage cell could take; TryConsumeFood drains it first.
    float StashFood() const { return m_food_stash; }
    void DepositFood(float amount) { m_food_stash += std::max(0.f, amount); }

    // The team's queen ({id, generation} handle). Her death loses the match;
    // the controller detects it by matching defeated mobs against this.
    void SetQueen(SEouEntityHandle handle) { m_queen = handle; }
    SEouEntityHandle Queen() const { return m_queen; }
    bool IsQueen(int id, std::uint64_t generation) const
    {
        return m_queen.id > 0 && m_queen.id == id && m_queen.generation == generation;
    }

    // Keys a cell into the connectivity set (unifies same-type neighbours).
    void AddCell(CGameWorld& world, CEouCellMob& cell);
    void RemoveCell(CGameWorld& world, CEouCellMob& cell);
    // Food pool across every storage cell: consumption/pooled queries used by
    // the eggs and the worker deposit. DepositFood stays a receiver stub.
    float StorageFoodTotal(CGameWorld& world) const;
    bool TryConsumeFood(CGameWorld& world, float amount);
    float DepositToNearestStorage(CGameWorld& world, florrbt::Vector2f pos, float amount);
    // The cell (same team) whose position equals `pos`'s grid cell.
    CEouCellMob* GetCellAt(CGameWorld& world, florrbt::Vector2f pos);
    const CEouCellMob* GetCellAt(CGameWorld& world, florrbt::Vector2f pos) const;
    const CCellGroupSet& Cells() const { return m_cells; }

    // Pheromone lifecycle.
    CEouPheromoneGroup* CreateGroup(florrbt::Vector2f target);
    // Returns the group with `group_id`, creating it on demand. Slot-bound
    // pheromone petals rely on stable group ids (slot index == group id).
    CEouPheromoneGroup* EnsureGroup(int group_id, florrbt::Vector2f target);
    bool RemoveGroup(int group_id);
    CEouPheromoneGroup* FindGroup(int group_id);
    const CEouPheromoneGroup* FindGroup(int group_id) const;
    std::size_t GroupCount() const { return m_groups.size(); }
    std::size_t MemberCount() const;

    // Ticks all groups and prunes empty ones.
    void OnTick(CGameWorld& world, float dt);

    // Marks every member mob (and the queen) for destruction, drops all groups.
    void Dissolve(CGameWorld& world);

    void CaptureSnapshot(CSnapshotWriter& writer) const;
    bool RestoreSnapshot(const CSnapshotReader& reader, std::uint32_t version, std::string& error);

  private:
    int m_team_id;
    int m_next_group_id = 1;
    const CTileBoard& m_board;
    std::vector<std::unique_ptr<CEouPheromoneGroup>> m_groups;
    florrbt::Vector2f m_home_pos{};
    bool m_has_home = false;
    float m_food_stash = 0.f;
    SEouEntityHandle m_queen;
    CCellGroupSet m_cells;
};