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
    static constexpr float k_dim[4]  = {0.55f, 0.55f, 0.60f, 1.f};
    static constexpr float k_dir[4]  = {0.55f, 0.75f, 0.95f, 1.f};
    static constexpr float k_text[4] = {0.90f, 0.90f, 0.90f, 1.f};

    void ZUIProjectViewComponent::Initialize(Tetragrama::Layers::ZUILayer* parent,
                                             cstring name, bool visibility)
    {
        ParentLayer = parent;
        Name        = name;
        Visible     = visibility;
        parent->LocalArena.CreateSubArena(ZKilo(64), &m_arena);
    }

    void ZUIProjectViewComponent::BuildUI(ZUIContext* ctx)
    {
        if (!Visible) { return; }

        if (!m_initialized)
        {
            m_current_path = VFSPath::Root();
            m_initialized  = true;
        }

        if (RegionW == 0) {
            RegionW = (float)ctx->ScreenW * 0.48f; RegionH = 200.f;
            RegionX = (float)ctx->ScreenW * 0.19f;
            RegionY = (float)ctx->ScreenH - RegionH - 28.f;
        }

        ZUIBox* panel      = ZUIBeginColumn(ctx, "##zui_proj_panel", ZPx(RegionW), ZPx(RegionH));
        panel->Flags       = panel->Flags | ZUI_DrawBackground | ZUI_FloatX | ZUI_FloatY;
        panel->FloatPos[0] = RegionX;
        panel->FloatPos[1] = RegionY;
        panel->BgColor[0]  = 0.12f; panel->BgColor[1] = 0.12f;
        panel->BgColor[2]  = 0.14f; panel->BgColor[3]  = 0.96f;

        // --- Header row: draggable title + path + up button (Gap 4) ---
        ZUIBox* hdr = ZUIBeginRow(ctx, "##proj_hdr", ZFill(), ZPx(24.f));
        hdr->Flags  = hdr->Flags | ZUI_Clickable;
            ZUILabel(ctx, Name ? Name : "Project", k_dim);
            ZUISpacer(ctx, 4.f);
            const char* path_str = m_current_path.CStr() ? m_current_path.CStr() : "/";
            ZUILabel(ctx, path_str, k_dim);
            ZUISpacer(ctx, 8.f);
            ZUISignal up_sig   = ZUIButton(ctx, "Up##proj");
            ZUISignal drag_sig = ZUISignalFromBox(ctx, hdr);
        ZUIEndRow(ctx);
        if ((drag_sig.Flags & ZUI_SignalHeld) &&
            (drag_sig.DragDelta[0] != 0.f || drag_sig.DragDelta[1] != 0.f))
        {
            RegionX += drag_sig.DragDelta[0];
            RegionY += drag_sig.DragDelta[1];
            Detached = true;
            panel->FloatPos[0] = RegionX;
            panel->FloatPos[1] = RegionY;
        }
        if (drag_sig.Flags & ZUI_SignalDoubleClicked) { Detached = false; }
        ZUISeparator(ctx);

        // --- Directory listing ---
        auto* vfs = ZEngine::Engine::GetContext()->VFS;
        if (vfs)
        {
            auto scratch   = ZGetScratch(&m_arena);
            auto list_res  = vfs->List(m_current_path, scratch.Arena);
            if (list_res.Succeeded())
            {
                auto& entries = list_res.Value();
                for (uint32_t i = 0; i < entries.size(); ++i)
                {
                    const VFSDirEntry& entry = entries[i];
                    char name_buf[256] = {};
                    entry.Path.CopyFilename(name_buf, sizeof(name_buf));

                    // Build a stable row key
                    char row_key[32];
                    snprintf(row_key, sizeof(row_key), "##prow_%u", i);

                    ZUIBox* row  = ZUIBeginRow(ctx, row_key, ZFill(), ZPx(20.f));
                    row->Flags   = row->Flags | ZUI_DrawBackground | ZUI_Clickable;

                    const float* color = entry.IsDirectory ? k_dir : k_text;
                    ZUILabel(ctx, entry.IsDirectory ? "[D] " : "    ", k_dim);
                    ZUILabel(ctx, name_buf, color);

                    ZUISignal row_sig = ZUISignalFromBox(ctx, row);

                    // Gap 2: files are drag sources for the scene viewport drop target
                    if (!entry.IsDirectory)
                    {
                        const char* native_path = entry.Path.CStr();
                        if (native_path)
                        {
                            ZUIBeginDragSource(ctx, row,
                                               native_path,
                                               (uint32_t)ZEngine::Helpers::secure_strlen(native_path));
                        }
                    }

                    ZUIEndRow(ctx);

                    if ((row_sig.Flags & ZUI_SignalClicked) && entry.IsDirectory)
                    {
                        m_current_path = entry.Path;
                    }
                }
            }
            ZReleaseScratch(scratch);
        }

        // Apply up navigation
        if ((up_sig.Flags & ZUI_SignalClicked) && !m_current_path.IsRoot())
        {
            m_current_path = m_current_path.Parent();
        }

        ZUIEndColumn(ctx);
    }
} // namespace Tetragrama::Components
