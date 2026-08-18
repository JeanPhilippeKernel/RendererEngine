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

namespace ZEngine::Core::Containers
{
    enum class EntryState
    {
        Empty,
        Occupied,
        Deleted
    };

    template <typename K, typename V>
    struct OrderedHashEntry
    {
        K           key;
        V           value;
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
        using value_type        = std::conditional_t<IsConst, std::pair<const K, const V>, std::pair<const K, V>>;
        using reference         = value_type;
        using pointer           = value_type*;
        using iterator_category = std::forward_iterator_tag;
        using difference_type   = std::ptrdiff_t;

        // Constructs an iterator for the hash map's entries,
        // @param entries Pointer to the hash table slot array.
        // @param index   Current slot index;
        OrderedHashMapIterator(EntryPointer entries, std::size_t index) : m_entries(entries), m_index(index) {}
        OrderedHashMapIterator& operator++()
        {
            m_index = m_entries[m_index].next;
            return *this;
        }

        // Checks if two iterators are not equal based on their index.
        // @param other The iterator to compare with.
        // @return True if the iterators point to different indices, false otherwise.
        bool operator!=(const OrderedHashMapIterator& other) const
        {
            return m_index != other.m_index;
        }

        // Checks if two iterators are equal based on their index.
        // @param other The iterator to compare with.
        // @return True if the iterators point to the same index, false otherwise.
        bool operator==(const OrderedHashMapIterator& other) const
        {
            return m_index == other.m_index;
        }

        // Dereferences the iterator to return a key-value pair for the current entry.
        // @return A pair containing references to the key and value (const or non-const based on IsConst).
        value_type operator*() const
        {
            const auto& entry = m_entries[m_index];
            if constexpr (IsConst)
            {
                return {entry.key, entry.value};
            }
            else
            {
                return {entry.key, const_cast<V&>(entry.value)};
            }
        }

        // Provides pointer-like access to the current key-value pair.
        // @return A pointer to a temporary key-value pair.
        pointer operator->() const
        {
            return std::addressof(**this);
        }

        // Returns a const reference to the key at the current position.
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

        // Initializes the hash map with an allocator, initial capacity, and load factor.
        // @param allocator Pointer to the arena allocator for memory management.
        // @param initial_capacity Initial number of slots (default: 16).
        // @param load_factor Maximum load factor before resizing (default: 0.75).
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

        // Inserts a key-value pair into the hash map, updating the value if the key exists.
        // Resizes the map if the load factor would be exceeded.
        // @param key The key to insert.
        // @param value The value to associate with the key.
        // @throws std::runtime_error if the table is full and cannot be resized.
        void insert(const K& key, const V& value)
            requires std::is_copy_assignable_v<V>
        {
            maybe_grow();

            size_type index = probe_for_insert(key);
            auto&     entry = m_entries[index];

            if (entry.state == EntryState::Empty || entry.state == EntryState::Deleted)
            {
                entry.key   = key;
                entry.value = value;
                entry.state = EntryState::Occupied;
                link_tail(index);
                ++m_size;
            }
            else
            {
                entry.value = value;
            }
        }

        void insert(const K& key, V&& value)
        {
            maybe_grow();

            size_type index = probe_for_insert(key);
            auto&     entry = m_entries[index];

            if (entry.state == EntryState::Empty || entry.state == EntryState::Deleted)
            {
                entry.key   = key;
                entry.value = std::move(value);
                entry.state = EntryState::Occupied;
                link_tail(index);
                ++m_size;
            }
            else
            {
                entry.value = std::move(value);
            }
        }

        V& operator[](const K& key)
        {
            maybe_grow();

            size_type index = probe_for_insert(key);
            auto&     entry = m_entries[index];

            if (entry.state == EntryState::Empty || entry.state == EntryState::Deleted)
            {
                entry.key   = key;
                entry.value = V{};
                entry.state = EntryState::Occupied;
                link_tail(index);
                ++m_size;
            }
            return entry.value;
        }

        // Retrieves a const reference to the value associated with a key.
        // @param key The key to look up.
        // @return Const reference to the value.
        // @throws std::out_of_range if the key is not found.
        const V& at(const K& key) const
        {
            size_type index = probe_for_key(key);
            if (index == size_type(-1))
            {
                throw std::out_of_range("Key not found in HashMap");
            }
            return m_entries[index].value;
        }

        // Finds the value associated with a key.
        // @param key The key to look up.
        // @return Pointer to the value if found, nullptr otherwise.
        V* find(const K& key)
        {
            size_type index = probe_for_key(key);
            return (index != size_type(-1)) ? &m_entries[index].value : nullptr;
        }

        // Finds the value associated with a key (const version).
        // @param key The key to look up.
        // @return Const pointer to the value if found, nullptr otherwise.
        const V* find(const K& key) const
        {
            size_type index = probe_for_key(key);
            return (index != size_type(-1)) ? &m_entries[index].value : nullptr;
        }

        // Returns a pointer to the stored key if found, nullptr otherwise.
        // Useful for callers that need a stable pointer into the map's key storage.
        const K* find_key(const K& key) const
        {
            size_type index = probe_for_key(key);
            return (index != size_type(-1)) ? &m_entries[index].key : nullptr;
        }

        // Checks if a key exists in the hash map.
        // @param key The key to check.
        // @return True if the key exists, false otherwise.
        bool contains(const K& key) const
        {
            return find(key) != nullptr;
        }

        // Removes the entry for a key, preserving insertion order of remaining entries.
        // @note Marks the slot as Deleted; does not shrink the table.
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

        // Clears all entries, resetting insertion-order state without changing capacity.
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

            auto             scratch = ZGetScratch(m_allocator);

            Array<size_type> indices;
            indices.init(scratch.Arena, m_size);

            size_type cur = m_head;
            while (cur != size_type(-1))
            {
                indices.push(cur);
                cur = m_entries[cur].next;
            }

            // Sort indices by key.
            std::sort(indices.data(), indices.data() + indices.size(), [this](size_type a, size_type b) { return key_less(m_entries[a].key, m_entries[b].key); });

            // Rebuild linked list in the new order.
            m_head = indices[0];
            m_tail = indices[indices.size() - 1];

            for (size_type i = 0; i < indices.size(); ++i)
            {
                m_entries[indices[i]].prev = (i > 0) ? indices[i - 1] : size_type(-1);
                m_entries[indices[i]].next = (i + 1 < indices.size()) ? indices[i + 1] : size_type(-1);
            }

            ZReleaseScratch(scratch);
        }

        // Checks if the hash map is empty.
        // @return True if the map contains no key-value pairs, false otherwise.
        bool empty() const
        {
            return m_size == 0;
        }

        // Returns the number of key-value pairs in the hash map.
        // @return The number of occupied entries.
        size_type size() const
        {
            return m_size;
        }

        // Returns the current capacity of the hash map.
        // @return The number of slots in the underlying array.
        size_type capacity() const
        {
            return m_entries.size();
        }

        // Returns an iterator to the first occupied entry.
        // @return Iterator pointing to the first key-value pair or end() if empty.
        // @note Iterators are invalidated by insert, remove, or reserve operations.
        iterator begin()
        {
            return iterator(m_entries.data(), m_head);
        }

        // Returns an iterator to the end of the hash map.
        // @return Iterator representing the past-the-end position.
        iterator end()
        {
            return iterator(m_entries.data(), size_type(-1));
        }

        // Returns a const iterator to the first occupied entry.
        // @return Const iterator pointing to the first key-value pair or end() if empty.
        // @note Iterators are invalidated by insert, remove, or reserve operations.
        const_iterator begin() const
        {
            return const_iterator(m_entries.data(), m_head);
        }

        // Returns a const iterator to the end of the hash map.
        // @return Const iterator representing the past-the-end position.
        const_iterator end() const
        {
            return const_iterator(m_entries.data(), size_type(-1));
        }

        // Returns a const iterator to the first occupied entry (alias for begin() const).
        // @return Const iterator pointing to the first key-value pair or end() if empty.
        const_iterator cbegin() const
        {
            return begin();
        }

        // Returns a const iterator to the end of the hash map (alias for end() const).
        // @return Const iterator representing the past-the-end position.
        const_iterator cend() const
        {
            return end();
        }

        // Ensures the hash map has at least the specified capacity.
        // @param new_capacity Desired minimum number of slots.
        // @note Rehashes the map if the new capacity is greater than the current capacity.
        void reserve(size_type new_capacity)
        {
            if (new_capacity > m_entries.size())
            {
                rehash(new_capacity);
            }
        }

    private:
        // Appends slot index to the tail of the insertion-order linked list.
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

        // Removes slot index from the insertion-order linked list.
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

        // Compare keys, specialized for const char*
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

        // Rehashes the hash map to a new capacity, reinserting all occupied entries.
        // @param new_capacity The new number of slots.
        // @note Moves the old entries to avoid copying and skips Deleted entries.
        void rehash(size_type new_capacity)
        {
            Array<Entry> old_entries = std::move(m_entries);
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
                entry.key          = std::move(old_entries[cur].key);
                entry.value        = std::move(old_entries[cur].value);
                entry.state        = EntryState::Occupied;
                link_tail(index);
                ++m_size;
                cur = old_next;
            }
        }

        // Probes for a key using quadratic probing.
        // @param key The key to look up.
        // @return Index of the key if found, or size_type(-1) if not found.
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

        // Probes for a slot to insert a key, preferring deleted slots if available.
        // @param key The key to insert.
        // @return Index of the slot to use for insertion or the existing key.
        // @throws std::runtime_error if the table is full and no slot is found.
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

            throw std::runtime_error("HashMap probe failed: table full");
        }

        // Computes the hash value for a key using the provided hasher.
        // @param key The key to hash.
        // @return The hash value.
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

    // Computes a hash value for a C-string using rapidhash.
    // @param str The null-terminated string to hash.
    // @return The hash value.
    inline uint64_t hash_compute(const char* str)
    {
        return rapidhash(str, Helpers::secure_strlen(str));
    }
} // namespace ZEngine::Core::Containers
