#pragma once
#include <Allocator.h>
#include <Array.h>
#include <rapidhash.h>


using namespace ZEngine::Core::Memory;

namespace ZEngine::Core::Containers
{
    enum class EntryState
    {
        Empty,
        Occupied,
        DELETED
    };

    template <typename K, typename V>
    struct HashEntry
    {
        K          key;
        V          value;
        EntryState state = EntryState::Empty;
    };

    template <typename K, typename V>
    struct HashMap
    {
        using Entry     = HashEntry<K, V>;
        using size_type = size_t;

        void init(Memory::ArenaAllocator* allocator, size_type initial_capacity, size_t initial_size = 0U)
        {
            m_allocator = allocator;
            m_entries.init(allocator, initial_capacity, initial_size);
            for (size_type i = 0; i < initial_capacity; ++i)
            {
                m_entries.push({});
            }
            m_size = initial_size;
        }

        void insert(const K& key, const V& value)
        {
            if ((m_size + 1) > m_entries.size() * 0.75)
            {
                rehash(m_entries.size() * 2);
            }

            size_type index = probe_for_insert(key);
            if (m_entries[index].state != EntryState::Occupied)
            {
                m_entries[index].key   = key;
                m_entries[index].value = value;
                m_entries[index].state = EntryState::Occupied;
                ++m_size;
            }

            m_entries[index].value = value;
        }

        V& operator[](const K& key)
        {
            if ((m_size + 1) > m_entries.size() * 0.75)
            {
                rehash(m_entries.size() * 2);
            }

            size_type index = probe_for_insert(key);
            if (m_entries[index].state != EntryState::Occupied)
            {
                m_entries[index].key   = key;
                m_entries[index].value = V{};
                m_entries[index].state = EntryState::Occupied;
                ++m_size;
            }

            return m_entries[index].value;
        }

        V* find(const K& key)
        {
            size_type index = probe_for_key(key);
            if (index != size_type(-1) && m_entries[index].state == EntryState::Occupied)
            {
                return &m_entries[index].value;
            }
            return nullptr;
        }

        void remove(const K& key)
        {
            size_type index = probe_for_key(key);
            if (index != size_type(-1) && m_entries[index].state == EntryState::Occupied)
            {
                m_entries[index].state = EntryState::DELETED;
                --m_size;
            }
        }

        bool contains(const K& key)
        {
            return find(key) != nullptr;
        }

        size_t count(const K& key)
        {
            size_type index = probe_for_key(key);
            return (index != size_type(-1) && m_entries[index].state == EntryState::Occupied) ? 1 : 0;
        }

        void clear()
        {
            for (auto& entry : m_entries)
            {
                entry.state = EntryState::Empty;
            }
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

    private:
        size_type probe_for_insert(const K& key)
        {
            size_type index = hash(key) % m_entries.size();
            size_type start = index;
            while (m_entries[index].state == EntryState::Occupied && !(m_entries[index].key == key))
            {
                index = (index + 1) % m_entries.size();
            }
            return index;
        }

        size_type probe_for_key(const K& key)
        {
            size_type index = hash(key) % m_entries.size();
            size_type start = index;
            while (m_entries[index].state != EntryState::Empty)
            {
                if (m_entries[index].state == EntryState::Occupied && m_entries[index].key == key)
                {
                    return index;
                }
                index = (index + 1) % m_entries.size();
                if (index == start)
                    break;
            }
            return size_type(-1);
        }

        void rehash(size_type new_capacity)
        {
            Array<Entry> old_entries = m_entries;
            m_entries.init(m_allocator, new_capacity);
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

        size_type hash(const K& key) const
        {
            return rapidhash(&key, sizeof(K)) % m_entries.size();
        }

    public:
        struct Iterator
        {
            struct KeyValuePair
            {
                const K& first;
                V&       second;

                KeyValuePair(const K& k, V& v) : first(k), second(v) {}
            };

            Iterator(Array<Entry>& entries, size_type index) : m_entries(entries), m_index(index)
            {
                advance_to_valid();
            }

            KeyValuePair operator*() const
            {
                return KeyValuePair(m_entries[m_index].key, m_entries[m_index].value);
            }

            Iterator& operator++()
            {
                ++m_index;
                advance_to_valid();
                return *this;
            }

            bool operator!=(const Iterator& other) const
            {
                return m_index != other.m_index;
            }

        private:
            void advance_to_valid()
            {
                while (m_index < m_entries.size() && m_entries[m_index].state != EntryState::Occupied)
                {
                    ++m_index;
                }
            }

            Array<Entry>& m_entries;
            size_type     m_index;
        };

        struct HashMapView
        {
            HashMapView(HashMap* map) : m_map(map) {}

            Iterator begin()
            {
                return Iterator(m_map->m_entries, 0);
            }

            Iterator end()
            {
                return Iterator(m_map->m_entries, m_map->m_entries.size());
            }

            HashMap* m_map;
        };

        HashMapView view()
        {
            return HashMapView(this);
        }

    private:
        Memory::ArenaAllocator* m_allocator = nullptr;
        Array<Entry>            m_entries;
        size_type               m_size = 0;
    };
} // namespace ZEngine::Core::Containers
