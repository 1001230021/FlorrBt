#pragma once
#include "entity.h"
#include "../../Shared/shared.h"
#include "../../Shared/math_types.h"
#include <atomic>
#include <limits>
#include <stdexcept>
#include <optional>
#include <functional>

class CMobBase;
class CGameWorld;

constexpr float endless = -1.f;
enum class EStateApplyTiming { EventBoundary, DamageProtection };

struct SStateContinuousEffect
{
    float damage_per_second = 0.f;
    EDamageType damage_type = EDamageType::Poison;
    SEntityHandle applier;
};

class CState
{
  public:
    CState(CMobBase* owner, float timer, ERarity rarity, EStateType type);
    virtual ~CState();
    virtual bool PrepareAttachment(CMobBase&) { return true; }
    virtual void OnAttached(CMobBase&) {}
    std::uint64_t EffectSourceId() const { return m_effect_source_id; }
    bool WasAttached() const { return m_attached; }
    bool IsRuleManaged() const { return m_rule_managed; }
    void SetRuleManaged(bool managed) { m_rule_managed = managed; }

    virtual void Tick(float dt) = 0;
    // 以值保存本帧已经存在的持续效果，状态刷新或替换不能抹掉这段结算。
    virtual std::optional<SStateContinuousEffect> CaptureContinuousEffect() const { return std::nullopt; }
    virtual void RemapTransferredReferences(TEntityHandleRemap) noexcept {}
    virtual bool AffectsMovementTarget() const { return false; }
    virtual florrbt::Vector2f ModifyMovementTarget(const CMobBase&, const florrbt::Vector2f& target_pos) const
    {
        return target_pos;
    }
    EStateType GetType() const { return m_type; }

    // 状态不拥有生物，按完整句柄解析宿主；宿主析构前一并清除句柄与借用的世界上下文。
    CMobBase* ResolveOwner() const;
    void RebindOwner(CMobBase* owner);
    void DetachOwner();

    float m_timer = endless;
    ERarity m_rarity = ERarity::Null;

  protected:
    bool DeferMutation(std::function<void(CState&)> mutation);
    static void RemoveReplacedState(CMobBase& owner, CState* state);
  private:
    friend class CMobBase;
    void AttachToOwner(CMobBase& owner) { m_attached = true; OnAttached(owner); }
    bool m_attached = false;
    bool m_rule_managed = false;
    inline static std::atomic<std::uint64_t> s_next_effect_source_id{0};
    static std::uint64_t AllocateEffectSourceId()
    {
        auto previous = s_next_effect_source_id.load();
        do {
            if (previous == (std::numeric_limits<std::uint64_t>::max)())
                throw std::overflow_error("State effect source identity exhausted");
        } while (!s_next_effect_source_id.compare_exchange_weak(previous, previous + 1));
        return previous + 1;
    }
    const std::uint64_t m_effect_source_id = AllocateEffectSourceId();
    CGameWorld* m_p_owner_world = nullptr;
    SEntityHandle m_owner_ref;
    EStateType m_type = EStateType::None;
};
