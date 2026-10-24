#pragma once
#include <ZEngine/Core/VFS/IVFSContext.h>
#include <cstdint>

namespace Tetragrama::Panels
{
    /// @brief Bounded, append-only directory index. Indices stay valid until Invalidate().
    /// Listings are lazy outside search; search discovers at most listing_budget directories
    /// per frame. Query changes reuse listings, and idle frames do not refilter or access VFS.
    struct FolderTreeCache
    {
        static constexpr uint32_t Capacity = 4096;
        static constexpr uint32_t None     = UINT32_MAX;
        struct Node
        {
            char     Path[512]   = {};
            uint32_t Parent      = None;
            uint32_t FirstChild  = None;
            uint32_t LastChild   = None;
            uint32_t NextSibling = None;
            uint32_t PrevSibling = None;
            uint16_t NameOffset  = 0;
            uint16_t Depth       = 0;
            bool     Loaded      = false;
            bool     Visible     = true;
        };

        Node     Nodes[Capacity] = {};
        uint32_t Count           = 0;
        bool     Incomplete      = false;

        void     Invalidate();
        void     LoadChildren(ZEngine::Core::VFS::IVFSContext& vfs, ZEngine::Core::Memory::ArenaAllocator& scratch, uint32_t parent);
        void     UpdateSearch(ZEngine::Core::VFS::IVFSContext& vfs, ZEngine::Core::Memory::ArenaAllocator& scratch, const char* query, uint32_t listing_budget = 16);
        bool     Searching() const
        {
            return m_query[0] && m_scan < Count;
        }

    private:
        char     m_query[128]     = {};
        uint32_t m_scan           = 0;
        uint32_t m_filtered_count = None;
    };
} // namespace Tetragrama::Panels
