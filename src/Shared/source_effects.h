#pragma once
#include <algorithm>
#include <cstdint>
#include <vector>

// 策略提供基础值，运行时来源独立持有作用权限；撤销单一来源不能覆盖其他来源或更新后的策略。
class CSourcedFlag
{
  public:
    bool Value() const { return m_policy || !m_sources.empty(); }
    bool Policy() const { return m_policy; }
    void SetPolicy(bool value) { m_policy = value; }
    void SetSource(std::uint64_t source, bool active)
    {
        if (!source) return;
        const auto found = std::find(m_sources.begin(), m_sources.end(), source);
        if (active && found == m_sources.end()) m_sources.push_back(source);
        else if (!active && found != m_sources.end()) m_sources.erase(found);
    }
    void ClearSources() { m_sources.clear(); }
  private:
    bool m_policy = false;
    std::vector<std::uint64_t> m_sources;
};

// 覆盖值先按优先级、再按附加先后选择；修改基础值不撤销活跃来源。
template<class T> class CSourcedValue
{
  public:
    const T& Value() const { return m_value; }
    const T& Base() const { return m_base; }
    void SetBase(T value) { m_base = value; Recompute(); }
    void SetSource(std::uint64_t source, T value, int priority)
    {
        if (!source) return;
        const auto found = std::find_if(m_sources.begin(), m_sources.end(), [source](const auto& item) { return item.source == source; });
        if (found == m_sources.end()) m_sources.push_back({source, value, priority});
        else { found->value = value; found->priority = priority; }
        Recompute();
    }
    void RemoveSource(std::uint64_t source)
    { std::erase_if(m_sources, [source](const auto& item) { return item.source == source; }); Recompute(); }
    void ClearSources() { m_sources.clear(); Recompute(); }
  private:
    struct SEntry { std::uint64_t source; T value; int priority; };
    void Recompute()
    {
        m_value = m_base;
        const SEntry* selected = nullptr;
        for (const auto& source : m_sources)
            if (!selected || source.priority >= selected->priority) selected = &source;
        if (selected) m_value = selected->value;
    }
    T m_base{}, m_value{};
    std::vector<SEntry> m_sources;
};
