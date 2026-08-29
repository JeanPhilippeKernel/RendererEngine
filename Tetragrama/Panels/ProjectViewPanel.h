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
    // Two-pane file browser: left = recursive folder tree, right = icon grid.
    // Matches the develop ProjectViewUIComponent feature set ported to ZUI.
    //
    struct ProjectViewPanel : ZEngine::UI::ZUIPanelView
    {
        ProjectViewPanel();

        Tetragrama::Layers::ZUILayer* m_layer = nullptr; // set by ZUIPanelManagerComponent::Initialize

        void                          BuildContent(ZEngine::UI::ZUIContext* ctx, float rect[4]) override;

    private:
        // ── Navigation ───────────────────────────────────────────────────────
        ZEngine::Core::VFS::VFSPath m_current_dir;
        bool                        m_root_init = false;

        // ── Cached directory listing ─────────────────────────────────────────
        struct Entry
        {
            char name[256]      = {};
            char full_path[512] = {};
            bool is_dir         = false;
        };
        static constexpr int kMaxEntries = 512;
        Entry                m_entries[kMaxEntries];
        int                  m_nentries        = 0;
        char                 m_listed_str[512] = {}; // path of last successful List
        bool                 m_needs_refresh   = true;

        // ── Search ───────────────────────────────────────────────────────────
        char                 m_search[256]     = {};

        // ── Modal popups ──────────────────────────────────────────────────────
        enum class Modal
        {
            None,
            CreateFile,
            CreateFolder,
            RenameItem,
            DeleteItem
        };
        Modal                       m_modal = Modal::None;
        ZEngine::Core::VFS::VFSPath m_modal_target;
        bool                        m_modal_is_dir   = false;
        char                        m_modal_buf[512] = {};
        bool                        m_modal_opened   = false; // first-frame guard

        // ── Helpers ───────────────────────────────────────────────────────────
        void                        RefreshListing(ZEngine::Core::VFS::IVFSContext* vfs);
        void                        DrawFolderTree(ZEngine::UI::ZUIContext* ctx, ZEngine::Core::VFS::IVFSContext* vfs, const ZEngine::Core::VFS::VFSPath& dir, int depth);
        void                        DrawBreadcrumb(ZEngine::UI::ZUIContext* ctx, float fh, float pw);
        void                        DrawGrid(ZEngine::UI::ZUIContext* ctx, float pw, float ph);
        void                        DrawModals(ZEngine::UI::ZUIContext* ctx, ZEngine::Core::VFS::IVFSContext* vfs);
        static const float*         ExtColor(const char* name); // extension → icon color
    };
} // namespace Tetragrama::Panels
