#pragma once
#include <Allocator.h>
#include <Array.h>
#include <MemoryOperations.h>
#include <rapidhash.h>

using namespace ZEngine::Core::Memory;

namespace ZEngine::Core::Containers
{
    template <typename K, typename V>
    struct HashMap
    {
        struct Node
        {
            K     key;
            V     value;
            Node* next = nullptr;
        };

        struct KeyValue
        {
            Node* head = nullptr;
        };

        void init(Memory::ArenaAllocator* allocator, size_t initial_capacity, size_t initial_size = 0U)
        {
            m_allocator = allocator;
            m_size      = initial_size;
            m_capacity  = 0;
            m_data      = nullptr;
            reserve(initial_capacity);
        }

        void insert(const K& key, const V& value)
        {
            if (m_size >= m_capacity * 0.75)
            {
                reserve(m_capacity ? m_capacity * 2 : 8);
            }

            size_t index   = hash(key);
            Node*  current = m_data[index].head;

            while (current)
            {
                if (current->key == key)
                {
                    current->value = value;
                    return;
                }
                current = current->next;
            }

            Node* new_node     = static_cast<Node*>(ZAlloc(m_allocator, sizeof(Node), ZAlignof(Node)));
            new_node->key      = key;
            new_node->value    = value;
            new_node->next     = m_data[index].head;
            m_data[index].head = new_node;

            ++m_size;
        }

        bool contains(const K& key)
        {
            return find(key) != nullptr;
        }

        V& operator[](const K& key)
        {
            size_t index   = hash(key);
            Node*  current = m_data[index].head;

            while (current)
            {
                if (current->key == key)
                {
                    return current->value;
                }
                current = current->next;
            }

            if (m_size >= m_capacity * 0.75)
            {
                reserve(m_capacity ? m_capacity * 2 : 8);
                index = hash(key);
            }

            Node* new_node     = static_cast<Node*>(ZAlloc(m_allocator, sizeof(Node), ZAlignof(Node)));
            new_node->key      = key;
            new_node->value    = V{};
            new_node->next     = m_data[index].head;
            m_data[index].head = new_node;

            ++m_size;
            return new_node->value;
        }

        V* find(const K& key)
        {
            size_t index   = hash(key);
            Node*  current = m_data[index].head;

            while (current)
            {
                if (current->key == key)
                {
                    return &current->value;
                }
                current = current->next;
            }
            return nullptr;
        }

        void remove(const K& key)
        {
            size_t index   = hash(key);
            Node*  current = m_data[index].head;
            Node*  prev    = nullptr;

            while (current)
            {
                if (current->key == key)
                {
                    if (prev)
                        prev->next = current->next;
                    else
                        m_data[index].head = current->next;
                    --m_size;
                    return;
                }
                prev    = current;
                current = current->next;
            }
        }

        ~HashMap()
        {
            clear();
            if (m_data)
            {
                m_data = nullptr;
            }
        }

        void clear()
        {
            for (size_t i = 0; i < m_capacity; ++i)
            {
                Node* current  = m_data[i].head;
                m_data[i].head = nullptr;
            }
            m_size = 0;
        }

        void reserve(size_t new_capacity)
        {
            KeyValue* old_data     = m_data;
            size_t    old_capacity = m_capacity;

            m_data                 = static_cast<KeyValue*>(ZAlloc(m_allocator, new_capacity * sizeof(KeyValue), ZAlignof(KeyValue)));
            Helpers::secure_memset(m_data, 0, new_capacity * sizeof(KeyValue), m_capacity);

            m_capacity = new_capacity;

            for (size_t i = 0; i < old_capacity; ++i)
            {
                Node* current = old_data[i].head;
                while (current)
                {
                    Node*  next            = current->next;

                    size_t new_index       = hash(current->key);
                    current->next          = m_data[new_index].head;
                    m_data[new_index].head = current;

                    current                = next;
                }
            }
        }

        size_t size() const
        {
            return m_size;
        }
        size_t capacity() const
        {
            return m_capacity;
        }
        bool empty() const
        {
            return m_size == 0;
        }

        size_t hash(const K& key) const
        {
            return rapidhash(&key, sizeof(K)) % m_capacity;
        }

        Memory::ArenaAllocator* m_allocator = nullptr;
        size_t                  m_size      = 0;
        size_t                  m_capacity  = 0;
        KeyValue*               m_data      = nullptr;
    };

} // namespace ZEngine::Core::Containers
