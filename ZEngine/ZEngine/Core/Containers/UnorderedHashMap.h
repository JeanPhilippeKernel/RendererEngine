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

    template <typename K, typename V>
    struct HashEntry
    {
        K          key;
        V          value;
        EntryState state = EntryState::Empty;
    };

    template <typename K, typename V, bool IsConst>
    class HashMapIterator
    {
    public:
        using Entry             = HashEntry<K, V>;
        using EntryPointer      = std::conditional_t<IsConst, const Entry*, Entry*>;
        using value_type        = std::conditional_t<IsConst, std::pair<const K, const V>, std::pair<const K, V>>;
        using reference         = value_type;
        using pointer           = value_type*;
        using iterator_category = std::input_iterator_tag;
        using difference_type   = std::ptrdiff_t;

        // Constructs an iterator for the hash map's entries, starting at the given index.
        // @param entries Value to the array of hash map entries.
        // @param index Starting index for iteration.
        HashMapIterator(EntryPointer entries, std::size_t index, std::size_t size) : m_entries(entries), m_index(index), m_size(size)
        {
            advance_to_valid();
        }

        // Advances the iterator to the next occupied entry.
        // @return Reference to the incremented iterator.
        HashMapIterator& operator++()
        {
            ++m_index;
            advance_to_valid();
            return *this;
        }

        // Checks if two iterators are not equal based on their index.
        // @param other The iterator to compare with.
        // @return True if the iterators point to different indices, false otherwise.
        bool operator!=(const HashMapIterator& other) const
        {
            return m_index != other.m_index;
        }

        // Checks if two iterators are equal based on their index.
        // @param other The iterator to compare with.
        // @return True if the iterators point to the same index, false otherwise.
        bool operator==(const HashMapIterator& other) const
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
        // Advances the iterator to the next occupied entry, skipping empty or deleted entries.
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

    template <typename K, typename V>
    class UnorderedHashMap
    {
    public:
        using Entry          = HashEntry<K, V>;
        using size_type      = std::size_t;
        using iterator       = HashMapIterator<K, V, false>;
        using const_iterator = HashMapIterator<K, V, true>;

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
        }

        // Inserts a key-value pair into the hash map, updating the value if the key exists.
        // Resizes the map if the load factor would be exceeded.
        // @param key The key to insert.
        // @param value The value to associate with the key.
        // @throws std::runtime_error if the table is full and cannot be resized.
        void insert(const K& key, const V& value)
        {
            maybe_grow();

            size_type index = probe_for_insert(key);
            auto&     entry = m_entries[index];

            if (entry.state == EntryState::Empty || entry.state == EntryState::Deleted)
            {
                entry.key   = key;
                entry.value = value;
                entry.state = EntryState::Occupied;
                ++m_size;
            }
            else
            {
                entry.value = value;
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
                throw std::out_of_range("Key not found in UnorderedHashMap");
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

        // Removes a key-value pair from the hash map.
        // @param key The key to remove.
        // @note Marks the entry as Deleted; does not shrink the table.
        void remove(const K& key)
        {
            size_type index = probe_for_key(key);
            if (index != size_type(-1))
            {
                m_entries[index].state = EntryState::Deleted;
                --m_size;
            }
        }

        // Clears all entries in the hash map, resetting it to an empty state.
        // @note Sets all entries to Empty; does not change capacity.
        void clear()
        {
            for (auto& entry : m_entries)
            {
                entry.state = EntryState::Empty;
            }
            m_size = 0;
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
            return iterator(m_entries.data(), 0, m_entries.size());
        }

        // Returns an iterator to the end of the hash map.
        // @return Iterator representing the past-the-end position.
        iterator end()
        {
            return iterator(m_entries.data(), m_entries.size(), m_entries.size());
        }

        // Returns a const iterator to the first occupied entry.
        // @return Const iterator pointing to the first key-value pair or end() if empty.
        // @note Iterators are invalidated by insert, remove, or reserve operations.
        const_iterator begin() const
        {
            return const_iterator(m_entries.data(), 0, m_entries.size());
        }

        // Returns a const iterator to the end of the hash map.
        // @return Const iterator representing the past-the-end position.
        const_iterator end() const
        {
            return const_iterator(m_entries.data(), m_entries.size(), m_entries.size());
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
        // Checks if the hash map needs to grow based on the load factor and resizes if necessary.
        // @note Triggers rehashing if (m_size + 1) / capacity > load_factor.
        void maybe_grow()
        {
            if (static_cast<float>(m_size + 1) / m_entries.size() > m_load_factor)
            {
                size_type new_capacity = std::max<size_type>(16, static_cast<size_type>(m_entries.size() * 1.5f)); // Growth factor 1.5
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

        // Rehashes the hash map to a new capacity, reinserting all occupied entries.
        // @param new_capacity The new number of slots.
        // @note Moves the old entries to avoid copying and skips Deleted entries.
        void rehash(size_type new_capacity)
        {
            Array<Entry> old_entries = m_entries; // Move to avoid copying
            m_entries                = Array<Entry>{};
            m_entries.init(m_allocator, new_capacity);
            for (size_type i = 0; i < new_capacity; ++i)
            {
                m_entries.push({});
            }
            m_size = 0;

            for (size_type i = 0; i < old_entries.size(); ++i)
            {
                if (old_entries[i].state == EntryState::Occupied)
                {
                    size_type index  = probe_for_insert(old_entries[i].key);
                    m_entries[index] = old_entries[i]; // Direct assignment
                    ++m_size;
                }
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

            throw std::runtime_error("UnorderedHashMap probe failed: table full");
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
    };

    // Computes a hash value for a C-string using rapidhash.
    // @param str The null-terminated string to hash.
    // @return The hash value.
    inline uint64_t hash_compute(const char* str)
    {
        return rapidhash(str, Helpers::secure_strlen(str));
    }
} // namespace ZEngine::Core::Containers