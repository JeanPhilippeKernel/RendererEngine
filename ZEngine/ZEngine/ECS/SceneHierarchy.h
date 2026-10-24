#pragma once
#include <ZEngine/Core/Memory/Allocator.h>
#include <ZEngine/ECS/EntityRegistry.h>
#include <span>

namespace ZEngine::ECS
{
    class Scene;

    /// @brief Cached forest topology and derived-frame revisions. Main-thread owned.
    /// No ECS pointers survive synchronization; component storage may move independently.
    class SceneHierarchy
    {
    public:
        static constexpr uint32_t NoIndex = UINT32_MAX;

        struct Node
        {
            EntityID Entity          = {};
            EntityID Parent          = {};
            uint64_t WorldRevision   = 0;
            uint32_t ParentIndex     = NoIndex;
            uint32_t ParentTransform = NoIndex;
            uint32_t FirstChild      = NoIndex;
            uint32_t NextSibling     = NoIndex;
            bool     HasTransform    = false;
            bool     TopologyValid   = false;
            bool     FrameValid      = false;
            bool     Ordered         = false;
        };

        [[nodiscard]] bool     Initialize(Core::Memory::ArenaAllocator* arena);
        /// @brief Grow bounded workspace before publishing entity creation, never during frame sync.
        [[nodiscard]] bool     Reserve(uint32_t entities);
        /// @brief Conservative arena bytes for Reserve; requests must not exceed MAX_ENTITIES.
        [[nodiscard]] size_t   ReservationMemoryBound(uint32_t entities) const;
        void                   Shutdown();
        void                   Invalidate();
        void                   Synchronize(Scene& scene);

        [[nodiscard]] uint64_t Revision() const
        {
            return m_revision;
        }
        [[nodiscard]] uint64_t BuildCount() const
        {
            return m_build_count;
        }
        [[nodiscard]] const Node*           Find(EntityID entity) const;
        [[nodiscard]] uint64_t              ParentWorldRevision(EntityID entity) const;
        [[nodiscard]] std::span<const Node> Nodes() const
        {
            return m_dirty ? std::span<const Node>{} : std::span<const Node>{m_nodes, m_count};
        }
        [[nodiscard]] std::span<const uint32_t> Order() const
        {
            return m_dirty ? std::span<const uint32_t>{} : std::span<const uint32_t>{m_order, m_count};
        }

    private:
        [[nodiscard]] uint32_t        IndexOf(EntityID entity) const;
        void                          RefreshTopology(Scene& scene);
        void                          BuildOrder();

        Node*                         m_nodes               = nullptr;
        uint32_t*                     m_lookup              = nullptr;
        uint32_t*                     m_order               = nullptr;
        Core::Memory::ArenaAllocator* m_arena               = nullptr;
        uint32_t                      m_capacity            = 0;
        uint32_t                      m_count               = 0;
        uint64_t                      m_revision            = 0;
        uint64_t                      m_build_count         = 0;
        uint64_t                      m_world_revision      = 0;
        uint64_t                      m_structural_revision = UINT64_MAX;
        bool                          m_dirty               = true;
    };
} // namespace ZEngine::ECS
