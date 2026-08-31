#pragma once
#include <Tetragrama/Layers/ZUILayer.h>
#include <ZEngine/Core/VFS/IVFSBackend.h>
#include <ZEngine/Core/VFS/IVFSContext.h>
#include <ZEngine/Core/VFS/VFSPath.h>
#include <ZEngine/UI/ZUIPanel.h>
#include <cstdint>

namespace Tetragrama::Panels
{
    // ── ProjectViewPanel ──────────────────────────────────────────────────────
    // Three-column content browser — matches UE5 Content Browser layout:
    //   [Sources tree | Filters | Search + Icon grid]
    //
    struct ProjectViewPanel : ZEngine::UI::ZUIPanelView
    {
        ProjectViewPanel();

        Tetragrama::Layers::ZUILayer* m_layer = nullptr;

        void BuildContent(ZEngine::UI::ZUIContext* ctx, float rect[4]) override;

    private:
        // ── Navigation ───────────────────────────────────────────────────────
        ZEngine::Core::VFS::VFSPath m_current_dir;
        bool                        m_root_init = false;

        // ── Grid listing cache ────────────────────────────────────────────────
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

        // ── Sources tree per-directory listing cache ──────────────────────────
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

        // ── Search + type filter ──────────────────────────────────────────────
        char m_search[256]     = {};
        char m_type_filter[32] = "All";

        // ── Selection + double-click ──────────────────────────────────────────
        char  m_selected_path[512]   = {};
        char  m_last_click_path[512] = {};
        float m_last_click_time      = -1.f;

        // ── Modals ────────────────────────────────────────────────────────────
        enum class Modal { None, CreateFile, CreateFolder, RenameItem, DeleteItem };
        Modal                       m_modal        = Modal::None;
        ZEngine::Core::VFS::VFSPath m_modal_target;
        bool                        m_modal_is_dir = false;
        char                        m_modal_buf[512]   = {};
        char                        m_modal_error[128] = {}; // non-empty = show error
        bool                        m_modal_opened     = false;

        // ── Helpers ───────────────────────────────────────────────────────────
        void RefreshListing(ZEngine::Core::VFS::IVFSContext* vfs);
        void DrawBreadcrumb(ZEngine::UI::ZUIContext* ctx, float fh);
        void DrawSourcesTree(ZEngine::UI::ZUIContext* ctx,
                             ZEngine::Core::VFS::IVFSContext* vfs);
        void DrawFilters(ZEngine::UI::ZUIContext* ctx);
        void DrawGrid(ZEngine::UI::ZUIContext* ctx, float pw);
        void DrawModals(ZEngine::UI::ZUIContext* ctx,
                        ZEngine::Core::VFS::IVFSContext* vfs);

        // Returns subdirectories of `dir` from cache (populates cache on miss).
        const TreeDirEntry* GetCachedSubdirs(ZEngine::Core::VFS::IVFSContext* vfs,
                                             const ZEngine::Core::VFS::VFSPath& dir,
                                             int* out_count);

        static const float* ExtColor(const char* name);
        static const char*  TypeCategory(const char* name);
        static bool         PassesFilters(const Entry& e, const char* search, const char* type_filter);
    };

} // namespace Tetragrama::Panels
