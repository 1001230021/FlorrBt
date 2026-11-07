#pragma once
#include "../entity.h"
#include <functional>
#include <memory>
#include <unordered_set>
#include <vector>

class CGameWorld;
class CMobBase;
class CState;
class CCombatEventScope;

// 伤害同步返回结果；命中产生的状态在最外层事件结束后统一发布。
class CCombatEventSystem
{
  public:
    explicit CCombatEventSystem(CGameWorld& world);
    ~CCombatEventSystem();
    bool IsActive() const { return m_depth != 0 || m_committing; }
    bool IsCollecting() const { return m_depth != 0; }
    bool QueueAddition(CMobBase& owner, std::unique_ptr<CState>& state);
    bool QueueRemoval(CMobBase& owner, std::uint64_t source);
    bool QueueMutation(CMobBase& owner, std::function<void(CMobBase&)> mutation);
    void Touch(CEntity& entity);
    void StateChanged(CMobBase& owner);
  private:
    friend class CCombatEventScope;
    void Begin();
    void End(bool failed);
    void Flush();
    void Discard() noexcept;
    struct SChange
    {
        SEntityHandle owner;
        std::unique_ptr<CState> addition;
        std::uint64_t removal = 0;
        std::function<void(CMobBase&)> mutation;
    };
    CGameWorld& m_world;
    unsigned m_depth = 0;
    bool m_failed = false, m_committing = false;
    std::vector<SChange> m_changes;
    std::vector<SEntityHandle> m_dirty_views;
    std::unordered_set<SEntityHandle, SEntityHandleHash> m_queued_views;
};

class CCombatEventScope
{
  public:
    explicit CCombatEventScope(CCombatEventSystem* events);
    ~CCombatEventScope() noexcept(false);
    CCombatEventScope(const CCombatEventScope&) = delete;
    CCombatEventScope& operator=(const CCombatEventScope&) = delete;
  private:
    CCombatEventSystem* m_events;
    int m_exceptions;
};
