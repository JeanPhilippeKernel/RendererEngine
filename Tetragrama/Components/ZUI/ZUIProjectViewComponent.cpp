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

        float sw = RegionW > 0 ? RegionW : (float)ctx->ScreenW * 0.48f;
        float sh = RegionW > 0 ? RegionH : 200.f;
        float sx = RegionW > 0 ? RegionX : (float)ctx->ScreenW * 0.19f;
        float sy = RegionW > 0 ? RegionY : (float)ctx->ScreenH - sh - 28.f; // above status bar

        ZUIBox* panel      = ZUIBeginColumn(ctx, "##zui_proj_panel", ZPx(sw), ZPx(sh));
        panel->Flags       = panel->Flags | ZUI_DrawBackground | ZUI_FloatX | ZUI_FloatY;
        panel->FloatPos[0] = sx;
        panel->FloatPos[1] = sy;
        panel->BgColor[0]  = 0.12f; panel->BgColor[1] = 0.12f;
        panel->BgColor[2]  = 0.14f; panel->BgColor[3]  = 0.96f;

        // --- Header row: path + up button ---
        ZUIBeginRow(ctx, "##proj_hdr", ZFill(), ZPx(24.f));
            const char* path_str = m_current_path.CStr() ? m_current_path.CStr() : "/";
            ZUILabel(ctx, path_str, k_dim);
            ZUISpacer(ctx, 8.f);
            ZUISignal up_sig = ZUIButton(ctx, "Up##proj");
        ZUIEndRow(ctx);
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
