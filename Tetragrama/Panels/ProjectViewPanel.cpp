#include <Tetragrama/Editor.h>
#include <Tetragrama/Panels/PanelHelpers.h>
#include <Tetragrama/Panels/ProjectViewPanel.h>
#include <ZEngine/Engine.h>
#include <ZEngine/Helpers/MemoryOperations.h>
#include <ZEngine/UI/ZUIWidgets.h>
#include <algorithm>
#include <cctype>
#include <cmath>
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
    namespace
    {
        constexpr float kSurface[4]        = ZUI_TOKEN(Tokens::BgSurface);
        constexpr float kControl[4]        = ZUI_TOKEN(Tokens::BgControl);
        constexpr float kHover[4]          = ZUI_TOKEN(Tokens::BgHover);
        constexpr float kSelection[4]      = ZUI_TOKEN(Tokens::AccentSelection);
        constexpr float kBorder[4]         = ZUI_TOKEN(Tokens::BorderSubtle);
        constexpr float kStrong[4]         = ZUI_TOKEN(Tokens::BorderStrong);
        constexpr float kSelectedBorder[4] = ZUI_TOKEN(Tokens::BorderSelected);
        constexpr float kText[4]           = ZUI_TOKEN(Tokens::TextPrimary);
        constexpr float kSecondary[4]      = ZUI_TOKEN(Tokens::TextSecondary);
        constexpr float kSelectedText[4]   = ZUI_TOKEN(Tokens::TextSelected);
        constexpr float kDisabledText[4]   = ZUI_TOKEN(Tokens::TextDisabled);
        constexpr float kToolbarH          = 36.f;
        constexpr float kTreeW             = 210.f;

        bool            BeginBrowserMenu(ZUIContext* ctx, const char* key, float width, const ZUISignal* signal = nullptr)
        {
            const float saved_width = ctx->PopupDesiredW;
            ctx->PopupDesiredW      = width - 2.f * (Metrics::Space4 + 1.f);
            const bool open         = signal ? ZUIBeginPopupContextItem(ctx, key, *signal) : ZUIBeginPopup(ctx, key);
            ctx->PopupDesiredW      = saved_width;
            if (open)
                for (float& padding : ctx->Current->Padding)
                    padding = Metrics::Space4 + ctx->Current->BorderThickness;
            return open;
        }

        const char* NameError(const char* name)
        {
            if (!name[0])
                return "Name must not be empty";
            if (strchr(name, '/') || strchr(name, '\\'))
                return "Name must not contain path separators";
            const size_t length = secure_strlen(name);
            if (name[length - 1] == '.' || name[length - 1] == ' ')
                return "Name must not end with a dot or space";
            for (const auto* p = name; *p; ++p)
                if (static_cast<unsigned char>(*p) < 32 || strchr("<>:\"|?*", *p))
                    return "Name contains an unsupported character";
            // Device names are reserved even with an extension on Windows.
            // Apply the same rule on every platform so project assets stay portable.
            char   stem[5] = {};
            size_t count   = 0;
            while (name[count] && name[count] != '.' && count < sizeof(stem))
            {
                if (count < sizeof(stem) - 1)
                    stem[count] = name[count] >= 'a' && name[count] <= 'z' ? name[count] - ('a' - 'A') : name[count];
                ++count;
            }
            if (count < sizeof(stem) && (secure_strcmp(stem, "CON") == 0 || secure_strcmp(stem, "PRN") == 0 || secure_strcmp(stem, "AUX") == 0 || secure_strcmp(stem, "NUL") == 0 || (count == 4 && stem[3] >= '1' && stem[3] <= '9' && (secure_memcmp(stem, sizeof(stem), "COM", 4, 3) == 0 || secure_memcmp(stem, sizeof(stem), "LPT", 4, 3) == 0))))
                return "Name is reserved on Windows";
            return nullptr;
        }

        VFSResult<VFSPath> RebasePath(const VFSPath& path, const VFSPath& source, const VFSPath& destination)
        {
            if (!source.IsPrefixOf(path))
                return VFSResult<VFSPath>::Ok(path);
            char         buffer[VFS_MAX_PATH] = {};
            const size_t suffix               = path.Length() - source.Length();
            if (destination.Length() + suffix >= sizeof(buffer))
                return VFSResult<VFSPath>::Fail(VFSError::InvalidPath);
            secure_memcpy(buffer, sizeof(buffer), destination.CStr(), destination.Length());
            secure_memcpy(buffer + destination.Length(), sizeof(buffer) - destination.Length(), path.CStr() + source.Length(), suffix + 1);
            return VFSPath::Parse(buffer);
        }

        void DrawTreeNote(ZUIContext* ctx, const char* key, const char* text, const float color[4])
        {
            auto* note       = ZUIBeginColumn(ctx, key, ZFill(), ZFit());
            note->Padding[0] = note->Padding[2] = Metrics::Space9;
            note->Padding[1]                    = Metrics::Space8;
            note->Flags                         = note->Flags | ZUI_ClipChildren;
            char label_key[64]                  = {};
            snprintf(label_key, sizeof(label_key), "%s_label", key);
            ZUILabelEllipsized(ctx, text, kTreeW - 2.f * Metrics::Space9, color, ZUIFontSize::Small, label_key);
            ZUIEndColumn(ctx);
        }

        // Editor controls contain fixed-size icons and tight measured labels.
        float ControlWidth(const ZUIBox* box)
        {
            float    width = box->Padding[0] + box->Padding[2];
            uint32_t count = 0;
            for (auto* child = box->FirstChild; child; child = child->NextSib)
            {
                width += child->Size[0].Value;
                ++count;
            }
            return fmaxf(box->MinSize[0], width + (count ? count - 1 : 0) * box->Gap);
        }

        void OpenBelow(ZUIContext* ctx, const char* popup, uint64_t owner)
        {
            const auto* bounds = ZUIStateGetOrInsert(&ctx->StateStore, owner);
            if (bounds && bounds->ScreenMaxX > bounds->ScreenMinX)
                ZUIOpenPopup(ctx, popup, bounds->ScreenMinX, bounds->ScreenMaxY);
            else
                ZUIOpenPopup(ctx, popup);
        }

        // Compare numeric runs by significant length and then digits. No integer
        // parsing/overflow, allocation, locale dependence, or repeated rescans.
        int NaturalNameCompare(const char* a, const char* b)
        {
            const auto  digit = [](char c) { return c >= '0' && c <= '9'; };
            const auto  lower = [](unsigned char c) { return c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c; };
            const char* left  = a;
            const char* right = b;
            while (*left && *right)
            {
                if (digit(*left) && digit(*right))
                {
                    while (*left == '0')
                        ++left;
                    while (*right == '0')
                        ++right;
                    const char* left_digits  = left;
                    const char* right_digits = right;
                    while (digit(*left))
                        ++left;
                    while (digit(*right))
                        ++right;
                    const auto left_length  = left - left_digits;
                    const auto right_length = right - right_digits;
                    if (left_length != right_length)
                        return left_length < right_length ? -1 : 1;
                    for (decltype(left - left_digits) i = 0; i < left_length; ++i)
                        if (left_digits[i] != right_digits[i])
                            return left_digits[i] < right_digits[i] ? -1 : 1;
                    continue;
                }
                const auto lc = lower(static_cast<unsigned char>(*left));
                const auto rc = lower(static_cast<unsigned char>(*right));
                if (lc != rc)
                    return lc < rc ? -1 : 1;
                ++left;
                ++right;
            }
            if (*left || *right)
                return *left ? 1 : -1;
            // Equal numeric values / case folds still have a deterministic order,
            // independent of backend enumeration (including leading-zero ties).
            return secure_strcmp(a, b);
        }
    } // namespace

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

    bool ProjectViewPanel::NavigateTo(const VFSPath& path, bool record_history)
    {
        if (!path.IsValid() || (m_history_count && path == m_current_dir))
            return false;
        if (record_history)
        {
            // Discard the forward branch. Evict the oldest visit only when full;
            // no shifting, heap allocation or full VFSPath copies in the ring.
            m_history_count = m_history_count ? m_history_cursor + 1 : 0;
            if (m_history_count == kHistoryCapacity)
            {
                m_history_start = (m_history_start + 1) % kHistoryCapacity;
                --m_history_count;
            }
            auto& slot = m_history[(m_history_start + m_history_count) % kHistoryCapacity];
            secure_memcpy(slot, sizeof(slot), path.CStr(), path.Length() + 1);
            m_history_cursor = m_history_count++;
        }
        m_current_dir      = path;
        m_root_init        = true;
        m_selected_path[0] = m_last_click_path[0] = '\0';
        m_last_click_time                         = -1.f;
        m_nentries                                = 0; // Never show or act on the previous folder's cache.
        m_listing_valid                           = false;
        m_truncated                               = false;
        m_needs_refresh = m_reset_grid_input = m_reset_grid_scroll = true;
        return true;
    }

    bool ProjectViewPanel::NavigateHistory(int direction)
    {
        if ((direction != -1 && direction != 1) || !m_history_count)
            return false;
        const int next = static_cast<int>(m_history_cursor) + direction;
        if (next < 0 || next >= static_cast<int>(m_history_count))
            return false;
        auto path = VFSPath::Parse(m_history[(m_history_start + static_cast<uint32_t>(next)) % kHistoryCapacity]);
        if (path.Failed() || !NavigateTo(path.Value(), false))
            return false;
        m_history_cursor = static_cast<uint32_t>(next);
        return true;
    }

    void ProjectViewPanel::RequestRefresh()
    {
        m_needs_refresh = true;
        m_folder_tree.Invalidate();
    }

    // VFS listing
    void ProjectViewPanel::RefreshListing(IVFSContext* vfs)
    {
        m_nentries           = 0;
        m_listing_valid      = false;
        m_truncated          = false;
        m_reset_grid_input   = true;
        m_last_click_path[0] = '\0';
        m_last_click_time    = -1.f;
        if (!vfs)
            return;
        auto* eng = ZEngine::Engine::GetContext();
        if (!eng)
            return;
        auto scratch = ZGetScratch(&eng->VFSArena);
        auto res     = vfs->List(m_current_dir, scratch.Arena);
        if (res.Succeeded())
            CacheListing(res.Value());
        ZReleaseScratch(scratch);
        m_needs_refresh = false;
    }

    bool ProjectViewPanel::EntryLess(const Entry& a, const Entry& b)
    {
        if (a.is_dir != b.is_dir)
            return a.is_dir;
        const int order = NaturalNameCompare(a.name, b.name);
        return order ? order < 0 : secure_strcmp(a.full_path, b.full_path) < 0;
    }

    void ProjectViewPanel::CacheListing(const ZEngine::Core::Containers::Array<VFSDirEntry>& listing)
    {
        m_nentries      = 0;
        m_listing_valid = true;
        m_truncated     = false;
        for (size_t i = 0; i < listing.size(); ++i)
        {
            const auto& entry = listing[i];
            const auto  name  = entry.Path.Filename();
            if (!entry.Path.IsValid() || name.Empty() || name.Data[0] == '.')
                continue;
            const auto extension = entry.Path.Extension();
            if (!entry.IsDirectory && extension.Data && strcasecmp(extension.Data, ".meta") == 0)
                continue;
            // Look for one real extra entry. Hidden files and metadata sidecars
            // beyond the cap must not turn an exact-size listing into overflow.
            if (m_nentries == kMaxEntries)
            {
                m_truncated = true;
                break;
            }
            auto& cached = m_entries[m_nentries++];
            // VFSPath bounds both lengths. Copy only live bytes, not the whole
            // cache slot, and terminate reused names without clearing the cache.
            secure_memcpy(cached.name, sizeof(cached.name), name.Data, name.Length);
            cached.name[name.Length] = '\0';
            secure_memcpy(cached.full_path, sizeof(cached.full_path), entry.Path.CStr(), entry.Path.Length() + 1);
            cached.is_dir = entry.IsDirectory;
        }
        std::sort(m_entries, m_entries + m_nentries, EntryLess);
    }

    // Breadcrumb
    void ProjectViewPanel::DrawBreadcrumb(ZUIContext* ctx, float width)
    {
        const VFSPath  path                             = m_current_dir; // Clicking must not invalidate component views.
        const uint32_t count                            = path.ComponentCount() + 1;
        float          measured[VFS_MAX_COMPONENTS + 1] = {};
        char           name[VFS_MAX_PATH]               = {};
        auto           name_at                          = [&](uint32_t i) {
            if (!i)
                secure_strcpy(name, sizeof(name), "All");
            else
            {
                const auto part = path.ComponentAt(i - 1);
                secure_strncpy(name, sizeof(name), part.Data, part.Length);
            }
        };
        auto navigate = [&](uint32_t i) {
            if (!i)
                NavigateTo(VFSPath::Root());
            else
            {
                const auto part   = path.ComponentAt(i - 1);
                auto       target = VFSPath::Parse(path.CStr(), static_cast<size_t>(part.Data - path.CStr()) + part.Length);
                if (target.Succeeded())
                    NavigateTo(target.Value());
            }
        };
        float       full_width = 0.f;
        const auto* font       = ctx->GetFont(ZUIFontSize::Body);
        for (uint32_t i = 0; i < count; ++i)
        {
            name_at(i);
            const auto text      = ZUIPushStr(&ctx->FrameArena, name, static_cast<uint32_t>(secure_strlen(name)));
            float      extent[2] = {};
            if (font && font->Glyphs)
                ZUIMeasureText(font, text.Ptr, text.Len, extent);
            measured[i]  = extent[0];
            full_width  += extent[0];
        }
        constexpr float separator  = 10.f + 2.f * Metrics::Space8;
        full_width                += (count - 1) * separator;
        width                      = std::isfinite(width) ? fmaxf(width, 0.f) : 0.f;
        const bool collapsed       = count > 2 && full_width > width;
        auto*      bar             = ZUIBeginRow(ctx, "##pv_bc_row", ZFill(), ZPx(Metrics::HeightControl));
        bar->Flags                 = bar->Flags | ZUI_ClipChildren;
        bar->Gap                   = Metrics::Space8;
        bar->CrossAlign            = ZUIAlign::Center;
        float used                 = 0.f;
        auto  draw                 = [&](const char* text, const char* key, float available, bool link) {
            const auto hash = ZUIHashStr(key, static_cast<uint32_t>(secure_strlen(key)));
            auto*      box  = ZUILabelEllipsized(ctx, text, available, !link || ctx->HotKey == hash ? kText : kSecondary, ZUIFontSize::Body, key);
            if (link)
                box->Flags = box->Flags | ZUI_Clickable | ZUI_KeyboardActivate;
            const auto sig = link ? ZUISignalFromBox(ctx, box) : ZUISignal{};
            if (link)
                ZUISetTooltip(ctx, sig, text);
            used += box->Size[0].Value;
            return sig;
        };
        auto sep = [&]() {
            ZUIIconBox(ctx, ZUIIcon::ChevronRight, 10.f, kSecondary);
            used += separator;
        };
        bool more = false;
        for (uint32_t i = 0; i < count; ++i)
        {
            if (collapsed && i > 0 && i < count - 1)
            {
                if (i == 1)
                {
                    sep();
                    const auto sig = draw(ZUI_GLYPH_ELLIPSIS, "##pv_bc_more", fmaxf(width - used, 0.f), true);
                    if (sig.Flags & ZUI_SignalClicked)
                        OpenBelow(ctx, "##pv_bc_menu", sig.Key);
                }
                more = true;
                continue;
            }
            if (i)
                sep();
            name_at(i);
            char key[32] = {};
            snprintf(key, sizeof(key), "##pv_bc_%u", i);
            const auto sig = draw(name, key, i == count - 1 ? fmaxf(width - used, 0.f) : measured[i], i != count - 1);
            if (sig.Flags & ZUI_SignalClicked)
                navigate(i);
        }
        ZUIEndRow(ctx);
        if (more && ZUIBeginPopup(ctx, "##pv_bc_menu"))
        {
            for (uint32_t i = 1; i + 1 < count; ++i)
            {
                name_at(i);
                char key[32] = {};
                snprintf(key, sizeof(key), "##pv_bcm_%u", i);
                if (ZUIMenuItem(ctx, name, true, key))
                    navigate(i);
            }
            ZUIEndPopup(ctx);
        }
    }

    void ProjectViewPanel::DrawToolbar(ZUIContext* ctx, float width)
    {
        auto* bar       = ZUIBeginRow(ctx, "##pv_toolbar", ZFill(), ZPx(kToolbarH));
        bar->Flags      = bar->Flags | ZUI_ClipChildren;
        bar->Padding[0] = bar->Padding[2]  = Metrics::Space10;
        bar->Gap                           = Metrics::Space8;
        bar->CrossAlign                    = ZUIAlign::Center;
        float      fixed_width             = 2.f * Metrics::Space10 + 6.f * Metrics::Space8 + 13.f + 13.f + 14.f;
        const auto add                     = ZUIEditorControl(ctx, "##pv_add", ZUIIcon::Plus, "Add", true, true, nullptr, false, true);
        fixed_width                       += ControlWidth(bar->LastChild);
        if (add.Flags & ZUI_SignalClicked)
            OpenBelow(ctx, "##pv_add_menu", add.Key);
        const auto import_sig  = ZUIEditorControl(ctx, "##pv_import", ZUIIcon::Upload, "Import", false);
        fixed_width           += ControlWidth(bar->LastChild);
        if (import_sig.Flags & ZUI_SignalClicked)
        {
            auto* editor = m_layer && m_layer->CurrentApp ? reinterpret_cast<Tetragrama::EditorPtr>(m_layer->CurrentApp) : nullptr;
            if (editor && editor->Configuration)
            {
                editor->Configuration->ShowImporter  = true;
                editor->Configuration->FocusImporter = true;
            }
        }
        ZUIBeginDisabled(ctx);
        ZUIEditorControl(ctx, "##pv_saveall", ZUIIcon::Save, "Save All", false);
        ZUIEndDisabled(ctx);
        fixed_width += ControlWidth(bar->LastChild);

        auto arrow   = [&](const char* key, ZUIIcon icon, bool enabled, const char* tooltip) {
            auto* box    = ZUIPushBox(ctx, key, static_cast<uint32_t>(secure_strlen(key)), ZUI_DrawIcon | (enabled ? ZUI_Clickable | ZUI_KeyboardActivate : ZUI_Disabled));
            box->Size[0] = box->Size[1] = ZPx(13.f);
            box->IconId                 = static_cast<uint16_t>(icon);
            secure_memcpy(box->TextColor, sizeof(box->TextColor), enabled ? kText : kDisabledText, sizeof(box->TextColor));
            const auto sig = ZUISignalFromBox(ctx, box);
            ZUIPopBox(ctx);
            ZUISetTooltip(ctx, sig, tooltip);
            return (sig.Flags & ZUI_SignalClicked) != 0;
        };
        if (arrow("##pv_back", ZUIIcon::ChevronLeft, m_history_cursor > 0, "Back"))
            NavigateHistory(-1);
        if (arrow("##pv_forward", ZUIIcon::ChevronRight, m_history_cursor + 1 < m_history_count, "Forward"))
            NavigateHistory(1);
        ZUIIconBox(ctx, ZUIIcon::Folder, 14.f, kSecondary);
        DrawBreadcrumb(ctx, fmaxf(width - fixed_width, 0.f));

        auto* line         = ZUIPushBox(ctx, "##pv_tb_line", 12, ZUI_DrawBackground | ZUI_FloatX | ZUI_FloatY);
        line->Size[0]      = ZPct(1.f);
        line->Size[1]      = ZPx(1.f);
        line->FloatPos[1]  = kToolbarH - 1.f;
        line->EdgeSoftness = 0.f;
        ZUIBoxSetColorArr(line, kBorder);
        ZUIPopBox(ctx);
        ZUIEndRow(ctx);

        if (BeginBrowserMenu(ctx, "##pv_add_menu", 180.f))
        {
            DrawCreateMenu(ctx, m_current_dir);
            ZUIEndPopup(ctx);
        }
    }

    // Sources tree: stable indices, lazy listings, and cached ancestor-preserving search.
    void ProjectViewPanel::DrawSourcesTree(ZUIContext* ctx, IVFSContext* vfs)
    {
        if (!vfs)
            return;

        m_folder_tree.UpdateSearch(*vfs, ctx->FrameArena, m_folder_search);
        const bool searching = m_folder_search[0] != '\0';
        uint32_t*  stk       = ZPushArray<uint32_t>(&ctx->FrameArena, FolderTreeCache::Capacity);
        uint32_t   sp        = 0;
        for (uint32_t i = m_folder_tree.Nodes[0].LastChild; i != FolderTreeCache::None; i = m_folder_tree.Nodes[i].PrevSibling)
            stk[sp++] = i;
        const auto* scroll         = ZUIStateGetOrInsert(&ctx->StateStore, ZUIHashStr("##pv_src_scroll", 15));
        const float scroll_y       = scroll ? scroll->ScrollY : 0.f;
        const float height         = scroll && scroll->ScreenMaxY > scroll->ScreenMinY ? scroll->ScreenMaxY - scroll->ScreenMinY : (float) ctx->ScreenH;
        const float first_y        = fmaxf(0.f, scroll_y - 46.f);
        const float last_y         = scroll_y + height + 46.f;
        float       row_y          = searching ? 0.f : 23.f; // The root row is hidden during search.
        float       skipped        = 0.f;
        uint32_t    listing_budget = 16;
        bool        has_match      = false;

        // HOT PATH — runs every frame, no heap allocation allowed.
        while (sp > 0)
        {
            const uint32_t index = stk[--sp];
            const auto&    e     = m_folder_tree.Nodes[index];
            if (searching && !e.Visible)
                continue;
            has_match             = true;
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
                col->Gap          = Metrics::Space7;
                col->CrossAlign   = ZUIAlign::Center;
                col->EdgeSoftness = 0.f;
                {
                    const float rest[4] = {0.f, 0.f, 0.f, 0.f};
                    ZUIBoxSetColorArr(col, is_sel ? kSelection : (ctx->HotKey == col->Key ? kHover : rest));
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
                ZUIIconBox(ctx, (is_open || is_sel) ? ZUIIcon::FolderOpen : ZUIIcon::Folder, 13.f, is_sel ? kSelectedText : kSecondary);
                snprintf(chk, sizeof(chk), "##stname_%llu", (unsigned long long) key);
                ZUILabelTight(ctx, name, kText, ZUIFontSize::Body, chk);
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
                        NavigateTo(pr.Value());
                    }
                }

                // Context menu — unique key per node to prevent wrong-node deletion
                char ctx_key[48] = {};
                snprintf(ctx_key, sizeof(ctx_key), "##stctx_%llu", (unsigned long long) key);
                if (BeginBrowserMenu(ctx, ctx_key, 200.f, &sig))
                {
                    const auto path = VFSPath::Parse(e.Path);
                    if (path.Succeeded())
                        DrawItemMenu(ctx, path.Value(), true);
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
            DrawTreeNote(ctx, "##pv_src_searching", "Searching folders...", kSecondary);
        else if (searching && !has_match && !m_folder_tree.Incomplete)
        {
            char message[sizeof(m_folder_search) + 32] = {};
            snprintf(message, sizeof(message), "No folders match " ZUI_GLYPH_LQUOTE "%s" ZUI_GLYPH_RQUOTE ".", m_folder_search);
            DrawTreeNote(ctx, "##pv_src_no_match", message, kSecondary);
        }
        if (m_folder_tree.Incomplete)
        {
            constexpr float warning[4] = ZUI_TOKEN(Tokens::AccentOrange);
            DrawTreeNote(ctx, "##pv_src_incomplete", "Folder index incomplete; refresh to retry.", warning);
        }
    }

    // Filters panel (middle column)
    void ProjectViewPanel::DrawFilters(ZUIContext* ctx)
    {
        static const char* kCategories[] = {"All", "Folders", "Scenes", "Models", "Materials", "Textures", "Scripts", "Shaders", "Other"};
        const float        saved_width   = ctx->PopupDesiredW;
        ctx->PopupDesiredW               = 150.f; // 160 outer width minus 4 px padding + 1 px border on each side.
        const bool open                  = ZUIBeginPopup(ctx, "##pv_filter_menu");
        ctx->PopupDesiredW               = saved_width;
        if (open)
        {
            for (float& padding : ctx->Current->Padding)
                padding = Metrics::Space4 + ctx->Current->BorderThickness;
            for (const char* category : kCategories)
            {
                const bool active = (ZEngine::Helpers::secure_strcmp(m_type_filter, category) == 0);
                if (ZUIMenuItemEx(ctx, category, nullptr, active) && !active)
                {
                    secure_strcpy(m_type_filter, sizeof(m_type_filter), category);
                    m_reset_grid_scroll  = true;
                    m_last_click_path[0] = '\0';
                    m_last_click_time    = -1.f;
                }
            }
            ZUIEndPopup(ctx);
        }
    }

    void ProjectViewPanel::DrawFilterControl(ZUIContext* ctx)
    {
        const bool active = secure_strcmp(m_type_filter, "All") != 0;
        const auto sig    = ZUIEditorControl(ctx, "##pv_filters", ZUIIcon::ListFilter, active ? m_type_filter : "Filters", true, active, kSecondary);
        // Keep the active glyph secondary without overriding disabled styling.
        if (active && !ctx->Disabled)
        {
            auto* icon = ctx->Current->LastChild->FirstChild;
            secure_memcpy(icon->TextColor, sizeof(icon->TextColor), kSecondary, sizeof(icon->TextColor));
        }
        if (sig.Flags & ZUI_SignalClicked)
            OpenBelow(ctx, "##pv_filter_menu", sig.Key);
    }

    void ProjectViewPanel::HandleShortcuts(ZUIContext* ctx, const ZUIBox* root)
    {
        if ((!ctx->F2Pressed && !ctx->DeletePressed) || !ctx->FocusKey || ctx->Disabled || ctx->TextInputActive || ctx->DragSourceKey || ctx->CtrlDown || ctx->AltDown || ctx->ShiftDown || ctx->PopupStackSize || ctx->PendingPopupKey || ctx->ModalBox || m_modal != Modal::None || !m_selected_path[0])
            return;

        // Do not rely on the preferred tab scope, which can outlive focus after
        // clicking elsewhere. Only a currently built browser control may own it.
        bool owns_focus = false;
        for (const auto* box = root; box;)
        {
            if (box->Key == ctx->FocusKey && (box->Flags & ZUI_Clickable) && !(box->Flags & ZUI_Disabled))
            {
                owns_focus = true;
                break;
            }
            if (box->FirstChild)
                box = box->FirstChild;
            else
            {
                while (box != root && !box->NextSib)
                    box = box->Parent;
                box = box == root ? nullptr : box->NextSib;
            }
        }
        if (!owns_focus)
            return;
        for (int i = 0; i < m_nentries; ++i)
        {
            const auto& entry = m_entries[i];
            if (secure_strcmp(m_selected_path, entry.full_path) != 0 || !PassesFilters(entry, m_search, m_type_filter))
                continue;
            const auto path = VFSPath::Parse(entry.full_path);
            if (path.Failed() || path.Value().IsRoot())
                return;
            OpenItemModal(ctx->F2Pressed ? Modal::RenameItem : Modal::DeleteItem, path.Value(), entry.is_dir);
            ctx->F2Pressed = ctx->DeletePressed = false;
            return;
        }
    }

    // Content grid (right area)
    void ProjectViewPanel::DrawEmptyState(ZUIContext* ctx, float width)
    {
        const bool  filtered   = m_search[0] || secure_strcmp(m_type_filter, "All") != 0;
        const float text_width = std::isfinite(width) ? fmaxf(width - Metrics::Space12, 0.f) : 0.f;
        auto*       message    = ZUIBeginColumn(ctx, "##pv_empty", ZFill(), ZFill());
        message->Flags         = message->Flags | ZUI_ClipChildren;
        message->MainAlign = message->CrossAlign = ZUIAlign::Center;
        message->Padding[0] = message->Padding[2] = Metrics::Space12;
        message->Padding[1]                       = Metrics::Space9;
        message->Gap                              = Metrics::Space6;
        auto* icon                                = ZUIIconBox(ctx, filtered ? ZUIIcon::Search : ZUIIcon::FolderOpen, 38.f, kDisabledText);
        icon->Flags                               = icon->Flags | ZUI_AssetIcon;
        char title[sizeof(m_search) + 32]         = {};
        char hint[128]                            = {};
        if (!filtered)
        {
            secure_strcpy(title, sizeof(title), "This folder is empty");
            secure_strcpy(hint, sizeof(hint), "Use Add to create a file or a folder, or Import to bring in assets.");
        }
        else
        {
            if (m_search[0])
                snprintf(title, sizeof(title), "No items match " ZUI_GLYPH_LQUOTE "%s" ZUI_GLYPH_RQUOTE, m_search);
            else
                snprintf(title, sizeof(title), "No items in %s", m_type_filter);
            if (secure_strcmp(m_type_filter, "All") != 0)
                snprintf(hint, sizeof(hint), "The search covers this folder only. %s is the active filter.", m_type_filter);
            else
                secure_strcpy(hint, sizeof(hint), "The search covers this folder only.");
        }
        ZUILabelEllipsized(ctx, title, text_width, kSecondary, ZUIFontSize::Heading, "##pv_empty_title");
        ZUILabelEllipsized(ctx, hint, fminf(380.f, text_width), kSecondary, ZUIFontSize::Small, "##pv_empty_hint");
        if (filtered && (ZUIButton(ctx, "Clear filters##pv_clear_filters", ZPx(96.f), ZPx(Metrics::HeightControl), ZUIButtonKind::Secondary).Flags & ZUI_SignalClicked))
        {
            m_search[0] = m_selected_path[0] = m_last_click_path[0] = '\0';
            secure_strcpy(m_type_filter, sizeof(m_type_filter), "All");
            m_last_click_time   = -1.f;
            m_reset_grid_scroll = true;
        }
        ZUIEndColumn(ctx);
    }

    int ProjectViewPanel::DrawGrid(ZUIContext* ctx, float pw, float height)
    {
        // Card constants
        m_thumbnail_scale                = std::isfinite(m_thumbnail_scale) ? std::clamp(m_thumbnail_scale, .75f, 1.5f) : 1.f;
        const float            scale     = m_thumbnail_scale;
        const float            kTileW    = 110.f * scale;
        static constexpr float kTileGap  = Metrics::Space10;
        const float            kCardW    = kTileW + kTileGap;
        const float            kCardH    = 128.f * scale;
        const float            kPitchY   = kCardH + kTileGap;
        static constexpr float kTop      = 9.f;
        static constexpr float kBottom   = 16.f;
        const int              col_count = std::isfinite(pw) ? static_cast<int>(std::clamp(pw / kCardW, 1.f, static_cast<float>(kMaxEntries))) : 1;

        // Cache-slot keys remain stable through filtering/reflow and consume at
        // most kMaxEntries persistent states, not one state per visited path.
        // A listing refresh reuses those slots: retire their interaction state
        // first so a stale popup, hover or click cannot act on a different asset.
        if (m_reset_grid_input)
        {
            for (int i = 0; i < kMaxEntries; ++i)
            {
                char key[32] = {};
                snprintf(key, sizeof(key), "##pvc_%d", i);
                const auto hash = ZUIHashStr(key, static_cast<uint32_t>(secure_strlen(key)));
                for (auto* state : {&ctx->HotKey, &ctx->ActiveKey, &ctx->FocusKey, &ctx->SavedFocusKey})
                    if (*state == hash)
                        *state = 0;
                if (ctx->TooltipHoverKey == hash)
                {
                    ctx->TooltipHoverKey  = 0;
                    ctx->TooltipHoverTime = 0.f;
                }
                if (ctx->DragSourceKey == hash)
                {
                    ctx->DragSourceKey   = 0;
                    ctx->DragPayloadLen  = 0;
                    ctx->DragPayload[0]  = '\0';
                    ctx->DragPayloadKind = ZUIDragPayloadKind::Opaque;
                }
                snprintf(key, sizeof(key), "##pvgctx_%d", i);
                const auto popup_hash = ZUIHashStr(key, static_cast<uint32_t>(secure_strlen(key)));
                if (ctx->PendingPopupKey == popup_hash)
                    ctx->PendingPopupKey = 0;
                for (uint32_t popup = 0; popup < ctx->PopupStackSize; ++popup)
                    if (ctx->PopupStack[popup].Key == popup_hash)
                    {
                        ctx->PopupStackSize = popup;
                        break;
                    }
            }
            m_reset_grid_input = false;
        }

        // Collect filtered entries
        uint16_t vis[kMaxEntries] = {};
        int      nvis             = 0;
        bool     selected_visible = false;
        for (int i = 0; i < m_nentries; ++i)
            if (PassesFilters(m_entries[i], m_search, m_type_filter))
            {
                vis[nvis++] = static_cast<uint16_t>(i);
                if (!selected_visible && m_selected_path[0])
                    selected_visible = secure_strcmp(m_selected_path, m_entries[i].full_path) == 0;
            }
        if (!selected_visible && m_selected_path[0])
        {
            m_selected_path[0] = m_last_click_path[0] = '\0';
            m_last_click_time                         = -1.f;
        }

        if (m_reset_grid_scroll)
        {
            if (auto* scroll = ZUIStateGetOrInsert(&ctx->StateStore, ZUIHashStr("##pv_grid_scroll", 16)))
                scroll->ScrollY = scroll->ScrollYTarget = 0.f;
            m_reset_grid_scroll = false;
        }
        ZUIBox* grid_scroll    = ZUIBeginScrollRegion(ctx, "##pv_grid_scroll", ZFill(), ZFill());
        grid_scroll->Flags     = grid_scroll->Flags | ZUI_NavList;
        auto* scroll           = ZUIStateGetOrInsert(&ctx->StateStore, grid_scroll->Key);
        height                 = std::isfinite(height) ? fmaxf(height, 0.f) : 0.f;
        const int   row_count  = (nvis + col_count - 1) / col_count;
        const float max_scroll = nvis ? fmaxf(kTop + row_count * kPitchY + kBottom - height, 0.f) : 0.f;
        if (scroll)
        {
            // Filtering/reflow may shrink the list. Clamp both the current and
            // smoothed destination before computing which rows to build.
            scroll->MaxScrollY    = max_scroll;
            scroll->ScrollY       = std::isfinite(scroll->ScrollY) ? std::clamp(scroll->ScrollY, 0.f, max_scroll) : 0.f;
            scroll->ScrollYTarget = std::isfinite(scroll->ScrollYTarget) ? std::clamp(scroll->ScrollYTarget, 0.f, max_scroll) : scroll->ScrollY;
        }
        const float scroll_y  = scroll ? scroll->ScrollY : 0.f;
        const int   first_row = std::clamp(static_cast<int>(fmaxf(0.f, scroll_y - kTop) / kPitchY) - 1, 0, row_count);
        const int   last_row  = std::min(row_count, static_cast<int>(fminf((scroll_y + height) / kPitchY, static_cast<float>(row_count))) + 2);
        if (nvis)
            ZUISpacer(ctx, kTop);
        if (first_row)
            ZUISpacer(ctx, first_row * kPitchY);

        // HOT PATH — O(entries) filtering, O(visible tiles) boxes, no heap.
        // One overscan row on each side avoids gaps during smooth scrolling.
        for (int r = first_row; r < last_row; ++r)
        {
            char rk[32] = {};
            snprintf(rk, sizeof(rk), "##pvrow_%d", r);
            ZUIBeginRow(ctx, rk, ZFill(), ZPx(kCardH));
            ZUISpacer(ctx, 12.f);

            for (int c = 0; c < col_count; ++c)
            {
                int ei = r * col_count + c;
                if (ei >= nvis)
                    break;
                else
                {
                    const int    entry_index = vis[ei];
                    const Entry& e           = m_entries[entry_index];
                    bool         sel         = (ZEngine::Helpers::secure_strcmp(m_selected_path, e.full_path) == 0);
                    bool         hov         = {};

                    // Figma "Asset tile": 110 x 128, 1 px inside border, 79 px thumbnail,
                    // 3 px type strip, 46 px metadata (Body name, Small type caption).
                    char         ck[32]      = {};
                    snprintf(ck, sizeof(ck), "##pvc_%d", entry_index);
                    float   cw   = kTileW;
                    ZUIBox* card = ZUIBeginColumn(ctx, ck, ZPx(cw), ZPx(kCardH));
                    card->Flags  = card->Flags | ZUI_DrawBackground | ZUI_Clickable | ZUI_ClipChildren | ZUI_KeyboardActivate;
                    hov          = (ctx->HotKey == card->Key);
                    ZUIBoxSetColorArr(card, sel ? kSelection : hov ? kHover : kSurface);
                    ZUIBoxSetCornerRadius(card, Metrics::Radius3);
                    card->EdgeSoftness   = 0.5f;

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
                        glyph = ZUIIcon::Engine;
                        kind  = "Scene";
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
                        snprintf(tk, sizeof(tk), "##pvth_%d", entry_index);
                        ZUIBox* th       = ZUIBeginColumn(ctx, tk, ZFill(), ZPx(79.f * scale));
                        th->Flags        = th->Flags | ZUI_DrawBackground;
                        th->MainAlign    = ZUIAlign::Center;
                        th->CrossAlign   = ZUIAlign::Center;
                        th->EdgeSoftness = 0.f;
                        ZUIBoxSetColorArr(th, kControl);
                        th->CornerRadii[0] = th->CornerRadii[1] = Metrics::Radius3;
                        auto* icon                              = ZUIIconBox(ctx, glyph, 38.f * scale, kSecondary);
                        icon->Flags                             = icon->Flags | ZUI_AssetIcon;
                        ZUIEndColumn(ctx);
                    }
                    {
                        char sk[32] = {};
                        snprintf(sk, sizeof(sk), "##pvst_%d", entry_index);
                        ZUIBox* st  = ZUIPushBox(ctx, sk, (uint32_t) ZEngine::Helpers::secure_strlen(sk), ZUI_DrawBackground);
                        st->Size[0] = ZFill();
                        st->Size[1] = ZPx(3.f * scale);
                        ZUIBoxSetColorArr(st, strip);
                        st->EdgeSoftness = 0.f;
                        ZUIPopBox(ctx);
                    }
                    // Metadata
                    {
                        char mk[32] = {};
                        snprintf(mk, sizeof(mk), "##pvmd_%d", entry_index);
                        ZUIBox* md     = ZUIBeginColumn(ctx, mk, ZFill(), ZPx(46.f * scale));
                        md->Padding[0] = md->Padding[2] = Metrics::Space5 * scale;
                        md->Padding[1] = md->Padding[3] = Metrics::Space8 * scale;
                        md->Gap                         = Metrics::Space4 * scale;
                        md->EdgeSoftness                = 0.f;
                        snprintf(mk, sizeof(mk), "##pvname_%d", entry_index);
                        ZUILabelEllipsized(ctx, e.name, cw - 10.f * scale, kText, ZUIFontSize::Body, mk, scale);
                        ZUILabelTight(ctx, kind, sel ? kSelectedText : kSecondary, ZUIFontSize::Small, nullptr, scale);
                        ZUIEndColumn(ctx);
                    }

                    // Paint the inside border after the thumbnail so opaque
                    // children cannot cover its top and side strokes.
                    char border_key[40] = {};
                    snprintf(border_key, sizeof(border_key), "##pvbd_%d", entry_index);
                    auto* border    = ZUIPushBox(ctx, border_key, static_cast<uint32_t>(secure_strlen(border_key)), ZUI_DrawBorder | ZUI_FloatX | ZUI_FloatY);
                    border->Size[0] = border->Size[1] = ZPct(1.f);
                    border->BorderThickness           = 1.f;
                    secure_memcpy(border->BorderColor, sizeof(border->BorderColor), sel ? kSelectedBorder : hov ? kStrong : kBorder, sizeof(border->BorderColor));
                    ZUIBoxSetCornerRadius(border, Metrics::Radius3);
                    ZUIPopBox(ctx);

                    ZUISignal card_sig = ZUISignalFromBox(ctx, card);
                    ZUIEndColumn(ctx); // card
                    ZUISetTooltip(ctx, card_sig, e.name);

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
                                NavigateTo(pr.Value());
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

                    // Right-click context menu — unique key per cached entry, so
                    // opening one card's menu doesn't also open every other card's.
                    char ctx_key[32] = {};
                    snprintf(ctx_key, sizeof(ctx_key), "##pvgctx_%d", entry_index);
                    if (BeginBrowserMenu(ctx, ctx_key, 200.f, &card_sig))
                    {
                        // Right-click also selects the card, so the visible highlight
                        // always matches the item the menu is acting on.
                        secure_strncpy(m_selected_path, sizeof(m_selected_path), e.full_path, sizeof(m_selected_path) - 1);

                        const auto path = VFSPath::Parse(e.full_path);
                        if (path.Succeeded())
                            DrawItemMenu(ctx, path.Value(), e.is_dir);
                        ZUIEndPopup(ctx);
                    }
                }
                ZUISpacer(ctx, kTileGap);
            }
            ZUIEndRow(ctx);
            ZUISpacer(ctx, kTileGap);
        }

        if (last_row < row_count)
            ZUISpacer(ctx, (row_count - last_row) * kPitchY);
        if (nvis)
            ZUISpacer(ctx, kBottom);
        else if (m_listing_valid)
            DrawEmptyState(ctx, pw);
        ZUIEndScrollRegion(ctx);

        // Right-click on empty grid space → New File / New Folder. Cards sit deeper
        // in the box tree and always win the hit test, so this never fires over one.
        ZUISignal bg_sig = ZUISignalFromBox(ctx, grid_scroll);
        if (BeginBrowserMenu(ctx, "##pv_grid_bgctx", 180.f, &bg_sig))
        {
            DrawCreateMenu(ctx, m_current_dir);
            ZUIEndPopup(ctx);
        }
        return nvis;
    }

    void ProjectViewPanel::OpenItemModal(Modal modal, const VFSPath& target, bool is_dir)
    {
        m_modal          = modal;
        m_modal_target   = target;
        m_modal_is_dir   = is_dir;
        m_modal_error[0] = '\0';
        m_modal_opened = m_modal_input_pending = false;
        if (modal == Modal::CreateFile || modal == Modal::CreateFolder)
            secure_strcpy(m_modal_buf, sizeof(m_modal_buf), modal == Modal::CreateFile ? "NewFile.txt" : "NewFolder");
        else
            target.CopyFilename(m_modal_buf, sizeof(m_modal_buf));
    }

    void ProjectViewPanel::DrawCreateMenu(ZUIContext* ctx, const VFSPath& parent)
    {
        if (ZUIMenuItemEx(ctx, "New File", nullptr, false, true, static_cast<int>(ZUIIcon::Plus)))
            OpenItemModal(Modal::CreateFile, parent, false);
        if (ZUIMenuItemEx(ctx, "New Folder", nullptr, false, true, static_cast<int>(ZUIIcon::Folder)))
            OpenItemModal(Modal::CreateFolder, parent, true);
    }

    void ProjectViewPanel::DrawItemMenu(ZUIContext* ctx, const VFSPath& target, bool is_dir)
    {
        if (is_dir)
        {
            if (ZUIMenuItemEx(ctx, "Open", nullptr))
                NavigateTo(target);
            if (ZUIMenuItemEx(ctx, "New Folder", nullptr, false, true, static_cast<int>(ZUIIcon::Folder)))
                OpenItemModal(Modal::CreateFolder, target, true);
            ZUISeparator(ctx);
        }
        if (ZUIMenuItemEx(ctx, "Rename", "F2"))
            OpenItemModal(Modal::RenameItem, target, is_dir);
        if (ZUIMenuItemEx(ctx, "Delete", "Del", false, true, static_cast<int>(ZUIIcon::X)))
            OpenItemModal(Modal::DeleteItem, target, is_dir);
    }

    void ProjectViewPanel::UpdateNavigationAfterMutation(const VFSPath& source, const VFSPath* destination)
    {
        const bool current_affected = source.IsPrefixOf(m_current_dir);
        auto       current          = m_current_dir;
        if (current_affected)
        {
            const auto rebased = destination ? RebasePath(current, source, *destination) : VFSResult<VFSPath>::Fail(VFSError::NotFound);
            current            = rebased.Succeeded() ? rebased.Value() : (destination ? *destination : source.Parent());
        }

        // Compact the bounded ring in visit order; never shift a multi-megabyte
        // tree or retain visits into a removed subtree. Destinations trail reads.
        uint32_t kept = 0, cursor = 0;
        for (uint32_t i = 0; i < m_history_count; ++i)
        {
            const auto path = VFSPath::Parse(m_history[(m_history_start + i) % kHistoryCapacity]);
            if (path.Failed())
                continue;
            auto updated = path;
            if (source.IsPrefixOf(path.Value()))
            {
                if (!destination)
                    continue;
                updated = RebasePath(path.Value(), source, *destination);
                if (updated.Failed())
                    continue;
            }
            auto& slot = m_history[(m_history_start + kept) % kHistoryCapacity];
            // The source may be this same slot; copy from the parsed value instead.
            secure_memcpy(slot, sizeof(slot), updated.Value().CStr(), updated.Value().Length() + 1);
            if (i <= m_history_cursor)
                cursor = kept;
            ++kept;
        }
        m_history_count  = kept;
        m_history_cursor = cursor;
        if (current_affected)
        {
            const bool represented = kept && secure_strcmp(m_history[(m_history_start + cursor) % kHistoryCapacity], current.CStr()) == 0;
            NavigateTo(current, !represented);
        }
        m_selected_path[0] = m_last_click_path[0] = '\0';
        m_last_click_time                         = -1.f;
    }

    bool ProjectViewPanel::SubmitModal(IVFSContext* vfs)
    {
        m_modal_error[0] = '\0';
        const auto fail  = [&](const char* message) {
            secure_strcpy(m_modal_error, sizeof(m_modal_error), message);
            return false;
        };
        if (!vfs)
            return fail("Filesystem unavailable; try again");
        const bool creating = m_modal == Modal::CreateFile || m_modal == Modal::CreateFolder;
        if (m_modal == Modal::None || !m_modal_target.IsValid() || (!creating && m_modal_target.IsRoot()))
            return fail("This target cannot be changed");
        const auto source_stat = vfs->Stat(m_modal_target);
        if (source_stat.Failed())
            return fail("Target unavailable; refresh and try again");
        if (source_stat.Value().IsDirectory != (creating || m_modal_is_dir))
            return fail("Target changed; refresh and try again");

        VFSResult<void> result      = VFSResult<void>::Fail(VFSError::Unsupported);
        VFSPath         destination = {};
        if (m_modal == Modal::DeleteItem)
            result = m_modal_is_dir ? vfs->RemoveAll(m_modal_target) : vfs->Remove(m_modal_target);
        else
        {
            if (const auto* error = NameError(m_modal_buf))
                return fail(error);
            const auto parent = creating ? m_modal_target : m_modal_target.Parent();
            const auto path   = parent.Append(m_modal_buf);
            if (path.Failed() || path.Value().Parent() != parent)
                return fail("Name or path is too long");
            destination = path.Value();
            if (!creating && destination == m_modal_target)
                return true; // Unchanged name is a successful no-op, not a replace.
            const auto existing = vfs->Stat(destination);
            if (existing.Succeeded())
                return fail("A file or folder with this name already exists");
            if (existing.Error() != VFSError::NotFound)
                return fail("Cannot check this name; check permissions");
            if (m_modal == Modal::CreateFile)
            {
                auto opened = vfs->Open(destination, VFSOpenFlags::Write | VFSOpenFlags::Create | VFSOpenFlags::Exclusive);
                if (opened.Succeeded())
                {
                    // Close through the owner so the pooled file slot is returned.
                    result = opened.Value()->Close();
                    vfs->Close(opened.Value());
                }
                else
                    result = VFSResult<void>::Fail(opened.Error());
            }
            else if (m_modal == Modal::CreateFolder)
                result = vfs->CreateDirExclusive(destination);
            else if (m_modal == Modal::RenameItem)
                result = vfs->RenameNoReplace(m_modal_target, destination);
        }
        if (result.Failed())
        {
            if (result.Error() == VFSError::AlreadyExists)
                return fail("A file or folder with this name already exists");
            if (result.Error() == VFSError::Unsupported)
                return fail("This filesystem does not support the operation");
            return fail(m_modal == Modal::DeleteItem ? "Delete failed " ZUI_GLYPH_EMDASH " check permissions" : m_modal == Modal::RenameItem ? "Rename failed " ZUI_GLYPH_EMDASH " check permissions or name conflict" : "Operation failed " ZUI_GLYPH_EMDASH " check name or permissions");
        }
        if (!creating)
            UpdateNavigationAfterMutation(m_modal_target, m_modal == Modal::RenameItem ? &destination : nullptr);
        RequestRefresh();
        return true;
    }

    // Modals
    void ProjectViewPanel::DrawModals(ZUIContext* ctx, IVFSContext* vfs)
    {
        if (m_modal == Modal::None)
            return;
        constexpr float width       = 288.f;
        constexpr float padding     = Metrics::Space16 + 1.f; // Inside border.
        constexpr float inner_width = width - 2.f * padding;
        if (!m_modal_opened)
        {
            m_modal_error[0] = '\0';
            ZUIOpenPopup(ctx, "##pv_modal", fmaxf(0.f, (ctx->ScreenW - width) * 0.5f), fmaxf(0.f, (ctx->ScreenH - 115.f) * 0.5f));
            m_modal_opened = m_modal_input_pending = true;
            return; // Opening is staged until the frame ends.
        }
        const float saved_width = ctx->PopupDesiredW;
        ctx->PopupDesiredW      = inner_width;
        const bool open         = ZUIBeginPopup(ctx, "##pv_modal");
        ctx->PopupDesiredW      = saved_width;
        if (!open)
        {
            m_modal               = Modal::None;
            m_modal_input_pending = false;
            return;
        }
        if (ctx->EscapePressed)
        {
            m_modal               = Modal::None;
            m_modal_input_pending = false;
            ZUIClosePopup(ctx);
            ZUIEndPopup(ctx);
            return;
        }

        auto* popup                    = ctx->Current;
        popup->Size[0]                 = ZPx(width);
        popup->ShadowKind              = ZUIShadowKind::Modal;
        constexpr float panel_color[4] = ZUI_TOKEN(Tokens::BgPanel);
        ZUIBoxSetColorArr(popup, panel_color);
        ZUIBoxSetCornerRadius(popup, Metrics::Radius6);
        for (float& value : popup->Padding)
            value = padding;
        for (int c = 0; c < 4; ++c)
            popup->BorderColor[c] = kStrong[c];

        const bool     deleting     = m_modal == Modal::DeleteItem;
        const char*    action       = deleting ? "Delete##pvd" : m_modal == Modal::RenameItem ? "Rename##pvr" : "Create##pvc";
        constexpr char cancel_key[] = "Cancel##pv_modal_cancel";
        // The first focus pass saves the opener. On the following build, make
        // Delete the default action without replacing that saved browser focus.
        if (deleting && m_modal_input_pending && ctx->FocusTrapKey == popup->Key)
        {
            ctx->FocusKey         = ZUIHashStr(action, static_cast<uint32_t>(secure_strlen(action)));
            m_modal_input_pending = false;
        }

        auto* content                 = ZUIBeginColumn(ctx, "##pv_modal_content", ZFill(), ZFit());
        content->Gap                  = Metrics::Space10;
        char title[VFS_MAX_PATH + 16] = {};
        if (deleting)
        {
            char name[VFS_MAX_PATH] = {};
            m_modal_target.CopyFilename(name, sizeof(name));
            snprintf(title, sizeof(title), "Delete " ZUI_GLYPH_LQUOTE "%s" ZUI_GLYPH_RQUOTE "?", name);
        }
        else
            secure_strcpy(title, sizeof(title), m_modal == Modal::RenameItem ? "Rename" : m_modal == Modal::CreateFile ? "New File" : "New Folder");
        ZUILabelEllipsized(ctx, title, inner_width, kText, ZUIFontSize::Heading, "##pv_modal_title");

        ZUIBox* field = nullptr;
        if (deleting)
        {
            auto* body = ZUIBeginColumn(ctx, "##pv_modal_body", ZFill(), ZFit());
            body->Gap  = 0.f;
            ZUILabelTight(ctx, m_modal_is_dir ? "This folder and everything in it will be deleted." : "This file will be deleted. This cannot be undone.", kSecondary, ZUIFontSize::Body, "##pv_modal_body_1");
            if (m_modal_is_dir)
                ZUILabelTight(ctx, "This cannot be undone.", kSecondary, ZUIFontSize::Body, "##pv_modal_body_2");
            ZUIEndColumn(ctx);
        }
        else
        {
            if (ZUITextField(ctx, "##pv_mi", m_modal_buf, sizeof(m_modal_buf), inner_width))
                m_modal_error[0] = '\0';
            field          = content->LastChild;
            field->Size[1] = ZPx(Metrics::HeightField);
            if (m_modal_input_pending)
            {
                if (auto* state = ZUIStateGetOrInsert(&ctx->StateStore, field->Key))
                {
                    state->UserData    = static_cast<float>(secure_strlen(m_modal_buf));
                    state->SelectStart = 0;
                }
                if (ctx->UndoFieldKey == field->Key)
                {
                    ctx->UndoFieldKey = 0;
                    ctx->UndoTop = ctx->RedoTop = 0;
                }
                m_modal_input_pending = false;
            }
        }

        if (m_modal_error[0])
        {
            constexpr float error[4] = ZUI_TOKEN(Tokens::AccentRed);
            if (field)
            {
                field->Flags = field->Flags | ZUI_CustomFocusBorder;
                for (int c = 0; c < 4; ++c)
                    field->BorderColor[c] = error[c];
            }
            ZUILabelEllipsized(ctx, m_modal_error, inner_width, error, ZUIFontSize::Small, "##pv_modal_error");
        }
        auto* buttons         = ZUIBeginRow(ctx, "##pv_mbtns", ZFill(), ZPx(Metrics::HeightControl));
        buttons->MainAlign    = ZUIAlign::End;
        buttons->Gap          = Metrics::Space8;
        const bool cancel     = (ZUIButton(ctx, cancel_key, ZPx(84.f), ZPx(Metrics::HeightControl), ZUIButtonKind::Secondary).Flags & ZUI_SignalClicked) != 0;
        const bool can_submit = deleting || m_modal_buf[0];
        if (!can_submit)
            ZUIBeginDisabled(ctx);
        const bool clicked = (ZUIButton(ctx, action, ZPx(84.f), ZPx(Metrics::HeightControl), ZUIButtonKind::Primary).Flags & ZUI_SignalClicked) != 0;
        if (!can_submit)
            ZUIEndDisabled(ctx);
        const bool confirm = can_submit && ctx->EnterPressed && ctx->FocusKey != ZUIHashStr(cancel_key, sizeof(cancel_key) - 1) && !ctx->CtrlDown && !ctx->AltDown && !ctx->ShiftDown;
        ZUIEndRow(ctx);

        if (cancel || ((clicked || confirm) && SubmitModal(vfs)))
        {
            m_modal               = Modal::None;
            m_modal_input_pending = false;
            ZUIClosePopup(ctx);
        }
        // All content is measured, so centering also follows error/body height.
        float    height = padding * 2.f;
        uint32_t count  = 0;
        for (const auto* child = content->FirstChild; child; child = child->NextSib)
        {
            float child_height = child->Size[1].Value;
            if (child->Size[1].Kind == ZUISizeKind::ChildrenSum)
            {
                child_height = 0.f;
                for (const auto* line = child->FirstChild; line; line = line->NextSib)
                    child_height += line->Size[1].Value;
            }
            height += child_height;
            ++count;
        }
        height             += count ? (count - 1) * content->Gap : 0.f;
        popup->FloatPos[0]  = fmaxf(0.f, (ctx->ScreenW - width) * 0.5f);
        popup->FloatPos[1]  = fmaxf(0.f, (ctx->ScreenH - height) * 0.5f);
        ZUIEndColumn(ctx);
        ZUIEndPopup(ctx);
    }

    void ProjectViewPanel::DrawStatus(ZUIContext* ctx, int visible_count, float width)
    {
        constexpr float orange[4] = ZUI_TOKEN(Tokens::AccentOrange);
        auto*           status    = ZUIBeginRow(ctx, "##pv_status", ZFill(), ZPx(25.f)); // Plus the 1 px top divider.
        status->Padding[0] = status->Padding[2] = Metrics::Space12;
        status->Gap                             = Metrics::Space10;
        status->CrossAlign                      = ZUIAlign::Center;
        status->Flags                           = status->Flags | ZUI_ClipChildren;
        const bool has_selection                = m_selected_path[0] != '\0';
        char       count[64]                    = {};
        if (m_truncated)
            snprintf(count, sizeof(count), "512+ items%s", has_selection ? " (1 selected)" : "");
        else
            snprintf(count, sizeof(count), "%d item%s%s", visible_count, visible_count == 1 ? "" : "s", has_selection ? " (1 selected)" : "");
        const auto* count_label = ZUILabelTight(ctx, count, m_truncated ? orange : kSecondary, ZUIFontSize::Small, "##pv_status_count");
        auto*       fill        = ZUIPushBox(ctx, "##pv_st_fill", 12, ZUI_None);
        fill->Size[0]           = ZFill();
        fill->Size[1]           = ZPx(1.f);
        ZUIPopBox(ctx);
        if (has_selection)
        {
            // The path yields first: the count, icon, caption and shared slider
            // retain their authored dimensions even in a narrow panel.
            constexpr char caption[] = "Thumbnail size";
            float          extent[2] = {};
            if (const auto* font = ctx->GetFont(ZUIFontSize::Small); font && font->Glyphs)
                ZUIMeasureText(font, caption, sizeof(caption) - 1, extent);
            const float available = std::isfinite(width) ? fmaxf(width - 2.f * Metrics::Space12 - 5.f * Metrics::Space10 - count_label->Size[0].Value - 12.f - extent[0] - 56.f, 0.f) : 0.f;
            ZUILabelEllipsized(ctx, m_selected_path, available, kSecondary, ZUIFontSize::Small, "##pv_status_path");
        }
        ZUIIconBox(ctx, ZUIIcon::Grid2x2, 12.f, kSecondary);
        ZUILabelTight(ctx, "Thumbnail size", kSecondary, ZUIFontSize::Small, "##pv_status_label");
        ZUISliderFloat(ctx, "##pv_st_slider", &m_thumbnail_scale, .75f, 1.5f, ZPx(56.f));
        ZUIEndRow(ctx);
    }

    // BuildContent
    void ProjectViewPanel::BuildContent(ZUIContext* ctx, float rect[4])
    {
        constexpr float kSearchH = 33.f;
        constexpr float kStatusH = 25.f;
        auto*           vfs      = static_cast<IVFSContext*>(ZEngine::Engine::GetContext() ? ZEngine::Engine::GetContext()->VFS : nullptr);

        if (!m_root_init)
        {
            NavigateTo(VFSPath::Root());
        }

        float pw    = (rect[2] - rect[0] > 1.f) ? rect[2] - rect[0] : 400.f;

        int   nvis  = 0;

        auto  hline = [&](const char* key) {
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

        DrawToolbar(ctx, pw);

        // Body: folder tree (210) | asset library (fill)
        ZUIBeginRow(ctx, "##pv_body", ZFill(), ZFill());

        {
            ZUIBox* sc = ZUIBeginColumn(ctx, "##pv_src", ZPx(kTreeW - 1.f), ZFill()); // The 210 px tree includes its divider.
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
            if (!m_folder_search[0])
            {
                const bool is_root  = m_current_dir.IsRoot();
                ZUIBox*    row      = ZUIBeginRow(ctx, "##pv_root_row", ZFill(), ZPx(23.f));
                row->Flags          = row->Flags | ZUI_DrawBackground | ZUI_Clickable | ZUI_KeyboardActivate;
                row->Padding[0]     = 9.f + 16.f;
                row->Gap            = 7.f;
                row->CrossAlign     = ZUIAlign::Center;
                row->EdgeSoftness   = 0.f;
                const float rest[4] = {0.f, 0.f, 0.f, 0.f};
                ZUIBoxSetColorArr(row, is_root ? kSelection : (ctx->HotKey == row->Key ? kHover : rest));
                ZUIIconBox(ctx, is_root ? ZUIIcon::FolderOpen : ZUIIcon::Folder, 13.f, is_root ? kSelectedText : kSecondary);
                ZUILabelTight(ctx, "All", kText, ZUIFontSize::Body);
                ZUISignal rs = ZUISignalFromBox(ctx, row);
                ZUIEndRow(ctx);
                if (rs.Flags & ZUI_SignalClicked)
                {
                    NavigateTo(VFSPath::Root());
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
            if (m_needs_refresh)
                RefreshListing(vfs);

            ZUIBox* sr     = ZUIBeginRow(ctx, "##pv_search_row", ZFill(), ZPx(kSearchH));
            sr->Padding[0] = 12.f;
            sr->Padding[2] = 12.f;
            sr->Padding[1] = Metrics::Space7;
            sr->Gap        = 8.f;
            sr->CrossAlign = ZUIAlign::Center;
            DrawFilterControl(ctx);
            char placeholder[VFS_MAX_PATH + 8] = {};
            char folder[VFS_MAX_PATH]          = {};
            m_current_dir.CopyFilename(folder, sizeof(folder));
            snprintf(placeholder, sizeof(placeholder), "Search %s", m_current_dir.IsRoot() ? "Content" : folder);
            if (ZUISearchBox(ctx, "##pv_search", m_search, sizeof(m_search), placeholder, ZFill(), true))
                m_reset_grid_scroll = true;
            ZUIEndRow(ctx);
            DrawFilters(ctx);

            // Content grid (fills remaining height - status row)
            nvis = DrawGrid(ctx, pw - kTreeW - Metrics::Space12, fmaxf(rect[3] - rect[1] - kToolbarH - kSearchH - kStatusH - 1.f, 0.f));

            ZUIEndColumn(ctx);
        }

        ZUIEndRow(ctx); // end body split

        // Figma "Content status": 26 px, item count left; selection path, thumbnail size right.
        hline("##pv_st_line");
        DrawStatus(ctx, nvis, pw);
        HandleShortcuts(ctx, bg);
        ZUIEndColumn(ctx);

        // Modals (outside main layout so they float freely)
        DrawModals(ctx, vfs);
    }

} // namespace Tetragrama::Panels
