#include <Tetragrama/Editor.h>
#include <Tetragrama/Panels/PanelHelpers.h>
#include <Tetragrama/Panels/ProjectViewPanel.h>
#include <ZEngine/Engine.h>
#include <ZEngine/Helpers/MemoryOperations.h>
#include <ZEngine/UI/ZUIWidgets.h>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <string_view>

#ifdef _WIN32
#define strcasecmp _stricmp
#endif

using namespace ZEngine::Core::VFS;
using namespace ZEngine::Helpers;
using namespace ZEngine::UI;

namespace Tetragrama::Panels
{
    // Static helpers

    // Shared, exact extension dispatch for the browser's type filters and tiles.
    struct ExtInfo
    {
        const float* Color    = nullptr;
        const char*  Category = "Other";
    };

    static ExtInfo GetExtInfo(const char* name)
    {
        static const float kScript[4]  = {0.396f, 0.604f, 0.824f, 1.f}; // #659ad2 Material CPP blue
        static const float kHeader[4]  = {0.627f, 0.455f, 0.769f, 1.f}; // #a074c4 Material header purple
        static const float kShader[4]  = {0.80f, 0.40f, 0.85f, 1.f};    // purple
        static const float kTexture[4] = {0.90f, 0.75f, 0.30f, 1.f};    // gold
        static const float kScene[4]   = {0.55f, 0.80f, 0.45f, 1.f};    // green
        static const float kMesh[4]    = {0.55f, 0.75f, 0.90f, 1.f};    // sky blue
        static const float kDefault[4] = {0.55f, 0.55f, 0.60f, 1.f};    // gray
        if (!name)
            return {kDefault, "Other"};
        const char* dot = strrchr(name, '.');
        if (!dot)
            return {kDefault, "Other"};
        // All supported extensions fit here. Normalize once without allocating or
        // classifying unknown longer extensions from just their first few bytes.
        char   extension[16] = {};
        size_t length        = 0;
        for (const char* p = dot + 1; *p; ++p)
        {
            if (length == sizeof(extension) - 1)
                return {kDefault, "Other"};
            extension[length++] = (char) tolower((unsigned char) *p);
        }
        const std::string_view ext(extension, length);

        // Scripts
        if (ext == "cpp" || ext == "cc" || ext == "cxx" || ext == "c" || ext == "cs" || ext == "js" || ext == "ts" || ext == "py" || ext == "lua" || ext == "rs")
            return {kScript, "Scripts"};
        if (ext == "hpp" || ext == "hxx" || ext == "h")
            return {kHeader, "Scripts"};

        // Shaders
        if (ext == "glsl" || ext == "vert" || ext == "frag" || ext == "comp" || ext == "hlsl" || ext == "spv")
            return {kShader, "Shaders"};

        if (ext == "zematerial")
            return {kShader, "Materials"};

        // Textures
        if (ext == "png" || ext == "psd" || ext == "pfm" || ext == "jpg" || ext == "jpeg" || ext == "bmp" || ext == "tga" || ext == "tif" || ext == "tiff" || ext == "dds" || ext == "hdr" || ext == "exr" || ext == "webp" || ext == "ktx" || ext == "ktx2" || ext == "zetextures" || ext == "zenvmap")
            return {kTexture, "Textures"};

        // Scenes
        if (ext == "zescene")
            return {kScene, "Scenes"};

        // Models
        if (ext == "zemesh" || ext == "glb" || ext == "gltf" || ext == "fbx" || ext == "obj" || ext == "dae" || ext == "usd" || ext == "usda" || ext == "usdc" || ext == "stl" || ext == "ply")
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
        if (ZEngine::Helpers::secure_strcmp(type_filter, "All") != 0)
        {
            if (ZEngine::Helpers::secure_strcmp(type_filter, "Folders") == 0 && !e.is_dir)
                return false;
            if (ZEngine::Helpers::secure_strcmp(type_filter, "Folders") != 0)
            {
                if (e.is_dir)
                    return false; // folders don't match type categories
                if (ZEngine::Helpers::secure_strcmp(TypeCategory(e.name), type_filter) != 0)
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

    // Constructor
    ProjectViewPanel::ProjectViewPanel()
    {
        Title = "Content Browser";
        Icon  = (int) ZEngine::UI::ZUIIcon::FolderOpen;
    }

    void ProjectViewPanel::RequestRefresh()
    {
        m_needs_refresh = true;
        m_folder_tree.Invalidate();
    }

    // VFS listing
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
        m_needs_refresh = false;
    }

    // Breadcrumb
    void ProjectViewPanel::DrawBreadcrumb(ZUIContext* ctx, float fh)
    {
        ZUIBox* bar     = ZUIBeginRow(ctx, "##pv_bc_row", ZFill(), ZPx(std::max(fh, Metrics::HeightControl)));
        bar->CrossAlign = ZUIAlign::Center;
        bar->Flags      = bar->Flags | ZUI_DrawBackground;
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

    // Sources tree: stable indices, lazy listings, and cached ancestor-preserving search.
    void ProjectViewPanel::DrawSourcesTree(ZUIContext* ctx, IVFSContext* vfs)
    {
        if (!vfs)
            return;

        m_folder_tree.UpdateSearch(*vfs, ctx->FrameArena, m_folder_search);
        const bool searching = m_folder_search[0] != '\0';
        uint32_t*  stk       = ZPushArray(&ctx->FrameArena, uint32_t, FolderTreeCache::Capacity);
        uint32_t   sp        = 0;
        for (uint32_t i = m_folder_tree.Nodes[0].LastChild; i != FolderTreeCache::None; i = m_folder_tree.Nodes[i].PrevSibling)
            stk[sp++] = i;
        const auto* scroll         = ZUIStateGetOrInsert(&ctx->StateStore, ZUIHashStr("##pv_src_scroll", 15));
        const float scroll_y       = scroll ? scroll->ScrollY : 0.f;
        const float height         = scroll && scroll->ScreenMaxY > scroll->ScreenMinY ? scroll->ScreenMaxY - scroll->ScreenMinY : (float) ctx->ScreenH;
        const float first_y        = fmaxf(0.f, scroll_y - 46.f);
        const float last_y         = scroll_y + height + 46.f;
        float       row_y          = 23.f; // The caller has already drawn the root row.
        float       skipped        = 0.f;
        uint32_t    listing_budget = 16;

        // HOT PATH — runs every frame, no heap allocation allowed.
        while (sp > 0)
        {
            const uint32_t index = stk[--sp];
            const auto&    e     = m_folder_tree.Nodes[index];
            if (searching && !e.Visible)
                continue;
            const char* name      = e.Path + e.NameOffset;
            uint64_t    key       = ZUIHashStr(e.Path, (uint32_t) ZEngine::Helpers::secure_strlen(e.Path));
            auto*       ps        = searching ? nullptr : ZUIStateGetOrInsert(&ctx->StateStore, key);
            bool        is_open   = searching || (ps && ps->UserData > 0.5f);
            bool        is_sel    = (ZEngine::Helpers::secure_strcmp(e.Path, m_current_dir.CStr()) == 0);

            // Only visible rows build boxes. Spacers preserve total scroll height;
            // broad searches cannot exhaust the UI's 8192-box frame budget.
            const bool  draw_row  = row_y + 23.f >= first_y && row_y <= last_y;
            row_y                += 23.f;
            if (!draw_row)
                skipped += 23.f;
            else
            {
                if (skipped > 0.f)
                {
                    ZUISpacer(ctx, skipped);
                    skipped = 0.f;
                }

                // Figma "Content folder": 23 px row, 9 px lead (+19 per level), 9 px chevron, 13 px folder, 7 px gaps.
                // Share the row's persistent state with expansion state. Separate
                // row/chevron/expansion keys would exceed the state table on large trees.
                ZUIBox* col       = ZUIBeginRow(ctx, e.Path, ZFill(), ZPx(23.f));
                col->Flags        = col->Flags | ZUI_DrawBackground | ZUI_Clickable | ZUI_KeyboardActivate;
                col->Padding[0]   = 9.f + (float) (e.Depth - 1) * 19.f;
                col->Gap          = 7.f;
                col->CrossAlign   = ZUIAlign::Center;
                col->EdgeSoftness = 0.f;
                {
                    const float rest[4] = {0.f, 0.f, 0.f, 0.f};
                    ZUIBoxSetColorArr(col, is_sel ? ctx->Theme.ButtonActiveBg : (ctx->HotKey == col->Key ? ctx->Theme.ButtonHoveredBg : rest));
                }
                char chk[80] = {};
                snprintf(chk, sizeof(chk), "##stch_%llu", (unsigned long long) key);
                ZUIBox* chev  = ZUIPushBox(ctx, chk, (uint32_t) ZEngine::Helpers::secure_strlen(chk), ZUI_DrawIcon);
                chev->Size[0] = ZPx(9.f);
                chev->Size[1] = ZPx(9.f);
                chev->IconId  = (uint16_t) (is_open ? ZUIIcon::ChevronDown : ZUIIcon::ChevronRight);
                for (int c = 0; c < 4; ++c)
                    chev->TextColor[c] = ctx->Theme.TextDim[c];
                ZUIPopBox(ctx);
                ZUIIconBox(ctx, (is_open || is_sel) ? ZUIIcon::FolderOpen : ZUIIcon::Folder, 13.f, is_sel ? ctx->Theme.TextDefault : ctx->Theme.TextDim);
                ZUILabelTight(ctx, name, ctx->Theme.TextDefault, ZUIFontSize::Body);
                ZUISignal sig = ZUISignalFromBox(ctx, col);
                ZUIEndRow(ctx);
                const auto* row_state       = ps ? ps : ZUIStateGetOrInsert(&ctx->StateStore, key);
                const float chevron_x       = row_state->ScreenMinX + col->Padding[0];
                const bool  chevron_clicked = (sig.Flags & ZUI_SignalClicked) && ctx->MousePos[0] >= chevron_x && ctx->MousePos[0] <= chevron_x + 9.f;
                if (!searching && chevron_clicked)
                    is_open = !is_open;
                if (ps && !searching)
                    ps->UserData = is_open ? 1.f : 0.f;
                if (chevron_clicked)
                    sig.Flags &= ~(uint32_t) ZUI_SignalClicked;

                if (sig.Flags & ZUI_SignalClicked)
                {
                    auto pr = VFSPath::Parse(e.Path);
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
                    auto pr = VFSPath::Parse(e.Path);
                    if (ZUIMenuItemEx(ctx, "New Folder", nullptr, false, true, (int) ZUIIcon::FolderPlus) && pr.Succeeded())
                    {
                        m_modal        = Modal::CreateFolder;
                        m_modal_target = pr.Value();
                        m_modal_buf[0] = '\0';
                        m_modal_opened = false;
                    }
                    if (ZUIMenuItemEx(ctx, "Rename", "F2") && pr.Succeeded())
                    {
                        m_modal        = Modal::RenameItem;
                        m_modal_target = pr.Value();
                        m_modal_is_dir = true;
                        secure_strncpy(m_modal_buf, sizeof(m_modal_buf), name, sizeof(m_modal_buf) - 1);
                        m_modal_opened = false;
                    }
                    if (ZUIMenuItemEx(ctx, "Delete", "Del", false, true, (int) ZUIIcon::X) && pr.Succeeded())
                    {
                        m_modal        = Modal::DeleteItem;
                        m_modal_target = pr.Value();
                        m_modal_is_dir = true;
                        m_modal_opened = false;
                    }
                    ZUIEndPopup(ctx);
                }
            }

            // If open, push children in reverse order for correct DFS display
            if (is_open)
            {
                // Search discovery is budgeted above; never perform extra listings here.
                if (!searching && !e.Loaded && listing_budget > 0)
                {
                    m_folder_tree.LoadChildren(*vfs, ctx->FrameArena, index);
                    --listing_budget;
                }
                for (uint32_t i = e.LastChild; i != FolderTreeCache::None; i = m_folder_tree.Nodes[i].PrevSibling)
                    stk[sp++] = i;
            }
        }
        if (skipped > 0.f)
            ZUISpacer(ctx, skipped);
        if (m_folder_tree.Searching())
            ZUILabel(ctx, "Searching folders...", ctx->Theme.TextDim);
        if (m_folder_tree.Incomplete)
            ZUILabel(ctx, "Folder index incomplete; refresh to retry.", ctx->Theme.TextWarn);
    }

    // Filters panel (middle column)
    void ProjectViewPanel::DrawFilters(ZUIContext* ctx)
    {
        static const char* kCategories[] = {"All", "Folders", "Scenes", "Models", "Materials", "Textures", "Scripts", "Shaders", "Other"};
        if (ZUIBeginPopup(ctx, "##pv_filter_menu"))
        {
            for (const char* category : kCategories)
            {
                const bool active = (ZEngine::Helpers::secure_strcmp(m_type_filter, category) == 0);
                if (ZUIMenuItemEx(ctx, category, nullptr, active))
                    secure_strncpy(m_type_filter, sizeof(m_type_filter), category, sizeof(m_type_filter) - 1);
            }
            ZUIEndPopup(ctx);
        }
    }

    // Content grid (right area)
    void ProjectViewPanel::DrawGrid(ZUIContext* ctx, float pw)
    {
        // Card constants
        static const float kTileW    = 110.f; // Figma "Asset tile"
        static const float kTileGap  = 10.f;
        static const float kCardW    = kTileW + kTileGap;
        float              card_h    = 128.f;
        int                col_count = (int) (pw / kCardW);
        if (col_count < 1)
            col_count = 1;

        // ~13 chars/line × 2 lines = 26 (wider card allows more before ellipsis)
        static constexpr int kMaxNameChars    = 18;

        // Collect filtered entries
        int                  vis[kMaxEntries] = {};
        int                  nvis             = 0;
        for (int i = 0; i < m_nentries; ++i)
            if (PassesFilters(m_entries[i], m_search, m_type_filter))
                vis[nvis++] = i;

        ZUIBox* grid_scroll = ZUIBeginScrollRegion(ctx, "##pv_grid_scroll", ZFill(), ZFill());
        grid_scroll->Flags  = grid_scroll->Flags | ZUI_NavList;
        ZUISpacer(ctx, 9.f);

        // HOT PATH — runs every frame, no heap allocation allowed.
        for (int r = 0; r * col_count < nvis; ++r)
        {
            char rk[32] = {};
            snprintf(rk, sizeof(rk), "##pvrow_%d", r);
            ZUIBeginRow(ctx, rk, ZFill(), ZPx(card_h));
            ZUISpacer(ctx, 12.f);

            for (int c = 0; c < col_count; ++c)
            {
                int ei = r * col_count + c;
                if (ei >= nvis)
                {
                    char fk[32] = {};
                    snprintf(fk, sizeof(fk), "##pvfl_%d_%d", r, c);
                    ZUIBox* fil  = ZUIPushBox(ctx, fk, (uint32_t) ZEngine::Helpers::secure_strlen(fk), ZUI_None);
                    fil->Size[0] = ZPx(kCardW);
                    ZUIPopBox(ctx);
                }
                else
                {
                    const Entry& e      = m_entries[vis[ei]];
                    bool         sel    = (ZEngine::Helpers::secure_strcmp(m_selected_path, e.full_path) == 0);
                    bool         hov    = {};

                    // Figma "Asset tile": 110 x 128, 1 px border, 79 px thumbnail, 3 px type strip, 38 px metadata.
                    char         ck[32] = {};
                    snprintf(ck, sizeof(ck), "##pvc_%d_%d", r, c);
                    float   cw   = kTileW;
                    ZUIBox* card = ZUIBeginColumn(ctx, ck, ZPx(cw), ZPx(card_h));
                    card->Flags  = card->Flags | ZUI_DrawBackground | ZUI_DrawBorder | ZUI_Clickable | ZUI_ClipChildren | ZUI_KeyboardActivate;
                    hov          = (ctx->HotKey == card->Key);
                    ZUIBoxSetColorArr(card, ctx->Theme.ButtonBg);
                    ZUIBoxSetCornerRadius(card, 4.f);
                    card->EdgeSoftness = 0.5f;
                    {
                        const float  sel_border[4] = ZUI_TOKEN(ZEngine::UI::Tokens::BorderSelected);
                        const float* bc            = sel ? sel_border : ctx->Theme.PanelBorder;
                        for (int k = 0; k < 4; ++k)
                            card->BorderColor[k] = bc[k];
                        card->BorderThickness = 1.f;
                    }

                    // Truncated name (tile text is 100 px wide at 11 px)
                    char display[256] = {};
                    {
                        if ((int) ZEngine::Helpers::secure_strlen(e.name) > kMaxNameChars)
                        {
                            secure_strncpy(display, sizeof(display), e.name, (size_t) (kMaxNameChars - 3));
                            display[kMaxNameChars - 3] = '.';
                            display[kMaxNameChars - 2] = '.';
                            display[kMaxNameChars - 1] = '.';
                            display[kMaxNameChars]     = '\0';
                        }
                        else
                            secure_strncpy(display, sizeof(display), e.name, sizeof(display) - 1);
                    }

                    const char* category = e.is_dir ? "Folder" : TypeCategory(e.name);
                    ZUIIcon     glyph    = ZUIIcon::Square;
                    float       strip[4] = ZUI_TOKEN(ZEngine::UI::Tokens::TextSecondary);
                    const char* kind     = "File";
                    if (e.is_dir)
                    {
                        glyph = ZUIIcon::Folder;
                        kind  = "Folder";
                    }
                    else if (ZEngine::Helpers::secure_strcmp(category, "Models") == 0)
                    {
                        glyph = ZUIIcon::Box;
                        kind  = "Static Mesh";
                    }
                    else if (ZEngine::Helpers::secure_strcmp(category, "Textures") == 0)
                    {
                        glyph = ZUIIcon::Image;
                        kind  = "Texture";
                    }
                    else if (ZEngine::Helpers::secure_strcmp(category, "Scenes") == 0)
                    {
                        glyph = ZUIIcon::Map;
                        kind  = "Level";
                    }
                    else if (ZEngine::Helpers::secure_strcmp(category, "Scripts") == 0)
                    {
                        glyph               = ZUIIcon::Code;
                        kind                = "Script";
                        const float blue[4] = ZUI_TOKEN(ZEngine::UI::Tokens::AccentBlue);
                        for (int k = 0; k < 4; ++k)
                            strip[k] = blue[k];
                    }
                    else if (ZEngine::Helpers::secure_strcmp(category, "Materials") == 0 || ZEngine::Helpers::secure_strcmp(category, "Shaders") == 0)
                    {
                        glyph                = ZUIIcon::Workflow;
                        kind                 = ZEngine::Helpers::secure_strcmp(category, "Materials") == 0 ? "Material" : "Shader";
                        const float green[4] = ZUI_TOKEN(ZEngine::UI::Tokens::AccentGreen);
                        for (int k = 0; k < 4; ++k)
                            strip[k] = green[k];
                    }

                    // Thumbnail
                    {
                        char tk[32] = {};
                        snprintf(tk, sizeof(tk), "##pvth_%d_%d", r, c);
                        ZUIBox* th       = ZUIBeginColumn(ctx, tk, ZFill(), ZPx(79.f));
                        th->Flags        = th->Flags | ZUI_DrawBackground;
                        th->MainAlign    = ZUIAlign::Center;
                        th->CrossAlign   = ZUIAlign::Center;
                        th->EdgeSoftness = 0.f;
                        ZUIBoxSetColorArr(th, ctx->Theme.ThumbnailBg);
                        const float* gc = e.is_dir ? ExtColor(".dir") : ctx->Theme.TextDim;
                        ZUIIconBox(ctx, glyph, 38.f, gc);
                        ZUIEndColumn(ctx);
                    }
                    {
                        char sk[32] = {};
                        snprintf(sk, sizeof(sk), "##pvst_%d_%d", r, c);
                        ZUIBox* st  = ZUIPushBox(ctx, sk, (uint32_t) ZEngine::Helpers::secure_strlen(sk), ZUI_DrawBackground);
                        st->Size[0] = ZFill();
                        st->Size[1] = ZPx(3.f);
                        ZUIBoxSetColorArr(st, strip);
                        st->EdgeSoftness = 0.f;
                        ZUIPopBox(ctx);
                    }
                    // Metadata
                    {
                        char mk[32] = {};
                        snprintf(mk, sizeof(mk), "##pvmd_%d_%d", r, c);
                        ZUIBox* md     = ZUIBeginColumn(ctx, mk, ZFill(), ZFill());
                        md->Flags      = md->Flags | ZUI_DrawBackground;
                        md->Padding[0] = md->Padding[2] = 5.f;
                        md->Padding[1]                  = 6.f;
                        md->Gap                         = 4.f;
                        md->EdgeSoftness                = 0.f;
                        ZUIBoxSetColorArr(md, sel ? ctx->Theme.ButtonActiveBg : ctx->Theme.ButtonBg);
                        ZUILabelTight(ctx, display, ctx->Theme.TextDefault, ZUIFontSize::Body);
                        ZUILabelTight(ctx, kind, ctx->Theme.TextDim, ZUIFontSize::Body);
                        ZUIEndColumn(ctx);
                    }

                    ZUISignal card_sig = ZUISignalFromBox(ctx, card);
                    ZUIEndColumn(ctx); // card

                    // Single-click → select
                    if (card_sig.Flags & ZUI_SignalClicked)
                    {
                        secure_strncpy(m_selected_path, sizeof(m_selected_path), e.full_path, sizeof(m_selected_path) - 1);
                        float now = ctx->Time;
                        if (ZEngine::Helpers::secure_strcmp(m_last_click_path, e.full_path) == 0 && now - m_last_click_time < 0.30f && e.is_dir)
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
                        ZUIBeginDragSource(ctx, card, e.full_path, (uint32_t) ZEngine::Helpers::secure_strlen(e.full_path), ZUIDragPayloadKind::VFSPath);

                    // Right-click context menu — unique key per card (row/col), so
                    // opening one card's menu doesn't also open every other card's.
                    char ctx_key[32] = {};
                    snprintf(ctx_key, sizeof(ctx_key), "##pvgctx_%d_%d", r, c);
                    if (ZUIBeginPopupContextItem(ctx, ctx_key, card_sig))
                    {
                        // Right-click also selects the card, so the visible highlight
                        // always matches the item the menu is acting on.
                        secure_strncpy(m_selected_path, sizeof(m_selected_path), e.full_path, sizeof(m_selected_path) - 1);

                        auto pr = VFSPath::Parse(e.full_path);
                        if (e.is_dir)
                        {
                            if (ZUIMenuItem(ctx, "Open") && pr.Succeeded())
                            {
                                m_current_dir   = pr.Value();
                                m_needs_refresh = true;
                            }
                            if (ZUIMenuItemEx(ctx, "Rename", "F2") && pr.Succeeded())
                            {
                                m_modal        = Modal::RenameItem;
                                m_modal_target = pr.Value();
                                m_modal_is_dir = true;
                                secure_strncpy(m_modal_buf, sizeof(m_modal_buf), e.name, sizeof(m_modal_buf) - 1);
                                m_modal_opened = false;
                            }
                            if (ZUIMenuItemEx(ctx, "Delete", "Del", false, true, (int) ZUIIcon::X) && pr.Succeeded())
                            {
                                m_modal        = Modal::DeleteItem;
                                m_modal_target = pr.Value();
                                m_modal_is_dir = true;
                                m_modal_opened = false;
                            }
                        }
                        else
                        {
                            if (ZUIMenuItemEx(ctx, "Rename", "F2") && pr.Succeeded())
                            {
                                m_modal        = Modal::RenameItem;
                                m_modal_target = pr.Value();
                                m_modal_is_dir = false;
                                secure_strncpy(m_modal_buf, sizeof(m_modal_buf), e.name, sizeof(m_modal_buf) - 1);
                                m_modal_opened = false;
                            }
                            if (ZUIMenuItemEx(ctx, "Delete", "Del", false, true, (int) ZUIIcon::X) && pr.Succeeded())
                            {
                                m_modal        = Modal::DeleteItem;
                                m_modal_target = pr.Value();
                                m_modal_is_dir = false;
                                m_modal_opened = false;
                            }
                        }
                        ZUIEndPopup(ctx);
                    }
                }
                ZUISpacer(ctx, kTileGap);
            }
            ZUIEndRow(ctx);
            ZUISpacer(ctx, kTileGap);
        }

        ZUISpacer(ctx, 16.f); // bottom margin inside scroll
        ZUIEndScrollRegion(ctx);

        // Right-click on empty grid space → New File / New Folder. Cards sit deeper
        // in the box tree and always win the hit test, so this never fires over one.
        ZUISignal bg_sig = ZUISignalFromBox(ctx, grid_scroll);
        if (ZUIBeginPopupContextItem(ctx, "##pv_grid_bgctx", bg_sig))
        {
            if (ZUIMenuItemEx(ctx, "New File", nullptr, false, true, (int) ZUIIcon::Plus))
            {
                m_modal        = Modal::CreateFile;
                m_modal_target = m_current_dir;
                secure_strncpy(m_modal_buf, sizeof(m_modal_buf), "NewFile.txt", sizeof("NewFile.txt") - 1);
                m_modal_opened = false;
            }
            if (ZUIMenuItemEx(ctx, "New Folder", nullptr, false, true, (int) ZUIIcon::FolderPlus))
            {
                m_modal        = Modal::CreateFolder;
                m_modal_target = m_current_dir;
                secure_strncpy(m_modal_buf, sizeof(m_modal_buf), "NewFolder", sizeof("NewFolder") - 1);
                m_modal_opened = false;
            }
            ZUIEndPopup(ctx);
        }
    }

    // Modals
    void ProjectViewPanel::DrawModals(ZUIContext* ctx, IVFSContext* vfs)
    {
        if (m_modal == Modal::None)
            return;
        if (!m_modal_opened)
        {
            m_modal_error[0] = '\0'; // clear any previous error when opening fresh

            // Center on the window. Half-extent is a fixed estimate since the popup's
            // real size isn't known until ZFit() measures its content.
            float px = -1.f, py = -1.f;
            if (ctx->Root && ctx->Root->ScreenMax[0] > 0.f && ctx->Root->ScreenMax[1] > 0.f)
            {
                px = ctx->Root->ScreenMax[0] * 0.5f - 150.f;
                py = ctx->Root->ScreenMax[1] * 0.5f - 75.f;
            }
            ZUIOpenPopup(ctx, "##pv_modal", px, py);
            m_modal_opened = true;

            // Auto-focus the name field: ZUITextField only accepts input when
            // ctx->FocusKey matches, and nothing claims focus by default.
            if (m_modal == Modal::CreateFile || m_modal == Modal::CreateFolder || m_modal == Modal::RenameItem)
            {
                uint64_t field_key = ZUIHashStr("##pv_mi", 7);
                ctx->FocusKey      = field_key;
                if (m_modal == Modal::RenameItem)
                {
                    // Pre-select the existing name so typing immediately replaces it.
                    auto* ps = ZUIStateGetOrInsert(&ctx->StateStore, field_key);
                    if (ps)
                    {
                        ps->SelectStart = 0;
                        ps->UserData    = (float) secure_strlen(m_modal_buf);
                    }
                }
            }
            // ZUIOpenPopup only stages a request; it's committed next frame. Calling
            // ZUIBeginPopup for it now would always fail and wipe m_modal early.
            return;
        }
        if (!ZUIBeginPopup(ctx, "##pv_modal"))
        {
            m_modal = Modal::None;
            return;
        }
        if (ctx->EscapePressed)
        {
            // No early return: ZUIBeginPopup already pushed a box that must be
            // balanced by ZUIEndPopup below, or ctx->Current is left dangling.
            m_modal = Modal::None;
            ZUIClosePopup(ctx);
        }

        const float        button_row_height = fmaxf(ZUIGetFrameHeight(ctx), Metrics::HeightControl);
        static const float kErrCol[4]        = {1.f, 0.40f, 0.35f, 1.f}; // red error text
        // Right-aligns whatever is pushed next: an invisible box that fills the
        // remaining row width (same trick ZUIBeginPopup uses for its min-width sizer).
        auto               push_right_filler = [&]() {
            ZUIBox* filler  = ZUIPushBox(ctx, "##pv_mbtns_fill", 15, ZUI_None);
            filler->Size[0] = ZFill();
            filler->Size[1] = ZPx(0.f);
            ZUIPopBox(ctx);
        };
        ZUISpacer(ctx, 8.f);

        switch (m_modal)
        {
            case Modal::CreateFile:
            case Modal::CreateFolder:
            {
                ZUILabel(ctx, m_modal == Modal::CreateFile ? "New File" : "New Folder", ctx->Theme.TextDefault);
                ZUISpacer(ctx, 6.f);
                ZUITextField(ctx, "##pv_mi", m_modal_buf, sizeof(m_modal_buf), 240.f);
                if (m_modal_error[0])
                {
                    ZUISpacer(ctx, 4.f);
                    ZUILabel(ctx, m_modal_error, kErrCol);
                }
                ZUISpacer(ctx, 8.f);
                ZUIBeginRow(ctx, "##pv_mbtns", ZFill(), ZPx(button_row_height));
                push_right_filler();
                bool can_submit = m_modal_buf[0] != '\0';
                if (!can_submit)
                    ZUIBeginDisabled(ctx);
                bool clicked = ZUIButton(ctx, "Create##pvc").Flags & ZUI_SignalClicked;
                if (!can_submit)
                    ZUIEndDisabled(ctx);
                if (clicked || (can_submit && ctx->EnterPressed))
                {
                    m_modal_error[0] = '\0';
                    bool ok          = false;
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
                                if (f.Succeeded())
                                {
                                    f.Value()->Close();
                                    ok = true;
                                }
                            }
                        }
                        if (!ok)
                            secure_strncpy(m_modal_error, sizeof(m_modal_error), "Operation failed — check name or permissions", sizeof(m_modal_error) - 1);
                    }
                    if (ok)
                    {
                        RequestRefresh();
                        m_modal = Modal::None;
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
                if (m_modal_error[0])
                {
                    ZUISpacer(ctx, 4.f);
                    ZUILabel(ctx, m_modal_error, kErrCol);
                }
                ZUISpacer(ctx, 8.f);
                ZUIBeginRow(ctx, "##pv_mbtns", ZFill(), ZPx(button_row_height));
                push_right_filler();
                bool rename_can_submit = m_modal_buf[0] != '\0';
                if (!rename_can_submit)
                    ZUIBeginDisabled(ctx);
                bool rename_clicked = ZUIButton(ctx, "Rename##pvr").Flags & ZUI_SignalClicked;
                if (!rename_can_submit)
                    ZUIEndDisabled(ctx);
                if (rename_clicked || (rename_can_submit && ctx->EnterPressed))
                {
                    m_modal_error[0] = '\0';
                    bool ok          = false;
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
                        RequestRefresh();
                        m_modal = Modal::None;
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
                if (m_modal_error[0])
                {
                    ZUISpacer(ctx, 4.f);
                    ZUILabel(ctx, m_modal_error, kErrCol);
                }
                ZUISpacer(ctx, 8.f);
                ZUIBeginRow(ctx, "##pv_mbtns", ZFill(), ZPx(button_row_height));
                push_right_filler();
                // ZUIButton uses the shared Figma recipe, not the legacy surface slots.
                const bool delete_clicked = ZUIButton(ctx, "Delete##pvd").Flags & ZUI_SignalClicked;
                if (delete_clicked)
                {
                    m_modal_error[0] = '\0';
                    bool ok          = false;
                    if (vfs)
                    {
                        ok = (m_modal_is_dir ? vfs->RemoveAll(m_modal_target) : vfs->Remove(m_modal_target)).Succeeded();
                        if (ok && m_modal_target.CStr() && m_current_dir.CStr() && ZEngine::Helpers::secure_strcmp(m_modal_target.CStr(), m_current_dir.CStr()) == 0)
                            m_current_dir = m_current_dir.Parent();
                        if (!ok)
                            secure_strncpy(m_modal_error, sizeof(m_modal_error), "Delete failed — check permissions", sizeof(m_modal_error) - 1);
                    }
                    if (ok)
                    {
                        RequestRefresh();
                        m_modal = Modal::None;
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

    // BuildContent
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

        float pw   = (rect[2] - rect[0] > 1.f) ? rect[2] - rect[0] : 400.f;

        // Count visible entries for the status row
        int   nvis = 0;
        for (int i = 0; i < m_nentries; ++i)
            if (PassesFilters(m_entries[i], m_search, m_type_filter))
                ++nvis;

        auto hline = [&](const char* key) {
            ZUIBox* d  = ZUIPushBox(ctx, key, (uint32_t) ZEngine::Helpers::secure_strlen(key), ZUI_DrawBackground);
            d->Size[0] = ZFill();
            d->Size[1] = ZPx(1.f);
            ZUIBoxSetColorArr(d, ctx->Theme.PanelBorder);
            d->EdgeSoftness = 0.f;
            ZUIPopBox(ctx);
        };

        // Outer column (panel background)
        ZUIBox* bg = ZUIBeginColumn(ctx, "##pv_bg", ZFill(), ZFill());
        bg->Flags  = bg->Flags | ZUI_DrawBackground;
        ZUIBoxSetColorArr(bg, ctx->Theme.PanelBg);
        bg->EdgeSoftness = 0.f;

        // Figma "Content toolbar": 36 px, Add / Import / Save All, back-forward, path, Settings.
        {
            ZUIBox* tb     = ZUIBeginRow(ctx, "##pv_toolbar", ZFill(), ZPx(35.f));
            tb->Padding[0] = 10.f;
            tb->Padding[2] = 10.f;
            tb->Gap        = 8.f;
            tb->CrossAlign = ZUIAlign::Center;

            if (ZUIEditorControl(ctx, "##pv_add", ZUIIcon::Plus, "Add", true, true, nullptr, false, true).Flags & ZUI_SignalClicked)
                ZUIOpenPopup(ctx, "##pv_add_menu");
            if (ZUIEditorControl(ctx, "##pv_import", ZUIIcon::Upload, "Import", false).Flags & ZUI_SignalClicked)
            {
                auto* ed = (m_layer && m_layer->CurrentApp) ? reinterpret_cast<Tetragrama::EditorPtr>(m_layer->CurrentApp) : nullptr;
                if (ed && ed->Configuration)
                {
                    ed->Configuration->ShowImporter  = true;
                    ed->Configuration->FocusImporter = true;
                }
            }
            ZUIEditorControl(ctx, "##pv_saveall", ZUIIcon::Save, "Save All", false);

            // History: back goes to the parent directory; forward is inert (no history yet).
            ZUISpacer(ctx, 0.f);
            {
                const bool can_back = !m_current_dir.IsRoot();
                ZUIBox*    bk       = ZUIPushBox(ctx, "##pv_back", 9, ZUI_DrawIcon | (can_back ? ZUI_Clickable | ZUI_KeyboardActivate : ZUI_None));
                bk->Size[0]         = ZPx(13.f);
                bk->Size[1]         = ZPx(13.f);
                bk->IconId          = (uint16_t) ZUIIcon::ChevronLeft;
                const float* bc     = can_back ? ctx->Theme.TextDefault : ctx->Theme.TextDim;
                for (int k = 0; k < 4; ++k)
                    bk->TextColor[k] = bc[k];
                ZUISignal bs = ZUISignalFromBox(ctx, bk);
                ZUIPopBox(ctx);
                if (can_back && (bs.Flags & ZUI_SignalClicked))
                {
                    m_current_dir   = m_current_dir.Parent();
                    m_needs_refresh = true;
                }
                ZUIIconBox(ctx, ZUIIcon::ChevronRight, 13.f, ctx->Theme.TextDim);
            }
            ZUIIconBox(ctx, ZUIIcon::Folder, 14.f, ctx->Theme.TextDim);
            {
                char path[300] = {};
                snprintf(path, sizeof(path), "Content%s%s", m_current_dir.IsRoot() ? "" : " / ", m_current_dir.IsRoot() ? "" : (m_current_dir.CStr() ? m_current_dir.CStr() : ""));
                ZUILabelTight(ctx, path, ctx->Theme.TextDefault, ZUIFontSize::Large);
            }
            {
                ZUIBox* fill  = ZUIPushBox(ctx, "##pv_tb_fill", 12, ZUI_None);
                fill->Size[0] = ZFill();
                fill->Size[1] = ZPx(1.f);
                ZUIPopBox(ctx);
            }
            ZUIEditorControl(ctx, "##pv_settings", ZUIIcon::Settings, "Settings", false);
            ZUIEndRow(ctx);

            if (ZUIBeginPopup(ctx, "##pv_add_menu"))
            {
                if (ZUIMenuItemEx(ctx, "New File", nullptr, false, true, (int) ZUIIcon::Plus))
                {
                    m_modal        = Modal::CreateFile;
                    m_modal_target = m_current_dir;
                    secure_strncpy(m_modal_buf, sizeof(m_modal_buf), "NewFile.txt", sizeof("NewFile.txt") - 1);
                    m_modal_opened = false;
                }
                if (ZUIMenuItemEx(ctx, "New Folder", nullptr, false, true, (int) ZUIIcon::FolderPlus))
                {
                    m_modal        = Modal::CreateFolder;
                    m_modal_target = m_current_dir;
                    secure_strncpy(m_modal_buf, sizeof(m_modal_buf), "NewFolder", sizeof("NewFolder") - 1);
                    m_modal_opened = false;
                }
                ZUIEndPopup(ctx);
            }
        }
        hline("##pv_tb_line");

        // Body: folder tree (210) | asset library (fill)
        static constexpr float kTreeW = 210.f;
        ZUIBeginRow(ctx, "##pv_body", ZFill(), ZFill());

        {
            ZUIBox* sc = ZUIBeginColumn(ctx, "##pv_src", ZPx(kTreeW), ZFill());
            sc->Flags  = sc->Flags | ZUI_DrawBackground;
            ZUIBoxSetColorArr(sc, ctx->Theme.TitleBarBg);
            sc->EdgeSoftness = 0.f;

            ZUIBox* fs       = ZUIBeginRow(ctx, "##pv_folder_search", ZFill(), ZPx(39.f));
            fs->Padding[0] = fs->Padding[2] = 7.f;
            fs->CrossAlign                  = ZUIAlign::Center;
            if (ZUISearchBox(ctx, "##pv_folder_q", m_folder_search, sizeof(m_folder_search), "Search folders", ZFill()))
            {
                auto* scroll = ZUIStateGetOrInsert(&ctx->StateStore, ZUIHashStr("##pv_src_scroll", 15));
                if (scroll)
                    scroll->ScrollY = scroll->ScrollYTarget = 0.f;
            }
            ZUIEndRow(ctx);

            auto* sources  = ZUIBeginScrollRegion(ctx, "##pv_src_scroll", ZFill(), ZFill());
            sources->Flags = sources->Flags | ZUI_NavList;

            // "All" row: the content root.
            {
                const bool is_root  = m_current_dir.IsRoot();
                ZUIBox*    row      = ZUIBeginRow(ctx, "##pv_root_row", ZFill(), ZPx(23.f));
                row->Flags          = row->Flags | ZUI_DrawBackground | ZUI_Clickable | ZUI_KeyboardActivate;
                row->Padding[0]     = 9.f + 16.f;
                row->Gap            = 7.f;
                row->CrossAlign     = ZUIAlign::Center;
                row->EdgeSoftness   = 0.f;
                const float rest[4] = {0.f, 0.f, 0.f, 0.f};
                ZUIBoxSetColorArr(row, is_root ? ctx->Theme.ButtonActiveBg : (ctx->HotKey == row->Key ? ctx->Theme.ButtonHoveredBg : rest));
                ZUIIconBox(ctx, ZUIIcon::Folder, 13.f, is_root ? ctx->Theme.TextDefault : ctx->Theme.TextDim);
                ZUILabelTight(ctx, "All", ctx->Theme.TextDefault, ZUIFontSize::Body);
                ZUISignal rs = ZUISignalFromBox(ctx, row);
                ZUIEndRow(ctx);
                if (rs.Flags & ZUI_SignalClicked)
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

        {
            ZUIBox* d  = ZUIPushBox(ctx, "##pv_d1", 7, ZUI_DrawBackground);
            d->Size[0] = ZPx(1.f);
            d->Size[1] = ZFill();
            ZUIBoxSetColorArr(d, ctx->Theme.PanelBorder);
            d->EdgeSoftness = 0.f;
            ZUIPopBox(ctx);
        }

        // Asset library: filters + search, then the tile grid
        {
            ZUIBeginColumn(ctx, "##pv_right", ZFill(), ZFill());

            ZUIBox* sr     = ZUIBeginRow(ctx, "##pv_search_row", ZFill(), ZPx(33.f));
            sr->Padding[0] = 12.f;
            sr->Padding[2] = 12.f;
            sr->Padding[1] = 0.f;
            sr->Gap        = 8.f;
            sr->CrossAlign = ZUIAlign::Center;
            if (ZUIEditorControl(ctx, "##pv_filters", ZUIIcon::ListFilter, "Filters", true).Flags & ZUI_SignalClicked)
                ZUIOpenPopup(ctx, "##pv_filter_menu");
            ZUISearchBox(ctx, "##pv_search", m_search, sizeof(m_search), "Search Content", ZFill());
            ZUIIconBox(ctx, ZUIIcon::SlidersHorizontal, 14.f);
            ZUIEndRow(ctx);
            DrawFilters(ctx);

            // Content grid (fills remaining height - status row)
            DrawGrid(ctx, pw - kTreeW - 1.f - 12.f);

            ZUIEndColumn(ctx);
        }

        ZUIEndRow(ctx); // end body split

        // Figma "Content status": 26 px, item count left; selection path, thumbnail size right.
        hline("##pv_st_line");
        {
            ZUIBox* sb     = ZUIBeginRow(ctx, "##pv_status", ZFill(), ZPx(25.f));
            sb->Padding[0] = 12.f;
            sb->Padding[2] = 12.f;
            sb->Gap        = 10.f;
            sb->CrossAlign = ZUIAlign::Center;
            char st[64]    = {};
            bool has_sel   = (m_selected_path[0] != '\0');
            snprintf(st, sizeof(st), "%d item%s%s", nvis, nvis == 1 ? "" : "s", has_sel ? " (1 selected)" : "");
            ZUILabelTight(ctx, st, ctx->Theme.TextDim, ZUIFontSize::Body);
            {
                ZUIBox* fill  = ZUIPushBox(ctx, "##pv_st_fill", 12, ZUI_None);
                fill->Size[0] = ZFill();
                fill->Size[1] = ZPx(1.f);
                ZUIPopBox(ctx);
            }
            if (has_sel)
                ZUILabelTight(ctx, m_selected_path, ctx->Theme.TextDim, ZUIFontSize::Body);
            ZUIIconBox(ctx, ZUIIcon::Grid2x2, 12.f, ctx->Theme.TextDim);
            ZUILabelTight(ctx, "Thumbnail size", ctx->Theme.TextDim, ZUIFontSize::Body);
            {
                ZUIBox* track  = ZUIPushBox(ctx, "##pv_st_slider", 13, ZUI_DrawBackground);
                track->Size[0] = ZPx(56.f);
                track->Size[1] = ZPx(3.f);
                ZUIBoxSetColorArr(track, ctx->Theme.TextDim);
                ZUIBoxSetCornerRadius(track, 1.5f);
                ZUIPopBox(ctx);
            }
            ZUIEndRow(ctx);
        }
        ZUIEndColumn(ctx);

        // Modals (outside main layout so they float freely)
        DrawModals(ctx, vfs);
    }

} // namespace Tetragrama::Panels
