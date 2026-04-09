#pragma once
#include <HashMap.h>

namespace ZEngine::Core::Containers
{
    template <typename K, typename MapIt>
    class HashSetKeyIterator
    {
    public:
        using value_type        = K;
        using reference         = const K&;
        using pointer           = const K*;
        using iterator_category = std::forward_iterator_tag;
        using difference_type   = std::ptrdiff_t;

        explicit HashSetKeyIterator(MapIt it) : m_it(it) {}

        // Advances the iterator to the next entry in insertion order.
        // @return Reference to the incremented iterator.
        HashSetKeyIterator& operator++()
        {
            ++m_it;
            return *this;
        }

        // Checks if two iterators are not equal based on their index.
        // @param other The iterator to compare with.
        // @return True if the iterators point to different indices, false otherwise.
        bool operator==(const HashSetKeyIterator& other) const
        {
            return m_it == other.m_it;
        }

        // Checks if two iterators are equal based on their index.
        // @param other The iterator to compare with.
        // @return True if the iterators point to the same index, false otherwise.
        bool operator!=(const HashSetKeyIterator& other) const
        {
            return m_it != other.m_it;
        }

        // Dereferences the iterator to return a const reference to the current key.
        // @return Const reference to the key at the current position.
        const K& operator*() const
        {
            return m_it.key();
        }

        // Provides pointer-like access to the current key.
        // @return Const pointer to the key at the current position.
        const K* operator->() const
        {
            return &m_it.key();
        }

    private:
        MapIt m_it;
    };

    template <typename K>
    class HashSet
    {
    public:
        using MapType        = HashMap<K, bool>;
        using size_type      = std::size_t;
        using iterator       = HashSetKeyIterator<K, typename MapType::iterator>;
        using const_iterator = HashSetKeyIterator<K, typename MapType::const_iterator>;

        // @param allocator        Arena allocator for memory management.
        // @param initial_capacity Initial number of slots (default: 16).
        void init(Memory::ArenaAllocator* allocator, size_type initial_capacity = 16)
        {
            m_map.init(allocator, initial_capacity);
        }

        // Inserts a key into the set.
        // New keys are appended to the end of the insertion-order sequence.
        // Resizes the set if the load factor would be exceeded.
        // @param key The key to insert.
        void insert(const K& key)
        {
            m_map.insert(key, true);
        }

        // Finds a key in the set.
        // @param key The key to look up.
        // @return Const pointer to the stored key if found, nullptr otherwise.
        const K* find(const K& key) const
        {
            return m_map.find_key(key);
        }

        // Checks if a key exists in the hash set.
        // @param key The key to check.
        // @return True if the key exists, false otherwise.
        bool contains(const K& key) const
        {
            return m_map.contains(key);
        }

        // Removes a key from the set, preserving insertion order of remaining keys.
        // @param key The key to remove.
        // @note Marks the slot as Deleted.
        void remove(const K& key)
        {
            m_map.remove(key);
        }

        // Clears all entries, resetting insertion-order state without changing capacity.
        // @note Sets all entries to Empty; does not change capacity.
        void clear()
        {
            m_map.clear();
        }

        // Reorders the iteration sequence so keys are visited in ascending order (operator<).
        // For const char* keys, strcmp ordering is used.
        // Does not affect slot positions or lookup performance — only the linked-list order.
        void sort_keys()
        {
            m_map.sort_keys();
        }

        // Checks if the hash set is empty.
        // @return True if the set contains no keys, false otherwise.
        bool empty() const
        {
            return m_map.empty();
        }

        // Returns the number of keys in the hash set.
        // @return The number of occupied entries.
        size_type size() const
        {
            return m_map.size();
        }

        // Returns the current capacity of the hash set.
        // @return The number of slots in the underlying array.
        size_type capacity() const
        {
            return m_map.capacity();
        }

        // Returns an iterator to the first entry in the current iteration order.
        // @note Iterators are invalidated by insert, remove, or reserve operations.
        iterator begin()
        {
            return iterator(m_map.begin());
        }

        // Returns an iterator to the end of the hash set.
        // @return Iterator representing the past-the-end position.
        iterator end()
        {
            return iterator(m_map.end());
        }

        // Returns a const iterator to the first entry in the current iteration order.
        // @note Iterators are invalidated by insert, remove, or reserve operations.
        const_iterator begin() const
        {
            return const_iterator(m_map.begin());
        }

        // Returns a const iterator to the end of the hash set.
        // @return Const iterator representing the past-the-end position.
        const_iterator end() const
        {
            return const_iterator(m_map.end());
        }

        // Returns a const iterator to the first entry (alias for begin() const).
        const_iterator cbegin() const
        {
            return begin();
        }

        // Returns a const iterator to the end (alias for end() const).
        const_iterator cend() const
        {
            return end();
        }

        // Ensures the hash set has at least the specified capacity.
        // @param new_capacity Desired minimum number of slots.
        // @note Rehashes the set if the new capacity is greater than the current capacity.
        void reserve(size_type n)
        {
            m_map.reserve(n);
        }

    private:
        MapType m_map;
    };
} // namespace ZEngine::Core::Containers
