#pragma once
#include "../../../Shared/game_config.h"
#include "../../HotReload/snapshot_archive.h"
#include "../tile_board.h"
#include "mob.h"
#include "../../../Shared/math_types.h"
#include <algorithm>
#include <cmath>
#include <deque>
#include <optional>
#include <string>
#include <vector>

class CGameWorld;

class CEouFoodMob;

// Defined in eou_mob.cpp: resolves a fully-qualified food handle to a live
// food mob (nullptr when stale). Out of line because it dereferences the
// forward-declared CGameWorld.
CEouFoodMob* ResolveEouFoodTarget(CGameWorld* world, const SEntityHandle& handle);

inline constexpr float kEouArriveRadius = 16.f;

inline float EouDistance(florrbt::Vector2f a, florrbt::Vector2f b)
{
    const float dx = a.x - b.x;
    const float dy = a.y - b.y;
    return std::sqrt(dx * dx + dy * dy);
}

// Grid BFS pathfinding over a tile board: walks from `start` to `goal`
// (world coordinates) on the 8-neighbor grid, treating every cell with
// TileSolid() as blocked. Returns the compressed waypoint chain (start,
// corners, goal; cell centers in world units). Following the points in order
// always reaches the goal; collinear runs are merged so the chain is as
// small as possible. Empty when no path exists.
std::vector<florrbt::Vector2f> eou_find_path(const CTileBoard& board, florrbt::Vector2f start, florrbt::Vector2f goal);

// True when the straight segment from `from` to `to` crosses no solid cell
// (sampled along the line, exclusive of both endpoints' own cells) - the
// eou line-of-sight check used by worker ants when picking a dig target.
bool eou_has_line_of_sight(const CTileBoard& board, florrbt::Vector2f from, florrbt::Vector2f to);

// Type-erased interface the eou team/groups drive. Concrete eou mobs derive
// from CEOUMob which implements this.
class IEouMob
{
  public:
    virtual ~IEouMob() = default;

    virtual void SetBoard(const CTileBoard* board) = 0;
    virtual const CTileBoard* Board() const = 0;
    // Replaces the waypoint queue with a path to `goal`. Consecutive
    // waypoints are directly reachable (straight-line walkable).
    virtual void SetTarget(florrbt::Vector2f goal) = 0;
    virtual bool HasTarget() const = 0;
    virtual int EouGroupId() const = 0;
    virtual void SetEouGroupId(int group_id) = 0;
    // Goal resolution hook driven by the group's reroute: mobs that pursue
    // their own priorities (worker ants hunting team-flagged walls) return
    // that goal, everyone else falls back to `fallback`.
    virtual florrbt::Vector2f EouResolveGoal(florrbt::Vector2f fallback) const { return fallback; }
    // Every eou mob leaves a food corpse behind: the biomass this mob
    // converts into corpse food total.
    virtual float EouBiomass() const { return 0.f; }
    // Food currently being ferried home (reported by the /colony readout, so
    // in-flight cargo is not invisible).
    virtual float EouCarry() const { return 0.f; }
    // The team's home point: where workers ferry the gathered food to.
    virtual void SetHomePos(florrbt::Vector2f) {}
};

// EOU special mob base: a real CEntity (CMob) that follows a waypoint queue.
// Each tick it heads for the front waypoint, pops it on arrival, and wanders
// once the queue is empty. Teams own the decisions (SetTarget); the mob owns
// its own movement (Tick).
//
// Registration template: RegisterModMob<EModMobType::EouWorker, CEOUMob<SMobStats>>
// with a vanilla visual proxy (e.g. WorkerAnt).
template <typename TStats = SMobStats>
class CEOUMob : public CMob<TStats>, public IEouMob
{
  public:
    using CMob<TStats>::CMob;

    void Tick(float dt) override
    {
        CMob<TStats>::Tick(dt);

        const float arrival = std::max(kEouArriveRadius, this->Radius() + 4.f);
        while (!m_waypoints.empty() && EouDistance(this->Pos(), m_waypoints.front()) <= arrival)
            m_waypoints.pop_front();
        if (m_waypoints.empty())
        {
            if (m_food_phase == EFoodPhase::None) WanderEou(dt);
            else this->m_vel = { 0.f, 0.f };
            return;
        }
        this->MoveTowards(m_waypoints.front(), dt);
    }

    const CTileBoard* Board() const override { return m_board; }
    void SetBoard(const CTileBoard* board) override
    {
        if (m_board == board && board) return;
        const bool invalidate = m_board || !board;
        m_board = board;
        if (!invalidate) return; // Initial restore binding preserves saved waypoints.
        m_waypoints.clear();
        m_food_target = {};
        m_food_phase = EFoodPhase::None;
        m_food_scan_timer = m_food_path_timer = 0.f;
        OnBoardDetached();
    }
    void OnWorldTransferCommitted() override { SetBoard(nullptr); }

    void SetTarget(florrbt::Vector2f goal) override
    {
        TryReplaceEouPath(goal);
    }

    bool     HasTarget() const override { return !m_waypoints.empty(); }

    int EouGroupId() const override { return m_eou_group_id; }
    void SetEouGroupId(int group_id) override { m_eou_group_id = group_id; }
    void SetHomePos(florrbt::Vector2f pos) override { SetEouHome(pos); }
    float EouCarry() const override { return m_carry; }
    florrbt::Vector2f EouResolveGoal(florrbt::Vector2f fallback) const override
    {
        if (m_food_phase == EFoodPhase::Home) return m_home_pos;
        if (m_food_target.valid()) return m_food_target.pos;
        return fallback;
    }
    // Defaults to the biomass of the mob's base health: every eou mob leaves
    // a corpse with this much food.
    float EouBiomass() const override
    {
        return this->m_base_stats.max_health * game_config::eou_food_biomass_ratio;
    }

    std::uint32_t RuntimeSnapshotVersion() const override { return 1; }
    void CaptureRuntimeSnapshot(CSnapshotWriter& writer) const override
    {
        CJsonOwner waypoints = MakeJsonArray();
        for (const florrbt::Vector2f& point : m_waypoints)
        {
            CJsonOwner entry = MakeJsonArray();
            AppendJson(entry.get(), json_real(point.x));
            AppendJson(entry.get(), json_real(point.y));
            AppendJson(waypoints.get(), entry.release());
        }
        writer.Node("eou_waypoints", waypoints.release());
        writer.Field("eou_group_id", m_eou_group_id);
        writer.Field("eou_carry", m_carry);
        writer.Field("eou_home", m_home_pos);
        writer.Field("eou_foraging", m_food_phase != EFoodPhase::None);
    }

    bool RestoreRuntimeSnapshot(const CSnapshotReader& reader, std::uint32_t version,
                                std::string& error) override
    {
        if (version > RuntimeSnapshotVersion())
        {
            error = "Unsupported eou mob runtime snapshot version " + std::to_string(version);
            return false;
        }
        m_waypoints.clear();
        if (const json_t* node = reader.Node("eou_waypoints"); node && json_is_array(node))
        {
            constexpr std::size_t max_waypoints = 4096;
            const std::size_t count = std::min(json_array_size(node), max_waypoints);
            for (std::size_t i = 0; i < count; ++i)
            {
                const json_t* entry = json_array_get(node, i);
                if (!entry || !json_is_array(entry) || json_array_size(entry) < 2 ||
                    !json_is_number(json_array_get(entry, 0)) || !json_is_number(json_array_get(entry, 1)))
                    continue;
                const float x = static_cast<float>(json_number_value(json_array_get(entry, 0)));
                const float y = static_cast<float>(json_number_value(json_array_get(entry, 1)));
                if (std::isfinite(x) && std::isfinite(y)) m_waypoints.emplace_back(x, y);
            }
        }
        m_eou_group_id = reader.Int("eou_group_id", 0);
        m_carry = std::max(0.f, reader.Float("eou_carry", 0.f));
        m_home_pos = reader.Vector2("eou_home", {});
        // Targets are transient handles; rescan after restore without losing cargo.
        if (reader.Bool("eou_foraging", false)) m_waypoints.clear();
        m_food_phase = EFoodPhase::None;
        m_food_target = {};
        return true;
    }

  protected:
    const CTileBoard* m_board = nullptr;
    virtual void OnBoardDetached() {}

    void WanderEou(float dt)
    {
        m_wander_timer -= dt;
        if (m_wander_timer > 0.f)
        {
            this->m_vel *= std::max(0.f, 1.f - 2.f * dt);
            return;
        }
        m_wander_timer = GetLimitedRng(0.5f, 2.f);
        const float angle = GetLimitedRng(-game_config::pi, game_config::pi);
        const float speed = std::max(0.f, this->m_base_stats.max_velocity) * 0.2f;
        this->m_vel = { std::cos(angle) * speed, std::sin(angle) * speed };
    }

    // Food harvesting begins after the current group waypoint is reached.
    // Cargo survives interruptions, full storage, and snapshot restores.
    void EouFoodTick(float dt, float scan_interval)
    {
        m_food_path_timer -= dt;
        if (m_food_phase == EFoodPhase::None)
        {
            m_food_scan_timer -= dt;
            if (m_food_scan_timer > 0.f) return;
            m_food_scan_timer = scan_interval;
            if (HasTarget() && m_carry <= 0.f) return;
            BeginFoodPhase();
            return;
        }

        switch (m_food_phase)
        {
        case EFoodPhase::None:
            break;
        case EFoodPhase::Fetch:
        {
            if (!m_food_target.valid() || !m_food_target.alive())
            {
                m_food_target = {};
                m_food_phase = EFoodPhase::None;
                return;
            }
            CEouFoodMob* food = m_food_target.resolve();
            m_food_target.pos = food->Pos();
            const float reach = this->Radius() + food->Radius() + kEouArriveRadius;
            if (EouDistance(this->Pos(), m_food_target.pos) <= reach)
            {
                m_waypoints.clear();
                this->m_vel = { 0.f, 0.f };
                m_food_phase = EFoodPhase::Gather;
                return;
            }
            if (m_food_path_timer <= 0.f) RefreshFoodPath();
            break;
        }
        case EFoodPhase::Gather:
        {
            CEouFoodMob* food = m_food_target.resolve();
            if (!food || food->RemainingFood() <= 0.f)
            {
                m_food_target = {};
                m_food_phase = m_carry > 0.f ? EFoodPhase::Home : EFoodPhase::None;
                if (m_carry > 0.f) SetEouHomeTarget();
                return;
            }
            const float reach = this->Radius() + food->Radius() + kEouArriveRadius;
            if (EouDistance(this->Pos(), food->Pos()) > reach)
            {
                m_food_phase = EFoodPhase::Fetch;
                m_food_target.pos = food->Pos();
                RefreshFoodPath();
                return;
            }
            const float want = std::min(game_config::eou_food_gather_rate * dt,
                                        std::max(0.f, game_config::eou_food_carry_capacity - m_carry));
            m_carry += food->TakeFood(want);
            m_waypoints.clear();
            this->m_vel = { 0.f, 0.f };
            if (m_carry >= game_config::eou_food_carry_capacity || food->RemainingFood() <= 0.f)
            {
                m_food_target = {};
                m_food_phase = EFoodPhase::Home;
                SetEouHomeTarget();
            }
            break;
        }
        case EFoodPhase::Home:
        {
            const float reach = std::max(game_config::eou_food_home_radius,
                                         game_config::eou_queen_radius + this->Radius() + kEouArriveRadius);
            if (EouDistance(this->Pos(), m_home_pos) <= reach)
            {
                m_waypoints.clear();
                this->m_vel = { 0.f, 0.f };
                if (m_food_path_timer > 0.f) return;
                m_carry = std::max(0.f, m_carry - DepositFood(m_carry));
                m_food_path_timer = std::max(0.2f, scan_interval);
                if (m_carry <= 0.001f)
                {
                    m_carry = 0.f;
                    m_food_phase = EFoodPhase::None;
                    m_food_scan_timer = scan_interval;
                }
                return;
            }
            if (m_food_path_timer <= 0.f) SetEouHomeTarget();
            break;
        }
        }
    }

    virtual float DepositFood(float amount) = 0;

    // Drops the food routine mid-phase (a higher-priority job took over).
    // The carry is kept: the next idle Home phase delivers it.
    void AbortFoodPhase()
    {
        m_food_phase = EFoodPhase::None;
        m_food_target = {};
        m_waypoints.clear();
    }

    void SetEouHome(florrbt::Vector2f pos) { m_home_pos = pos; }

    // Drops the current waypoint chain (a direct chase takes over movement).
    void ClearEouWaypoints() { m_waypoints.clear(); }

  private:
    enum class EFoodPhase : std::uint8_t
    {
        None,
        Fetch,
        Gather,
        Home,
    };

    struct SFoodTarget
    {
        CGameWorld* world = nullptr;
        SEntityHandle ref;
        florrbt::Vector2f pos{};

        bool valid() const { return ref.IsValid(); }
        bool alive() const
        {
            CEouFoodMob* food = resolve();
            return food && !food->IsMarkedForDes() && !food->IsDead() && food->RemainingFood() > 0.f;
        }

        // Out of line (eou_mob.cpp): dereferencing the world needs the full
        // CGameWorld type, which this header only forward-declares.
        CEouFoodMob* resolve() const { return ResolveEouFoodTarget(world, ref); }
    };

    void BeginFoodPhase()
    {
        CGameWorld* world = this->GameWorld();
        if (!world) return;
        if (m_carry > 0.f)
        {
            m_food_phase = EFoodPhase::Home;
            SetEouHomeTarget();
            return;
        }
        m_food_target = FindEouFoodTarget(world);
        if (!m_food_target.valid())
        {
            // Nothing to fetch - but a partial carry (an aborted run or an
            // emptied corpse) still needs to reach home.
            if (m_carry > 0.f)
            {
                m_food_phase = EFoodPhase::Home;
                SetEouHomeTarget();
                return;
            }
            m_food_phase = EFoodPhase::None;
            m_food_scan_timer = game_config::eou_food_scan_interval;
            return;
        }
        m_food_phase = EFoodPhase::Fetch;
        RefreshFoodPath();
    }

    void SetEouHomeTarget()
    {
        m_food_path_timer = 2.f;
        TryReplaceEouPath(m_home_pos);
    }

    void RefreshFoodPath()
    {
        m_food_path_timer = 2.f;
        if (!m_board || !m_food_target.valid()) return;
        // Path refresh is transactional. A temporary obstruction must not
        // erase the still-usable route or abandon the selected food target;
        // keep following the old route and retry on the next refresh tick.
        TryReplaceEouPath(m_food_target.pos);
    }

    bool TryReplaceEouPath(florrbt::Vector2f goal)
    {
        if (!m_board) return false;
        const std::vector<florrbt::Vector2f> path = eou_find_path(*m_board, this->Pos(), goal);
        if (path.empty()) return false;

        std::deque<florrbt::Vector2f> replacement;
        std::size_t first = 0;
        if (path.size() > 1 && EouDistance(this->Pos(), path.front()) <= kEouArriveRadius) first = 1;
        for (std::size_t i = first; i < path.size(); ++i) replacement.push_back(path[i]);
        m_waypoints = std::move(replacement);
        return true;
    }

    SFoodTarget FindEouFoodTarget(CGameWorld* world);
    EFoodPhase m_food_phase = EFoodPhase::None;
    SFoodTarget m_food_target;
    float m_carry = 0.f;
    float m_food_scan_timer = 0.f;
    std::size_t m_food_search_cursor = 0;
    float m_food_path_timer = 0.f;
    florrbt::Vector2f m_home_pos{};

  private:
    std::deque<florrbt::Vector2f> m_waypoints;
    int m_eou_group_id = 0;
    float m_wander_timer = 0.f;
};

// EOU worker ant: the colony's hands. Priorities, highest first:
//   1. Dig: scan the team's flagged wall cells (CWall), pick the closest one
//      inside vision with an unblocked line of sight, walk there on its own
//      (the pheromone group only reroutes on pheromone changes, so fresh
//      flags would otherwise go unserviced) and chip the wall away after
//      eou_worker_dig_time seconds in range.
//   2. Forage: with no dig goal, run the food loop (fetch a corpse, gather,
//      ferry the carry home).
//   3. Pheromone: EouResolveGoal keeps reporting the dig goal to the group
//      so group reroutes agree with the ant's own priority.
class CEouWorkerAnt final : public CEOUMob<SMobStats>
{
  public:
    using CEOUMob<SMobStats>::CEOUMob;

    void Tick(float dt) override
    {
        CEOUMob<SMobStats>::Tick(dt);
        m_scan_timer -= dt;
        if (m_scan_timer <= 0.f)
        {
            m_scan_timer = game_config::eou_worker_scan_interval;
            const std::optional<florrbt::Vector2f> found = FindBestMarkedWall();
            if (found && (!m_goal || EouDistance(*found, *m_goal) > 1.f))
            {
                // New (or better) dig target: drop foraging and head there.
                m_goal = found;
                m_dig_progress = 0.f;
                AbortFoodPhase();
                SetTarget(*m_goal);
            }
            else if (!found && m_goal)
            {
                m_goal.reset();
                m_dig_progress = 0.f;
            }
        }
        TickDig(dt);
        if (!m_goal) EouFoodTick(dt, game_config::eou_food_scan_interval);
    }

    florrbt::Vector2f EouResolveGoal(florrbt::Vector2f fallback) const override
    {
        return m_goal.has_value() ? *m_goal : CEOUMob<SMobStats>::EouResolveGoal(fallback);
    }

    float DepositFood(float amount) override;

  private:
    void OnBoardDetached() override { m_goal.reset(); m_scan_timer = m_dig_progress = 0.f; }
    std::optional<florrbt::Vector2f> FindBestMarkedWall();
    // Digs while inside dig range of the goal; on completion asks the
    // controller to remove the wall, then rescans immediately.
    void TickDig(float dt);

    std::optional<florrbt::Vector2f> m_goal;
    float m_scan_timer = 0.f;
    float m_dig_progress = 0.f;
};

// EOU soldier ant: the colony's sword, hatched from soldier hatchery cells.
// Priorities, highest first:
//   1. Fight: scan for the closest enemy-team mob or flower inside
//      eou_soldier_aggro_range. While the line of sight is clear the chase is
//      direct (MoveTowards); through walls it paths on the scan cadence.
//      Contact damage comes from the normal collision rules. The target drops
//      once it dies or leaves eou_soldier_leash_range.
//   2. Pheromone: with no enemy, follow the group like every other eou mob.
// Soldiers never dig and never forage.
class CEouSoldierAnt final : public CEOUMob<SMobStats>
{
  public:
    using CEOUMob<SMobStats>::CEOUMob;

    void Tick(float dt) override;

    // Group reroutes agree with the fight: an engaged soldier reports its
    // target position instead of the pheromone fallback.
    florrbt::Vector2f EouResolveGoal(florrbt::Vector2f fallback) const override
    {
        return m_engaged ? m_target_pos : fallback;
    }

    // Soldiers never forage; the deposit hook is unused.
    float DepositFood(float) override { return 0.f; }

  private:
    // Revalidates the stored target handle: alive, unremoved and inside the
    // leash range. Clears the handle when the target is gone.
    CEntity* ResolveCombatTarget();
    void ScanForEnemy();

    SEntityHandle m_target_ref;
    florrbt::Vector2f m_target_pos{};
    bool m_engaged = false;
    float m_combat_scan_timer = 0.f;
};

// EOU queen: the colony's heart. Stationary (she never follows pheromones,
// only sheds collision shoves); when she dies her team loses the match (the
// controller reacts through OnMobDefeated).
class CEouQueenMob final : public CMob<SMobStats>
{
  public:
    using CMob<SMobStats>::CMob;
    bool IsCollisionPositionLocked() const override { return true; }

    void Tick(float dt) override
    {
        m_vel = { 0.f, 0.f };
        CMob<SMobStats>::Tick(dt);
        m_vel = { 0.f, 0.f };
    }
};

// Neutral food: an attackable/mobile-ish entity that stores food (the proxy
// type is reserved for the client visual, unused for now). Starts as the
// world's initial food and as the corpse every eou mob leaves behind.
class CEouFoodMob : public CMob<SMobStats>
{
  public:
    using CMob<SMobStats>::CMob;
    bool IsCollisionPositionLocked() const override { return true; }

    float RemainingFood() const { return m_food_total; }
    float FoodTotal() const { return m_food_total; }
    void SetFoodTotal(float total) { m_food_total = std::max(0.f, total); }
    int FoodType() const { return m_food_type; }
    void SetFoodType(int type) { m_food_type = type; }

    // Clamps the take to what is actually left (multiple ants can gather
    // from the same corpse); returns the amount removed.
    float TakeFood(float amount)
    {
        if (amount <= 0.f) return 0.f;
        const float taken = std::min(amount, m_food_total);
        m_food_total -= taken;
        if (m_food_total <= 0.f) m_food_total = 0.f;
        return taken;
    }

    void Tick(float dt) override
    {
        m_vel = { 0.f, 0.f };
        CMob<SMobStats>::Tick(dt);
        m_vel = { 0.f, 0.f };
        m_food_total -= std::max(0.f, game_config::eou_food_rotten_rate) * dt;
        if (m_food_total <= 0.f)
        {
            m_food_total = 0.f;
            MarkForDestroy(EEntityRemovalReason::Expired);
        }
    }

    std::uint32_t RuntimeSnapshotVersion() const override { return 1; }
    void CaptureRuntimeSnapshot(CSnapshotWriter& writer) const override
    {
        writer.Field("food_total", m_food_total);
        writer.Field("food_type", m_food_type);
    }
    bool RestoreRuntimeSnapshot(const CSnapshotReader& reader, std::uint32_t version, std::string& error) override
    {
        if (version > RuntimeSnapshotVersion())
        {
            error = "Unsupported eou food runtime snapshot version " + std::to_string(version);
            return false;
        }
        m_food_total = std::max(0.f, reader.Float("food_total", m_food_total));
        m_food_type = reader.Int("food_type", m_food_type);
        return true;
    }

  private:
    float m_food_total = 0.f;
    int m_food_type = 0;
};

// EOU cell types: storage cells hold food (workers deposit here); hatchery
// cells spawn and hatch eggs. Cells only union with same-type neighbours.
enum class ECouCellType : std::uint8_t
{
    Storage = 0,
    Hatchery = 1,
};

// EOU cell: a stationary piece of territory owned by one team. Neighbouring
// same-type cells union into a group (via the team's CCellGroupSet).
class CEouCellMob : public CMob<SMobStats>
{
  public:
    using CMob<SMobStats>::CMob;
    bool IsCollisionPositionLocked() const override { return true; }
    // Floors do not push ants or their own eggs away from the tile.
    bool CanCollide() const override { return false; }

    void SetCellType(ECouCellType type) { m_cell_type = type; }
    ECouCellType CellType() const { return m_cell_type; }

    // Storage cells: the food pool (workers deposit here, eggs consume).
    float StoredFood() const { return m_food_stored; }
    float TakeFood(float amount)
    {
        const float taken = std::min(std::max(0.f, amount), m_food_stored);
        m_food_stored -= taken;
        return taken;
    }
    float StoreFood(float amount)
    {
        const float added = std::min(std::max(0.f, amount), std::max(0.f, game_config::eou_cell_food_capacity - m_food_stored));
        m_food_stored += added;
        return added;
    }

    // Hatchery cells: bound egg handle and its hatch type (= egg type the
    // cell passes to the egg at construction) + retry timer when empty.
    bool HasEgg() const { return m_egg_ref.IsValid(); }
    void SetEgg(const SEntityHandle& ref) { m_egg_ref = ref; }
    void ClearEgg()
    {
        m_egg_ref = {};
    }
    const SEntityHandle& EggRef() const { return m_egg_ref; }
    void SetEggType(EMobType type) { m_egg_type = type; }
    EMobType EggType() const { return m_egg_type; }
    void TickEggRetry(float dt) { m_egg_retry_timer = std::max(0.f, m_egg_retry_timer - dt); }
    void ResetEggRetry() { m_egg_retry_timer = game_config::eou_cell_egg_retry_interval; }
    float EggRetryTimer() const { return m_egg_retry_timer; }

    // One tile, one ant (the EotU rule): the hatchery binds the ant it
    // hatched and only lays a fresh egg once that ant is dead or missing.
    bool HasBoundAnt() const { return m_ant_ref.IsValid(); }
    void SetBoundAnt(const SEntityHandle& ref) { m_ant_ref = ref; }
    void ClearBoundAnt()
    {
        m_ant_ref = {};
    }
    const SEntityHandle& BoundAntRef() const { return m_ant_ref; }

    int GroupId() const { return m_group_id; }
    void SetGroupId(int id) { m_group_id = id; }
    int TeamId() const { return m_team_id; }
    void SetTeamId(int team) { m_team_id = team; }

    void Tick(float dt) override
    {
        m_vel = { 0.f, 0.f };
        CMob<SMobStats>::Tick(dt);
        m_vel = { 0.f, 0.f };
        m_retire_timer -= dt;
    }
    void ResetRetireTimer() { m_retire_timer = 1.f; }

    std::uint32_t RuntimeSnapshotVersion() const override { return 2; }
    void CaptureRuntimeSnapshot(CSnapshotWriter& writer) const override
    {
        writer.Field("cell_type", static_cast<int>(m_cell_type));
        writer.Field("cell_group", m_group_id);
        writer.Field("cell_team", m_team_id);
        writer.Field("cell_food", m_food_stored);
        writer.Field("cell_egg_id", m_egg_ref.id);
        writer.UInt64("cell_egg_gen", m_egg_ref.generation);
        writer.Field("cell_egg_world_id", m_egg_ref.world_id);
        writer.UInt64("cell_egg_world_instance_generation", m_egg_ref.world_instance_generation);
        writer.Field("cell_egg_type", static_cast<int>(m_egg_type));
        writer.Field("cell_egg_retry", m_egg_retry_timer);
        writer.Field("cell_ant_id", m_ant_ref.id);
        writer.UInt64("cell_ant_gen", m_ant_ref.generation);
        writer.Field("cell_ant_world_id", m_ant_ref.world_id);
        writer.UInt64("cell_ant_world_instance_generation", m_ant_ref.world_instance_generation);
    }
    bool RestoreRuntimeSnapshot(const CSnapshotReader& reader, std::uint32_t version, std::string& error) override
    {
        if (version > RuntimeSnapshotVersion())
        {
            error = "Unsupported eou cell runtime snapshot version " + std::to_string(version);
            return false;
        }
        m_cell_type = static_cast<ECouCellType>(reader.Int("cell_type", static_cast<int>(m_cell_type)));
        m_group_id = reader.Int("cell_group", m_group_id);
        m_team_id = reader.Int("cell_team", m_team_id);
        m_food_stored = std::max(0.f, reader.Float("cell_food", m_food_stored));
        m_egg_ref.id = reader.Int("cell_egg_id", -1);
        m_egg_ref.generation = reader.UInt64("cell_egg_gen");
        m_egg_ref.world_id = reader.UInt32("cell_egg_world_id");
        m_egg_ref.world_instance_generation = reader.UInt64("cell_egg_world_instance_generation");
        m_egg_type = static_cast<EMobType>(reader.Int("cell_egg_type", static_cast<int>(m_egg_type)));
        m_egg_retry_timer = std::max(0.f, reader.Float("cell_egg_retry", m_egg_retry_timer));
        // Version 1 snapshots carry no ant binding: the cell simply lays a
        // fresh egg on the next tick, which matches the old behaviour.
        m_ant_ref.id = reader.Int("cell_ant_id", -1);
        m_ant_ref.generation = reader.UInt64("cell_ant_gen");
        m_ant_ref.world_id = reader.UInt32("cell_ant_world_id");
        m_ant_ref.world_instance_generation = reader.UInt64("cell_ant_world_instance_generation");
        return true;
    }

  private:
    ECouCellType m_cell_type = ECouCellType::Storage;
    int m_group_id = 0;
    int m_team_id = 0;
    float m_food_stored = 0.f;
    SEntityHandle m_egg_ref;
    EMobType m_egg_type = EMobType::None;
    float m_egg_retry_timer = 0.f;
    float m_retire_timer = 1.f;
    SEntityHandle m_ant_ref;
};

// EOU egg: grows over time (radius eou_egg_size_start -> eou_egg_size_final),
// then hatches into the type the hatchery cell passed at construction when
// the team can afford the food cost (the controller drives the hatch).
class CEouEggMob : public CMob<SMobStats>
{
  public:
    using CMob<SMobStats>::CMob;
    bool IsCollisionPositionLocked() const override { return true; }

    void SetHatchType(EMobType type) { m_hatch_type = type; }
    EMobType HatchType() const { return m_hatch_type; }
    void SetTeamId(int team) { m_team_id = team; }
    int TeamId() const { return m_team_id; }
    void SetBinding(const SEntityHandle& ref) { m_cell_ref = ref; }
    const SEntityHandle& CellRef() const { return m_cell_ref; }
    void SetGrown(bool grown) { m_grown = grown; }
    bool Grown() const { return m_grown; }
    float GrowProgress() const { return m_grow_progress; }

    void Tick(float dt) override
    {
        m_vel = { 0.f, 0.f };
        CMob<SMobStats>::Tick(dt);
        m_vel = { 0.f, 0.f };
        const float grow_time = std::max(0.1f, game_config::eou_egg_grow_time);
        m_age += dt;
        m_grow_progress = std::clamp(m_age / grow_time, 0.f, 1.f);
        const float radius = game_config::eou_egg_size_start +
                             (game_config::eou_egg_size_final - game_config::eou_egg_size_start) * m_grow_progress;
        Radius() = radius;
        if (m_grow_progress >= 1.f) m_grown = true;
    }

    std::uint32_t RuntimeSnapshotVersion() const override { return 1; }
    void CaptureRuntimeSnapshot(CSnapshotWriter& writer) const override
    {
        writer.Field("egg_hatch_type", static_cast<int>(m_hatch_type));
        writer.Field("egg_team", m_team_id);
        writer.Field("egg_cell", m_cell_ref.id);
        writer.UInt64("egg_cell_gen", m_cell_ref.generation);
        writer.Field("egg_cell_world_id", m_cell_ref.world_id);
        writer.UInt64("egg_cell_world_instance_generation", m_cell_ref.world_instance_generation);
        writer.Field("egg_age", m_age);
    }
    bool RestoreRuntimeSnapshot(const CSnapshotReader& reader, std::uint32_t version, std::string& error) override
    {
        if (version > RuntimeSnapshotVersion()) { error = "Unsupported eou egg snapshot"; return false; }
        m_hatch_type = static_cast<EMobType>(reader.Int("egg_hatch_type", static_cast<int>(m_hatch_type)));
        m_team_id = reader.Int("egg_team", m_team_id);
        m_cell_ref.id = reader.Int("egg_cell", -1);
        m_cell_ref.generation = reader.UInt64("egg_cell_gen");
        m_cell_ref.world_id = reader.UInt32("egg_cell_world_id");
        m_cell_ref.world_instance_generation = reader.UInt64("egg_cell_world_instance_generation");
        m_age = std::max(0.f, reader.Float("egg_age", 0.f));
        m_grow_progress = std::clamp(m_age / std::max(0.1f, game_config::eou_egg_grow_time), 0.f, 1.f);
        m_grown = m_grow_progress >= 1.f;
        return true;
    }

  private:
    EMobType m_hatch_type = EMobType::None;
    int m_team_id = 0;
    SEntityHandle m_cell_ref;
    float m_age = 0.f;
    float m_grow_progress = 0.f;
    bool m_grown = false;
};

// Registers the eou mod mobs (visual proxies keep the vanilla client sprites
// working without client mods). Call from RegisterMobs before Finalize.
bool RegisterEouMobs(std::string& error);
