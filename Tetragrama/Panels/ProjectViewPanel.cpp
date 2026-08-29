#include <Tetragrama/Editor.h>
#include <Tetragrama/Panels/PanelHelpers.h>
#include <Tetragrama/Panels/ProjectViewPanel.h>
#include <ZEngine/Engine.h>
#include <ZEngine/Helpers/MemoryOperations.h>
#include <ZEngine/UI/ZUIWidgets.h>
#include <cctype>
#include <cstdio>
#include <cstring>

namespace Tetragrama::Panels
{
    using namespace ZEngine::Core::VFS;
    using namespace ZEngine::Helpers;
    using namespace ZEngine::UI;

    // ── Extension colour palette (matches develop DrawContentIcon tints) ──────
    const float* ProjectViewPanel::ExtColor(const char* name)
    {
        static const float kDir[4]   = {0.85f, 0.65f, 0.15f, 1.f}; // amber  — folder
        static const float kCpp[4]   = {0.40f, 0.65f, 0.90f, 1.f}; // blue   — .cpp/.c
        static const float kH[4]     = {0.45f, 0.85f, 0.55f, 1.f}; // teal   — .h/.hpp
        static const float kGlsl[4]  = {0.80f, 0.40f, 0.85f, 1.f}; // purple — .glsl/.vert/.frag
        static const float kImg[4]   = {0.90f, 0.75f, 0.30f, 1.f}; // gold   — .png/.jpg/.dds
        static const float kScene[4] = {0.55f, 0.80f, 0.45f, 1.f}; // green  — .zescene
        static const float kMesh[4]  = {0.55f, 0.75f, 0.90f, 1.f}; // sky    — .glb/.gltf/.fbx/.obj
        static const float kDef[4]   = {0.55f, 0.55f, 0.60f, 1.f}; // gray   — unknown

        if (!name)
            return kDef;
        const char* dot = strrchr(name, '.');
        if (!dot)
            return kDef;
        const char* ext = dot + 1;

        // case-insensitive single-char dispatch
        char        e0  = (char) tolower((unsigned char) ext[0]);
        char        e1  = ext[0] ? (char) tolower((unsigned char) ext[1]) : 0;

        if (e0 == 'c' && (e1 == 'p' || e1 == 0))
            return kCpp;
        if (e0 == 'h' && (e1 == 'p' || e1 == 0))
            return kH;
        if (e0 == 'g' && e1 == 'l')
            return kGlsl;
        if (e0 == 'v' && e1 == 'e')
            return kGlsl; // .vert
        if (e0 == 'f' && e1 == 'r')
            return kGlsl; // .frag
        if (e0 == 'p' || e0 == 'j' || e0 == 'd')
            return kImg;
        if (e0 == 'z')
            return kScene;
        if (e0 == 'g' || e0 == 'f' || e0 == 'o')
            return kMesh;
        return kDef;
    }

    // ── Constructor ───────────────────────────────────────────────────────────
    ProjectViewPanel::ProjectViewPanel()
    {
        Title = "Project";
        memset(m_entries, 0, sizeof(m_entries));
    }

    // ── VFS listing ───────────────────────────────────────────────────────────
    void ProjectViewPanel::RefreshListing(IVFSContext* vfs)
    {
        m_nentries = 0;
        if (!vfs)
            return;

        // Use the engine VFS arena as scratch for the directory listing
        auto* eng = ZEngine::Engine::GetContext();
        if (!eng)
            return;
        auto scratch = ZGetScratch(&eng->VFSArena);

        auto res     = vfs->List(m_current_dir, scratch.Arena);
        if (!res.Succeeded())
        {
            ZReleaseScratch(scratch);
            secure_strncpy(m_listed_str, sizeof(m_listed_str), m_current_dir.CStr() ? m_current_dir.CStr() : "", sizeof(m_listed_str) - 1);
            m_needs_refresh = false;
            return;
        }

        auto& entries = res.Value();
        for (uint32_t i = 0; i < entries.size() && m_nentries < kMaxEntries; ++i)
        {
            const VFSDirEntry& e = entries[i];

            // Skip .meta files (matches develop behavior)
            {
                char filename[256] = {};
                e.Path.CopyFilename(filename, sizeof(filename));
                const char* dot = strrchr(filename, '.');
                if (dot && strcasecmp(dot, ".meta") == 0)
                    continue;
            }

            Entry& c = m_entries[m_nentries++];
            e.Path.CopyFilename(c.name, sizeof(c.name));
            c.is_dir = e.IsDirectory;
            secure_strncpy(c.full_path, sizeof(c.full_path), e.Path.CStr() ? e.Path.CStr() : "", sizeof(c.full_path) - 1);
        }

        ZReleaseScratch(scratch);
        secure_strncpy(m_listed_str, sizeof(m_listed_str), m_current_dir.CStr() ? m_current_dir.CStr() : "", sizeof(m_listed_str) - 1);
        m_needs_refresh = false;
    }

    // ── Breadcrumb navigation bar ─────────────────────────────────────────────
    void ProjectViewPanel::DrawBreadcrumb(ZUIContext* ctx, float fh, float pw)
    {
        ZUIBox* bar = ZUIBeginRow(ctx, "##pv_bc", ZFill(), ZPx(fh));
        bar->Flags  = bar->Flags | ZUI_DrawBackground;
        ZUIBoxSetColorArr(bar, ctx->Theme.TitleBarBg);
        bar->EdgeSoftness = 0.f;
        ZUISpacer(ctx, 6.f);

        uint32_t nc = m_current_dir.ComponentCount();
        if (nc == 0 || m_current_dir.IsRoot())
        {
            ZUILabel(ctx, "/", ctx->Theme.TextDim);
        }
        else
        {
            // Root "/" clickable button
            {
                ZUISignal s = ZUISmallButton(ctx, "/##pv_root");
                if (s.Flags & ZUI_SignalClicked)
                {
                    m_current_dir   = VFSPath::Root();
                    m_needs_refresh = true;
                }
            }

            // Each path component
            VFSPath accum = VFSPath::Root();
            for (uint32_t i = 0; i < nc; ++i)
            {
                VFSPathComponent comp = m_current_dir.ComponentAt(i);
                if (!comp.Data || comp.Length == 0)
                    continue;

                // Separator
                ZUILabel(ctx, " › ", ctx->Theme.TextDim);

                // Build partial path for navigation
                char seg[256] = {};
                secure_strncpy(seg, sizeof(seg), comp.Data, comp.Length < sizeof(seg) - 1 ? comp.Length : sizeof(seg) - 1);
                auto next_res = accum.Append(seg);
                if (next_res.Succeeded())
                    accum = next_res.Value();

                if (i == nc - 1)
                {
                    // Last segment — plain label (current dir, not clickable)
                    ZUILabel(ctx, seg, ctx->Theme.TextDefault);
                }
                else
                {
                    // Intermediate segment — clickable
                    char btn_key[64];
                    snprintf(btn_key, sizeof(btn_key), "%s##pv_bc%u", seg, i);
                    ZUISignal s = ZUISmallButton(ctx, btn_key);
                    if (s.Flags & ZUI_SignalClicked)
                    {
                        m_current_dir   = accum;
                        m_needs_refresh = true;
                    }
                }
            }
        }
        ZUISpacer(ctx, 6.f);
        ZUIEndRow(ctx);
    }

    // ── Recursive folder tree (left pane) ─────────────────────────────────────
    void ProjectViewPanel::DrawFolderTree(ZUIContext* ctx, IVFSContext* vfs, const VFSPath& dir, int depth)
    {
        if (!vfs)
            return;

        // List dir — use FrameArena so no persistent allocation needed
        auto res = vfs->List(dir, &ctx->FrameArena);
        if (!res.Succeeded())
            return;

        static const float kFolderCol[4] = {0.85f, 0.65f, 0.15f, 1.f};
        static const float kSelBg[4]     = {0.26f, 0.44f, 0.70f, 0.30f};

        auto&              entries       = res.Value();
        for (uint32_t i = 0; i < entries.size(); ++i)
        {
            const VFSDirEntry& e = entries[i];
            if (!e.IsDirectory)
                continue;

            char name[256] = {};
            e.Path.CopyFilename(name, sizeof(name));
            if (!name[0])
                continue;

            // Is this directory currently selected?
            bool     is_current = (e.Path.CStr() && m_current_dir.CStr() && strcmp(e.Path.CStr(), m_current_dir.CStr()) == 0);

            // Persistent open state via ZUIStateGetOrInsert
            uint64_t path_key   = ZUIHashStr(e.Path.CStr() ? e.Path.CStr() : name, (uint32_t) strlen(e.Path.CStr() ? e.Path.CStr() : name));
            auto*    ps         = ZUIStateGetOrInsert(&ctx->StateStore, path_key);
            bool     is_open    = ps ? (ps->UserData > 0.5f) : false;

            // Indent
            ZUISpacer(ctx, (float) (depth * 12));

            // Folder icon (14px amber square)
            {
                char ik[64];
                snprintf(ik, sizeof(ik), "##fti_%llu", (unsigned long long) path_key);
                ZUIBox* icon  = ZUIPushBox(ctx, ik, (uint32_t) strlen(ik), ZUI_DrawBackground);
                icon->Size[0] = ZPx(14.f);
                icon->Size[1] = ZPx(ZUIGetFrameHeight(ctx));
                ZUIBoxSetColorArr(icon, kFolderCol);
                ZUIBoxSetCornerRadius(icon, 2.f);
                icon->EdgeSoftness = 0.f;
                ZUIPopBox(ctx);
            }
            ZUISpacer(ctx, 4.f);

            // Chevron + label via ZUITreeNode
            {
                // Row wrapper for selection highlight
                char rk[64];
                snprintf(rk, sizeof(rk), "##ftr_%llu", (unsigned long long) path_key);
                ZUIBox* row     = ZUIPushBox(ctx, rk, (uint32_t) strlen(rk), ZUI_DrawBackground | ZUI_Clickable);
                row->Size[0]    = ZFill();
                row->Size[1]    = ZPx(ZUIGetFrameHeight(ctx));
                row->LayoutAxis = ZUIAxis::X;
                if (is_current)
                    ZUIBoxSetColorArr(row, kSelBg);
                else
                    ZUIBoxSetColor(row, 0.f, 0.f, 0.f, 0.f);

                // Tree node with chevron
                char tn_key[256];
                snprintf(tn_key, sizeof(tn_key), "%s##tn_%llu", name, (unsigned long long) path_key);
                ZUISignal tn_sig = ZUITreeNode(ctx, tn_key, &is_open);
                if (ps)
                    ps->UserData = is_open ? 1.f : 0.f;

                ZUISignal row_sig = ZUISignalFromBox(ctx, row);
                ZUIPopBox(ctx);

                // Click → navigate
                if ((tn_sig.Flags & ZUI_SignalClicked) || (row_sig.Flags & ZUI_SignalClicked))
                {
                    m_current_dir   = e.Path;
                    m_needs_refresh = true;
                }

                // Right-click context menu
                if (ZUIBeginPopupContextItem(ctx, "##tree_ctx", row_sig))
                {
                    if (ZUIMenuItem(ctx, "Create Folder"))
                    {
                        m_modal        = Modal::CreateFolder;
                        m_modal_target = e.Path;
                        m_modal_buf[0] = '\0';
                        m_modal_opened = false;
                    }
                    if (ZUIMenuItem(ctx, "Rename"))
                    {
                        m_modal        = Modal::RenameItem;
                        m_modal_target = e.Path;
                        m_modal_is_dir = true;
                        secure_strncpy(m_modal_buf, sizeof(m_modal_buf), name, sizeof(m_modal_buf) - 1);
                        m_modal_opened = false;
                    }
                    if (ZUIMenuItem(ctx, "Delete"))
                    {
                        m_modal        = Modal::DeleteItem;
                        m_modal_target = e.Path;
                        m_modal_is_dir = true;
                        m_modal_opened = false;
                    }
                    ZUIEndPopup(ctx);
                }
            }

            // Recurse if open
            if (is_open)
                DrawFolderTree(ctx, vfs, e.Path, depth + 1);
        }
    }

    // ── Icon grid (right pane) ────────────────────────────────────────────────
    void ProjectViewPanel::DrawGrid(ZUIContext* ctx, float pw, float ph)
    {
        (void) ph;
        static const float kFolderCol[4] = {0.85f, 0.65f, 0.15f, 1.f};
        static const float kSelBg[4]     = {0.26f, 0.44f, 0.70f, 0.35f};

        const float        item_w        = 88.f;
        const float        item_h        = 96.f;

        ZUIBeginScrollRegion(ctx, "##pv_grid_scroll", ZFill(), ZFill());
        ZUIBeginGridView(ctx, "##pv_grid", item_w, item_h);

        bool  search_active = (m_search[0] != '\0');
        float fh            = ZUIGetFrameHeight(ctx);

        for (int i = 0; i < m_nentries; ++i)
        {
            const Entry& e = m_entries[i];

            // Search filter (case-insensitive)
            if (search_active)
            {
                bool match = false;
                for (const char* h = e.name; *h && !match; ++h)
                {
                    const char* n = m_search;
                    const char* p = h;
                    while (*p && *n && tolower((unsigned char) *p) == tolower((unsigned char) *n))
                    {
                        ++p;
                        ++n;
                    }
                    if (!*n)
                        match = true;
                }
                if (!match)
                    continue;
            }

            char item_key[64];
            snprintf(item_key, sizeof(item_key), "##pvi_%d", i);
            bool clicked = ZUIGridViewNextItem(ctx, item_key);

            // ── Icon area (top 60px) ──────────────────────────────────────────
            {
                const float* col = e.is_dir ? kFolderCol : ExtColor(e.name);
                char         ik[64];
                snprintf(ik, sizeof(ik), "##gico_%d", i);
                ZUIBox* icon  = ZUIPushBox(ctx, ik, (uint32_t) strlen(ik), ZUI_DrawBackground);
                icon->Size[0] = ZPx(item_w - 16.f);
                icon->Size[1] = ZPx(56.f);
                ZUIBoxSetColorArr(icon, col);
                ZUIBoxSetCornerRadius(icon, 4.f);
                icon->EdgeSoftness = 0.5f;
                ZUIPopBox(ctx);
            }
            ZUISpacer(ctx, 4.f);

            // ── Filename (bottom) ─────────────────────────────────────────────
            {
                char lk[64];
                snprintf(lk, sizeof(lk), "##gnm_%d", i);
                uint32_t llen     = (uint32_t) strlen(e.name);
                ZUIBox*  lbl      = ZUIPushBox(ctx, lk, (uint32_t) strlen(lk), ZUI_DrawText);
                lbl->Size[0]      = ZPx(item_w - 8.f);
                lbl->Size[1]      = ZPx(fh * 1.5f);
                lbl->Label        = ZUIPushStr(&ctx->FrameArena, e.name, llen);
                lbl->TextAlign    = ZUITextAlign::Center;
                lbl->TextColor[0] = ctx->Theme.TextDefault[0];
                lbl->TextColor[1] = ctx->Theme.TextDefault[1];
                lbl->TextColor[2] = ctx->Theme.TextDefault[2];
                lbl->TextColor[3] = ctx->Theme.TextDefault[3];
                ZUIPopBox(ctx);
            }

            ZUIGridViewEndItem(ctx);

            // Double-click: navigate into directory
            if (clicked && e.is_dir)
            {
                auto res = VFSPath::Parse(e.full_path);
                if (res.Succeeded())
                {
                    m_current_dir   = res.Value();
                    m_needs_refresh = true;
                }
            }

            // Right-click context menu on tile
            {
                char ctx_key[64];
                snprintf(ctx_key, sizeof(ctx_key), "##tile_ctx_%d", i);
                // We use the last signal from GridViewNextItem — approximate via HotKey check
            }
        }

        ZUIEndGridView(ctx);
        ZUIEndScrollRegion(ctx);
    }

    // ── Modal popups ──────────────────────────────────────────────────────────
    void ProjectViewPanel::DrawModals(ZUIContext* ctx, IVFSContext* vfs)
    {
        if (m_modal == Modal::None)
            return;

        // Auto-focus input on first frame
        if (!m_modal_opened)
        {
            ZUIOpenPopup(ctx, "##pv_modal");
            m_modal_opened = true;
        }

        if (!ZUIBeginPopup(ctx, "##pv_modal"))
        {
            m_modal = Modal::None;
            return;
        }

        float fh = ZUIGetFrameHeight(ctx);
        ZUISpacer(ctx, 8.f);

        switch (m_modal)
        {
            case Modal::CreateFile:
            case Modal::CreateFolder:
            {
                const char* title = (m_modal == Modal::CreateFile) ? "New File" : "New Folder";
                ZUILabel(ctx, title, ctx->Theme.TextDefault);
                ZUISpacer(ctx, 6.f);
                ZUITextField(ctx, "##pv_modal_input", m_modal_buf, sizeof(m_modal_buf), 240.f);
                ZUISpacer(ctx, 8.f);
                ZUIBeginRow(ctx, "##pv_modal_btns", ZFill(), ZPx(fh));
                ZUISpacer(ctx, 8.f);
                if (ZUIButton(ctx, "Create##pv_mc").Flags & ZUI_SignalClicked)
                {
                    if (m_modal_buf[0] && vfs && m_modal_target.CStr())
                    {
                        auto new_path = m_modal_target.Append(m_modal_buf);
                        if (new_path.Succeeded())
                        {
                            if (m_modal == Modal::CreateFolder)
                                vfs->CreateDir(new_path.Value());
                            else
                            {
                                auto f = vfs->Open(new_path.Value(), VFSOpenFlags::Write | VFSOpenFlags::Create | VFSOpenFlags::Truncate);
                                if (f.Succeeded())
                                    f.Value()->Close();
                            }
                        }
                    }
                    m_needs_refresh = true;
                    m_modal         = Modal::None;
                    ZUIClosePopup(ctx);
                }
                ZUISpacer(ctx, 4.f);
                if (ZUIButton(ctx, "Cancel##pv_mc").Flags & ZUI_SignalClicked)
                {
                    m_modal = Modal::None;
                    ZUIClosePopup(ctx);
                }
                ZUIEndRow(ctx);
                break;
            }

            case Modal::RenameItem:
            {
                ZUILabel(ctx, "Rename", ctx->Theme.TextDefault);
                ZUISpacer(ctx, 6.f);
                ZUITextField(ctx, "##pv_modal_input", m_modal_buf, sizeof(m_modal_buf), 240.f);
                ZUISpacer(ctx, 8.f);
                ZUIBeginRow(ctx, "##pv_modal_btns", ZFill(), ZPx(fh));
                ZUISpacer(ctx, 8.f);
                if (ZUIButton(ctx, "Rename##pv_mr").Flags & ZUI_SignalClicked)
                {
                    if (m_modal_buf[0] && vfs)
                    {
                        auto parent   = m_modal_target.Parent();
                        auto dst_path = parent.Append(m_modal_buf);
                        if (dst_path.Succeeded())
                            vfs->Rename(m_modal_target, dst_path.Value());
                        // Reset to root if renamed current dir
                        if (m_modal_is_dir && m_modal_target.CStr() && m_current_dir.CStr() && strncmp(m_current_dir.CStr(), m_modal_target.CStr(), strlen(m_modal_target.CStr())) == 0)
                        {
                            m_current_dir = VFSPath::Root();
                        }
                    }
                    m_needs_refresh = true;
                    m_modal         = Modal::None;
                    ZUIClosePopup(ctx);
                }
                ZUISpacer(ctx, 4.f);
                if (ZUIButton(ctx, "Cancel##pv_mr").Flags & ZUI_SignalClicked)
                {
                    m_modal = Modal::None;
                    ZUIClosePopup(ctx);
                }
                ZUIEndRow(ctx);
                break;
            }

            case Modal::DeleteItem:
            {
                ZUILabel(ctx, "Delete — are you sure?", ctx->Theme.TextDefault);
                ZUISpacer(ctx, 6.f);
                {
                    char target_name[256] = {};
                    m_modal_target.CopyFilename(target_name, sizeof(target_name));
                    static const float kWarn[4] = {1.f, 0.85f, 0.20f, 1.f};
                    ZUILabel(ctx, target_name, kWarn);
                }
                ZUISpacer(ctx, 8.f);
                ZUIBeginRow(ctx, "##pv_modal_btns", ZFill(), ZPx(fh));
                ZUISpacer(ctx, 8.f);
                if (ZUIButton(ctx, "Delete##pv_md").Flags & ZUI_SignalClicked)
                {
                    if (vfs)
                    {
                        if (m_modal_is_dir)
                            vfs->RemoveAll(m_modal_target);
                        else
                            vfs->Remove(m_modal_target);
                        // Navigate up if we deleted current dir
                        if (m_modal_target.CStr() && m_current_dir.CStr() && strncmp(m_current_dir.CStr(), m_modal_target.CStr(), strlen(m_modal_target.CStr())) == 0)
                        {
                            m_current_dir = m_current_dir.Parent();
                        }
                    }
                    m_needs_refresh = true;
                    m_modal         = Modal::None;
                    ZUIClosePopup(ctx);
                }
                ZUISpacer(ctx, 4.f);
                if (ZUIButton(ctx, "Cancel##pv_md").Flags & ZUI_SignalClicked)
                {
                    m_modal = Modal::None;
                    ZUIClosePopup(ctx);
                }
                ZUIEndRow(ctx);
                break;
            }

            default:
                break;
        }

        ZUISpacer(ctx, 8.f);
        ZUIEndPopup(ctx);
    }

    // ── BuildContent ──────────────────────────────────────────────────────────
    void ProjectViewPanel::BuildContent(ZUIContext* ctx, float rect[4])
    {
        if (!m_layer || !m_layer->CurrentApp)
        {
            EmptyPanelBg(ctx, "##pv_empty", ctx->Theme.PanelBg, "No workspace");
            return;
        }

        auto* vfs = reinterpret_cast<IVFSContext*>(ZEngine::Engine::GetContext()->VFS);

        // Initialize root on first call
        if (!m_root_init)
        {
            m_current_dir   = VFSPath::Root();
            m_needs_refresh = true;
            m_root_init     = true;
        }

        // Refresh listing when directory changes
        if (m_needs_refresh)
            RefreshListing(vfs);

        float   fh = ZUIGetFrameHeight(ctx);
        float   pw = (rect[2] - rect[0] > 1.f) ? rect[2] - rect[0] : 300.f;
        float   ph = (rect[3] - rect[1] > 1.f) ? rect[3] - rect[1] : 200.f;

        ZUIBox* bg = ZUIBeginColumn(ctx, "##pv_bg", ZFill(), ZFill());
        bg->Flags  = bg->Flags | ZUI_DrawBackground;
        ZUIBoxSetColorArr(bg, ctx->Theme.PanelBg);
        bg->EdgeSoftness              = 0.f;

        // ── Horizontal split: [tree pane | right pane] ───────────────────────
        static constexpr float kTreeW = 180.f;
        ZUIBox*                split  = ZUIBeginRow(ctx, "##pv_split", ZFill(), ZFill());
        split->Flags                  = split->Flags;

        // ── Left: folder tree ─────────────────────────────────────────────────
        {
            ZUIBox* left_col = ZUIBeginColumn(ctx, "##pv_left", ZPx(kTreeW), ZFill());
            left_col->Flags  = left_col->Flags | ZUI_DrawBackground;
            ZUIBoxSetColorArr(left_col, ctx->Theme.TitleBarBg);
            left_col->EdgeSoftness = 0.f;

            ZUISpacer(ctx, 4.f);

            // Root "/" button
            {
                static const float kSelBg[4] = {0.26f, 0.44f, 0.70f, 0.30f};
                bool               is_root   = m_current_dir.IsRoot();
                ZUIBox*            row       = ZUIBeginRow(ctx, "##pv_root_row", ZFill(), ZPx(fh));
                row->Flags                   = row->Flags | ZUI_DrawBackground | ZUI_Clickable;
                if (is_root)
                    ZUIBoxSetColorArr(row, kSelBg);
                else
                    ZUIBoxSetColor(row, 0.f, 0.f, 0.f, 0.f);
                ZUISpacer(ctx, 8.f);
                ZUILabel(ctx, "Assets", is_root ? ctx->Theme.TextDefault : ctx->Theme.TextDim);
                ZUISignal root_sig = ZUISignalFromBox(ctx, row);
                ZUIEndRow(ctx);
                if (root_sig.Flags & ZUI_SignalClicked)
                {
                    m_current_dir   = VFSPath::Root();
                    m_needs_refresh = true;
                }
            }

            ZUIBox* tree_scroll = ZUIBeginScrollRegion(ctx, "##pv_tree_scroll", ZFill(), ZFill());
            (void) tree_scroll;
            ZUISpacer(ctx, 2.f);

            DrawFolderTree(ctx, vfs, VFSPath::Root(), 0);

            ZUIEndScrollRegion(ctx);
            ZUIEndColumn(ctx);
        }

        // ── Thin divider ──────────────────────────────────────────────────────
        {
            ZUIBox* div  = ZUIPushBox(ctx, "##pv_div", 7, ZUI_DrawBackground);
            div->Size[0] = ZPx(1.f);
            div->Size[1] = ZFill();
            ZUIBoxSetColor(div, ctx->Theme.PanelBorder[0], ctx->Theme.PanelBorder[1], ctx->Theme.PanelBorder[2], 0.4f);
            div->EdgeSoftness = 0.f;
            ZUIPopBox(ctx);
        }

        // ── Right: breadcrumb + search + grid ─────────────────────────────────
        {
            ZUIBeginColumn(ctx, "##pv_right", ZFill(), ZFill());

            // Breadcrumb
            DrawBreadcrumb(ctx, fh, pw - kTreeW);

            // Search bar
            ZUISpacer(ctx, 4.f);
            ZUIBeginRow(ctx, "##pv_search_row", ZFill(), ZPx(fh));
            ZUISpacer(ctx, 8.f);
            ZUISearchBox(ctx, "##pv_search", m_search, sizeof(m_search), "Search...", ZFill());
            ZUISpacer(ctx, 4.f);
            // "New File" button
            if (ZUISmallButton(ctx, "+File##pv").Flags & ZUI_SignalClicked)
            {
                m_modal        = Modal::CreateFile;
                m_modal_target = m_current_dir;
                m_modal_buf[0] = '\0';
                secure_strncpy(m_modal_buf, sizeof(m_modal_buf), "NewFile.txt", 11);
                m_modal_opened = false;
            }
            ZUISpacer(ctx, 4.f);
            // "New Folder" button
            if (ZUISmallButton(ctx, "+Dir##pv").Flags & ZUI_SignalClicked)
            {
                m_modal        = Modal::CreateFolder;
                m_modal_target = m_current_dir;
                m_modal_buf[0] = '\0';
                secure_strncpy(m_modal_buf, sizeof(m_modal_buf), "NewFolder", 9);
                m_modal_opened = false;
            }
            ZUISpacer(ctx, 8.f);
            ZUIEndRow(ctx);
            ZUISpacer(ctx, 4.f);

            // Icon grid
            DrawGrid(ctx, pw - kTreeW, ph - fh * 3.f);

            ZUIEndColumn(ctx);
        }

        ZUIEndRow(ctx); // end split
        ZUIEndColumn(ctx);

        // Modals (outside the split layout so they float freely)
        DrawModals(ctx, vfs);
    }

} // namespace Tetragrama::Panels
