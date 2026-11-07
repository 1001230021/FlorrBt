#pragma once
#include "../../../Shared/game_config.h"
#include "../../../Shared/math_types.h"
#include "../../../Shared/shared.h"
#include "../controller.h"
#include "../entity.h"
#include "../prototype_registry.h"
#include "../state.h"
#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <vector>

class CPlayer;
class CGameContext;
class CSnapshotReader;
class CSnapshotWriter;
class CPickupController;

struct SControlEquipment
{
    struct SSlot { uint8_t type = 0, rarity = 0; bool available = true, policy_banned = false; };
    int capacity = 0;
    std::vector<SSlot> slots;
};

struct CDamageData
{
    std::string m_account_name;
    float m_total_dmg = 0.f;
    float m_reset_timer = 0.f;
    // 临时进度来源的奖励不能在玩家离开后写入同账号的正式进度。
    std::string m_progress_account;
    std::string m_inventory_account;

    void ResetTimer() { m_reset_timer = 120.f; }
    CPlayer* ResolvePlayer(const CGameContext* context) const;
    bool MatchesInventoryScope(const CPlayer& player) const;
};

class CMobBase;
bool TrySharePsionicDamage(CMobBase* receiver, float dmg, CEntity* attacker, EDamageType dmg_type);
bool ShouldBlockDiggingDamage(CMobBase* receiver, CEntity* attacker, EDamageType dmg_type);

class CMobBase : public CEntity
{
  public:
    CMobBase(CGameWorld* pworld, florrbt::Vector2f pos, float r, EMobType mob_type)
        : CEntity(pworld, pos.x, pos.y, r, MakeMobEntityType(static_cast<std::uint8_t>(mob_type)))
    {
    }

    virtual ~CMobBase();
    CMobBase(const CMobBase&) = delete;
    CMobBase& operator=(const CMobBase&) = delete;

    void AddState(std::unique_ptr<CState> state, EStateApplyTiming timing = EStateApplyTiming::EventBoundary);
    bool DeferStateMutation(std::function<void(CMobBase&)> mutation);
    CState* FindStateBySource(std::uint64_t source) const;
    virtual void CommitActionView();
    void BeginStateTick();
    void EnsureStateTickPrepared() { if (!m_state_tick_prepared) BeginStateTick(); }
    void TickStates(float dt);
    void OnTickBegin(float dt) override;
    void OnTickIntent(float dt) override;
    void OnTickMotion(float dt) override;
    void OnTickEnvironment(float dt) override;
    void OnTickEffects(float dt) override;
    void OnTickActionRequests(float dt) override;
    void CollectTickDependencies(std::vector<SEntityHandle>& output) const override;
    bool IsPlayerControlled() const { return m_p_controller && m_p_controller->IsPlayerControlled(); }
    EWallQuery WallCollisionMask() const override
    { return IsPlayerControlled() ? EWallQuery::Solid : EWallQuery::All; }
    const SCapabilityState& Capabilities() const { return m_capabilities; }
    bool IsInShortcut() const { return m_capabilities.HasSource(ERestrictionSource::Shortcut); }
    bool AllowsStatModifiers() const
    { return !m_capabilities.Restricted(ECapabilityRestriction::StatModifiers); }
    // 锁定进入时的接触范围，避免尺寸加成被禁用后缩出区域并反复恢复。
    float EnvironmentProbeRadius() const
    { return std::max(std::max(0.f, WallCollisionRadius()), m_shortcut_probe_radius); }
    float ShortcutProbeRadius() const { return m_shortcut_probe_radius; }
    void SynchronizeEnvironment(std::optional<florrbt::Vector2f> movement_start = std::nullopt);
    void SynchronizeEnvironmentAfterDisplacement();
    void IntegrateMovement(float dt);
    void SetShortcutContact(bool touching);
    void RestoreShortcutContact(bool touching, float probe_radius = 0.f)
    {
        m_capabilities.Set(ERestrictionSource::Shortcut, CShortcutRule::Restrictions(true, touching));
        m_shortcut_probe_radius = touching && std::isfinite(probe_radius) ? std::max(0.f, probe_radius) : 0.f;
    }
    bool TickDropPickup(CPlayer* player);
    // 接近距离包含双方半径，使近战实际进入接触范围。
    virtual void MoveTowards(const florrbt::Vector2f& target_pos, float dt, float target_radius = -1.f);
    // 模拟输入用距离与满幅偏移之比决定速度；负目标半径表示无身体死区的点目标。
    virtual void MoveTowards(const florrbt::Vector2f& target_pos, float dt, float target_radius,
                             float full_speed_distance);
    void ApplyDamageDirect(float dmg, CEntity* attacker, EDamageType damage_type = EDamageType::Normal);

    virtual const SMobStats* GetBaseStats() const = 0;
    virtual const SMobStats* GetFinalStats() const = 0;
    virtual void RestoreBaseStats(const SMobStats& stats) = 0;
    EMobType GetMobType() const { return static_cast<EMobType>(GetNetworkType()); }
    const SEntityStats& GetEntityStats() const override
    {
        const SMobStats* stats = GetFinalStats();
        return stats ? static_cast<const SEntityStats&>(*stats) : CEntity::GetEntityStats();
    }
    float TickHorizon() const override
    {
        const SMobStats* stats = GetFinalStats();
        return stats ? std::max(0.f, stats->horizon) : CEntity::TickHorizon();
    }
    virtual ERarity GetRarity() const = 0;
    // 自然刷新标记独立于稀有度，控制台、精确刷怪和技能召唤不消耗自然稀有配额。
    bool IsNaturallySpawned() const { return m_naturally_spawned; }
    void SetNaturallySpawned(bool value) { m_naturally_spawned = value; }
    virtual bool IsFacingLocked() const { return false; }
    virtual bool IsMovementControlledBySkill() const { return false; }
    virtual CPickupController* GetPickupController();
    virtual const CPickupController* GetPickupController() const;
    // 墙碰撞半径按玩家花朵半径封顶，避免零半径接触导致高速穿墙。
    float WallCollisionRadius() const override
    {
        return std::clamp(Radius(), 0.f, game_config::mob_player_flower_radius);
    }
    virtual std::uint32_t RuntimeSnapshotVersion() const { return 1; }
    virtual void CaptureRuntimeSnapshot(CSnapshotWriter&) const {}
    virtual bool RestoreRuntimeSnapshot(const CSnapshotReader&, std::uint32_t version, std::string& error)
    {
        if (version <= RuntimeSnapshotVersion()) return true;
        error = "Unsupported mob runtime snapshot version";
        return false;
    }

    std::vector<CDamageData>& GetDamageData() { return m_damage_data; }
    const std::vector<CDamageData>& GetDamageData() const { return m_damage_data; }
    float InactiveSeconds() const { return m_inactive_seconds; }
    void AddInactiveSeconds(float dt) { m_inactive_seconds += dt; }
    void ResetInactiveSeconds() { m_inactive_seconds = 0.f; }
    const std::vector<std::unique_ptr<CState>>& GetStates() const { return m_states; }
    void ClearStatesForRestore();
    void ClearTeamOverrideStates();
    void RebindStateOwners();
    void RemapTransferredReferences(TEntityHandleRemap mapping) noexcept override
    {
        RebindStateOwners();
        for (auto& state : m_states) if (state) state->RemapTransferredReferences(mapping);
    }

    template <typename TState> std::vector<TState*> FindStates()
    {
        std::vector<TState*> states;
        for (auto& state : m_states)
        {
            auto* casted = dynamic_cast<TState*>(state.get());
            if (casted) states.push_back(casted);
        }
        return states;
    }

    template <typename TState> TState* FindFirstState()
    {
        for (auto& state : m_states)
        {
            auto* casted = dynamic_cast<TState*>(state.get());
            if (casted) return casted;
        }
        return nullptr;
    }

    template <typename TState> const TState* FindFirstState() const
    {
        for (const auto& state : m_states)
        {
            auto* casted = dynamic_cast<const TState*>(state.get());
            if (casted) return casted;
        }
        return nullptr;
    }

    template <typename TState> bool HasState() const { return FindFirstState<TState>() != nullptr; }

    template <typename TState, typename TVisitor> void ForEachState(TVisitor visitor)
    {
        for (auto& state : m_states)
        {
            auto* casted = dynamic_cast<TState*>(state.get());
            if (casted) visitor(casted);
        }
    }

    template <typename TState, typename TVisitor> void ForEachState(TVisitor visitor) const
    {
        for (const auto& state : m_states)
        {
            auto* casted = dynamic_cast<const TState*>(state.get());
            if (casted) visitor(casted);
        }
    }

    bool RemoveState(CState* state, EStateApplyTiming timing = EStateApplyTiming::EventBoundary);

    void SetController(std::unique_ptr<IController> controller);
    IController* GetController() { return m_p_controller.get(); }
    const IController* GetController() const { return m_p_controller.get(); }
    // 附身期间挂起原 AI，保留冷却与行为状态；控制权交接由迁移事务负责。
    std::unique_ptr<SControlEquipment> CaptureControlEquipment() const;
    const SControlEquipment* ControlReturnEquipment() const { return m_control_return_equipment.get(); }
    void PreserveControlEquipment(std::unique_ptr<SControlEquipment> equipment)
    { if (!m_control_return_equipment) m_control_return_equipment = std::move(equipment); }
    void AcquirePlayerControl(std::unique_ptr<IController> controller, std::unique_ptr<SControlEquipment> equipment);
    void ReleasePlayerControl(std::unique_ptr<IController> default_ai);
    const IController* SuspendedController() const { return m_suspended_controller.get(); }
    bool HasPlayerControlLease() const { return m_has_control_lease; }
    int ControlReturnTeam() const { return m_control_return_team; }
    bool RestorePlayerControlLease(bool active, int team, std::unique_ptr<IController> controller, std::string& error,
                                   std::unique_ptr<SControlEquipment> equipment = {});

    florrbt::Vector2f m_vel = { 0.f, 0.f };

  private:
    friend class CCombatEventSystem;
    friend class CState;
    void AddStateImmediate(std::unique_ptr<CState> state);
    bool RemoveStateImmediate(std::uint64_t source);

  protected:
    virtual void OnEnvironmentChanged() {}
    SCapabilityState m_capabilities;
    float m_shortcut_probe_radius = 0.f;
    florrbt::Vector2f m_environment_sample_position{};
    std::uint64_t m_environment_sample_world = 0;
    florrbt::Vector2f ApplyMovementTargetStates(const florrbt::Vector2f& target_pos) const;
    void MoveTowardsWithWave(const florrbt::Vector2f& target_pos, float dt, float target_radius,
                             float full_speed_distance, float& wave_timer);
    void UnregisterMovementTargetState(CState* state);

    std::vector<CDamageData> m_damage_data;
    std::vector<std::unique_ptr<CState>> m_states;
    struct SFrameState
    {
        std::uint64_t source;
        float remaining;
        std::optional<SStateContinuousEffect> effect;
    };
    std::vector<SFrameState> m_frame_states;
    bool m_state_tick_prepared = false;
    std::vector<CState*> m_movement_target_states;
    std::unique_ptr<IController> m_p_controller;
    std::unique_ptr<IController> m_suspended_controller;
    std::unique_ptr<SControlEquipment> m_control_return_equipment;
    bool m_has_control_lease = false;
    int m_control_return_team = 0;
    float m_inactive_seconds = 0.f;
    bool m_naturally_spawned = false;
};

template <typename TStats = SMobStats> class CMob : public CMobBase
{
  public:
    using stats_type = TStats;

    CMob(CGameWorld* pworld, florrbt::Vector2f pos, float r, EMobType mob_type, ERarity rarity, const TStats& stats)
        : CMobBase(pworld, pos, r, mob_type), m_base_stats(stats), m_final_stats(stats), m_rarity(rarity)
    {
        if (rarity == ERarity::Eternal || rarity == ERarity::Unique || rarity == ERarity::Primordial)
            TickMode() = EEntityTickMode::Always;
        SetTickHorizon(std::max(0.f, stats.horizon));
        Health() = stats.max_health;
        Mass() = stats.mass;
        FacingAngle() = GetLimitedRng(-game_config::pi, game_config::pi);
        HasFacing() = true;
    }

    ~CMob() override;

    const SMobStats* GetBaseStats() const override { return &m_base_stats; }
    const SMobStats* GetFinalStats() const override { return AllowsStatModifiers() ? &m_final_stats : &m_base_stats; }
    void RestoreBaseStats(const SMobStats& stats) override
    {
        static_cast<SMobStats&>(m_base_stats) = stats;
        static_cast<SMobStats&>(m_final_stats) = stats;
    }
    ERarity GetRarity() const override { return m_rarity; }

    void OnTakeDamage(float dmg, CEntity* attacker, EDamageType dmg_type) override
    {
        if (ShouldBlockDiggingDamage(this, attacker, dmg_type)) return;
        if (dmg_type == EDamageType::Normal) dmg = std::max(0.f, dmg - m_base_stats.armor);
        if (dmg <= 0.f) return;
        if (TrySharePsionicDamage(this, dmg, attacker, dmg_type)) return;

        ApplyDamageDirect(dmg, attacker, dmg_type);
    }

    TStats m_base_stats;
    TStats m_final_stats;
    ERarity m_rarity;
};

// 多节根与体节仅互持带代数句柄，实体所有权仍归世界。
enum class EMultiSegmentAssemblyState : std::uint8_t
{
    NotStarted,
    InProgress,
    Complete,
    Failed,
};

struct SMobSegmentReference
{
    int index = -1;
    SEntityHandle entity_ref;
};

class IMultiSegmentRootMob
{
  public:
    virtual ~IMultiSegmentRootMob() = default;

    virtual const std::vector<SMobSegmentReference>& GetSegmentReferences() const = 0;
    virtual bool AttachSegment(const SEntityHandle& entity_ref, int index) = 0;
    virtual bool DetachSegment(const SEntityHandle& entity_ref, int index) = 0;
    virtual void ClearSegmentReferences() = 0;
    virtual EMultiSegmentAssemblyState SegmentAssemblyState() const = 0;
    virtual void SetSegmentAssemblyState(EMultiSegmentAssemblyState state) = 0;
    virtual void RestoreSegmentReference(const SEntityHandle& entity_ref, int index) = 0;
};

class IMultiSegmentBodyMob
{
  public:
    virtual ~IMultiSegmentBodyMob() = default;

    virtual const SEntityHandle& RootHandle() const = 0;
    virtual const SEntityHandle& NextSegmentHandle() const = 0;
    virtual int SegmentIndex() const = 0;
    virtual void SetRootReference(const SEntityHandle& root_ref, int index) = 0;
    virtual void SetNextSegmentReference(const SEntityHandle& next_segment_ref) = 0;
};

// 实现放在 mob.cpp，避免模板循环包含 gameworld.h。
CMobBase* ResolveMultiSegmentRoot(const CGameWorld* world, const SEntityHandle& root_ref);
CMobBase* ResolveMultiSegmentBody(const CGameWorld* world, const SEntityHandle& body_ref);
void DestroyMultiSegmentChildren(CMobBase& root, const std::vector<SMobSegmentReference>& references);
void DetachMultiSegmentBody(CMobBase& body, const SEntityHandle& root_ref, int segment_index);
void AssembleMultiSegmentMob(CMobBase& root);
void ResolveMultiSegmentReferences(CGameWorld& world);
void ConstrainMultiSegmentChain(CMobBase& root, float dt);
bool UsesMultiSegmentChainConstraint(const CMobBase& mob);
bool SharesMultiSegmentRootHealth(const CMobBase& mob);
bool ShareMultiSegmentAssembly(const CEntity* lhs, const CEntity* rhs);

template <typename TStats = SMobStats>
class CMultiSegmentRootMob : public CMob<TStats>, public IMultiSegmentRootMob
{
  public:
    using stats_type = TStats;
    using CMob<TStats>::CMob;

    void OnTickConstraints(float dt) override
    {
        CMob<TStats>::OnTickConstraints(dt);
        ConstrainMultiSegmentChain(*this, dt);
    }

    const std::vector<SMobSegmentReference>& GetSegmentReferences() const override { return m_segment_references; }
    void CollectTransferDependencies(std::vector<SEntityHandle>& output) const override
    {
        for (const auto& reference : m_segment_references) output.push_back(reference.entity_ref);
    }
    void RemapTransferredReferences(TEntityHandleRemap mapping) noexcept override
    {
        CMob<TStats>::RemapTransferredReferences(mapping);
        for (auto& reference : m_segment_references)
            reference.entity_ref = RemapTransferredHandle(reference.entity_ref, mapping);
        std::erase_if(m_segment_references, [](const auto& reference) { return !reference.entity_ref.IsValid(); });
    }
    std::size_t SegmentCount() const { return m_segment_references.size(); }

    const SMobSegmentReference* FindSegmentReference(int index) const
    {
        const auto it = std::find_if(m_segment_references.begin(), m_segment_references.end(),
                                     [index](const SMobSegmentReference& reference) {
                                         return reference.index == index;
                                     });
        return it == m_segment_references.end() ? nullptr : &*it;
    }

    bool AttachSegment(const SEntityHandle& entity_ref, int index) override
    {
        if (!entity_ref.IsValid() || index <= 0) return false;
        for (const auto& reference : m_segment_references)
        {
            if (reference.index == index) return reference.entity_ref.RefersTo(entity_ref);
            if (reference.entity_ref.RefersTo(entity_ref)) return false;
        }
        m_segment_references.push_back({ index, entity_ref });
        std::sort(m_segment_references.begin(), m_segment_references.end(),
                  [](const SMobSegmentReference& lhs, const SMobSegmentReference& rhs) {
                      return lhs.index < rhs.index;
                  });
        return true;
    }

    bool DetachSegment(const SEntityHandle& entity_ref, int index) override
    {
        const auto it = std::find_if(m_segment_references.begin(), m_segment_references.end(),
                                     [&](const SMobSegmentReference& reference) {
                                         return reference.index == index && reference.entity_ref.RefersTo(entity_ref);
                                     });
        if (it == m_segment_references.end()) return false;
        m_segment_references.erase(it);
        return true;
    }

    void ClearSegmentReferences() override { m_segment_references.clear(); }

    EMultiSegmentAssemblyState SegmentAssemblyState() const override { return m_segment_assembly_state; }
    void SetSegmentAssemblyState(EMultiSegmentAssemblyState state) override { m_segment_assembly_state = state; }

    void RestoreSegmentReference(const SEntityHandle& entity_ref, int index) override
    {
        if (!entity_ref.IsValid() || index <= 0) return;
        // 重复或冲突条目不建立引用，避免根与体节关系产生歧义。
        AttachSegment(entity_ref, index);
    }

    void OnBeforeRemoved(EEntityRemovalReason) override
    {
        if (m_segment_cleanup_started) return;
        m_segment_cleanup_started = true;
        const auto references = m_segment_references;
        m_segment_references.clear();
        DestroyMultiSegmentChildren(*this, references);
    }

  private:
    std::vector<SMobSegmentReference> m_segment_references;
    EMultiSegmentAssemblyState m_segment_assembly_state = EMultiSegmentAssemblyState::NotStarted;
    bool m_segment_cleanup_started = false;
};

template <typename TStats = SMobStats>
class CMultiSegmentBodyMob : public CMob<TStats>, public IMultiSegmentBodyMob
{
  public:
    using stats_type = TStats;
    using CMob<TStats>::CMob;

    const SEntityHandle& RootHandle() const override { return m_root_ref; }
    void CollectTransferDependencies(std::vector<SEntityHandle>& output) const override
    {
        if (m_root_ref.IsValid()) output.push_back(m_root_ref);
    }
    void RemapTransferredReferences(TEntityHandleRemap mapping) noexcept override
    {
        CMob<TStats>::RemapTransferredReferences(mapping);
        m_root_ref = RemapTransferredHandle(m_root_ref, mapping);
        m_next_segment_ref = RemapTransferredHandle(m_next_segment_ref, mapping);
    }
    const SEntityHandle& NextSegmentHandle() const override { return m_next_segment_ref; }
    int SegmentIndex() const override { return m_segment_index; }

    void SetRootReference(const SEntityHandle& root_ref, int index) override
    {
        m_root_ref = root_ref;
        m_segment_index = index;
    }

    void SetNextSegmentReference(const SEntityHandle& next_segment_ref) override
    {
        m_next_segment_ref = next_segment_ref;
    }

    CMobBase* ResolveRoot() const { return ResolveMultiSegmentRoot(this->GameWorld(), m_root_ref); }
    bool IsDead() const override
    {
        if (m_root_ref.IsValid())
        {
            const auto* root = ResolveRoot();
            if (!root || root->IsDead()) return true;
        }
        return CMob<TStats>::IsDead();
    }
    CMobBase* ResolveNextSegment() const
    {
        return ResolveMultiSegmentBody(this->GameWorld(), m_next_segment_ref);
    }

    void OnTickIntent(float dt) override
    { if (!UsesMultiSegmentChainConstraint(*this)) CMob<TStats>::OnTickIntent(dt); }
    void OnTickMotion(float dt) override
    { if (!UsesMultiSegmentChainConstraint(*this)) CMob<TStats>::OnTickMotion(dt); }

    void OnTakeDamage(float dmg, CEntity* attacker, EDamageType dmg_type) override
    {
        if (SharesMultiSegmentRootHealth(*this))
        {
            if (CMobBase* root = ResolveRoot())
            {
                root->TakeDamage(dmg, attacker, dmg_type);
                return;
            }
        }
        CMob<TStats>::OnTakeDamage(dmg, attacker, dmg_type);
    }

    bool CollidesWithWalls() const override
    {
        // 硬约束多节体由头部导航，体节各自解墙会使转角处链条撕裂。
        return !UsesMultiSegmentChainConstraint(*this);
    }

    void OnBeforeRemoved(EEntityRemovalReason) override
    {
        if (m_detach_started) return;
        m_detach_started = true;
        DetachMultiSegmentBody(*this, m_root_ref, m_segment_index);
    }

  private:
    SEntityHandle m_root_ref;
    SEntityHandle m_next_segment_ref;
    int m_segment_index = -1;
    bool m_detach_started = false;
};

template <typename TStats = SMobStats>
using CMultiSegmentHeadMob = CMultiSegmentRootMob<TStats>;

class IAttackableMob
{
  public:
    virtual ~IAttackableMob() = default;

    virtual bool IsAttacking() const = 0;
    virtual bool IsDefending() const = 0;
    virtual void SetAttacking(bool attacking) = 0;
    virtual void SetDefending(bool defending) = 0;
    virtual bool TryAttack(CEntity* target) = 0;

    void ClearAttackState()
    {
        SetAttacking(false);
        SetDefending(false);
    }
};

class ISkillCasterMob
{
  public:
    virtual ~ISkillCasterMob() = default;

    virtual int GetSkillCount() const = 0;
    virtual bool CanCastSkill(int skill_index) const = 0;
    virtual bool TryCastSkill(int skill_index, CEntity* target) = 0;
    virtual bool IsSkillBusy() const = 0;
    virtual uint8_t GetWindupSkillId() const = 0;
};

class IHornetMob
{
  public:
    virtual ~IHornetMob() = default;
    virtual bool TryCastMissileInDirection(florrbt::Vector2f direction) = 0;
    virtual void SetMissileGenerationSuppressed(bool suppressed, bool discard_loaded) = 0;
    virtual bool RestoreSkillActions(const CSnapshotReader& reader, std::uint32_t version, std::string& error) = 0;
};

// 冲刺由生物技能统一处理校验、冷却与位移，控制器只提交请求。
class ICrabDashMob
{
  public:
    virtual ~ICrabDashMob() = default;
    virtual bool IsDashActive() const = 0;
    virtual bool IsDashRecovering() const = 0;
};

// 增幅源通过接口提供闪电强度；稀有度重载用于按连锁自身档位封顶。
class ILightningBoostSource
{
  public:
    virtual ~ILightningBoostSource() = default;
    virtual float LightningBoostDamage() const = 0;
    virtual float LightningBoostDamageAtRarity(ERarity rarity) const = 0;
};

template <typename TStats = SMobStats> class CAttackableMob : public CMob<TStats>, public IAttackableMob
{
  public:
    using stats_type = TStats;
    using CMob<TStats>::CMob;

    bool IsAttacking() const override { return m_attacking; }
    bool IsDefending() const override { return m_defending; }
    void SetAttacking(bool attacking) override { m_attacking = attacking; }
    void SetDefending(bool defending) override { m_defending = defending; }
    bool TryAttack(CEntity*) override { return false; }

    bool m_attacking = false;
    bool m_defending = false;
};

template <typename TStats = SMobStats> class CSkillCasterMob : public CAttackableMob<TStats>, public ISkillCasterMob
{
  public:
    using stats_type = TStats;
    using CAttackableMob<TStats>::CAttackableMob;

    int GetSkillCount() const override { return 0; }
    bool CanCastSkill(int) const override { return false; }
    bool TryCastSkill(int, CEntity*) override { return false; }
    bool IsSkillBusy() const override { return false; }
    uint8_t GetWindupSkillId() const override { return 0; }
};

class CMobPrototype
{
  public:
    using stats_factory = std::function<SMobStats(ERarity)>;
    using flower_stats_factory = std::function<SFlowerStats(ERarity)>;
    using controller_factory = std::function<std::unique_ptr<IController>(ERarity)>;
    using after_create = std::function<void(CMobBase&, ERarity)>;
    using rarity_resolver = std::function<ERarity(CGameWorld&, florrbt::Vector2f, ERarity)>;
    using mob_factory = std::function<std::unique_ptr<CMobBase>(CGameWorld*, florrbt::Vector2f, ERarity)>;
    using segment_type_factory = std::function<EMobType(int)>;
    using segment_count_factory = std::function<int(ERarity)>;
    using segment_visual_variant_factory = std::function<std::uint8_t(int, int)>;

    static constexpr int max_segment_count = 256;

    CMobPrototype() = default;
    CMobPrototype(const CMobPrototype&) = delete;
    CMobPrototype& operator=(const CMobPrototype&) = delete;
    CMobPrototype(CMobPrototype&&) = default;
    CMobPrototype& operator=(CMobPrototype&&) = default;

    static void ApplyBaseMassMultiplier(SMobStats& stats)
    {
        const float multiplier = std::max(0.f, game_config::mob_base_mass_multiplier);
        const float max_mass = std::numeric_limits<float>::max();
        if (stats.mass >= max_mass / std::max(1.f, multiplier))
            stats.mass = max_mass;
        else
            stats.mass *= multiplier;
    }

    SMobStats BuildStats(ERarity rarity) const
    {
        SMobStats stats = m_stats_factory ? m_stats_factory(rarity) : m_base_stats;
        ApplyBaseMassMultiplier(stats);
        return stats;
    }
    SFlowerStats BuildFlowerStats(ERarity rarity) const
    {
        SFlowerStats stats = m_flower_stats_factory ? m_flower_stats_factory(rarity) : m_base_flower_stats;
        if (!m_flower_stats_factory && m_stats_factory)
            static_cast<SMobStats&>(stats) = m_stats_factory(rarity);
        ApplyBaseMassMultiplier(stats);
        return stats;
    }

    EMobType m_type = EMobType::None;
    std::string m_name;
    // 快照使用基础生物的视觉代理类型，客户端无需认识服务端扩展编号。
    EMobType m_visual_proxy_type = EMobType::None;
    SMobStats m_base_stats;
    SFlowerStats m_base_flower_stats;
    int m_team = 2;
    stats_factory m_stats_factory;
    flower_stats_factory m_flower_stats_factory;
    controller_factory m_controller_factory;
    after_create m_after_create;
    rarity_resolver m_rarity_resolver;
    mob_factory m_factory;
    // 非零体节数声明多节根；根为第 0 节，工厂为第 1 至 count 节返回已注册类型。
    int m_segment_count = 0;
    segment_count_factory m_segment_count_factory;
    segment_type_factory m_segment_type_factory;
    // 服务端将体节序号与总数映射为紧凑视觉变体，客户端只按快照绘制及排序。
    segment_visual_variant_factory m_segment_visual_variant_factory;
    // 硬约束使用相邻较小半径计算间距，稀有度缩放后仍保持一致重叠。
    bool m_constrain_segment_chain = false;
    bool m_share_segment_health = false;
    float m_segment_center_distance_radius_scale = 2.f;
    float m_segment_max_turn_radians = game_config::pi;
    bool m_allow_skip_tick = true;

    int ResolveSegmentCount(ERarity rarity) const
    {
        const int count = m_segment_count_factory ? m_segment_count_factory(rarity) : m_segment_count;
        return std::clamp(count, 0, max_segment_count);
    }

    template <typename TStats> TStats BuildTypedStats(ERarity rarity) const
    {
        TStats stats;
        static_cast<SMobStats&>(stats) = BuildStats(rarity);
        return stats;
    }
};

template <> inline SFlowerStats CMobPrototype::BuildTypedStats<SFlowerStats>(ERarity rarity) const
{
    return BuildFlowerStats(rarity);
}

// 注册槽位覆盖基础与预留扩展区，扩展编号可选，不要求冻结时全部填满。
using CMobRegistry =
    TPrototypeRegistry<EMobType, CMobPrototype, static_cast<std::size_t>(kMobIdCapacity),
                       static_cast<std::size_t>(EMobType::FireflyLarva) + 1>;
namespace mob_registry_detail
{
inline CMobRegistry& MutableRegistry()
{
    static CMobRegistry registry("mob", static_cast<CMobRegistry::name_function>(GetMobTypeName));
    return registry;
}
}

inline const CMobRegistry& GetMobRegistry() { return mob_registry_detail::MutableRegistry(); }

template <EMobType Type, typename TMob> bool RegisterMobPrototype(CMobPrototype prototype)
{
    static_assert(std::is_base_of_v<CMobBase, TMob>, "TMob must derive from CMobBase");

    constexpr bool is_segment_root = std::is_base_of_v<IMultiSegmentRootMob, TMob>;
    const bool has_segment_layout = prototype.m_segment_count > 0 || static_cast<bool>(prototype.m_segment_count_factory);
    if (prototype.m_segment_count < 0 || prototype.m_segment_count > CMobPrototype::max_segment_count)
    {
        mob_registry_detail::MutableRegistry().ReportError("invalid multi-segment count for " +
                                                            std::string(GetMobTypeName(Type)));
        return false;
    }
    if (prototype.m_segment_count > 0 && prototype.m_segment_count_factory)
    {
        mob_registry_detail::MutableRegistry().ReportError("fixed and dynamic multi-segment counts both set for " +
                                                            std::string(GetMobTypeName(Type)));
        return false;
    }
    if (prototype.m_segment_count_factory)
    {
        for (int value = static_cast<int>(ERarity::Common); value <= static_cast<int>(ERarity::Exotic); ++value)
        {
            const int count = prototype.m_segment_count_factory(static_cast<ERarity>(value));
            if (count < 0 || count > CMobPrototype::max_segment_count)
            {
                mob_registry_detail::MutableRegistry().ReportError("dynamic multi-segment count out of range for " +
                                                                    std::string(GetMobTypeName(Type)));
                return false;
            }
        }
    }
    if (has_segment_layout && !prototype.m_segment_type_factory)
    {
        mob_registry_detail::MutableRegistry().ReportError("multi-segment root " +
                                                            std::string(GetMobTypeName(Type)) +
                                                            " requires a segment type factory");
        return false;
    }
    if (!has_segment_layout && prototype.m_segment_type_factory)
    {
        mob_registry_detail::MutableRegistry().ReportError("segment type factory without segment count for " +
                                                            std::string(GetMobTypeName(Type)));
        return false;
    }
    if (has_segment_layout && !is_segment_root)
    {
        mob_registry_detail::MutableRegistry().ReportError("multi-segment configuration on non-root mob " +
                                                            std::string(GetMobTypeName(Type)));
        return false;
    }

    using stats_type = typename TMob::stats_type;
    if constexpr (std::is_same_v<stats_type, SFlowerStats>)
    {
        if (!prototype.m_flower_stats_factory)
        {
            mob_registry_detail::MutableRegistry().ReportError("missing flower stats factory for " +
                                                                std::string(GetMobTypeName(Type)));
            return false;
        }
    } else if (!prototype.m_stats_factory)
    {
        mob_registry_detail::MutableRegistry().ReportError("missing stats factory for " +
                                                            std::string(GetMobTypeName(Type)));
        return false;
    }

    prototype.m_type = Type;
    prototype.m_name = std::string(GetMobTypeName(Type));
    auto ptr = std::make_unique<CMobPrototype>(std::move(prototype));
    CMobPrototype* raw_ptr = ptr.get();
    raw_ptr->m_factory = [raw_ptr](CGameWorld* world, florrbt::Vector2f pos, ERarity rarity) -> std::unique_ptr<CMobBase> {
        if (!world) return nullptr;

        typename TMob::stats_type stats = raw_ptr->BuildTypedStats<typename TMob::stats_type>(rarity);
        auto mob = std::make_unique<TMob>(world, pos, stats.radius, Type, rarity, stats);
        mob->SetTeam(raw_ptr->m_team);
        mob->AllowSkipTick() = raw_ptr->m_allow_skip_tick;
        if (raw_ptr->m_after_create) raw_ptr->m_after_create(*mob, rarity);
        if (raw_ptr->m_controller_factory) mob->SetController(raw_ptr->m_controller_factory(rarity));
        return mob;
    };
    return mob_registry_detail::MutableRegistry().Add(Type, std::move(ptr));
}

const CMobPrototype* FindMobPrototype(EMobType type);

// 扩展生物必须指定基础视觉代理，内部编号不进入协议或客户端类型表。
template <EModMobType Type, typename TMob> bool RegisterModMob(CMobPrototype prototype, EMobType visual_proxy)
{
    static_assert(IsModMobTypeId(static_cast<int>(Type)), "mod mobs must live in the reserved mod id range");
    static_assert(!GetModMobTypeName(static_cast<int>(Type)).empty(),
                  "declare the mod mob in the mod_mob_type.h controller manifest first");
    if (visual_proxy == EMobType::None || static_cast<std::size_t>(visual_proxy) >= mob_type_names.size())
    {
        mob_registry_detail::MutableRegistry().ReportError(
            "mod mob " + std::string(GetModMobTypeName(static_cast<int>(Type))) +
            " requires a vanilla visual proxy");
        return false;
    }
    prototype.m_visual_proxy_type = visual_proxy;
    return RegisterMobPrototype<ToMobType(Type), TMob>(std::move(prototype));
}

std::unique_ptr<CMobBase> CreateMob(EMobType type, CGameWorld* world, florrbt::Vector2f pos, ERarity rarity,
                                    bool resolve_special_rarity = true, bool naturally_spawned = false);
inline std::unique_ptr<CMobBase> CreateMob(EModMobType type, CGameWorld* world, florrbt::Vector2f pos, ERarity rarity,
                                           bool resolve_special_rarity = true, bool naturally_spawned = false)
{
    return CreateMob(ToMobType(type), world, pos, rarity, resolve_special_rarity, naturally_spawned);
}
bool RegisterMobs(std::string& error);
