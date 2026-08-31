#pragma once
#include <Tetragrama/Layers/ZUILayer.h>
#include <ZEngine/Core/VFS/IVFSBackend.h>
#include <ZEngine/Core/VFS/IVFSContext.h>
#include <ZEngine/Core/VFS/VFSPath.h>
#include <ZEngine/UI/ZUIPanel.h>
#include <cstdint>

namespace Tetragrama::Panels
{
    // ProjectViewPanel
    // Three-column content browser — matches UE5 Content Browser layout:
    //   [Sources tree | Filters | Search + Icon grid]

    /// @brief Three-column content browser panel.  Displays the VFS as a sources
    ///        tree (left), a type filter list (middle), and an icon grid (right).
    ///        Supports create/rename/delete file operations via inline modals, a
    ///        search bar, drag-source for assets, and double-click navigation.
    struct ProjectViewPanel : ZEngine::UI::ZUIPanelView
    {
        ProjectViewPanel();

        Tetragrama::Layers::ZUILayer* m_layer = nullptr;

        /// @brief Builds the full content-browser UI for this frame.
        /// @param ctx ZUI context for the current frame.
        /// @param rect Panel bounding rect [x0, y0, x1, y1].
        void BuildContent(ZEngine::UI::ZUIContext* ctx, float rect[4]) override;

    private:
        // Navigation
        ZEngine::Core::VFS::VFSPath m_current_dir = {};
        bool                        m_root_init = false;

        // Grid listing cache
        struct Entry
        {
            char name[256]      = {};
            char full_path[512] = {};
            bool is_dir         = false;
        };
        static constexpr int kMaxEntries = 512;
        Entry                m_entries[kMaxEntries] = {};
        int                  m_nentries             = 0;
        bool                 m_needs_refresh        = true;

        // Sources tree per-directory listing cache
        // Avoids live vfs->List() calls every frame per open tree node.
        // Invalidated alongside the grid listing on m_needs_refresh.
        struct TreeDirEntry
        {
            char name[256]      = {};
            char full_path[512] = {};
        };
        static constexpr int kMaxTreeEntries = 128; // subdirs per cached dir
        static constexpr int kMaxCachedDirs  = 48;
        struct TreeDirCache
        {
            char         key[512]                 = {};
            TreeDirEntry entries[kMaxTreeEntries] = {};
            int          count                    = 0;
            bool         valid                    = false;
        };
        TreeDirCache m_tree_cache[kMaxCachedDirs] = {};
        int          m_tree_cache_count           = 0;

        // Search + type filter
        char m_search[256]     = {};
        char m_type_filter[32] = "All";

        // Selection + double-click
        char  m_selected_path[512]   = {};
        char  m_last_click_path[512] = {};
        float m_last_click_time      = -1.f;

        // Modals
        enum class Modal
        {
            None         = 0, ///<  No modal active.
            CreateFile   = 1, ///<  Create-file dialog.
            CreateFolder = 2, ///<  Create-folder dialog.
            RenameItem   = 3, ///<  Rename dialog.
            DeleteItem   = 4, ///<  Delete confirmation.
        };
        Modal                       m_modal        = Modal::None;
        ZEngine::Core::VFS::VFSPath m_modal_target = {};
        bool                        m_modal_is_dir = false;
        char                        m_modal_buf[512]   = {};
        char                        m_modal_error[128] = {}; // non-empty = show error
        bool                        m_modal_opened     = false;

        // Helpers
        /// @brief Refresh the current directory file listing.
        void RefreshListing(ZEngine::Core::VFS::IVFSContext* vfs);
        /// @brief Draw the path breadcrumb navigation bar.
        void DrawBreadcrumb(ZEngine::UI::ZUIContext* ctx, float fh);
        /// @brief Draw the left-side sources/bookmarks tree.
        void DrawSourcesTree(ZEngine::UI::ZUIContext* ctx,
                             ZEngine::Core::VFS::IVFSContext* vfs);
        /// @brief Draw the file-type filter toolbar.
        void DrawFilters(ZEngine::UI::ZUIContext* ctx);
        /// @brief Draw the main file grid content area.
        void DrawGrid(ZEngine::UI::ZUIContext* ctx, float pw);
        /// @brief Draw any open modal dialogs (rename, delete, etc.).
        void DrawModals(ZEngine::UI::ZUIContext* ctx,
                        ZEngine::Core::VFS::IVFSContext* vfs);

        /// @brief Return (and cache) the subdirectory list for a given path.
        const TreeDirEntry* GetCachedSubdirs(ZEngine::Core::VFS::IVFSContext* vfs,
                                             const ZEngine::Core::VFS::VFSPath& dir,
                                             int* out_count);

        /// @brief Returns the icon color for a file given its name/extension.
        /// @param name File name (may include path).
        /// @returns Pointer to a 4-float RGBA color array.
        static const float* ExtColor(const char* name);

        /// @brief Returns the type-filter category string for a file name.
        /// @param name File name (may include path).
        /// @returns Category string such as "Textures", "Models", "Scripts", etc.
        static const char*  TypeCategory(const char* name);

        /// @brief Returns true if @p e passes both the type filter and search filter.
        /// @param e           Entry to test.
        /// @param search      Search string (case-insensitive substring match).
        /// @param type_filter Active type-filter category, or "All".
        /// @returns True when the entry should be visible.
        static bool         PassesFilters(const Entry& e, const char* search, const char* type_filter);
    };

} // namespace Tetragrama::Panels
