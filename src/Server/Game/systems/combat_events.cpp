#include "combat_events.h"
#include "../gameworld.h"
#include "../entities/mob.h"
#include "../entities/projectile.h"
#include "../state.h"
#include <exception>
#include <stdexcept>

CCombatEventSystem::CCombatEventSystem(CGameWorld& world) : m_world(world) {}
CCombatEventSystem::~CCombatEventSystem() = default;

void CCombatEventSystem::Begin() { ++m_depth; }
void CCombatEventSystem::End(bool failed)
{
    m_failed = m_failed || failed;
    if (--m_depth || m_committing) return;
    if (m_failed)
    {
        Discard();
        if (!failed) throw std::runtime_error("Combat event aborted after a nested failure");
        return;
    }
    Flush();
}

bool CCombatEventSystem::QueueAddition(CMobBase& owner, std::unique_ptr<CState>& state)
{
    if (!m_depth || owner.GameWorld() != &m_world || !owner.GetHandle().IsValid()) return false;
    m_changes.push_back({owner.GetHandle(), std::move(state), 0, {}});
    Touch(owner);
    return true;
}
bool CCombatEventSystem::QueueRemoval(CMobBase& owner, std::uint64_t source)
{
    if (!m_depth || owner.GameWorld() != &m_world || !owner.GetHandle().IsValid()) return false;
    m_changes.push_back({owner.GetHandle(), {}, source, {}});
    Touch(owner);
    return true;
}
bool CCombatEventSystem::QueueMutation(CMobBase& owner, std::function<void(CMobBase&)> mutation)
{
    if (!m_depth || owner.GameWorld() != &m_world || !owner.GetHandle().IsValid()) return false;
    m_changes.push_back({owner.GetHandle(), {}, 0, std::move(mutation)});
    Touch(owner);
    return true;
}

void CCombatEventSystem::Touch(CEntity& entity)
{
    if (!IsActive()) return;
    CEntity* current = &entity;
    for (int depth = 0; current && depth < 8; ++depth)
    {
        if (current->GameWorld() != &m_world) return;
        if (dynamic_cast<CMobBase*>(current))
        {
            const auto handle = current->GetHandle();
            if (handle.IsValid() && m_queued_views.insert(handle).second) m_dirty_views.push_back(handle);
        }
        const auto* projectile = dynamic_cast<const CProjectile*>(current);
        CEntity* next = projectile ? projectile->GetOwner() : nullptr;
        if (next == current) return;
        current = next;
    }
}
void CCombatEventSystem::StateChanged(CMobBase& owner) { Touch(owner); }

void CCombatEventSystem::Discard() noexcept
{
    m_changes.clear(); m_dirty_views.clear(); m_queued_views.clear(); m_failed = false;
}

void CCombatEventSystem::Flush()
{
    struct SCommitGuard { bool& value; ~SCommitGuard() { value = false; } } guard{m_committing};
    m_committing = true;
    try
    {
        std::size_t cursor = 0;
        while (cursor < m_changes.size() || !m_dirty_views.empty())
        {
            while (cursor < m_changes.size())
            {
                auto change = std::move(m_changes[cursor++]);
                auto* owner = dynamic_cast<CMobBase*>(m_world.ResolveEntity(change.owner));
                if (!owner) continue;
                if (change.removal) owner->RemoveStateImmediate(change.removal);
                else if (!owner->IsDead() && !owner->IsMarkedForDes())
                {
                    if (change.addition) owner->AddStateImmediate(std::move(change.addition));
                    else if (change.mutation) change.mutation(*owner);
                }
                if (m_failed) throw std::runtime_error("Nested combat state publication failed");
            }
            if (!m_dirty_views.empty())
            {
                const auto handle = m_dirty_views.back(); m_dirty_views.pop_back(); m_queued_views.erase(handle);
                if (auto* owner = dynamic_cast<CMobBase*>(m_world.ResolveEntity(handle))) owner->CommitActionView();
                if (m_failed) throw std::runtime_error("Nested combat view publication failed");
            }
        }
        Discard();
    }
    catch (const std::exception& error)
    {
        Discard(); m_world.EnterRecovery("战斗状态发布失败：" + std::string(error.what())); throw;
    }
    catch (...) { Discard(); m_world.EnterRecovery("战斗状态发布发生未知异常"); throw; }
}

CCombatEventScope::CCombatEventScope(CCombatEventSystem* events)
    : m_events(events), m_exceptions(std::uncaught_exceptions())
{
    if (m_events) m_events->Begin();
}
CCombatEventScope::~CCombatEventScope() noexcept(false)
{
    if (m_events) m_events->End(std::uncaught_exceptions() > m_exceptions);
}
