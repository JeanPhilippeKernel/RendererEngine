#pragma once
#include <ZEngine/Core/Containers/Array.h>
#include <ZEngine/Core/Memory/Allocator.h>
#include <ZEngine/Helpers/MemoryOperations.h>
#include <ZEngine/ZEngineDef.h>
#include <rapidhash.h>
#include <cstddef>
#include <stdexcept>
#include <type_traits>
#include <utility>

// Open-addressing hash map backed by Array<Entry>.
//
// Capacity contract:
//   - Always a power of 2. init() rounds the requested slot count up to the next
//     power of 2 (minimum 16) so (index + 1) & mask visits every slot.
//   - Pass at least 2× the number of entries you intend to insert so the load
//     stays ≤50% and rehash is never needed.
//   - Capacity is pre-allocated via Array::init(arena, cap, cap) — no push loop,
//     all slots are zeroed (EntryState::Empty = 0) and no Arena waste on grow.

namespace ZEngine::Core::Containers
{
    enum class EntryState : uint8_t
    {
        Empty    = 0,
        Occupied = 1,
        Deleted  = 2,
    };

    template <typename K, typename V>
    struct HashEntry
    {
        K          key   = {};
        V          value = {};
        EntryState state = EntryState::Empty;
    };

    template <typename K, typename V, bool IsConst>
    class HashMapIterator
    {
    public:
        using Entry             = HashEntry<K, V>;
        using EntryPointer      = std::conditional_t<IsConst, const Entry*, Entry*>;
        using value_type        = std::conditional_t<IsConst, std::pair<const K&, const V&>, std::pair<const K&, V&>>;
        using reference         = value_type;
        using pointer           = value_type*;
        using iterator_category = std::input_iterator_tag;
        using difference_type   = std::ptrdiff_t;

        HashMapIterator(EntryPointer entries, std::size_t index, std::size_t capacity) : m_entries(entries), m_index(index), m_capacity(capacity)
        {
            skip_to_occupied();
        }

        HashMapIterator& operator++()
        {
            ++m_index;
            skip_to_occupied();
            return *this;
        }

        bool operator!=(const HashMapIterator& o) const
        {
            return m_index != o.m_index;
        }
        bool operator==(const HashMapIterator& o) const
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

        const K& key() const
        {
            return m_entries[m_index].key;
        }

    private:
        void skip_to_occupied()
        {
            while (m_index < m_capacity && m_entries[m_index].state != EntryState::Occupied)
                ++m_index;
        }

        EntryPointer m_entries;
        std::size_t  m_index;
        std::size_t  m_capacity;
    };

    template <typename K, typename V>
    class UnorderedHashMap
    {
    public:
        using Entry          = HashEntry<K, V>;
        using size_type      = std::size_t;
        using iterator       = HashMapIterator<K, V, false>;
        using const_iterator = HashMapIterator<K, V, true>;

        // Pre-allocate capacity slots (rounded to next power of 2 ≥ 16).
        // Uses Array::init(arena, cap, cap) so the full block is reserved up-front;
        // all entries are zeroed — EntryState::Empty = 0 needs no explicit loop.
        void init(Memory::ArenaAllocator* arena, size_type slot_capacity = 16)
        {
            m_allocator     = arena;
            m_slab          = nullptr;
            size_type cap   = next_pow2(slot_capacity < 16 ? 16 : slot_capacity);
            m_capacity_mask = cap - 1;
            m_entries.init(arena, cap, cap);
            Helpers::secure_memset(m_entries.data(), 0, cap * sizeof(Entry), cap * sizeof(Entry));
            m_size = 0;
        }

        /// @brief Init backed by a TLSFSlab — rehash reallocates in-place when possible.
        void init(Memory::TLSFSlab* slab, size_type slot_capacity = 16)
        {
            m_allocator     = nullptr;
            m_slab          = slab;
            size_type cap   = next_pow2(slot_capacity < 16 ? 16 : slot_capacity);
            m_capacity_mask = cap - 1;
            m_entries.init(slab, cap, cap);
            Helpers::secure_memset(m_entries.data(), 0, cap * sizeof(Entry), cap * sizeof(Entry));
            m_size = 0;
        }

        void insert(const K& key, const V& value)
        {
            upsert(key, value);
        }
        void insert(const K& key, V&& value)
        {
            upsert(key, std::move(value));
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
                ++m_size;
            }
            return e.value;
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

        const V& at(const K& key) const
        {
            size_type i = probe_find(key);
            ZENGINE_VALIDATE_ASSERT(i != npos, "UnorderedHashMap::at: key not found")
            return m_entries[i].value;
        }

        void remove(const K& key)
        {
            size_type i = probe_find(key);
            if (i != npos)
            {
                m_entries[i].state = EntryState::Deleted;
                --m_size;
            }
        }

        void clear()
        {
            Helpers::secure_memset(m_entries.data(), 0, m_entries.size() * sizeof(Entry), m_entries.size() * sizeof(Entry));
            m_size = 0;
        }

        size_type size() const
        {
            return m_size;
        }
        size_type capacity() const
        {
            return m_entries.size();
        }
        bool empty() const
        {
            return m_size == 0;
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
            return {m_entries.data(), 0, capacity()};
        }
        iterator end()
        {
            return {m_entries.data(), capacity(), capacity()};
        }
        const_iterator begin() const
        {
            return {m_entries.data(), 0, capacity()};
        }
        const_iterator end() const
        {
            return {m_entries.data(), capacity(), capacity()};
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

        bool key_eq(const K& a, const K& b) const
        {
            if constexpr (std::is_same_v<K, const char*>)
                return Helpers::secure_strcmp(a, b) == 0;
            else
                return a == b;
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
                if (e.state == EntryState::Occupied && key_eq(e.key, key))
                    return idx;
                idx = (idx + 1) & m_capacity_mask;
            }
            return npos;
        }

        size_type probe_insert(const K& key)
        {
            ZENGINE_VALIDATE_ASSERT(!m_entries.empty(), "UnorderedHashMap: call init() before insert")
            size_type idx       = hash_of(key) & m_capacity_mask;
            size_type tombstone = npos;
            for (size_type i = 0; i <= m_capacity_mask; ++i)
            {
                Entry& e = m_entries[idx];
                if (e.state == EntryState::Occupied && key_eq(e.key, key))
                    return idx;
                if (e.state == EntryState::Empty)
                    return tombstone != npos ? tombstone : idx;
                if (e.state == EntryState::Deleted && tombstone == npos)
                    tombstone = idx;
                idx = (idx + 1) & m_capacity_mask;
            }
            if (tombstone != npos)
                return tombstone;
            ZENGINE_VALIDATE_ASSERT(false, "UnorderedHashMap: table full — pre-size with init(arena, 2*count)")
            return npos;
        }

        void guard_load()
        {
            if (m_entries.empty())
                return;
            ZENGINE_VALIDATE_ASSERT(static_cast<float>(m_size + 1) / static_cast<float>(capacity()) <= 0.75f, "UnorderedHashMap: load > 75% — call reserve() or increase init() capacity")
        }

        template <typename Val>
        void upsert(const K& key, Val&& val)
        {
            guard_load();
            size_type idx = probe_insert(key);
            if (idx == npos)
                return;
            Entry& e = m_entries[idx];
            if (e.state != EntryState::Occupied)
            {
                e.key   = key;
                e.state = EntryState::Occupied;
                ++m_size;
            }
            e.value = std::forward<Val>(val);
        }

        void rehash(size_type new_cap)
        {
            Array<Entry> old     = std::move(m_entries);
            size_type    old_cap = old.size();

            m_capacity_mask      = new_cap - 1;
            if (m_slab)
                m_entries.init(m_slab, new_cap, new_cap);
            else
                m_entries.init(m_allocator, new_cap, new_cap);
            Helpers::secure_memset(m_entries.data(), 0, new_cap * sizeof(Entry), new_cap * sizeof(Entry));
            m_size = 0;

            for (size_type i = 0; i < old_cap; ++i)
            {
                if (old[i].state == EntryState::Occupied)
                {
                    size_type idx  = probe_insert(old[i].key);
                    m_entries[idx] = std::move(old[i]);
                    ++m_size;
                }
            }
        }

        Memory::ArenaAllocator* m_allocator     = nullptr;
        Memory::TLSFSlab*       m_slab          = nullptr;
        Array<Entry>            m_entries       = {};
        size_type               m_capacity_mask = 0;
        size_type               m_size          = 0;
    };

    inline uint64_t hash_compute(const char* str)
    {
        return rapidhash(str, Helpers::secure_strlen(str));
    }

} // namespace ZEngine::Core::Containers
