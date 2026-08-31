#pragma once
#include <Tetragrama/Components/ZUI/ZUIComponent.h>
#include <ZEngine/Core/Memory/Allocator.h>
#include <ZEngine/Core/VFS/VFSPath.h>

namespace Tetragrama::Components
{
    /// @brief Legacy floating project-view panel (pre-ZUIPanelManager path).
    ///        Lists VFS entries with lazy per-directory caching, drag-source for
    ///        asset files, navigation, and a context-menu import trigger.
    ///        Superseded by ProjectViewPanel but kept as the legacy code path.
    class ZUIProjectViewComponent : public ZUIComponent
    {
    public:
        ZUIProjectViewComponent()           = default;
        ~ZUIProjectViewComponent() override = default;

        /// @brief Allocates the entry arena and stores parent/name/visibility.
        /// @param parent     Owning ZUI layer.
        /// @param name       Component name.
        /// @param visibility Initial visibility.
        void Initialize(Tetragrama::Layers::ZUILayer* parent, cstring name = "Project", bool visibility = true) override;

        /// @brief Builds the floating project-view panel for this frame.
        /// @param ctx ZUI context for the current frame.
        void BuildUI(ZEngine::UI::ZUIContext* ctx) override;

    private:
        struct CachedEntry
        {
            char name[256]      = {};
            char full_path[512] = {};
            bool is_dir         = false;
        };

        static constexpr uint32_t             kMaxEntries       = 256;

        ZEngine::Core::Memory::ArenaAllocator m_arena           = {};
        ZEngine::Core::VFS::VFSPath           m_current_path    = {};
        ZEngine::Core::VFS::VFSPath           m_listed_path     = {};
        CachedEntry*                          m_entries         = nullptr;
        uint32_t                              m_entry_count     = 0;
        bool                                  m_initialized     = false;
        char                                  m_search_buf[256] = {};

        // Re-lists only when m_current_path != m_listed_path — NOT every frame
        void                                  RefreshIfNeeded();

    public:
        char PendingImportPath[512] = {};
        bool ShowImporter           = false;
    };
    ZDEFINE_PTR(ZUIProjectViewComponent);
} // namespace Tetragrama::Components
