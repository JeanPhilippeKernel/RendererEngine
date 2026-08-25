#include <Tetragrama/Components/ZUI/ZUISceneViewportComponent.h>
#include <Tetragrama/Editor.h>
#include <ZEngine/Applications/AppRenderPipeline.h>
#include <ZEngine/Rendering/Renderers/GraphicRenderer.h>
#include <ZEngine/UI/ZUIWidgets.h>

using namespace ZEngine::UI;

namespace Tetragrama::Components
{
    void ZUISceneViewportComponent::Initialize(Tetragrama::Layers::ZUILayer* parent,
                                               cstring name, bool visibility)
    {
        ParentLayer = parent;
        Name        = name;
        Visible     = visibility;
    }

    void ZUISceneViewportComponent::BuildUI(ZUIContext* ctx)
    {
        if (!Visible || !ParentLayer || !ParentLayer->CurrentApp) { return; }

        auto* app = reinterpret_cast<EditorPtr>(ParentLayer->CurrentApp);
        if (!app->RenderPipeline || !app->RenderPipeline->SceneRenderer) { return; }

        // Fetch latest scene render output — updated every render frame
        m_scene_texture = app->RenderPipeline->SceneRenderer->GetFrameOutput();
        if (!m_scene_texture.Valid()) { return; }

        float sw = RegionW > 0 ? RegionW : (float)ctx->ScreenW * 0.60f;
        float sh = RegionW > 0 ? RegionH : (float)ctx->ScreenH * 0.72f;
        float sx = RegionW > 0 ? RegionX : (float)ctx->ScreenW * 0.19f;
        float sy = RegionW > 0 ? RegionY : 28.f; // below menu bar

        ZUIBox* panel      = ZUIBeginColumn(ctx, "##zui_vp_panel", ZPx(sw), ZPx(sh));
        panel->Flags       = panel->Flags | ZUI_FloatX | ZUI_FloatY;
        panel->FloatPos[0] = sx;
        panel->FloatPos[1] = sy;

        // Scene image fills the panel
        ZUIImage(ctx, "##scene_img", m_scene_texture.Index, ZFill(), ZFill());

        // Request resize if dimensions changed
        if ((uint32_t)sw != m_last_w || (uint32_t)sh != m_last_h)
        {
            m_last_w = (uint32_t)sw;
            m_last_h = (uint32_t)sh;
            if (app->State)
            {
                app->State->RenderTargetResizeRequests.Emplace(
                    {.Width = m_last_w, .Height = m_last_h});
            }
        }

        ZUIEndColumn(ctx);
    }
} // namespace Tetragrama::Components
