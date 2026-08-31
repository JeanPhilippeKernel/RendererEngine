#include <Tetragrama/Components/ZUI/ZUIProjectViewComponent.h>
#include <Tetragrama/Layers/ZUILayer.h>
#include <ZEngine/Core/VFS/IVFSContext.h>
#include <ZEngine/Engine.h>
#include <ZEngine/Helpers/MemoryOperations.h>
#include <ZEngine/UI/ZUIWidgets.h>
#include <cstdio>

using namespace ZEngine::UI;
using namespace ZEngine::Core::VFS;

namespace Tetragrama::Components
{

    void ZUIProjectViewComponent::Initialize(Tetragrama::Layers::ZUILayer* parent, cstring name, bool visibility)
    {
        ParentLayer = parent;
        Name        = name;
        Visible     = visibility;
        parent->LocalArena.CreateSubArena(ZKilo(256), &m_arena);
    }

    void ZUIProjectViewComponent::RefreshIfNeeded()
    {
        if (m_initialized && m_current_path == m_listed_path)
        {
            return;
        }

        // Reset cache and re-list
        m_arena.Clear();
        m_entries     = ZPushArray(&m_arena, CachedEntry, kMaxEntries);
        m_entry_count = 0;

        auto* vfs     = ZEngine::Engine::GetContext()->VFS;
        if (!vfs)
        {
            m_listed_path = m_current_path;
            m_initialized = true;
            return;
        }

        auto scratch  = ZGetScratch(&m_arena);
        auto list_res = vfs->List(m_current_path, scratch.Arena);
        if (list_res.Succeeded())
        {
            auto&    entries = list_res.Value();
            uint32_t n       = entries.size() < kMaxEntries ? (uint32_t) entries.size() : kMaxEntries;
            for (uint32_t i = 0; i < n; ++i)
            {
                const VFSDirEntry& e = entries[i];
                CachedEntry&       c = m_entries[m_entry_count++];
                e.Path.CopyFilename(c.name, sizeof(c.name));
                c.is_dir = e.IsDirectory;
                if (e.Path.CStr())
                    ZEngine::Helpers::secure_strncpy(c.full_path, sizeof(c.full_path), e.Path.CStr(), sizeof(c.full_path) - 1);
            }
        }
        ZReleaseScratch(scratch);

        m_listed_path = m_current_path;
        m_initialized = true;
    }

    void ZUIProjectViewComponent::BuildUI(ZUIContext* ctx)
    {
        if (!Visible)
        {
            return;
        }

        if (!m_initialized)
        {
            m_current_path = VFSPath::Root();
        }
        RefreshIfNeeded(); // no-op unless path changed

        if (RegionW == 0)
        {
            RegionW = (float) ctx->ScreenW * 0.48f;
            RegionH = 200.f;
            RegionX = (float) ctx->ScreenW * 0.19f;
            RegionY = (float) ctx->ScreenH - RegionH - 28.f;
        }

        ZUIBox* panel      = ZUIBeginColumn(ctx, "##zui_proj_panel", ZPx(RegionW), ZPx(RegionH));
        panel->Flags       = panel->Flags | ZUI_DrawBackground | ZUI_DrawBorder | ZUI_FloatX | ZUI_FloatY;
        panel->FloatPos[0] = RegionX;
        panel->FloatPos[1] = RegionY;
        ZUIBoxSetColorArr(panel, ctx->Theme.PanelBg);
        panel->BorderColor[0]  = ctx->Theme.PanelBorder[0];
        panel->BorderColor[1]  = ctx->Theme.PanelBorder[1];
        panel->BorderColor[2]  = ctx->Theme.PanelBorder[2];
        panel->BorderColor[3]  = ctx->Theme.PanelBorder[3];
        panel->BorderColor[3]  = 1.0f;
        panel->BorderThickness = 1.f;
        panel->EdgeSoftness    = 0.f;

        // Header: draggable title + path + up button
        ZUIBox* hdr            = ZUIBeginRow(ctx, "##proj_hdr", ZFill(), ZSPx(ctx, 24.f));
        hdr->Flags             = hdr->Flags | ZUI_DrawBackground | ZUI_Clickable;
        ZUIBoxSetColorArr(hdr, ctx->Theme.TitleBarBg);
        hdr->EdgeSoftness = 0.f;
        ZUILabel(ctx, Name ? Name : "Project", ctx->Theme.TextDim);
        ZUISpacer(ctx, 4.f);
        const char* path_str = m_current_path.CStr() ? m_current_path.CStr() : "/";
        ZUILabel(ctx, path_str, ctx->Theme.TextDim);
        ZUISpacer(ctx, 8.f);
        ZUISignal up_sig   = ZUIButton(ctx, "Up##proj");
        ZUISignal drag_sig = ZUISignalFromBox(ctx, hdr);
        ZUIEndRow(ctx);
        if ((drag_sig.Flags & ZUI_SignalHeld) && (drag_sig.DragDelta[0] != 0.f || drag_sig.DragDelta[1] != 0.f))
        {
            RegionX            += drag_sig.DragDelta[0];
            RegionY            += drag_sig.DragDelta[1];
            Detached            = true;
            panel->FloatPos[0]  = RegionX;
            panel->FloatPos[1]  = RegionY;
        }
        if (drag_sig.Flags & ZUI_SignalDoubleClicked)
        {
            Detached = false;
        }

        // Search row
        ZUIBeginRow(ctx, "##proj_search_row", ZFill(), ZSPx(ctx, 24.f));
        ZUILabel(ctx, "Search:", ctx->Theme.TextDim);
        ZUISpacer(ctx, 4.f);
        ZUITextField(ctx, "##proj_search", m_search_buf, sizeof(m_search_buf), 160.f);
        ZUIEndRow(ctx);

        ZUISeparator(ctx);

        // Cached directory entries (scrollable)
        // Extension color palette
        static const float kColDir[4]   = {1.f, 0.9f, 0.2f, 1.f}; // yellow  — directories
        static const float kColMesh[4]  = {0.4f, 0.8f, 1.f, 1.f}; // blue    — .glb/gltf/fbx/obj
        static const float kColScene[4] = {0.8f, 0.5f, 1.f, 1.f}; // purple  — .zescene
        static const float kColAsset[4] = {0.5f, 1.f, 0.5f, 1.f}; // green   — .zemesh
        static const float kColTex[4]   = {1.f, 0.7f, 0.4f, 1.f}; // orange  — image files

        auto               ExtColor     = [&](const char* name) -> const float* {
            const char* dot = strrchr(name, '.');
            if (!dot)
            {
                return ctx->Theme.TextDefault;
            }
            if (strcmp(dot, ".glb") == 0 || strcmp(dot, ".gltf") == 0 || strcmp(dot, ".fbx") == 0 || strcmp(dot, ".obj") == 0)
            {
                return kColMesh;
            }
            if (strcmp(dot, ".zescene") == 0)
            {
                return kColScene;
            }
            if (strcmp(dot, ".zemesh") == 0)
            {
                return kColAsset;
            }
            if (strcmp(dot, ".png") == 0 || strcmp(dot, ".jpg") == 0 || strcmp(dot, ".jpeg") == 0)
            {
                return kColTex;
            }
            return ctx->Theme.TextDefault;
        };

        ZUIBeginScrollRegion(ctx, "##proj_scroll", ZFill(), ZFill());
        // HOT PATH — runs every frame, no heap allocation allowed.
        for (uint32_t i = 0; i < m_entry_count; ++i)
        {
            const CachedEntry& e = m_entries[i];
            if (!e.name[0])
            {
                continue;
            }

            // Search filter
            if (m_search_buf[0] && !strstr(e.name, m_search_buf))
            {
                continue;
            }

            char row_key[32];
            snprintf(row_key, sizeof(row_key), "##prow_%u", i);

            // Row: icon square + name + ext tag
            ZUIBox* row  = ZUIBeginRow(ctx, row_key, ZFill(), ZSPx(ctx, 28.f));
            row->Flags   = row->Flags | ZUI_DrawBackground | ZUI_Clickable;
            bool row_hov = (ctx->HotKey == row->Key);
            bool row_act = (ctx->ActiveKey == row->Key);
            ZUIBoxSetColor(row, 0.45f, 0.45f, 0.50f, row_act ? 0.20f : row_hov ? 0.12f : 0.f);

            ZUISpacer(ctx, 4.f);
            // Type icon — 14×14 colored square
            {
                const float* icon_col = e.is_dir ? kColDir : ExtColor(e.name);
                char         icon_key[40];
                snprintf(icon_key, sizeof(icon_key), "##picon_%u", i);
                ZUIBox* icon  = ZUIPushBox(ctx, icon_key, (uint32_t) ZEngine::Helpers::secure_strlen(icon_key), ZUI_DrawBackground);
                icon->Size[0] = ZSPx(ctx, 16.f);
                icon->Size[1] = ZSPx(ctx, 16.f);
                ZUIBoxSetColorArr(icon, icon_col);
                icon->EdgeSoftness = 0.5f;
                ZUIBoxSetCornerRadius(icon, 3.f);
                ZUIPopBox(ctx);
            }
            ZUISpacer(ctx, 5.f);
            // Name
            {
                const float* name_col = e.is_dir ? kColDir : ExtColor(e.name);
                ZUILabel(ctx, e.name, name_col);
            }
            ZUISpacer(ctx, 4.f);

            ZUISignal row_sig = ZUISignalFromBox(ctx, row);

            // Drag source for file assets → drop on scene viewport
            if (!e.is_dir && e.full_path[0])
                ZUIBeginDragSource(ctx, row, e.full_path, (uint32_t) ZEngine::Helpers::secure_strlen(e.full_path));

            ZUIEndRow(ctx);

            // Double-click on directory: navigate into it
            if ((row_sig.Flags & ZUI_SignalDoubleClicked) && e.is_dir)
            {
                auto next = m_current_path.Append(e.name);
                if (next.Succeeded())
                {
                    m_current_path = next.Value();
                }
            }

            // Context menu
            char ctx_key[40];
            snprintf(ctx_key, sizeof(ctx_key), "##proj_ctx_%u", i);
            if (ZUIBeginPopupContextItem(ctx, ctx_key, row_sig))
            {
                if (e.is_dir)
                {
                    if (ZUIMenuItem(ctx, "Open##proj"))
                    {
                        auto next = m_current_path.Append(e.name);
                        if (next.Succeeded())
                        {
                            m_current_path = next.Value();
                        }
                    }
                }
                else
                {
                    if (ZUIMenuItem(ctx, "Import##proj") && e.full_path[0])
                    {
                        ZEngine::Helpers::secure_strncpy(PendingImportPath, sizeof(PendingImportPath), e.full_path, sizeof(PendingImportPath) - 1);
                        ShowImporter = true;
                    }
                }
                ZUIEndContextMenu(ctx);
            }

            // Single-click on directory also navigates (kept for discoverability)
            if ((row_sig.Flags & ZUI_SignalClicked) && e.is_dir)
            {
                auto next = m_current_path.Append(e.name);
                if (next.Succeeded())
                {
                    m_current_path = next.Value();
                }
            }
        }

        ZUIEndScrollRegion(ctx);

        // Up navigation (applied after rendering so signal is from prev frame)
        if ((up_sig.Flags & ZUI_SignalClicked) && !m_current_path.IsRoot())
            m_current_path = m_current_path.Parent();

        ZUIEndColumn(ctx);
    }
} // namespace Tetragrama::Components
