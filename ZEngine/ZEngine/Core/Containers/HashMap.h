#pragma once
#include <ZEngine/Core/Containers/Array.h>
#include <ZEngine/Core/Memory/Allocator.h>
#include <ZEngine/Helpers/MemoryOperations.h>
#include <ZEngine/ZEngineDef.h>
#include <rapidhash.h>
#include <algorithm>
#include <cstddef>
#include <iterator>
#include <stdexcept>
#include <type_traits>
#include <utility>

// Insertion-ordered open-addressing hash map.
//
// Same capacity contract as UnorderedHashMap:
//   - Capacity always a power of 2; init() rounds up to next pow2 ≥ 16.
//   - Pass at least 2× expected entry count so load stays ≤50%.
//   - Linear probing with bitmask — visits every slot for any table size.
// Insertion order is preserved via a doubly-linked list threaded through
// the slot array (prev/next indices; size_type(-1) = end sentinel).

namespace ZEngine::Core::Containers
{
    // EntryState is also defined in UnorderedHashMap.h — keep both identical.
    enum class EntryState : uint8_t
    {
        Empty    = 0,
        Occupied = 1,
        Deleted  = 2
    };

    template <typename K, typename V>
    struct OrderedHashEntry
    {
        K           key   = {};
        V           value = {};
        EntryState  state = EntryState::Empty;
        std::size_t prev  = std::size_t(-1);
        std::size_t next  = std::size_t(-1);
    };

    template <typename K, typename V, bool IsConst>
    class OrderedHashMapIterator
    {
    public:
        using Entry             = OrderedHashEntry<K, V>;
        using EntryPointer      = std::conditional_t<IsConst, const Entry*, Entry*>;
        using value_type        = std::conditional_t<IsConst, std::pair<const K&, const V&>, std::pair<const K&, V&>>;
        using reference         = value_type;
        using pointer           = value_type*;
        using iterator_category = std::forward_iterator_tag;
        using difference_type   = std::ptrdiff_t;

        OrderedHashMapIterator(EntryPointer entries, std::size_t index) : m_entries(entries), m_index(index) {}

        OrderedHashMapIterator& operator++()
        {
            m_index = m_entries[m_index].next;
            return *this;
        }

        bool operator!=(const OrderedHashMapIterator& o) const
        {
            return m_index != o.m_index;
        }
        bool operator==(const OrderedHashMapIterator& o) const
        {
            return m_index == o.m_index;
        }

        value_type operator*() const
        {
            if constexpr (IsConst)
                return {m_entries[m_index].key, m_entries[m_index].value};
            else
                return {m_entries[m_index].key, const_cast<V&>(m_entries[m_index].value)};
        }

        pointer operator->() const
        {
            return std::addressof(**this);
        }
        const K& key() const
        {
            return m_entries[m_index].key;
        }

    private:
        EntryPointer m_entries;
        std::size_t  m_index;
    };

    template <typename K, typename V>
    class HashMap
    {
    public:
        using Entry          = OrderedHashEntry<K, V>;
        using size_type      = std::size_t;
        using iterator       = OrderedHashMapIterator<K, V, false>;
        using const_iterator = OrderedHashMapIterator<K, V, true>;

        // Pre-allocate capacity slots (rounded to next power of 2 ≥ 16).
        // Uses Array::init(arena, cap, cap) + per-slot default construction so
        // prev/next are correctly set to size_type(-1) — cannot use memset(0).
        void init(Memory::ArenaAllocator* allocator, size_type slot_capacity = 16)
        {
            m_allocator     = allocator;
            size_type cap   = next_pow2(slot_capacity < 16 ? 16 : slot_capacity);
            m_capacity_mask = cap - 1;
            m_entries.init(m_allocator, cap, cap);
            for (size_type i = 0; i < cap; ++i)
                m_entries[i] = Entry{};
            m_size = 0;
            m_head = size_type(-1);
            m_tail = size_type(-1);
        }

        void insert(const K& key, const V& value)
            requires std::is_copy_assignable_v<V>
        {
            guard_load();
            size_type idx = probe_insert(key);
            Entry&    e   = m_entries[idx];
            if (e.state != EntryState::Occupied)
            {
                e.key   = key;
                e.value = value;
                e.state = EntryState::Occupied;
                link_tail(idx);
                ++m_size;
            }
            else
            {
                e.value = value;
            }
        }

        void insert(const K& key, V&& value)
        {
            guard_load();
            size_type idx = probe_insert(key);
            Entry&    e   = m_entries[idx];
            if (e.state != EntryState::Occupied)
            {
                e.key   = key;
                e.value = std::move(value);
                e.state = EntryState::Occupied;
                link_tail(idx);
                ++m_size;
            }
            else
            {
                e.value = std::move(value);
            }
        }

        V& operator[](const K& key)
        {
            guard_load();
            size_type idx = probe_insert(key);
            Entry&    e   = m_entries[idx];
            if (e.state != EntryState::Occupied)
            {
                e.key   = key;
                e.value = V{};
                e.state = EntryState::Occupied;
                link_tail(idx);
                ++m_size;
            }
            return e.value;
        }

        const V& at(const K& key) const
        {
            size_type idx = probe_find(key);
            ZENGINE_VALIDATE_ASSERT(idx != npos, "HashMap::at: key not found")
            return m_entries[idx].value;
        }

        V* find(const K& key)
        {
            size_type i = probe_find(key);
            return i != npos ? &m_entries[i].value : nullptr;
        }
        const V* find(const K& key) const
        {
            size_type i = probe_find(key);
            return i != npos ? &m_entries[i].value : nullptr;
        }

        const K* find_key(const K& key) const
        {
            size_type i = probe_find(key);
            return i != npos ? &m_entries[i].key : nullptr;
        }

        bool contains(const K& key) const
        {
            return probe_find(key) != npos;
        }

        void remove(const K& key)
        {
            size_type idx = probe_find(key);
            if (idx != npos)
            {
                unlink(idx);
                m_entries[idx].state = EntryState::Deleted;
                --m_size;
            }
        }

        void clear()
        {
            size_type cap = capacity();
            for (size_type i = 0; i < cap; ++i)
                m_entries[i] = Entry{};
            m_size = 0;
            m_head = size_type(-1);
            m_tail = size_type(-1);
        }

        void sort_keys()
        {
            if (m_size < 2)
                return;

            auto             scratch = ZGetScratch(m_allocator);
            Array<size_type> indices;
            indices.init(scratch.Arena, m_size);

            for (size_type cur = m_head; cur != npos; cur = m_entries[cur].next)
                indices.push(cur);

            std::sort(indices.data(), indices.data() + indices.size(), [this](size_type a, size_type b) { return key_less(m_entries[a].key, m_entries[b].key); });

            m_head = indices[0];
            m_tail = indices[indices.size() - 1];
            for (size_type i = 0; i < indices.size(); ++i)
            {
                m_entries[indices[i]].prev = (i > 0) ? indices[i - 1] : npos;
                m_entries[indices[i]].next = (i + 1 < indices.size()) ? indices[i + 1] : npos;
            }

            ZReleaseScratch(scratch);
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

        // Grow to next_pow2(new_count) — allocates a new Array block, abandons old.
        // Call sparingly; pre-size correctly via init() to avoid this entirely.
        void reserve(size_type new_count)
        {
            size_type new_cap = next_pow2(new_count);
            if (new_cap > capacity())
                rehash(new_cap);
        }

        iterator begin()
        {
            return {m_entries.data(), m_head};
        }
        iterator end()
        {
            return {m_entries.data(), npos};
        }
        const_iterator begin() const
        {
            return {m_entries.data(), m_head};
        }
        const_iterator end() const
        {
            return {m_entries.data(), npos};
        }
        const_iterator cbegin() const
        {
            return begin();
        }
        const_iterator cend() const
        {
            return end();
        }

    private:
        static constexpr size_type npos = size_type(-1);

        static size_type           next_pow2(size_type n)
        {
            if (n == 0)
                return 1;
            --n;
            n |= n >> 1;
            n |= n >> 2;
            n |= n >> 4;
            n |= n >> 8;
            n |= n >> 16;
            if constexpr (sizeof(size_type) > 4)
                n |= n >> 32;
            return n + 1;
        }

        bool key_equals(const K& a, const K& b) const
        {
            if constexpr (std::is_same_v<K, const char*>)
                return Helpers::secure_strcmp(a, b) == 0;
            else
                return a == b;
        }

        bool key_less(const K& a, const K& b) const
        {
            if constexpr (std::is_same_v<K, const char*>)
                return Helpers::secure_strcmp(a, b) < 0;
            else
                return a < b;
        }

        size_type hash_of(const K& key) const
        {
            if constexpr (std::is_same_v<K, const char*>)
                return rapidhash(key, Helpers::secure_strlen(key));
            else
                return rapidhash(&key, sizeof(K));
        }

        size_type probe_find(const K& key) const
        {
            if (m_entries.empty())
                return npos;
            size_type idx = hash_of(key) & m_capacity_mask;
            for (size_type i = 0; i <= m_capacity_mask; ++i)
            {
                const Entry& e = m_entries[idx];
                if (e.state == EntryState::Empty)
                    return npos;
                if (e.state == EntryState::Occupied && key_equals(e.key, key))
                    return idx;
                idx = (idx + 1) & m_capacity_mask;
            }
            return npos;
        }

        size_type probe_insert(const K& key)
        {
            ZENGINE_VALIDATE_ASSERT(!m_entries.empty(), "HashMap: call init() before insert")
            size_type idx       = hash_of(key) & m_capacity_mask;
            size_type tombstone = npos;
            for (size_type i = 0; i <= m_capacity_mask; ++i)
            {
                Entry& e = m_entries[idx];
                if (e.state == EntryState::Occupied && key_equals(e.key, key))
                    return idx;
                if (e.state == EntryState::Empty)
                    return tombstone != npos ? tombstone : idx;
                if (e.state == EntryState::Deleted && tombstone == npos)
                    tombstone = idx;
                idx = (idx + 1) & m_capacity_mask;
            }
            if (tombstone != npos)
                return tombstone;
            ZENGINE_VALIDATE_ASSERT(false, "HashMap: table full — pre-size with init(arena, 2*count)")
            return npos;
        }

        void guard_load()
        {
            if (m_entries.empty())
                return;
            ZENGINE_VALIDATE_ASSERT(static_cast<float>(m_size + 1) / static_cast<float>(capacity()) <= 0.75f, "HashMap: load > 75% — call reserve() or increase init() capacity")
        }

        void link_tail(size_type idx)
        {
            m_entries[idx].prev = m_tail;
            m_entries[idx].next = npos;
            if (m_tail != npos)
                m_entries[m_tail].next = idx;
            else
                m_head = idx;
            m_tail = idx;
        }

        void unlink(size_type idx)
        {
            auto& e = m_entries[idx];
            if (e.prev != npos)
                m_entries[e.prev].next = e.next;
            else
                m_head = e.next;
            if (e.next != npos)
                m_entries[e.next].prev = e.prev;
            else
                m_tail = e.prev;
            e.prev = npos;
            e.next = npos;
        }

        void rehash(size_type new_cap)
        {
            Array<Entry> old      = std::move(m_entries);
            size_type    old_head = m_head;

            m_capacity_mask       = new_cap - 1;
            m_entries.init(m_allocator, new_cap, new_cap);
            for (size_type i = 0; i < new_cap; ++i)
                m_entries[i] = Entry{};
            m_size = 0;
            m_head = npos;
            m_tail = npos;

            for (size_type cur = old_head; cur != npos;)
            {
                size_type next       = old[cur].next;
                size_type idx        = probe_insert(old[cur].key);
                m_entries[idx].key   = std::move(old[cur].key);
                m_entries[idx].value = std::move(old[cur].value);
                m_entries[idx].state = EntryState::Occupied;
                link_tail(idx);
                ++m_size;
                cur = next;
            }
        }

        Memory::ArenaAllocator* m_allocator = nullptr;
        Array<Entry>            m_entries;
        size_type               m_capacity_mask = 0;
        size_type               m_size          = 0;
        size_type               m_head          = npos;
        size_type               m_tail          = npos;
    };

    inline uint64_t hash_compute(const char* str)
    {
        return rapidhash(str, Helpers::secure_strlen(str));
    }
} // namespace ZEngine::Core::Containers
