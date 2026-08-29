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

        // ── Develop-exact grid constants ──────────────────────────────────────
        // columnCount = max(1, panelWidth / (thumbnail_size + padding))  [develop line ~540]
        static const float kThumbSz      = 80.f; // m_thumbnail_size in develop
        static const float kPadding      = 16.f;
        static const float kCardW        = kThumbSz + kPadding;
        static const float kRounding     = 4.f;
        static const float kFolderCol[4] = {0.85f, 0.65f, 0.15f, 1.f};

        float              fh            = ZUIGetFrameHeight(ctx);
        float              footer_h      = fh * 2.f + 8.f; // 2-line footer strip
        float              card_h        = kThumbSz + footer_h;
        int                col_count     = (int) (pw / kCardW);
        if (col_count < 1)
            col_count = 1;

        // ── Collect filtered visible entries ──────────────────────────────────
        static int vis[kMaxEntries];
        int        nvis = 0;
        for (int i = 0; i < m_nentries; ++i)
        {
            const Entry& e = m_entries[i];
            if (!m_search[0])
            {
                vis[nvis++] = i;
                continue;
            }
            // case-insensitive substring
            bool match = false;
            for (const char* h = e.name; *h && !match; ++h)
            {
                const char *p = h, *n = m_search;
                while (*p && *n && tolower((unsigned char) *p) == tolower((unsigned char) *n))
                {
                    ++p;
                    ++n;
                }
                if (!*n)
                    match = true;
            }
            if (match)
                vis[nvis++] = i;
        }

        // ── Scroll region ─────────────────────────────────────────────────────
        ZUIBeginScrollRegion(ctx, "##pv_scroll", ZFill(), ZFill());
        ZUISpacer(ctx, 8.f);

        for (int r = 0; r * col_count < nvis; ++r)
        {
            char rk[32];
            snprintf(rk, sizeof(rk), "##pvrow_%d", r);
            ZUIBeginRow(ctx, rk, ZFill(), ZPx(card_h));
            ZUISpacer(ctx, 8.f);

            for (int c = 0; c < col_count; ++c)
            {
                int ei = r * col_count + c;

                if (ei >= nvis)
                {
                    // Empty filler to preserve row structure
                    char fk[32];
                    snprintf(fk, sizeof(fk), "##pvfil_%d_%d", r, c);
                    ZUIBox* fil  = ZUIPushBox(ctx, fk, (uint32_t) strlen(fk), ZUI_None);
                    fil->Size[0] = ZPx(kCardW);
                    ZUIPopBox(ctx);
                }
                else
                {
                    const Entry& e        = m_entries[vis[ei]];
                    const float* icon_col = e.is_dir ? kFolderCol : ExtColor(e.name);

                    // ── Card column ───────────────────────────────────────────
                    char         ck[32];
                    snprintf(ck, sizeof(ck), "##pvc_%d", vis[ei]);
                    ZUIBox* card  = ZUIBeginColumn(ctx, ck, ZPx(kCardW - 8.f), ZPx(card_h));
                    card->Flags   = card->Flags | ZUI_DrawBackground | ZUI_Clickable;
                    bool card_hov = (ctx->HotKey == card->Key);
                    // Card body bg: dark background, brighter on hover (matches develop)
                    ZUIBoxSetColor(card, card_hov ? 0.27f : 0.16f, card_hov ? 0.27f : 0.16f, card_hov ? 0.33f : 0.20f, card_hov ? 0.90f : 0.70f);
                    ZUIBoxSetCornerRadius(card, kRounding);
                    card->EdgeSoftness = 0.5f;

                    // ── Icon area ─────────────────────────────────────────────
                    ZUISpacer(ctx, 6.f);
                    {
                        char ik[32];
                        snprintf(ik, sizeof(ik), "##pvico_%d", vis[ei]);
                        ZUIBox* ico       = ZUIPushBox(ctx, ik, (uint32_t) strlen(ik), ZUI_DrawActorIcon);
                        float   isz       = kThumbSz * 0.70f;
                        ico->Size[0]      = ZPx(isz);
                        ico->Size[1]      = ZPx(isz);
                        ico->TextColor[0] = icon_col[0];
                        ico->TextColor[1] = icon_col[1];
                        ico->TextColor[2] = icon_col[2];
                        ico->TextColor[3] = icon_col[3];
                        auto* ips         = ZUIStateGetOrInsert(&ctx->StateStore, ico->Key);
                        if (ips)
                            ips->UserData = e.is_dir ? ZUI_ICON_FOLDER : ZUI_ICON_ACTOR;
                        ZUIPopBox(ctx);
                    }

                    // ── Footer strip ──────────────────────────────────────────
                    ZUISpacer(ctx, 4.f);
                    {
                        char fnk[32];
                        snprintf(fnk, sizeof(fnk), "##pvfn_%d", vis[ei]);
                        uint32_t nlen     = (uint32_t) strlen(e.name);
                        ZUIBox*  lbl      = ZUIPushBox(ctx, fnk, (uint32_t) strlen(fnk), ZUI_DrawText);
                        lbl->Size[0]      = ZFill();
                        lbl->Size[1]      = ZPx(fh * 2.f);
                        lbl->Label        = ZUIPushStr(&ctx->FrameArena, e.name, nlen);
                        lbl->TextAlign    = ZUITextAlign::Center;
                        lbl->TextColor[0] = ctx->Theme.TextDefault[0];
                        lbl->TextColor[1] = ctx->Theme.TextDefault[1];
                        lbl->TextColor[2] = ctx->Theme.TextDefault[2];
                        lbl->TextColor[3] = ctx->Theme.TextDefault[3];
                        ZUIPopBox(ctx);
                    }

                    ZUISignal card_sig = ZUISignalFromBox(ctx, card);
                    ZUIEndColumn(ctx);

                    // Panel-level double-click (same pattern as HierarchyPanel)
                    if (card_sig.Flags & ZUI_SignalClicked)
                    {
                        float now = ctx->Time;
                        if (strcmp(m_last_click_path, e.full_path) == 0 && now - m_last_click_time < 0.30f && e.is_dir)
                        {
                            auto res = VFSPath::Parse(e.full_path);
                            if (res.Succeeded())
                            {
                                m_current_dir   = res.Value();
                                m_needs_refresh = true;
                            }
                            m_last_click_path[0] = '\0';
                        }
                        else
                        {
                            secure_strncpy(m_last_click_path, sizeof(m_last_click_path), e.full_path, sizeof(m_last_click_path) - 1);
                            m_last_click_time = now;
                        }
                    }

                    // Drag source for files (develop: CONTENT_BROWSER_FILE_DRAG_OP payload)
                    if (!e.is_dir)
                        ZUIBeginDragSource(ctx, card, e.full_path, (uint32_t) strlen(e.full_path));

                    // Right-click context menu
                    if (ZUIBeginPopupContextItem(ctx, "##pv_tile_ctx", card_sig))
                    {
                        if (e.is_dir)
                        {
                            if (ZUIMenuItem(ctx, "Create Folder"))
                            {
                                m_modal        = Modal::CreateFolder;
                                m_modal_target = m_current_dir;
                                m_modal_opened = false;
                            }
                            if (ZUIMenuItem(ctx, "Rename"))
                            {
                                m_modal = Modal::RenameItem;
                                auto r  = VFSPath::Parse(e.full_path);
                                if (r.Succeeded())
                                    m_modal_target = r.Value();
                                m_modal_is_dir = true;
                                secure_strncpy(m_modal_buf, sizeof(m_modal_buf), e.name, sizeof(m_modal_buf) - 1);
                                m_modal_opened = false;
                            }
                            if (ZUIMenuItem(ctx, "Delete"))
                            {
                                m_modal = Modal::DeleteItem;
                                auto r  = VFSPath::Parse(e.full_path);
                                if (r.Succeeded())
                                    m_modal_target = r.Value();
                                m_modal_is_dir = true;
                                m_modal_opened = false;
                            }
                        }
                        else
                        {
                            if (ZUIMenuItem(ctx, "Rename"))
                            {
                                m_modal = Modal::RenameItem;
                                auto r  = VFSPath::Parse(e.full_path);
                                if (r.Succeeded())
                                    m_modal_target = r.Value();
                                m_modal_is_dir = false;
                                secure_strncpy(m_modal_buf, sizeof(m_modal_buf), e.name, sizeof(m_modal_buf) - 1);
                                m_modal_opened = false;
                            }
                            if (ZUIMenuItem(ctx, "Delete"))
                            {
                                m_modal = Modal::DeleteItem;
                                auto r  = VFSPath::Parse(e.full_path);
                                if (r.Succeeded())
                                    m_modal_target = r.Value();
                                m_modal_is_dir = false;
                                m_modal_opened = false;
                            }
                        }
                        ZUIEndPopup(ctx);
                    }
                }
                ZUISpacer(ctx, 8.f); // gap between cards
            }

            ZUIEndRow(ctx);
            ZUISpacer(ctx, 8.f); // gap between rows
        }

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
