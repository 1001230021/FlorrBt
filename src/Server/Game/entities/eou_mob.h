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

// 按完整句柄解析食物实体，失效时返回空。
CEouFoodMob* ResolveEouFoodTarget(CGameWorld* world, const SEntityHandle& handle);

inline constexpr float kEouArriveRadius = 16.f;

inline float EouDistance(florrbt::Vector2f a, florrbt::Vector2f b)
{
    const float dx = a.x - b.x;
    const float dy = a.y - b.y;
    return std::sqrt(dx * dx + dy * dy);
}

// 八邻域 BFS，使用世界坐标起终点，返回压缩后的可通行路径；不可达时为空。
std::vector<florrbt::Vector2f> eou_find_path(const CTileBoard& board, florrbt::Vector2f start, florrbt::Vector2f goal);

// 检查线段途经格是否通行，不含起终点自身，用于工蚁选择挖掘目标。
bool eou_has_line_of_sight(const CTileBoard& board, florrbt::Vector2f from, florrbt::Vector2f to);

// 团队和分组通过此接口驱动 EOU 生物。
class IEouMob
{
  public:
    virtual ~IEouMob() = default;

    virtual void SetBoard(const CTileBoard* board) = 0;
    virtual const CTileBoard* Board() const = 0;
    // 寻路成功后替换路径，相邻路径点须可直达。
    virtual void SetTarget(florrbt::Vector2f goal) = 0;
    virtual bool HasTarget() const = 0;
    virtual int EouGroupId() const = 0;
    virtual void SetEouGroupId(int group_id) = 0;
    // 分组重规划时，生物可用挖掘或战斗等高优先级目标覆盖信息素目标。
    virtual florrbt::Vector2f EouResolveGoal(florrbt::Vector2f fallback) const { return fallback; }
    // 死亡后转为尸体食物的生物量。
    virtual float EouBiomass() const { return 0.f; }
    // 运输中的食物也计入 /colony 统计。
    virtual float EouCarry() const { return 0.f; }
    virtual void SetHomePos(florrbt::Vector2f) {}
};

// 团队决定目标，生物负责沿路径移动；无任务时游走。
template <typename TStats = SMobStats>
class CEOUMob : public CMob<TStats>, public IEouMob
{
  public:
    using CMob<TStats>::CMob;

    void OnTickIntent(float dt) override
    {
        CMob<TStats>::OnTickIntent(dt);
        if (this->IsPlayerControlled()) return;

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
        if (!invalidate) return; // 首次恢复绑定保留已存路径。
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
    // 默认按基础生命计算尸体食物量。
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
        // 恢复后重新扫描临时目标，保留已搬运食物。
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

    // 到达分组路径点后开始采集；任务中断、仓库满和快照恢复都不能丢失搬运量。
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

    // 高优先级任务中断采集时保留货物，空闲后继续送回。
    void AbortFoodPhase()
    {
        m_food_phase = EFoodPhase::None;
        m_food_target = {};
        m_waypoints.clear();
    }

    void SetEouHome(florrbt::Vector2f pos) { m_home_pos = pos; }

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
            // 无可采集目标时，仍需送回已有货物。
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
        // 新路径失败时保留旧路线和食物目标，等待下次刷新，避免暂时阻塞使工蚁停工。
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

template <>
CEOUMob<SMobStats>::SFoodTarget CEOUMob<SMobStats>::FindEouFoodTarget(CGameWorld* world);

// 工蚁优先挖掘可达的团队标记墙，其次采集和搬运食物。
// 分组重规划沿用工蚁当前目标，不能让信息素覆盖正在执行的挖掘任务。
class CEouWorkerAnt final : public CEOUMob<SMobStats>
{
  public:
    using CEOUMob<SMobStats>::CEOUMob;

    void OnTickActions(float dt) override
    {
        CEOUMob<SMobStats>::OnTickActions(dt);
        m_scan_timer -= dt;
        if (m_scan_timer <= 0.f)
        {
            m_scan_timer = game_config::eou_worker_scan_interval;
            const std::optional<florrbt::Vector2f> found = FindBestMarkedWall();
            if (found && (!m_goal || EouDistance(*found, *m_goal) > 1.f))
            {
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
    // 挖掘完成后请求移除墙体并立即重扫目标。
    void TickDig(float dt);

    std::optional<florrbt::Vector2f> m_goal;
    float m_scan_timer = 0.f;
    float m_dig_progress = 0.f;
};

// 兵蚁优先追击范围内敌人：直视时直接移动，隔墙时定期寻路，接触伤害由碰撞系统处理。
// 目标死亡或超出追击范围后恢复信息素跟随；不挖掘、不采集。
class CEouSoldierAnt final : public CEOUMob<SMobStats>
{
  public:
    using CEOUMob<SMobStats>::CEOUMob;

    void OnTickIntent(float dt) override;

    florrbt::Vector2f EouResolveGoal(florrbt::Vector2f fallback) const override
    {
        return m_engaged ? m_target_pos : fallback;
    }

    float DepositFood(float) override { return 0.f; }

  private:
    // 清除失效、死亡或超出追击范围的目标句柄。
    CEntity* ResolveCombatTarget();
    void ScanForEnemy();

    SEntityHandle m_target_ref;
    florrbt::Vector2f m_target_pos{};
    bool m_engaged = false;
    float m_combat_scan_timer = 0.f;
};

// 蚁后保持静止，死亡时由控制器通过 OnMobDefeated 判负。
class CEouQueenMob final : public CMob<SMobStats>
{
  public:
    using CMob<SMobStats>::CMob;
    bool IsCollisionPositionLocked() const override { return true; }

    void OnTickActions(float dt) override
    {
        m_vel = { 0.f, 0.f };
        CMob<SMobStats>::OnTickActions(dt);
        m_vel = { 0.f, 0.f };
    }
};

// 中立食物实体，用于初始食物与生物尸体。
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

    // 多只工蚁共享同一食物时按实际余量扣取，返回真正取得的数量。
    float TakeFood(float amount)
    {
        if (amount <= 0.f) return 0.f;
        const float taken = std::min(amount, m_food_total);
        m_food_total -= taken;
        if (m_food_total <= 0.f) m_food_total = 0.f;
        return taken;
    }

    void OnTickActions(float dt) override
    {
        m_vel = { 0.f, 0.f };
        CMob<SMobStats>::OnTickActions(dt);
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

// 储藏格存食物，孵化格产卵；仅同类型相邻格合并分组。
enum class ECouCellType : std::uint8_t
{
    Storage = 0,
    Hatchery = 1,
};

// 团队持有的固定领地格，连接关系由 CCellGroupSet 管理。
class CEouCellMob : public CMob<SMobStats>
{
  public:
    using CMob<SMobStats>::CMob;
    bool IsCollisionPositionLocked() const override { return true; }
    // 地板不把蚂蚁或自身卵推离格子。
    bool CanCollide() const override { return false; }

    void SetCellType(ECouCellType type) { m_cell_type = type; }
    ECouCellType CellType() const { return m_cell_type; }

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

    // 一格一蚁：绑定已孵化生物，死亡或失效后才能再次产卵。
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

    void OnTickActions(float dt) override
    {
        m_vel = { 0.f, 0.f };
        CMob<SMobStats>::OnTickActions(dt);
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
        // 版本 1 未保存绑定生物，按旧规则在下帧重新产卵。
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

// 卵随时间生长，食物足够时由控制器按孵化格指定类型孵化。
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

    void OnTickActions(float dt) override
    {
        m_vel = { 0.f, 0.f };
        CMob<SMobStats>::OnTickActions(dt);
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

// 在生物注册表冻结前注册 EOU 类型，使用基础生物的视觉代理。
bool RegisterEouMobs(std::string& error);
