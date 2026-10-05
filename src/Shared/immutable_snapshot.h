#pragma once
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace florrbt
{
// Main-thread builder; published objects are immutable and can safely be
// read by a background writer. Mutable access must first mark that key dirty.
template<class T> class ImmutableSnapshotCache
{
  public:
    using Snapshot = std::vector<std::shared_ptr<const T>>;
    void MarkDirty(const std::string& name) { m_dirty.insert(name); }
    void Reset() { m_values.clear(); m_dirty.clear(); }
    template<class TItems, class TInclude> Snapshot Capture(const TItems& items, TInclude include)
    {
        Snapshot result;
        result.reserve(items.size());
        for (const auto& item : items)
        {
            if (!include(item)) continue;
            auto& cached = m_values[item.name];
            if (!cached || m_dirty.contains(item.name)) cached = std::make_shared<const T>(item);
            result.push_back(cached);
        }
        if (m_values.size() > result.size())
        {
            std::unordered_set<std::string> present;
            for (const auto& item : result) present.insert(item->name);
            std::erase_if(m_values, [&](const auto& entry) { return !present.contains(entry.first); });
        }
        m_dirty.clear();
        return result;
    }
  private:
    std::unordered_map<std::string, std::shared_ptr<const T>> m_values;
    std::unordered_set<std::string> m_dirty;
};
}
