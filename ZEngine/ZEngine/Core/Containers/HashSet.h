#pragma once
#include <Allocator.h>
#include <Array.h>
#include <Helpers/MemoryOperations.h>
#include <ZEngineDef.h>
#include <rapidhash.h>
#include <algorithm>
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
    struct OrderedHashSetEntry
    {
        K           key   = {};
        EntryState  state = EntryState::Empty;
        std::size_t prev  = std::size_t(-1);
        std::size_t next  = std::size_t(-1);
    };

    template <typename K, bool IsConst>
    class OrderedHashSetIterator
    {
    public:
        using Entry             = OrderedHashSetEntry<K>;
        using EntryPointer      = std::conditional_t<IsConst, const Entry*, Entry*>;
        using value_type        = K;
        using reference         = const K&;
        using pointer           = const K*;
        using iterator_category = std::forward_iterator_tag;
        using difference_type   = std::ptrdiff_t;

        // @param entries Pointer to the hash table slot array.
        // @param index   Current slot index; std::size_t(-1) represents end.
        OrderedHashSetIterator(EntryPointer entries, std::size_t index) : m_entries(entries), m_index(index) {}

        OrderedHashSetIterator& operator++()
        {
            m_index = m_entries[m_index].next;
            return *this;
        }

        bool operator!=(const OrderedHashSetIterator& other) const
        {
            return m_index != other.m_index;
        }
        bool operator==(const OrderedHashSetIterator& other) const
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
        EntryPointer m_entries;
        std::size_t  m_index;
    };

    template <typename K>
    class HashSet
    {
    public:
        using Entry          = OrderedHashSetEntry<K>;
        using size_type      = std::size_t;
        using iterator       = OrderedHashSetIterator<K, false>;
        using const_iterator = OrderedHashSetIterator<K, true>;

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
            m_head = size_type(-1);
            m_tail = size_type(-1);
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
                link_tail(index);
                ++m_size;
            }
        }

        const K* find(const K& key) const
        {
            size_type index = probe_for_key(key);
            return (index != size_type(-1)) ? &m_entries[index].key : nullptr;
        }

        // Checks if a key exists in the hash set.
        // @param key The key to check.
        // @return True if the key exists, false otherwise.
        bool contains(const K& key) const
        {
            return probe_for_key(key) != size_type(-1);
        }

        void remove(const K& key)
        {
            size_type index = probe_for_key(key);
            if (index != size_type(-1))
            {
                unlink(index);
                m_entries[index].state = EntryState::Deleted;
                --m_size;
            }
        }

        void clear()
        {
            for (size_type i = 0; i < m_entries.size(); ++i)
            {
                m_entries[i].state = EntryState::Empty;
                m_entries[i].prev  = size_type(-1);
                m_entries[i].next  = size_type(-1);
            }
            m_size = 0;
            m_head = size_type(-1);
            m_tail = size_type(-1);
        }

        void sort_keys()
        {
            if (m_size < 2)
            {
                return;
            }

            Array<size_type> indices;
            indices.init(m_allocator, m_size);

            size_type cur = m_head;
            while (cur != size_type(-1))
            {
                indices.push(cur);
                cur = m_entries[cur].next;
            }

            std::sort(indices.data(), indices.data() + indices.size(), [this](size_type a, size_type b) { return key_less(m_entries[a].key, m_entries[b].key); });

            m_head = indices[0];
            m_tail = indices[indices.size() - 1];

            for (size_type i = 0; i < indices.size(); ++i)
            {
                m_entries[indices[i]].prev = (i > 0) ? indices[i - 1] : size_type(-1);
                m_entries[indices[i]].next = (i + 1 < indices.size()) ? indices[i + 1] : size_type(-1);
            }
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
            return iterator(m_entries.data(), m_head);
        }
        iterator end()
        {
            return iterator(m_entries.data(), size_type(-1));
        }
        const_iterator begin() const
        {
            return const_iterator(m_entries.data(), m_head);
        }
        const_iterator end() const
        {
            return const_iterator(m_entries.data(), size_type(-1));
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
                rehash(new_capacity);
        }

    private:
        void link_tail(size_type index)
        {
            m_entries[index].prev = m_tail;
            m_entries[index].next = size_type(-1);
            if (m_tail != size_type(-1))
            {
                m_entries[m_tail].next = index;
            }
            else
            {
                m_head = index;
            }
            m_tail = index;
        }

        void unlink(size_type index)
        {
            auto& entry = m_entries[index];
            if (entry.prev != size_type(-1))
            {
                m_entries[entry.prev].next = entry.next;
            }
            else
            {
                m_head = entry.next;
            }
            if (entry.next != size_type(-1))
            {
                m_entries[entry.next].prev = entry.prev;
            }
            else
            {
                m_tail = entry.prev;
            }
            entry.prev = size_type(-1);
            entry.next = size_type(-1);
        }

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

        bool key_less(const K& a, const K& b) const
        {
            if constexpr (std::is_same_v<K, const char*>)
            {
                return Helpers::secure_strcmp(a, b) < 0;
            }
            else
            {
                return a < b;
            }
        }

        void rehash(size_type new_capacity)
        {
            Array<Entry> old_entries = m_entries;
            size_type    old_head    = m_head;

            m_entries                = Array<Entry>{};
            m_entries.init(m_allocator, new_capacity);
            for (size_type i = 0; i < new_capacity; ++i)
            {
                m_entries.push({});
            }
            m_size        = 0;
            m_head        = size_type(-1);
            m_tail        = size_type(-1);

            size_type cur = old_head;
            while (cur != size_type(-1))
            {
                size_type old_next = old_entries[cur].next;
                size_type index    = probe_for_insert(old_entries[cur].key);
                auto&     entry    = m_entries[index];
                entry.key          = old_entries[cur].key;
                entry.state        = EntryState::Occupied;
                link_tail(index);
                ++m_size;
                cur = old_next;
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
            throw std::runtime_error("HashSet probe failed: table full");
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
        size_type               m_head        = size_type(-1);
        size_type               m_tail        = size_type(-1);
    };
} // namespace ZEngine::Core::Containers
