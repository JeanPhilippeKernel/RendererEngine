#pragma once
#include <Tetragrama/Layers/ZUILayer.h>
#include <Tetragrama/Panels/FolderTreeCache.h>
#include <ZEngine/Core/VFS/IVFSBackend.h>
#include <ZEngine/Core/VFS/IVFSContext.h>
#include <ZEngine/Core/VFS/VFSPath.h>
#include <ZEngine/UI/ZUIPanel.h>
#include <cstdint>

namespace Tetragrama::Panels
{
    // ProjectViewPanel
    // Content browser: sources tree beside a filter/search toolbar and tile grid.

    /// @brief Displays the VFS as a sources tree and a filtered, virtualized tile grid.
    ///        Supports create/rename/delete file operations via inline modals, a
    ///        search bar, drag-source for assets, and double-click navigation.
    struct ProjectViewPanel : ZEngine::UI::ZUIPanelView
    {
        ProjectViewPanel();

        Tetragrama::Layers::ZUILayer* m_layer = nullptr;

        /// @brief Builds the full content-browser UI for this frame.
        /// @param ctx ZUI context for the current frame.
        /// @param rect Panel bounding rect [x0, y0, x1, y1].
        void                          BuildContent(ZEngine::UI::ZUIContext* ctx, float rect[4]) override;

        /// @brief Invalidates cached listings after a main-thread file operation.
        void                          RequestRefresh();

    private:
        friend struct ProjectViewPanelTestAccess;

        // Navigation
        ZEngine::Core::VFS::VFSPath m_current_dir                                                 = {};
        bool                        m_root_init                                                   = false;
        static constexpr uint32_t   kHistoryCapacity                                              = 64;
        char                        m_history[kHistoryCapacity][ZEngine::Core::VFS::VFS_MAX_PATH] = {};
        uint32_t                    m_history_start                                               = 0;
        uint32_t                    m_history_count                                               = 0;
        uint32_t                    m_history_cursor                                              = 0;
        bool                        m_reset_grid_scroll                                           = false;

        // Grid listing cache
        struct Entry
        {
            char name[ZEngine::Core::VFS::VFS_MAX_PATH]      = {};
            char full_path[ZEngine::Core::VFS::VFS_MAX_PATH] = {};
            bool is_dir                                      = false;
        };
        static constexpr int kMaxEntries                                         = 512;
        Entry                m_entries[kMaxEntries]                              = {};
        int                  m_nentries                                          = 0;
        bool                 m_needs_refresh                                     = true;
        bool                 m_listing_valid                                     = false;
        bool                 m_truncated                                         = false;
        float                m_thumbnail_scale                                   = 1.f;
        bool                 m_reset_grid_input                                  = false;

        FolderTreeCache      m_folder_tree                                       = {};

        // Search + type filter
        char                 m_search[256]                                       = {};
        char                 m_folder_search[128]                                = {};
        char                 m_type_filter[32]                                   = "All";

        // Selection + double-click
        char                 m_selected_path[ZEngine::Core::VFS::VFS_MAX_PATH]   = {};
        char                 m_last_click_path[ZEngine::Core::VFS::VFS_MAX_PATH] = {};
        float                m_last_click_time                                   = -1.f;

        // Modals
        enum class Modal
        {
            None         = 0, ///<  No modal active.
            CreateFile   = 1, ///<  Create-file dialog.
            CreateFolder = 2, ///<  Create-folder dialog.
            RenameItem   = 3, ///<  Rename dialog.
            DeleteItem   = 4, ///<  Delete confirmation.
        };
        Modal                       m_modal                                       = Modal::None;
        ZEngine::Core::VFS::VFSPath m_modal_target                                = {};
        bool                        m_modal_is_dir                                = false;
        char                        m_modal_buf[ZEngine::Core::VFS::VFS_MAX_PATH] = {};
        char                        m_modal_error[128]                            = {}; // non-empty = show error
        bool                        m_modal_opened                                = false;
        bool                        m_modal_input_pending                         = false;

        // Helpers
        /// @brief Navigate once, clearing stale selection/input and recording a bounded visit history.
        bool                        NavigateTo(const ZEngine::Core::VFS::VFSPath& path, bool record_history = true);
        bool                        NavigateHistory(int direction);
        void                        OpenItemModal(Modal modal, const ZEngine::Core::VFS::VFSPath& target, bool is_dir);
        void                        DrawCreateMenu(ZEngine::UI::ZUIContext* ctx, const ZEngine::Core::VFS::VFSPath& parent);
        void                        DrawItemMenu(ZEngine::UI::ZUIContext* ctx, const ZEngine::Core::VFS::VFSPath& target, bool is_dir);
        bool                        SubmitModal(ZEngine::Core::VFS::IVFSContext* vfs);
        void                        UpdateNavigationAfterMutation(const ZEngine::Core::VFS::VFSPath& source, const ZEngine::Core::VFS::VFSPath* destination);
        void                        DrawToolbar(ZEngine::UI::ZUIContext* ctx, float width);
        /// @brief Refresh the current directory file listing.
        void                        RefreshListing(ZEngine::Core::VFS::IVFSContext* vfs);
        /// @brief Copy and sort a listing once; frame builders only filter this bounded cache.
        void                        CacheListing(const ZEngine::Core::Containers::Array<ZEngine::Core::VFS::VFSDirEntry>& listing);
        static bool                 EntryLess(const Entry& a, const Entry& b);
        /// @brief Draw the path breadcrumb navigation bar.
        void                        DrawBreadcrumb(ZEngine::UI::ZUIContext* ctx, float width);
        /// @brief Draw the left-side sources/bookmarks tree.
        void                        DrawSourcesTree(ZEngine::UI::ZUIContext* ctx, ZEngine::Core::VFS::IVFSContext* vfs);
        /// @brief Draw the file-type filter toolbar.
        void                        DrawFilters(ZEngine::UI::ZUIContext* ctx);
        void                        DrawFilterControl(ZEngine::UI::ZUIContext* ctx);
        /// @brief Handle asset actions only when a browser control owns focus and no editor/overlay is active.
        void                        HandleShortcuts(ZEngine::UI::ZUIContext* ctx, const ZEngine::UI::ZUIBox* root);
        /// @brief Draw the centered empty-folder or filtered-no-results message.
        void                        DrawEmptyState(ZEngine::UI::ZUIContext* ctx, float width);
        /// @brief Draw visible tile rows and return the filtered item count.
        int                         DrawGrid(ZEngine::UI::ZUIContext* ctx, float pw, float height);
        void                        DrawStatus(ZEngine::UI::ZUIContext* ctx, int visible_count, float width);
        /// @brief Draw any open modal dialogs (rename, delete, etc.).
        void                        DrawModals(ZEngine::UI::ZUIContext* ctx, ZEngine::Core::VFS::IVFSContext* vfs);

        /// @brief Returns the icon color for a file given its name/extension.
        /// @param name File name (may include path).
        /// @returns Pointer to a 4-float RGBA color array.
        static const float*         ExtColor(const char* name);

        /// @brief Returns the type-filter category string for a file name.
        /// @param name File name (may include path).
        /// @returns Category string such as "Textures", "Models", "Scripts", etc.
        static const char*          TypeCategory(const char* name);

        /// @brief Returns true if @p e passes both the type filter and search filter.
        /// @param e           Entry to test.
        /// @param search      Search string (case-insensitive substring match).
        /// @param type_filter Active type-filter category, or "All".
        /// @returns True when the entry should be visible.
        static bool                 PassesFilters(const Entry& e, const char* search, const char* type_filter);
    };

} // namespace Tetragrama::Panels
