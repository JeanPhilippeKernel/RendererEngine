#include <Tetragrama/Panels/PanelHelpers.h>
#include <Tetragrama/Panels/ProjectViewPanel.h>
#include <ZEngine/Engine.h>
#include <ZEngine/Helpers/MemoryOperations.h>
#include <ZEngine/UI/ZUIWidgets.h>
#include <cctype>
#include <cstdio>
#include <cstring>

#ifdef _WIN32
#define strcasecmp _stricmp
#endif

namespace Tetragrama::Panels
{
    using namespace ZEngine::Core::VFS;
    using namespace ZEngine::Helpers;
    using namespace ZEngine::UI;

    // ── Static helpers ────────────────────────────────────────────────────────

    // ── Unified extension dispatch (fixes #5 .dae/.dds split, prevents future divergence) ──
    struct ExtInfo
    {
        const float* Color;
        const char*  Category;
    };

    static ExtInfo GetExtInfo(const char* name)
    {
        static const float kCpp[4]     = {0.40f, 0.65f, 0.90f, 1.f}; // blue
        static const float kH[4]       = {0.45f, 0.85f, 0.55f, 1.f}; // teal
        static const float kShader[4]  = {0.80f, 0.40f, 0.85f, 1.f}; // purple
        static const float kTexture[4] = {0.90f, 0.75f, 0.30f, 1.f}; // gold
        static const float kScene[4]   = {0.55f, 0.80f, 0.45f, 1.f}; // green
        static const float kMesh[4]    = {0.55f, 0.75f, 0.90f, 1.f}; // sky blue
        static const float kDefault[4] = {0.55f, 0.55f, 0.60f, 1.f}; // gray
        if (!name)
            return {kDefault, "Other"};
        const char* dot = strrchr(name, '.');
        if (!dot)
            return {kDefault, "Other"};
        const char* ext = dot + 1;
        char        e0  = (char) tolower((unsigned char) ext[0]);
        char        e1  = ext[0] ? (char) tolower((unsigned char) ext[1]) : 0;
        if (e0 == 'c' && (e1 == 'p' || e1 == 0))
            return {kCpp, "Scripts"};
        if (e0 == 'h' && (e1 == 'p' || e1 == 0))
            return {kH, "Scripts"};
        if (e0 == 'g' && e1 == 'l' && tolower((unsigned char) ext[2]) == 's')
            return {kShader, "Shaders"};
        if (e0 == 'v' && e1 == 'e')
            return {kShader, "Shaders"}; // .vert
        if (e0 == 'f' && e1 == 'r')
            return {kShader, "Shaders"}; // .frag
        if (e0 == 'p' || e0 == 'j' || (e0 == 'd' && e1 == 'd'))
            return {kTexture, "Textures"}; // .png / .jpg / .dds
        if (e0 == 'z')
            return {kScene, "Scenes"};
        if (e0 == 'g' || e0 == 'f' || e0 == 'o' || e0 == 'd') // .glb / .fbx / .obj / .dae
            return {kMesh, "Models"};
        return {kDefault, "Other"};
    }

    const float* ProjectViewPanel::ExtColor(const char* name)
    {
        return GetExtInfo(name).Color;
    }

    const char* ProjectViewPanel::TypeCategory(const char* name)
    {
        return GetExtInfo(name).Category;
    }

    bool ProjectViewPanel::PassesFilters(const Entry& e, const char* search, const char* type_filter)
    {
        // Type filter
        if (strcmp(type_filter, "All") != 0)
        {
            if (strcmp(type_filter, "Folders") == 0 && !e.is_dir)
                return false;
            if (strcmp(type_filter, "Folders") != 0)
            {
                if (e.is_dir)
                    return false; // folders don't match type categories
                if (strcmp(TypeCategory(e.name), type_filter) != 0)
                    return false;
            }
        }
        // Search filter (case-insensitive)
        if (search[0])
        {
            bool match = false;
            for (const char* h = e.name; *h && !match; ++h)
            {
                const char *p = h, *n = search;
                while (*p && *n && tolower((unsigned char) *p) == tolower((unsigned char) *n))
                {
                    ++p;
                    ++n;
                }
                if (!*n)
                    match = true;
            }
            if (!match)
                return false;
        }
        return true;
    }

    // ── Constructor ───────────────────────────────────────────────────────────
    ProjectViewPanel::ProjectViewPanel()
    {
        Title = "Project";
    }

    // ── VFS listing ───────────────────────────────────────────────────────────
    void ProjectViewPanel::RefreshListing(IVFSContext* vfs)
    {
        m_nentries = 0;
        if (!vfs)
            return;
        auto* eng = ZEngine::Engine::GetContext();
        if (!eng)
            return;
        auto scratch = ZGetScratch(&eng->VFSArena);
        auto res     = vfs->List(m_current_dir, scratch.Arena);
        if (res.Succeeded())
        {
            for (uint32_t i = 0; i < res.Value().size() && m_nentries < kMaxEntries; ++i)
            {
                const VFSDirEntry& e       = res.Value()[i];
                char               fn[256] = {};
                e.Path.CopyFilename(fn, sizeof(fn));
                if (!fn[0])
                    continue;
                // Skip hidden files (. prefix) and .meta sidecar files
                if (fn[0] == '.')
                    continue;
                const char* dot = strrchr(fn, '.');
                if (dot && strcasecmp(dot, ".meta") == 0)
                    continue;
                Entry& c = m_entries[m_nentries++];
                secure_strncpy(c.name, sizeof(c.name), fn, sizeof(c.name) - 1);
                c.is_dir = e.IsDirectory;
                secure_strncpy(c.full_path, sizeof(c.full_path), e.Path.CStr() ? e.Path.CStr() : "", sizeof(c.full_path) - 1);
            }
        }
        ZReleaseScratch(scratch);
        m_tree_cache_count = 0; // invalidate sources-tree cache alongside grid
        m_needs_refresh    = false;
    }

    // ── Sources tree subdirectory cache ───────────────────────────────────────
    const ProjectViewPanel::TreeDirEntry* ProjectViewPanel::GetCachedSubdirs(
        IVFSContext* vfs, const VFSPath& dir, int* out_count)
    {
        *out_count = 0;
        const char* dir_str = dir.CStr();
        if (!dir_str || !vfs)
            return nullptr;

        // Cache hit
        for (int i = 0; i < m_tree_cache_count; ++i)
        {
            if (m_tree_cache[i].valid && strcmp(m_tree_cache[i].key, dir_str) == 0)
            {
                *out_count = m_tree_cache[i].count;
                return m_tree_cache[i].entries;
            }
        }

        // Cache full — evict all (simple LRU-free policy)
        if (m_tree_cache_count >= kMaxCachedDirs)
            m_tree_cache_count = 0;

        TreeDirCache& slot = m_tree_cache[m_tree_cache_count++];
        slot               = {};
        secure_strncpy(slot.key, sizeof(slot.key), dir_str, sizeof(slot.key) - 1);

        auto* eng = ZEngine::Engine::GetContext();
        if (eng)
        {
            auto scratch = ZGetScratch(&eng->VFSArena);
            auto res     = vfs->List(dir, scratch.Arena);
            if (res.Succeeded())
            {
                for (uint32_t i = 0; i < res.Value().size() && slot.count < kMaxTreeEntries; ++i)
                {
                    const VFSDirEntry& e = res.Value()[i];
                    if (!e.IsDirectory)
                        continue;
                    char fn[256] = {};
                    e.Path.CopyFilename(fn, sizeof(fn));
                    if (!fn[0] || fn[0] == '.')
                        continue;
                    TreeDirEntry& te = slot.entries[slot.count++];
                    secure_strncpy(te.name,      sizeof(te.name),      fn,                      sizeof(te.name)      - 1);
                    secure_strncpy(te.full_path, sizeof(te.full_path), e.Path.CStr() ? e.Path.CStr() : "", sizeof(te.full_path) - 1);
                }
            }
            ZReleaseScratch(scratch);
        }
        slot.valid = true;
        *out_count = slot.count;
        return slot.entries;
    }

    // ── Breadcrumb ────────────────────────────────────────────────────────────
    void ProjectViewPanel::DrawBreadcrumb(ZUIContext* ctx, float fh)
    {
        ZUIBox* bar = ZUIBeginRow(ctx, "##pv_bc_row", ZFill(), ZPx(fh));
        bar->Flags  = bar->Flags | ZUI_DrawBackground;
        ZUIBoxSetColorArr(bar, ctx->Theme.TitleBarBg);
        bar->EdgeSoftness = 0.f;
        ZUISpacer(ctx, 6.f);

        // Back arrow
        bool can_back = !m_current_dir.IsRoot();
        if (!can_back)
            ZUILabel(ctx, "‹ ", ctx->Theme.TextDim);
        else
        {
            if (ZUISmallButton(ctx, "‹##pv_back").Flags & ZUI_SignalClicked)
            {
                m_current_dir   = m_current_dir.Parent();
                m_needs_refresh = true;
            }
        }
        ZUISpacer(ctx, 4.f);

        // Root
        {
            ZUISignal s = ZUISmallButton(ctx, "All##pv_rt");
            if (s.Flags & ZUI_SignalClicked)
            {
                m_current_dir   = VFSPath::Root();
                m_needs_refresh = true;
            }
        }

        // Path segments — only render as button if all preceding Appends succeeded;
        // a failed Append leaves `accum` stale, so the remaining segments are labels.
        uint32_t nc         = m_current_dir.ComponentCount();
        VFSPath  accum      = VFSPath::Root();
        bool     path_valid = true; // becomes false on the first failed Append
        for (uint32_t i = 0; i < nc; ++i)
        {
            VFSPathComponent comp = m_current_dir.ComponentAt(i);
            if (!comp.Data || comp.Length == 0)
                continue;
            char seg[256] = {};
            secure_strncpy(seg, sizeof(seg), comp.Data, comp.Length < sizeof(seg) - 1 ? comp.Length : sizeof(seg) - 1);
            if (path_valid)
            {
                auto nr = accum.Append(seg);
                if (nr.Succeeded())
                    accum = nr.Value();
                else
                    path_valid = false;
            }
            ZUILabel(ctx, " › ", ctx->Theme.TextDim);
            if (i == nc - 1)
                ZUILabel(ctx, seg, ctx->Theme.TextDefault);
            else if (path_valid)
            {
                char bk[300] = {};
                snprintf(bk, sizeof(bk), "%s##pv_bc%u", seg, i);
                if (ZUISmallButton(ctx, bk).Flags & ZUI_SignalClicked)
                {
                    m_current_dir   = accum;
                    m_needs_refresh = true;
                }
            }
            else
            {
                ZUILabel(ctx, seg, ctx->Theme.TextDim); // stale path — plain label
            }
        }
        ZUISpacer(ctx, 6.f);
        ZUIEndRow(ctx);
    }

    // ── Sources tree (left pane) — iterative DFS ─────────────────────────────
    // Uses GetCachedSubdirs to avoid live vfs->List calls every frame.
    // Stack entries hold a full_path string pointer (stable in cache) + depth.
    void ProjectViewPanel::DrawSourcesTree(ZUIContext* ctx, IVFSContext* vfs)
    {
        if (!vfs)
            return;

        static const float kSelBg[4] = {0.26f, 0.44f, 0.70f, 0.30f};
        float              fh        = ZUIGetFrameHeight(ctx);

        // DFS stack: (full_path pointer into cache, depth)
        struct StackEntry { const char* full_path; int depth; };
        static constexpr int kMaxStack = 512;
        StackEntry* stk = ZPushArray(&ctx->FrameArena, StackEntry, kMaxStack);
        int         sp  = 0;

        // Seed the stack with top-level directories (pushed in reverse for L-to-R order)
        int                 root_count = 0;
        const TreeDirEntry* root_dirs  = GetCachedSubdirs(vfs, VFSPath::Root(), &root_count);
        for (int i = root_count - 1; i >= 0 && sp < kMaxStack; --i)
            stk[sp++] = {root_dirs[i].full_path, 1};

        while (sp > 0)
        {
            StackEntry e = stk[--sp];
            if (!e.full_path || !e.full_path[0])
                continue;

            // Re-derive name from path
            char name[256] = {};
            {
                const char* slash = strrchr(e.full_path, '/');
                const char* src   = slash ? slash + 1 : e.full_path;
                secure_strncpy(name, sizeof(name), src, sizeof(name) - 1);
            }
            if (!name[0])
                continue;

            uint64_t key     = ZUIHashStr(e.full_path, (uint32_t) strlen(e.full_path));
            auto*    ps      = ZUIStateGetOrInsert(&ctx->StateStore, key);
            bool     is_open = ps ? (ps->UserData > 0.5f) : false;
            bool     is_sel  = (m_current_dir.CStr() && strcmp(e.full_path, m_current_dir.CStr()) == 0);

            // Wrapper column: indentation + optional selection bg
            char    ck[64] = {};
            snprintf(ck, sizeof(ck), "##stc_%llu", (unsigned long long) key);
            ZUIBox* col       = ZUIBeginColumn(ctx, ck, ZFill(), ZPx(fh));
            col->Padding[0]   = (float) e.depth * ctx->Style.IndentSpacing;
            col->EdgeSoftness = 0.f;
            if (is_sel)
            {
                col->Flags = col->Flags | ZUI_DrawBackground;
                ZUIBoxSetColorArr(col, kSelBg);
            }

            char tn[300] = {};
            snprintf(tn, sizeof(tn), "%s##st_%llu", name, (unsigned long long) key);
            ZUISignal sig = ZUITreeNode(ctx, tn, &is_open);
            if (ps) ps->UserData = is_open ? 1.f : 0.f;
            ZUIEndColumn(ctx);

            if (sig.Flags & ZUI_SignalClicked)
            {
                auto pr = VFSPath::Parse(e.full_path);
                if (pr.Succeeded())
                {
                    m_current_dir   = pr.Value();
                    m_needs_refresh = true;
                }
            }

            // Context menu — unique key per node to prevent wrong-node deletion
            char ctx_key[48] = {};
            snprintf(ctx_key, sizeof(ctx_key), "##stctx_%llu", (unsigned long long) key);
            if (ZUIBeginPopupContextItem(ctx, ctx_key, sig))
            {
                auto pr = VFSPath::Parse(e.full_path);
                if (ZUIMenuItem(ctx, "Create Folder") && pr.Succeeded())
                {
                    m_modal        = Modal::CreateFolder;
                    m_modal_target = pr.Value();
                    m_modal_buf[0] = '\0';
                    m_modal_opened = false;
                }
                if (ZUIMenuItem(ctx, "Rename") && pr.Succeeded())
                {
                    m_modal        = Modal::RenameItem;
                    m_modal_target = pr.Value();
                    m_modal_is_dir = true;
                    secure_strncpy(m_modal_buf, sizeof(m_modal_buf), name, sizeof(m_modal_buf) - 1);
                    m_modal_opened = false;
                }
                if (ZUIMenuItem(ctx, "Delete") && pr.Succeeded())
                {
                    m_modal        = Modal::DeleteItem;
                    m_modal_target = pr.Value();
                    m_modal_is_dir = true;
                    m_modal_opened = false;
                }
                ZUIEndPopup(ctx);
            }

            ZUISpacer(ctx, 1.f);

            // If open, push children in reverse order for correct DFS display
            if (is_open && e.depth < 32)
            {
                auto pr = VFSPath::Parse(e.full_path);
                if (pr.Succeeded())
                {
                    int                 child_count = 0;
                    const TreeDirEntry* children    = GetCachedSubdirs(vfs, pr.Value(), &child_count);
                    for (int ci = child_count - 1; ci >= 0 && sp < kMaxStack; --ci)
                        stk[sp++] = {children[ci].full_path, e.depth + 1};
                }
            }
        }
    }

    // ── Filters panel (middle column) ─────────────────────────────────────────
    void ProjectViewPanel::DrawFilters(ZUIContext* ctx)
    {
        float                fh            = ZUIGetFrameHeight(ctx);
        float                row_h         = fh + 4.f; // taller than default for readability

        static const char*   kCategories[] = {"All", "Folders", "Scenes", "Models", "Textures", "Scripts", "Shaders", "Other"};
        static constexpr int kNCats        = 8;

        static const float   kActBg[4]     = {0.22f, 0.63f, 0.69f, 1.f}; // teal — fully opaque when active
        static const float   kHovBg[4]     = {1.f, 1.f, 1.f, 0.06f};     // subtle white hover
        static const float   kRestBg[4]    = {0.f, 0.f, 0.f, 0.f};

        ZUIBeginScrollRegion(ctx, "##pv_flt_scroll", ZFill(), ZFill());
        ZUISpacer(ctx, 8.f);

        for (int i = 0; i < kNCats; ++i)
        {
            bool active = (strcmp(m_type_filter, kCategories[i]) == 0);
            char rk[32] = {};
            snprintf(rk, sizeof(rk), "##pv_flt_%d", i);
            ZUIBox* row = ZUIBeginRow(ctx, rk, ZFill(), ZPx(row_h));
            row->Flags  = row->Flags | ZUI_DrawBackground | ZUI_Clickable;
            bool hov      = (ctx->HotKey == row->Key);
            bool pressing = !active && (ctx->ActiveKey == row->Key);
            if (active)
                ZUIBoxSetColorArr(row, kActBg);
            else if (pressing)
            {
                static const float kPressBg[4] = {1.f, 1.f, 1.f, 0.12f};
                ZUIBoxSetColorArr(row, kPressBg);
            }
            else if (hov)
                ZUIBoxSetColorArr(row, kHovBg);
            else
                ZUIBoxSetColorArr(row, kRestBg);
            ZUIBoxSetCornerRadius(row, 3.f);
            row->EdgeSoftness = 0.f;
            ZUISpacer(ctx, 10.f);
            ZUILabel(ctx, kCategories[i], ctx->Theme.TextDefault); // always bright — these are clickable filters
            ZUISignal s = ZUISignalFromBox(ctx, row);
            ZUIEndRow(ctx);
            if (s.Flags & ZUI_SignalClicked)
                secure_strncpy(m_type_filter, sizeof(m_type_filter), kCategories[i], sizeof(m_type_filter) - 1);
            ZUISpacer(ctx, 3.f);
        }

        ZUIEndScrollRegion(ctx);
    }

    // ── Content grid (right area) ─────────────────────────────────────────────
    void ProjectViewPanel::DrawGrid(ZUIContext* ctx, float pw)
    {
        // ── Card constants ────────────────────────────────────────────────────
        static const float kThumbSz      = 96.f; // wider: more icon room (was 80)
        static const float kPadding      = 20.f;
        static const float kCardW        = kThumbSz + kPadding; // 116px
        static const float kRounding     = 6.f;
        static const float kIconRatio    = 0.85f;
        static const float kFolderCol[4] = {0.85f, 0.65f, 0.15f, 1.f};

        float              fh            = ZUIGetFrameHeight(ctx);
        float              footer_h      = fh * 2.f + 18.f; // more vertical breathing room
        float              card_h        = kThumbSz + footer_h;
        int                col_count     = (int) (pw / kCardW);
        if (col_count < 1)
            col_count = 1;

        // ~13 chars/line × 2 lines = 26 (wider card allows more before ellipsis)
        static constexpr int kMaxNameChars = 26;

        // Collect filtered entries
        int vis[kMaxEntries] = {};
        int nvis = 0;
        for (int i = 0; i < m_nentries; ++i)
            if (PassesFilters(m_entries[i], m_search, m_type_filter))
                vis[nvis++] = i;

        ZUIBeginScrollRegion(ctx, "##pv_grid_scroll", ZFill(), ZFill());
        ZUISpacer(ctx, 16.f);

        for (int r = 0; r * col_count < nvis; ++r)
        {
            char rk[32] = {};
            snprintf(rk, sizeof(rk), "##pvrow_%d", r);
            ZUIBeginRow(ctx, rk, ZFill(), ZPx(card_h));
            ZUISpacer(ctx, 8.f);

            for (int c = 0; c < col_count; ++c)
            {
                int ei = r * col_count + c;
                if (ei >= nvis)
                {
                    char fk[32] = {};
                    snprintf(fk, sizeof(fk), "##pvfl_%d_%d", r, c);
                    ZUIBox* fil  = ZUIPushBox(ctx, fk, (uint32_t) strlen(fk), ZUI_None);
                    fil->Size[0] = ZPx(kCardW - 8.f);
                    ZUIPopBox(ctx);
                }
                else
                {
                    const Entry& e   = m_entries[vis[ei]];
                    bool         sel = (strcmp(m_selected_path, e.full_path) == 0);
                    bool         hov;

                    // ── Card column ───────────────────────────────────────────
                    char         ck[32] = {};
                    snprintf(ck, sizeof(ck), "##pvc_%d_%d", r, c);
                    float   cw   = kCardW - 8.f;
                    ZUIBox* card = ZUIBeginColumn(ctx, ck, ZPx(cw), ZPx(card_h));
                    card->Flags  = card->Flags | ZUI_DrawBackground | ZUI_DrawBorder | ZUI_Clickable;
                    hov              = (ctx->HotKey == card->Key);
                    bool pressing    = !sel && (ctx->ActiveKey == card->Key);

                    // Card bg: solid dark — pressing < hover < rest
                    float rb = pressing ? 0.22f : hov ? 0.25f : 0.16f;
                    float gb = pressing ? 0.22f : hov ? 0.25f : 0.16f;
                    float bb = pressing ? 0.26f : hov ? 0.30f : 0.19f;
                    ZUIBoxSetColor(card, rb, gb, bb, 1.f);
                    ZUIBoxSetCornerRadius(card, kRounding);
                    card->EdgeSoftness = 0.5f;

                    // Card border — always-visible 1px subtle edge (#3)
                    // Selected: bright teal 2px ring (#4)
                    if (sel)
                    {
                        card->BorderColor[0]  = ctx->Theme.TabActiveBorder[0];
                        card->BorderColor[1]  = ctx->Theme.TabActiveBorder[1];
                        card->BorderColor[2]  = ctx->Theme.TabActiveBorder[2];
                        card->BorderColor[3]  = 1.f;
                        card->BorderThickness = 2.f;
                    }
                    else
                    {
                        card->BorderColor[0]  = 1.f;
                        card->BorderColor[1]  = 1.f;
                        card->BorderColor[2]  = 1.f;
                        card->BorderColor[3]  = 0.08f; // 8% white — subtle always-on border
                        card->BorderThickness = 1.f;
                    }

                    // ── Icon area (thumbnail zone) ────────────────────────────
                    // Explicit horizontal centering: (cw - isz) / 2 each side (#6)
                    {
                        float        isz      = kThumbSz * kIconRatio; // 85% → 68px (#5)
                        float        side_pad = (cw - isz) * 0.5f;
                        float        top_pad  = (kThumbSz - isz) * 0.5f;
                        const float* icon_col = e.is_dir ? kFolderCol : ExtColor(e.name);

                        ZUISpacer(ctx, top_pad);
                        {
                            char rk2[32] = {};
                            snprintf(rk2, sizeof(rk2), "##pvicr_%d_%d", r, c);
                            ZUIBeginRow(ctx, rk2, ZFill(), ZPx(isz));
                        }
                        ZUISpacer(ctx, side_pad); // center horizontally (#6)

                        char ik[32] = {};
                        snprintf(ik, sizeof(ik), "##pvico_%d_%d", r, c);
                        ZUIBox* ico       = ZUIPushBox(ctx, ik, (uint32_t) strlen(ik), ZUI_DrawActorIcon);
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

                        ZUISpacer(ctx, side_pad);
                        ZUIEndRow(ctx);
                    }

                    // ── Footer strip (dark overlay with bottom rounding) (#1) ──
                    {
                        char ftk[32] = {};
                        snprintf(ftk, sizeof(ftk), "##pvft_%d_%d", r, c);
                        ZUIBox* footer = ZUIBeginColumn(ctx, ftk, ZFill(), ZPx(footer_h));
                        footer->Flags  = footer->Flags | ZUI_DrawBackground | ZUI_ClipChildren;
                        ZUIBoxSetColor(footer, 0.06f, 0.06f, 0.08f, 0.92f); // footer strip — opaque, slightly blue-dark
                        ZUIBoxSetBottomRadius(footer, kRounding);
                        footer->EdgeSoftness = 0.f;

                        ZUISpacer(ctx, 6.f);

                        // ── Filename — left-aligned, truncated with ellipsis (#2, #7) ──
                        {
                            char display[256] = {};
                            if ((int) strlen(e.name) > kMaxNameChars)
                            {
                                secure_strncpy(display, sizeof(display), e.name, (size_t) (kMaxNameChars - 3));
                                display[kMaxNameChars - 3] = '.';
                                display[kMaxNameChars - 2] = '.';
                                display[kMaxNameChars - 1] = '.';
                                display[kMaxNameChars]     = '\0';
                            }
                            else
                                secure_strncpy(display, sizeof(display), e.name, sizeof(display) - 1);

                            char fnk[32] = {};
                            snprintf(fnk, sizeof(fnk), "##pvfn_%d_%d", r, c);
                            uint32_t nl      = (uint32_t) strlen(display);
                            ZUIBox*  lb      = ZUIPushBox(ctx, fnk, (uint32_t) strlen(fnk), ZUI_DrawText);
                            lb->Size[0]      = ZFill();
                            lb->Size[1]      = ZPx(fh);
                            lb->Label        = ZUIPushStr(&ctx->FrameArena, display, nl);
                            lb->Padding[0]   = 4.f; // left indent — matches develop's pad (#2)
                            lb->TextColor[0] = ctx->Theme.TextDefault[0];
                            lb->TextColor[1] = ctx->Theme.TextDefault[1];
                            lb->TextColor[2] = ctx->Theme.TextDefault[2];
                            lb->TextColor[3] = ctx->Theme.TextDefault[3];
                            ZUIPopBox(ctx);
                        }

                        // ── Type label — left-aligned, dim (#2) ──────────────
                        {
                            const char* type_lbl = e.is_dir ? "Folder" : TypeCategory(e.name);
                            char        tlk[32] = {};
                            snprintf(tlk, sizeof(tlk), "##pvtl_%d_%d", r, c);
                            uint32_t tl       = (uint32_t) strlen(type_lbl);
                            ZUIBox*  tlb      = ZUIPushBox(ctx, tlk, (uint32_t) strlen(tlk), ZUI_DrawText);
                            tlb->Size[0]      = ZFill();
                            tlb->Size[1]      = ZPx(fh);
                            tlb->Label        = ZUIPushStr(&ctx->FrameArena, type_lbl, tl);
                            tlb->Padding[0]   = 4.f; // left indent (#2)
                            tlb->TextColor[0] = 0.70f;
                            tlb->TextColor[1] = 0.70f;
                            tlb->TextColor[2] = 0.75f; // slight blue tint
                            tlb->TextColor[3] = 1.f;
                            ZUIPopBox(ctx);
                        }

                        ZUIEndColumn(ctx); // footer
                    }

                    ZUISignal card_sig = ZUISignalFromBox(ctx, card);
                    ZUIEndColumn(ctx); // card

                    // Single-click → select
                    if (card_sig.Flags & ZUI_SignalClicked)
                    {
                        secure_strncpy(m_selected_path, sizeof(m_selected_path), e.full_path, sizeof(m_selected_path) - 1);
                        float now = ctx->Time;
                        if (strcmp(m_last_click_path, e.full_path) == 0 && now - m_last_click_time < 0.30f && e.is_dir)
                        {
                            auto pr = VFSPath::Parse(e.full_path);
                            if (pr.Succeeded())
                            {
                                m_current_dir   = pr.Value();
                                m_needs_refresh = true;
                            }
                            m_last_click_path[0] = '\0';
                            m_selected_path[0]   = '\0';
                        }
                        else
                        {
                            secure_strncpy(m_last_click_path, sizeof(m_last_click_path), e.full_path, sizeof(m_last_click_path) - 1);
                            m_last_click_time = now;
                        }
                    }

                    // Drag source for files
                    if (!e.is_dir)
                        ZUIBeginDragSource(ctx, card, e.full_path, (uint32_t) strlen(e.full_path) + 1);

                    // Right-click context menu
                    if (ZUIBeginPopupContextItem(ctx, "##pv_grid_ctx", card_sig))
                    {
                        if (e.is_dir)
                        {
                            if (ZUIMenuItem(ctx, "Open"))
                            {
                                auto pr = VFSPath::Parse(e.full_path);
                                if (pr.Succeeded())
                                {
                                    m_current_dir   = pr.Value();
                                    m_needs_refresh = true;
                                }
                            }
                            if (ZUIMenuItem(ctx, "Rename"))
                            {
                                m_modal = Modal::RenameItem;
                                auto pr = VFSPath::Parse(e.full_path);
                                if (pr.Succeeded())
                                    m_modal_target = pr.Value();
                                m_modal_is_dir = true;
                                secure_strncpy(m_modal_buf, sizeof(m_modal_buf), e.name, sizeof(m_modal_buf) - 1);
                                m_modal_opened = false;
                            }
                            if (ZUIMenuItem(ctx, "Delete"))
                            {
                                m_modal = Modal::DeleteItem;
                                auto pr = VFSPath::Parse(e.full_path);
                                if (pr.Succeeded())
                                    m_modal_target = pr.Value();
                                m_modal_is_dir = true;
                                m_modal_opened = false;
                            }
                        }
                        else
                        {
                            if (ZUIMenuItem(ctx, "Rename"))
                            {
                                m_modal = Modal::RenameItem;
                                auto pr = VFSPath::Parse(e.full_path);
                                if (pr.Succeeded())
                                    m_modal_target = pr.Value();
                                m_modal_is_dir = false;
                                secure_strncpy(m_modal_buf, sizeof(m_modal_buf), e.name, sizeof(m_modal_buf) - 1);
                                m_modal_opened = false;
                            }
                            if (ZUIMenuItem(ctx, "Delete"))
                            {
                                m_modal = Modal::DeleteItem;
                                auto pr = VFSPath::Parse(e.full_path);
                                if (pr.Succeeded())
                                    m_modal_target = pr.Value();
                                m_modal_is_dir = false;
                                m_modal_opened = false;
                            }
                        }
                        ZUIEndPopup(ctx);
                    }
                }
                ZUISpacer(ctx, 8.f);
            }
            ZUIEndRow(ctx);
            ZUISpacer(ctx, 8.f);
        }

        ZUISpacer(ctx, 16.f); // bottom margin inside scroll
        ZUIEndScrollRegion(ctx);
    }

    // ── Modals ────────────────────────────────────────────────────────────────
    void ProjectViewPanel::DrawModals(ZUIContext* ctx, IVFSContext* vfs)
    {
        if (m_modal == Modal::None)
            return;
        if (!m_modal_opened)
        {
            m_modal_error[0] = '\0'; // clear any previous error when opening fresh
            ZUIOpenPopup(ctx, "##pv_modal");
            m_modal_opened = true;
        }
        if (!ZUIBeginPopup(ctx, "##pv_modal"))
        {
            m_modal = Modal::None;
            return;
        }

        float              fh       = ZUIGetFrameHeight(ctx);
        static const float kErrCol[4] = {1.f, 0.40f, 0.35f, 1.f}; // red error text
        ZUISpacer(ctx, 8.f);

        switch (m_modal)
        {
            case Modal::CreateFile:
            case Modal::CreateFolder:
            {
                ZUILabel(ctx, m_modal == Modal::CreateFile ? "New File" : "New Folder", ctx->Theme.TextDefault);
                ZUISpacer(ctx, 6.f);
                ZUITextField(ctx, "##pv_mi", m_modal_buf, sizeof(m_modal_buf), 240.f);
                if (m_modal_error[0]) { ZUISpacer(ctx, 4.f); ZUILabel(ctx, m_modal_error, kErrCol); }
                ZUISpacer(ctx, 8.f);
                ZUIBeginRow(ctx, "##pv_mbtns", ZFill(), ZPx(fh));
                ZUISpacer(ctx, 8.f);
                if (ZUIButton(ctx, "Create##pvc").Flags & ZUI_SignalClicked)
                {
                    m_modal_error[0] = '\0';
                    bool ok = false;
                    if (m_modal_buf[0] && vfs)
                    {
                        auto np = m_modal_target.Append(m_modal_buf);
                        if (np.Succeeded())
                        {
                            if (m_modal == Modal::CreateFolder)
                                ok = vfs->CreateDir(np.Value()).Succeeded();
                            else
                            {
                                auto f = vfs->Open(np.Value(), VFSOpenFlags::Write | VFSOpenFlags::Create | VFSOpenFlags::Truncate);
                                if (f.Succeeded()) { f.Value()->Close(); ok = true; }
                            }
                        }
                        if (!ok)
                            secure_strncpy(m_modal_error, sizeof(m_modal_error), "Operation failed — check name or permissions", sizeof(m_modal_error) - 1);
                    }
                    if (ok)
                    {
                        m_needs_refresh = true;
                        m_modal         = Modal::None;
                        ZUIClosePopup(ctx);
                    }
                }
                ZUISpacer(ctx, 4.f);
                if (ZUIButton(ctx, "Cancel##pvc").Flags & ZUI_SignalClicked)
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
                ZUITextField(ctx, "##pv_mi", m_modal_buf, sizeof(m_modal_buf), 240.f);
                if (m_modal_error[0]) { ZUISpacer(ctx, 4.f); ZUILabel(ctx, m_modal_error, kErrCol); }
                ZUISpacer(ctx, 8.f);
                ZUIBeginRow(ctx, "##pv_mbtns", ZFill(), ZPx(fh));
                ZUISpacer(ctx, 8.f);
                if (ZUIButton(ctx, "Rename##pvr").Flags & ZUI_SignalClicked)
                {
                    m_modal_error[0] = '\0';
                    bool ok = false;
                    if (m_modal_buf[0] && vfs && !strchr(m_modal_buf, '/') && !strchr(m_modal_buf, '\\'))
                    {
                        auto dp = m_modal_target.Parent().Append(m_modal_buf);
                        if (dp.Succeeded())
                            ok = vfs->Rename(m_modal_target, dp.Value()).Succeeded();
                        if (!ok)
                            secure_strncpy(m_modal_error, sizeof(m_modal_error), "Rename failed — check permissions or name conflict", sizeof(m_modal_error) - 1);
                    }
                    else if (strchr(m_modal_buf, '/') || strchr(m_modal_buf, '\\'))
                        secure_strncpy(m_modal_error, sizeof(m_modal_error), "Name must not contain path separators", sizeof(m_modal_error) - 1);
                    if (ok)
                    {
                        m_needs_refresh = true;
                        m_modal         = Modal::None;
                        ZUIClosePopup(ctx);
                    }
                }
                ZUISpacer(ctx, 4.f);
                if (ZUIButton(ctx, "Cancel##pvr").Flags & ZUI_SignalClicked)
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
                    char n[256] = {};
                    m_modal_target.CopyFilename(n, sizeof(n));
                    static const float kW[4] = {1.f, 0.85f, 0.20f, 1.f};
                    ZUILabel(ctx, n, kW);
                }
                if (m_modal_error[0]) { ZUISpacer(ctx, 4.f); ZUILabel(ctx, m_modal_error, kErrCol); }
                ZUISpacer(ctx, 8.f);
                ZUIBeginRow(ctx, "##pv_mbtns", ZFill(), ZPx(fh));
                ZUISpacer(ctx, 8.f);
                if (ZUIButton(ctx, "Delete##pvd").Flags & ZUI_SignalClicked)
                {
                    m_modal_error[0] = '\0';
                    bool ok = false;
                    if (vfs)
                    {
                        ok = (m_modal_is_dir ? vfs->RemoveAll(m_modal_target) : vfs->Remove(m_modal_target)).Succeeded();
                        if (ok && m_modal_target.CStr() && m_current_dir.CStr() &&
                            strcmp(m_modal_target.CStr(), m_current_dir.CStr()) == 0)
                            m_current_dir = m_current_dir.Parent();
                        if (!ok)
                            secure_strncpy(m_modal_error, sizeof(m_modal_error), "Delete failed — check permissions", sizeof(m_modal_error) - 1);
                    }
                    if (ok)
                    {
                        m_needs_refresh = true;
                        m_modal         = Modal::None;
                        ZUIClosePopup(ctx);
                    }
                }
                ZUISpacer(ctx, 4.f);
                if (ZUIButton(ctx, "Cancel##pvd").Flags & ZUI_SignalClicked)
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
        auto* vfs = static_cast<IVFSContext*>(ZEngine::Engine::GetContext() ? ZEngine::Engine::GetContext()->VFS : nullptr);

        if (!m_root_init)
        {
            m_current_dir   = VFSPath::Root();
            m_needs_refresh = true;
            m_root_init     = true;
        }

        if (m_needs_refresh)
            RefreshListing(vfs);

        float fh   = ZUIGetFrameHeight(ctx);
        float pw   = (rect[2] - rect[0] > 1.f) ? rect[2] - rect[0] : 400.f;

        // Count visible entries for status bar
        int   nvis = 0;
        for (int i = 0; i < m_nentries; ++i)
            if (PassesFilters(m_entries[i], m_search, m_type_filter))
                ++nvis;

        // ── Outer column (panel background) ──────────────────────────────────
        ZUIBox* bg = ZUIBeginColumn(ctx, "##pv_bg", ZFill(), ZFill());
        bg->Flags  = bg->Flags | ZUI_DrawBackground;
        ZUIBoxSetColorArr(bg, ctx->Theme.PanelBg);
        bg->EdgeSoftness = 0.f;

        // ── Top bar: breadcrumb navigation ────────────────────────────────────
        DrawBreadcrumb(ctx, fh);
        ZUISeparator(ctx);

        // ── Three-column body ─────────────────────────────────────────────────
        static constexpr float kSourcesW = 180.f;
        static constexpr float kFiltersW = 110.f;

        ZUIBeginRow(ctx, "##pv_body", ZFill(), ZFill());

        // ── Left: Sources tree ────────────────────────────────────────────────
        {
            ZUIBox* sc = ZUIBeginColumn(ctx, "##pv_src", ZPx(kSourcesW), ZFill());
            sc->Flags  = sc->Flags | ZUI_DrawBackground;
            ZUIBoxSetColorArr(sc, ctx->Theme.TitleBarBg);
            sc->EdgeSoftness = 0.f;

            ZUISpacer(ctx, 4.f);
            ZUIBeginRow(ctx, "##pv_src_hdr", ZFill(), ZPx(fh));
            ZUISpacer(ctx, 8.f);
            ZUILabel(ctx, "Sources", ctx->Theme.TextDefault);
            ZUIEndRow(ctx);
            ZUISeparator(ctx);

            ZUIBeginScrollRegion(ctx, "##pv_src_scroll", ZFill(), ZFill());
            ZUISpacer(ctx, 4.f);

            // Root "Assets /" row
            {
                static const float kSelBg[4] = {0.26f, 0.44f, 0.70f, 0.30f};
                bool               is_root   = m_current_dir.IsRoot();
                char               rk[32]    = "##pv_root_row";
                ZUIBox*            row       = ZUIBeginColumn(ctx, rk, ZFill(), ZPx(fh));
                row->EdgeSoftness            = 0.f;
                if (is_root)
                {
                    row->Flags = row->Flags | ZUI_DrawBackground;
                    ZUIBoxSetColorArr(row, kSelBg);
                }
                bool clicked = ZUISelectable(ctx, "Assets", nullptr, ZPx(fh));
                ZUIEndColumn(ctx);
                if (clicked)
                {
                    m_current_dir   = VFSPath::Root();
                    m_needs_refresh = true;
                }
            }

            if (vfs)
                DrawSourcesTree(ctx, vfs);

            ZUIEndScrollRegion(ctx);
            ZUIEndColumn(ctx);
        }

        // ── Divider ───────────────────────────────────────────────────────────
        {
            ZUIBox* d  = ZUIPushBox(ctx, "##pv_d1", 7, ZUI_DrawBackground);
            d->Size[0] = ZPx(1.f);
            d->Size[1] = ZFill();
            ZUIBoxSetColor(d, ctx->Theme.PanelBorder[0], ctx->Theme.PanelBorder[1], ctx->Theme.PanelBorder[2], 0.4f);
            d->EdgeSoftness = 0.f;
            ZUIPopBox(ctx);
        }

        // ── Middle: Filters ───────────────────────────────────────────────────
        {
            ZUIBox* fc = ZUIBeginColumn(ctx, "##pv_flt", ZPx(kFiltersW), ZFill());
            fc->Flags  = fc->Flags | ZUI_DrawBackground;
            ZUIBoxSetColorArr(fc, ctx->Theme.TitleBarBg);
            fc->EdgeSoftness = 0.f;

            ZUISpacer(ctx, 4.f);
            ZUIBeginRow(ctx, "##pv_flt_hdr", ZFill(), ZPx(fh));
            ZUISpacer(ctx, 8.f);
            ZUILabel(ctx, "Filters", ctx->Theme.TextDefault);
            ZUIEndRow(ctx);
            ZUISeparator(ctx);

            DrawFilters(ctx);
            ZUIEndColumn(ctx);
        }

        // ── Divider ───────────────────────────────────────────────────────────
        {
            ZUIBox* d  = ZUIPushBox(ctx, "##pv_d2", 7, ZUI_DrawBackground);
            d->Size[0] = ZPx(1.f);
            d->Size[1] = ZFill();
            ZUIBoxSetColor(d, ctx->Theme.PanelBorder[0], ctx->Theme.PanelBorder[1], ctx->Theme.PanelBorder[2], 0.4f);
            d->EdgeSoftness = 0.f;
            ZUIPopBox(ctx);
        }

        // ── Right: Search + Grid + Status ─────────────────────────────────────
        {
            ZUIBeginColumn(ctx, "##pv_right", ZFill(), ZFill());

            // Search row
            ZUISpacer(ctx, 4.f);
            ZUIBeginRow(ctx, "##pv_search_row", ZFill(), ZPx(fh));
            ZUISpacer(ctx, 8.f);
            ZUISearchBox(ctx, "##pv_search", m_search, sizeof(m_search), "Search...", ZFill());
            ZUISpacer(ctx, 4.f);
            if (ZUISmallButton(ctx, "+File##pv").Flags & ZUI_SignalClicked)
            {
                m_modal        = Modal::CreateFile;
                m_modal_target = m_current_dir;
                secure_strncpy(m_modal_buf, sizeof(m_modal_buf), "NewFile.txt", sizeof("NewFile.txt") - 1);
                m_modal_opened = false;
            }
            ZUISpacer(ctx, 4.f);
            if (ZUISmallButton(ctx, "+Dir##pv").Flags & ZUI_SignalClicked)
            {
                m_modal        = Modal::CreateFolder;
                m_modal_target = m_current_dir;
                secure_strncpy(m_modal_buf, sizeof(m_modal_buf), "NewFolder", sizeof("NewFolder") - 1);
                m_modal_opened = false;
            }
            ZUISpacer(ctx, 8.f);
            ZUIEndRow(ctx);
            ZUISpacer(ctx, 4.f);
            ZUISeparator(ctx);

            // Content grid (fills remaining height - status bar)
            DrawGrid(ctx, pw - kSourcesW - kFiltersW - 2.f); // 2 dividers × 1px each

            // Status bar
            ZUISeparator(ctx);
            ZUIBox* sb = ZUIBeginRow(ctx, "##pv_status", ZFill(), ZPx(fh));
            sb->Flags  = sb->Flags | ZUI_DrawBackground;
            ZUIBoxSetColorArr(sb, ctx->Theme.TitleBarBg);
            sb->EdgeSoftness = 0.f;
            ZUISpacer(ctx, 8.f);
            {
                char st[64] = {};
                bool has_sel = (m_selected_path[0] != '\0');
                snprintf(st, sizeof(st), "%d item%s%s", nvis, nvis == 1 ? "" : "s", has_sel ? " (1 selected)" : "");
                ZUILabel(ctx, st, ctx->Theme.TextDim);
            }
            ZUIEndRow(ctx);

            ZUIEndColumn(ctx);
        }

        ZUIEndRow(ctx); // end body split
        ZUIEndColumn(ctx);

        // Modals (outside main layout so they float freely)
        DrawModals(ctx, vfs);
    }

} // namespace Tetragrama::Panels
