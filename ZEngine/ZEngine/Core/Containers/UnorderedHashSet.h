#pragma once
#include <Allocator.h>
#include <Array.h>
#include <Helpers/MemoryOperations.h>
#include <ZEngineDef.h>
#include <rapidhash.h>
#include <cstddef>
#include <iterator>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace ZEngine::Core::Containers
{
    enum class EntryState
    {
        Empty,
        Occupied,
        Deleted
    };

    template <typename K>
    struct HashSetEntry
    {
        K          key   = {};
        EntryState state = EntryState::Empty;
    };

    template <typename K, bool IsConst>
    class HashSetIterator
    {
    public:
        using Entry             = HashSetEntry<K>;
        using EntryPointer      = std::conditional_t<IsConst, const Entry*, Entry*>;
        using value_type        = K;
        using reference         = const K&;
        using pointer           = const K*;
        using iterator_category = std::input_iterator_tag;
        using difference_type   = std::ptrdiff_t;

        HashSetIterator(EntryPointer entries, std::size_t index, std::size_t size) : m_entries(entries), m_index(index), m_size(size)
        {
            advance_to_valid();
        }

        HashSetIterator& operator++()
        {
            ++m_index;
            advance_to_valid();
            return *this;
        }

        bool operator!=(const HashSetIterator& other) const
        {
            return m_index != other.m_index;
        }
        bool operator==(const HashSetIterator& other) const
        {
            return m_index == other.m_index;
        }

        const K& operator*() const
        {
            return m_entries[m_index].key;
        }
        const K* operator->() const
        {
            return &m_entries[m_index].key;
        }

    private:
        void advance_to_valid()
        {
            while (m_index < m_size && m_entries[m_index].state != EntryState::Occupied)
            {
                ++m_index;
            }
        }

        EntryPointer m_entries;
        std::size_t  m_index;
        std::size_t  m_size;
    };

    template <typename K>
    class UnorderedHashSet
    {
    public:
        using Entry          = HashSetEntry<K>;
        using size_type      = std::size_t;
        using iterator       = HashSetIterator<K, false>;
        using const_iterator = HashSetIterator<K, true>;

        // @param allocator        Arena allocator for memory management.
        // @param initial_capacity Initial number of slots (default: 16).
        void init(Memory::ArenaAllocator* allocator, size_type initial_capacity = 16)
        {
            m_allocator   = allocator;
            m_load_factor = 0.75f;
            m_entries.init(m_allocator, initial_capacity);
            for (size_type i = 0; i < initial_capacity; ++i)
            {
                m_entries.push({});
            }
            m_size = 0;
        }

        void insert(const K& key)
        {
            maybe_grow();

            size_type index = probe_for_insert(key);
            auto&     entry = m_entries[index];

            if (entry.state == EntryState::Empty || entry.state == EntryState::Deleted)
            {
                entry.key   = key;
                entry.state = EntryState::Occupied;
                ++m_size;
            }
        }

        const K* find(const K& key) const
        {
            size_type index = probe_for_key(key);
            return (index != size_type(-1)) ? &m_entries[index].key : nullptr;
        }

        // Checks if a key exists in the hash map.
        // @param key The key to check.
        // @return True if the key exists, false otherwise.
        bool contains(const K& key) const
        {
            return probe_for_key(key) != size_type(-1);
        }

        // Removes a key.
        void remove(const K& key)
        {
            size_type index = probe_for_key(key);
            if (index != size_type(-1))
            {
                m_entries[index].state = EntryState::Deleted;
                --m_size;
            }
        }

        void clear()
        {
            for (auto& entry : m_entries)
            {
                entry.state = EntryState::Empty;
            }
            m_size = 0;
        }

        bool empty() const
        {
            return m_size == 0;
        }
        size_type size() const
        {
            return m_size;
        }
        size_type capacity() const
        {
            return m_entries.size();
        }

        iterator begin()
        {
            return iterator(m_entries.data(), 0, m_entries.size());
        }
        iterator end()
        {
            return iterator(m_entries.data(), m_entries.size(), m_entries.size());
        }

        const_iterator begin() const
        {
            return const_iterator(m_entries.data(), 0, m_entries.size());
        }
        const_iterator end() const
        {
            return const_iterator(m_entries.data(), m_entries.size(), m_entries.size());
        }
        const_iterator cbegin() const
        {
            return begin();
        }
        const_iterator cend() const
        {
            return end();
        }

        void reserve(size_type new_capacity)
        {
            if (new_capacity > m_entries.size())
            {
                rehash(new_capacity);
            }
        }

    private:
        void maybe_grow()
        {
            if (static_cast<float>(m_size + 1) / m_entries.size() > m_load_factor)
            {
                size_type new_capacity = std::max<size_type>(16, static_cast<size_type>(m_entries.size() * 1.5f));
                rehash(new_capacity);
            }
        }

        bool key_equals(const K& a, const K& b) const
        {
            if constexpr (std::is_same_v<K, const char*>)
            {
                return Helpers::secure_strcmp(a, b) == 0;
            }
            else
            {
                return a == b;
            }
        }

        void rehash(size_type new_capacity)
        {
            Array<Entry> old_entries = m_entries;
            m_entries                = Array<Entry>{};
            m_entries.init(m_allocator, new_capacity);
            for (size_type i = 0; i < new_capacity; ++i)
                m_entries.push({});
            m_size = 0;

            for (size_type i = 0; i < old_entries.size(); ++i)
            {
                if (old_entries[i].state == EntryState::Occupied)
                {
                    size_type index  = probe_for_insert(old_entries[i].key);
                    m_entries[index] = old_entries[i];
                    ++m_size;
                }
            }
        }

        size_type probe_for_key(const K& key) const
        {
            size_type index = hash(key) % m_entries.size();
            size_type i     = 0;
            do
            {
                const auto& entry = m_entries[index];
                if (entry.state == EntryState::Empty)
                {
                    return size_type(-1);
                }
                if (entry.state == EntryState::Occupied && key_equals(entry.key, key))
                {
                    return index;
                }
                ++i;
                index = (index + i) % m_entries.size();
            } while (i < m_entries.size());
            return size_type(-1);
        }

        size_type probe_for_insert(const K& key)
        {
            size_type index         = hash(key) % m_entries.size();
            size_type first_deleted = size_type(-1);
            size_type i             = 0;
            do
            {
                auto& entry = m_entries[index];
                if (entry.state == EntryState::Occupied && key_equals(entry.key, key))
                {
                    return index;
                }
                if (entry.state == EntryState::Empty)
                {
                    return (first_deleted != size_type(-1)) ? first_deleted : index;
                }
                if (entry.state == EntryState::Deleted && first_deleted == size_type(-1))
                {
                    first_deleted = index;
                }
                ++i;
                index = (index + i) % m_entries.size();
            } while (i < m_entries.size());
            if (first_deleted != size_type(-1))
            {
                return first_deleted;
            }
            throw std::runtime_error("UnorderedHashSet probe failed: table full");
        }

        size_type hash(const K& key) const
        {
            if constexpr (std::is_same_v<K, const char*>)
            {
                return rapidhash(key, Helpers::secure_strlen(key));
            }
            else
            {
                return rapidhash(&key, sizeof(K));
            }
        }

        Memory::ArenaAllocator* m_allocator = nullptr;
        Array<Entry>            m_entries;
        size_type               m_size        = 0;
        float                   m_load_factor = 0.75f;
    };
} // namespace ZEngine::Core::Containers
