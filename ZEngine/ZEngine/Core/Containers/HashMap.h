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
        using ArrayType         = std::conditional_t<IsConst, const Array<Entry>, Array<Entry>>;
        using value_type        = std::conditional_t<IsConst, std::pair<const K&, const V&>, std::pair<const K&, V&>>;
        using reference         = value_type;
        using pointer           = value_type*;
        using iterator_category = std::forward_iterator_tag;
        using difference_type   = std::ptrdiff_t;

        // Constructs an iterator for the hash map's entries, starting at the given index.
        // @param entries Reference to the array of hash map entries.
        // @param index Starting index for iteration.
        HashMapIterator(ArrayType& entries, std::size_t index) : m_entries(entries), m_index(index)
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
            static value_type temp = **this;
            return &temp;
        }

    private:
        // Advances the iterator to the next occupied entry, skipping empty or deleted entries.
        void advance_to_valid()
        {
            while (m_index < m_entries.size() && m_entries[m_index].state != EntryState::Occupied)
            {
                ++m_index;
            }
        }

        ArrayType&  m_entries;
        std::size_t m_index;
    };

    template <typename K, typename V>
    class HashMap
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
        void init(Memory::ArenaAllocator* allocator, size_type initial_capacity = 32)
        {
            m_allocator   = allocator;
            m_load_factor = 0.75f;
            m_entries.init(m_allocator, initial_capacity, 0);
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

            if (entry.state == EntryState::Occupied && entry.key == key)
            {
                entry.value = value; // Update existing key
                return;
            }

            if (entry.state == EntryState::Empty || entry.state == EntryState::Deleted)
            {
                entry.key   = key;
                entry.value = value;
                entry.state = EntryState::Occupied;
                ++m_size;
            }
            else
            {
                throw std::runtime_error("HashMap insert failed: table full");
            }
        }

        // Accesses or inserts a value for a key, returning a reference to the value.
        // Inserts a default-constructed value if the key is not found.
        // @param key The key to access or insert.
        // @return Reference to the associated value.
        // @throws std::runtime_error if the table is full and cannot be resized.
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
            else if (entry.state == EntryState::Occupied && entry.key != key)
            {
                throw std::runtime_error("HashMap insert failed: table full");
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
            return iterator(m_entries, 0);
        }

        // Returns an iterator to the end of the hash map.
        // @return Iterator representing the past-the-end position.
        iterator end()
        {
            return iterator(m_entries, m_entries.size());
        }

        // Returns a const iterator to the first occupied entry.
        // @return Const iterator pointing to the first key-value pair or end() if empty.
        // @note Iterators are invalidated by insert, remove, or reserve operations.
        const_iterator begin() const
        {
            return const_iterator(m_entries, 0);
        }

        // Returns a const iterator to the end of the hash map.
        // @return Const iterator representing the past-the-end position.
        const_iterator end() const
        {
            return const_iterator(m_entries, m_entries.size());
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
                size_type new_capacity = std::max<size_type>(16, m_entries.size() * 3 / 2); // Growth factor 1.5
                rehash(new_capacity);
            }
        }

        // Rehashes the hash map to a new capacity, reinserting all occupied entries.
        // @param new_capacity The new number of slots.
        // @note Moves the old entries to avoid copying and skips Deleted entries.
        void rehash(size_type new_capacity)
        {
            Array<Entry> old_entries = m_entries; // Move to avoid copying
            m_entries                = Array<Entry>{};
            m_entries.init(m_allocator, new_capacity, 0);
            for (size_type i = 0; i < new_capacity; ++i)
            {
                m_entries.push({});
            }
            m_size = 0;

            for (const auto& entry : old_entries)
            {
                if (entry.state == EntryState::Occupied)
                {
                    insert(entry.key, entry.value);
                }
            }
        }

        // Probes for a key using quadratic probing.
        // @param key The key to look up.
        // @return Index of the key if found, or size_type(-1) if not found.
        size_type probe_for_key(const K& key) const
        {
            size_type index = hash(key) % m_entries.size();
            size_type step  = double_hash(key);
            size_type start = index;
            size_type i     = 0;

            do
            {
                const auto& entry = m_entries[index];
                if (entry.state == EntryState::Empty)
                {
                    return size_type(-1);
                }
                if (entry.state == EntryState::Occupied && entry.key == key)
                {
                    return index;
                }
                index = (index + step) % m_entries.size();
                ++i;
            } while (index != start && i < m_entries.size());

            return size_type(-1);
        }

        // Probes for a slot to insert a key, preferring deleted slots if available.
        // @param key The key to insert.
        // @return Index of the slot to use for insertion or the existing key.
        // @throws std::runtime_error if the table is full and no slot is found.
        size_type probe_for_insert(const K& key)
        {
            size_type index         = hash(key) % m_entries.size();
            size_type step          = double_hash(key);
            size_type start         = index;
            size_type first_deleted = size_type(-1);
            size_type i             = 0;

            do
            {
                auto& entry = m_entries[index];
                if (entry.state == EntryState::Empty)
                {
                    return (first_deleted != size_type(-1)) ? first_deleted : index;
                }
                if (entry.state == EntryState::Deleted && first_deleted == size_type(-1))
                {
                    first_deleted = index;
                }
                else if (entry.state == EntryState::Occupied && entry.key == key)
                {
                    return index;
                }
                index = (index + step) % m_entries.size();
                ++i;
            } while (index != start && i < m_entries.size());

            if (first_deleted != size_type(-1))
            {
                return first_deleted;
            }

            // Debug table state on failure
            size_type empty_count = 0, deleted_count = 0, occupied_count = 0;
            for (const auto& entry : m_entries)
            {
                if (entry.state == EntryState::Empty)
                    ++empty_count;
                else if (entry.state == EntryState::Deleted)
                    ++deleted_count;
                else if (entry.state == EntryState::Occupied)
                    ++occupied_count;
            }

            throw std::runtime_error("HashMap probe failed: table full");
        }

        // Computes the hash value for a key using the provided hasher.
        // @param key The key to hash.
        // @return The hash value.
        size_type hash(const K& key) const
        {
            return rapidhash(&key, sizeof(key));
        }

        // Computes a secondary hash for double hashing to determine probe step size.
        // @param key The key to hash.
        // @return A non-zero step size for probing.
        size_type double_hash(const K& key) const
        {
            size_type h = hash(key);
            // Ensure step is non-zero and relatively prime to capacity
            return (h % m_entries.size()) | 1; // Odd step size
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