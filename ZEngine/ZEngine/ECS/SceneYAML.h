#pragma once
#include <ZEngine/Core/Containers/Array.h>
#include <ZEngine/Core/Memory/Allocator.h>
#include <ZEngine/Helpers/IntrusivePtr.h>
#include <uuid.h>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

namespace ZEngine::ECS::SceneYAML
{
    struct NodeType
    {
        enum value : uint8_t
        {
            Undefined,
            Null,
            Scalar,
            Sequence,
            Map
        };
    };
    struct EmitterStyle
    {
        enum value : uint8_t
        {
            Block,
            Flow
        };
    };

    class Storage;
    template <bool IsMap>
    class Range;

    /// A borrowed index into one document's arena-backed records. No per-node
    /// ownership/allocation. The Storage must outlive every node and range.
    class Node
    {
    public:
        Node() = default;
        NodeType::value Type() const;
        bool            IsDefined() const
        {
            return Type() != NodeType::Undefined;
        }
        bool IsNull() const
        {
            return Type() == NodeType::Null;
        }
        bool IsScalar() const
        {
            return Type() == NodeType::Scalar;
        }
        bool IsSequence() const
        {
            return Type() == NodeType::Sequence;
        }
        bool IsMap() const
        {
            return Type() == NodeType::Map;
        }
        size_t              size() const;
        std::string_view    Scalar() const;
        bool                Plain() const;
        EmitterStyle::value Style() const;
        Range<false>        Sequence() const;
        Range<true>         Mapping() const;
        Node                operator[](size_t index) const;
        Node                operator[](std::string_view key) const;
        Storage*            Owner() const
        {
            return m_owner;
        }
        void reset(const Node& node)
        {
            *this = node;
        }
        void reset()
        {
            *this = {};
        }

    private:
        friend class Storage;
        template <bool>
        friend class Range;
        Storage* m_owner = nullptr;
        uint32_t m_index = UINT32_MAX;
        Node(Storage* owner, uint32_t index) : m_owner(owner), m_index(index) {}
    };

    struct Field
    {
        std::string_view Key   = {};
        Node             Value = {};
    };

    /// Flat, trivially copyable records and links in checked engine Arrays.
    /// Reallocation never invalidates indices or copied scalar text. All values
    /// share one bounded owner, released as a unit with no recursive destruction.
    class Storage
    {
    public:
        Storage()                                    = default;
        Storage(const Storage&)                      = delete;
        Storage&           operator=(const Storage&) = delete;
        [[nodiscard]] bool Initialize(size_t byte_budget = ZMega(128), uint32_t max_nodes = 1048576, uint32_t max_items = 65536, uint32_t max_scalar = 4096);
        [[nodiscard]] bool Good() const
        {
            return m_good;
        }
        size_t BytesUsed() const
        {
            return m_arena.m_current_offset;
        }
        size_t ByteCapacity() const
        {
            return m_arena.m_total_size;
        }
        Core::Memory::ArenaAllocator* Allocator()
        {
            return &m_arena;
        }
        Node Destination()
        {
            return {this, UINT32_MAX};
        }
        Node               Make(NodeType::value type, EmitterStyle::value style = EmitterStyle::Block);
        Node               Scalar(std::string_view text, bool plain = false);
        Node               UInt(uint32_t value);
        Node               Float(float value);
        Node               Bool(bool value);
        Node               UUID(const uuids::uuid& value);
        [[nodiscard]] bool Set(Node map, std::string_view key, Node value);
        [[nodiscard]] bool AppendField(Node map, Node key, Node value);
        [[nodiscard]] bool Append(Node sequence, Node value);
        [[nodiscard]] bool UniqueKeys(Node map);
        void               Freeze()
        {
            m_frozen = true;
        }

        struct Checkpoint
        {
            Core::Memory::ArenaTemp Arena        = {};
            size_t                  Nodes        = 0;
            size_t                  NodeCapacity = 0;
            void*                   NodeData     = nullptr;
            size_t                  Links        = 0;
            size_t                  LinkCapacity = 0;
            void*                   LinkData     = nullptr;
            bool                    Good         = false;
        };
        Checkpoint Mark();
        void       Rollback(const Checkpoint& checkpoint);

    private:
        friend class Node;
        template <bool>
        friend class Range;
        struct Record
        {
            std::string_view    Text  = {};
            uint32_t            First = UINT32_MAX;
            uint32_t            Last  = UINT32_MAX;
            uint32_t            Count = 0;
            NodeType::value     Type  = NodeType::Undefined;
            EmitterStyle::value Style = EmitterStyle::Block;
            bool                Plain = false;
        };
        struct Link
        {
            uint32_t Key   = UINT32_MAX;
            uint32_t Value = UINT32_MAX;
            uint32_t Next  = UINT32_MAX;
        };
        Core::Memory::ArenaAllocator    m_arena      = {};
        Core::Containers::Array<Record> m_nodes      = {};
        Core::Containers::Array<Link>   m_links      = {};
        uint32_t                        m_max_nodes  = 0;
        uint32_t                        m_max_items  = 0;
        uint32_t                        m_max_scalar = 0;
        bool                            m_good       = false;
        bool                            m_frozen     = false;
        bool                            Owns(Node node) const;
        bool                            AddLink(Node parent, uint32_t key, Node value);
        bool                            Contains(Node node, Node target, uint32_t depth, uint32_t& visited) const;
        bool                            Fail()
        {
            m_good = false;
            return false;
        }
    };

    template <bool IsMap>
    class Range
    {
    public:
        class Iterator
        {
        public:
            auto operator*() const
            {
                const auto& link = m_owner->m_links[m_link];
                if constexpr (IsMap)
                {
                    return Field{m_owner->m_nodes[link.Key].Text, Node(m_owner, link.Value)};
                }
                else
                {
                    return Node(m_owner, link.Value);
                }
            }
            Iterator& operator++()
            {
                m_link = m_owner->m_links[m_link].Next;
                return *this;
            }
            bool operator==(const Iterator& other) const
            {
                return m_owner == other.m_owner && m_link == other.m_link;
            }
            bool operator!=(const Iterator& other) const
            {
                return !(*this == other);
            }

        private:
            friend class Range;
            Storage* m_owner = nullptr;
            uint32_t m_link  = UINT32_MAX;
            Iterator(Storage* owner, uint32_t link) : m_owner(owner), m_link(link) {}
        };
        Iterator begin() const
        {
            return {m_owner, m_first};
        }
        Iterator end() const
        {
            return {m_owner, UINT32_MAX};
        }
        size_t size() const
        {
            return m_size;
        }
        bool empty() const
        {
            return m_size == 0;
        }
        auto front() const
        {
            return *begin();
        }

    private:
        friend class Node;
        Storage* m_owner = nullptr;
        uint32_t m_first = UINT32_MAX;
        uint32_t m_size  = 0;
        Range(Storage* owner, uint32_t first, uint32_t size) : m_owner(owner), m_first(first), m_size(size) {}
    };

    enum Event : uint8_t
    {
        BeginMap,
        EndMap,
        BeginSeq,
        EndSeq,
        Key,
        Value
    };

    /// Bounded, status-returning libyaml event writer.
    class Emitter
    {
    public:
        explicit Emitter(size_t max_bytes);
        ~Emitter() noexcept;
        Emitter(const Emitter&)            = delete;
        Emitter& operator=(const Emitter&) = delete;
        Emitter& operator<<(Event event);
        Emitter& operator<<(const Node& node);
        Emitter& operator<<(std::string_view value);
        Emitter& operator<<(const std::string& value)
        {
            return *this << std::string_view(value);
        }
        Emitter& operator<<(const char* value)
        {
            return *this << std::string_view(value ? value : "");
        }
        Emitter&           operator<<(uint32_t value);
        bool               good() const;
        [[nodiscard]] bool Output(std::string* out);

    private:
        struct Impl;
        ZEngine::Helpers::Scope<Impl> m_impl;
    };
} // namespace ZEngine::ECS::SceneYAML
