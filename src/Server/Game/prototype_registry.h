#pragma once

#include <array>
#include <cstddef>
#include <iterator>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

template <typename TId, typename TPrototype, std::size_t Size> class TPrototypeRegistry
{
  public:
    using name_function = std::string_view (*)(TId);
    using entry_type = std::pair<TId, const TPrototype*>;

    class const_iterator
    {
      public:
        using iterator_category = std::forward_iterator_tag;
        using value_type = entry_type;
        using difference_type = std::ptrdiff_t;

        const_iterator() = default;

        value_type operator*() const
        {
            return { static_cast<TId>(m_index), m_p_registry->m_entries[m_index].get() };
        }

        const_iterator& operator++()
        {
            ++m_index;
            SkipEmpty();
            return *this;
        }

        const_iterator operator++(int)
        {
            const_iterator previous = *this;
            ++(*this);
            return previous;
        }

        friend bool operator==(const const_iterator& lhs, const const_iterator& rhs)
        {
            return lhs.m_p_registry == rhs.m_p_registry && lhs.m_index == rhs.m_index;
        }

      private:
        friend class TPrototypeRegistry;

        const_iterator(const TPrototypeRegistry* registry, std::size_t index)
            : m_p_registry(registry), m_index(index)
        {
            SkipEmpty();
        }

        void SkipEmpty()
        {
            while (m_p_registry && m_index < Size && !m_p_registry->m_entries[m_index]) ++m_index;
        }

        const TPrototypeRegistry* m_p_registry = nullptr;
        std::size_t m_index = Size;
    };

    TPrototypeRegistry(std::string_view kind, name_function name)
        : m_kind(kind), m_name(name)
    {
    }

    TPrototypeRegistry(const TPrototypeRegistry&) = delete;
    TPrototypeRegistry& operator=(const TPrototypeRegistry&) = delete;

    bool Add(TId id, std::unique_ptr<TPrototype> prototype)
    {
        if (m_frozen)
        {
            AddError("cannot register " + Name(id) + " after the registry was frozen");
            return false;
        }

        const std::size_t index = Index(id);
        if (index == 0 || index >= Size)
        {
            AddError("invalid id " + std::to_string(index));
            return false;
        }
        if (!prototype)
        {
            AddError("null prototype for " + Name(id));
            return false;
        }
        if (!m_unsupported_reasons[index].empty())
        {
            AddError(Name(id) + " is already marked unsupported");
            return false;
        }
        if (m_entries[index])
        {
            AddError("duplicate registration for " + Name(id));
            return false;
        }

        m_entries[index] = std::move(prototype);
        ++m_registered_count;
        return true;
    }

    bool MarkUnsupported(TId id, std::string_view reason)
    {
        if (m_frozen)
        {
            AddError("cannot mark " + Name(id) + " unsupported after the registry was frozen");
            return false;
        }

        const std::size_t index = Index(id);
        if (index == 0 || index >= Size)
        {
            AddError("invalid unsupported id " + std::to_string(index));
            return false;
        }
        if (reason.empty())
        {
            AddError("unsupported id " + Name(id) + " requires a reason");
            return false;
        }
        if (m_entries[index])
        {
            AddError(Name(id) + " is already registered");
            return false;
        }
        if (!m_unsupported_reasons[index].empty())
        {
            AddError("duplicate unsupported declaration for " + Name(id));
            return false;
        }

        m_unsupported_reasons[index] = std::string(reason);
        return true;
    }

    void ReportError(std::string error) { AddError(std::move(error)); }

    bool Finalize(std::string& error)
    {
        if (m_frozen)
        {
            error.clear();
            return true;
        }

        for (std::size_t index = 1; index < Size; ++index)
        {
            if (!m_entries[index] && m_unsupported_reasons[index].empty())
                AddError("missing registration for " + Name(static_cast<TId>(index)));
        }

        if (!m_errors.empty())
        {
            error.clear();
            for (const std::string& item : m_errors)
            {
                if (!error.empty()) error += '\n';
                error += m_kind;
                error += ": ";
                error += item;
            }
            return false;
        }

        m_frozen = true;
        error.clear();
        return true;
    }

    const TPrototype* Find(TId id) const
    {
        const std::size_t index = Index(id);
        if (index >= Size) return nullptr;
        return m_entries[index].get();
    }

    const std::string* UnsupportedReason(TId id) const
    {
        const std::size_t index = Index(id);
        if (index >= Size || m_unsupported_reasons[index].empty()) return nullptr;
        return &m_unsupported_reasons[index];
    }

    bool IsFrozen() const { return m_frozen; }
    std::size_t size() const { return m_registered_count; }
    bool empty() const { return m_registered_count == 0; }

    const_iterator begin() const { return const_iterator(this, 0); }
    const_iterator end() const { return const_iterator(this, Size); }

  private:
    static std::size_t Index(TId id) { return static_cast<std::size_t>(id); }

    std::string Name(TId id) const
    {
        if (m_name)
        {
            const std::string_view name = m_name(id);
            if (!name.empty()) return std::string(name);
        }
        return std::to_string(Index(id));
    }

    void AddError(std::string error) { m_errors.push_back(std::move(error)); }

    std::string m_kind;
    name_function m_name = nullptr;
    std::array<std::unique_ptr<TPrototype>, Size> m_entries;
    std::array<std::string, Size> m_unsupported_reasons;
    std::vector<std::string> m_errors;
    std::size_t m_registered_count = 0;
    bool m_frozen = false;
};
