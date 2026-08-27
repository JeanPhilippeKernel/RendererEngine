#include <Tetragrama/Components/ZUI/ZUISceneViewportComponent.h>
#include <Tetragrama/Editor.h>
#include <Tetragrama/MessageToken.h>
#include <Tetragrama/Messengers/Messenger.h>
#include <ZEngine/Applications/AppRenderPipeline.h>
#include <ZEngine/Helpers/MemoryOperations.h>
#include <ZEngine/Logging/LoggerDefinition.h>
#include <ZEngine/Rendering/Renderers/GraphicRenderer.h>
#include <ZEngine/UI/ZUIWidgets.h>
#include <cstdio>
#include <cstring>

using namespace ZEngine::UI;

namespace Tetragrama::Components
{
    void ZUISceneViewportComponent::Initialize(Tetragrama::Layers::ZUILayer* parent, cstring name, bool visibility)
    {
        ParentLayer = parent;
        Name        = name;
        Visible     = visibility;
    }

    void ZUISceneViewportComponent::BuildUI(ZUIContext* ctx)
    {
        if (!Visible || !ParentLayer || !ParentLayer->CurrentApp)
        {
            return;
        }

        auto* app = reinterpret_cast<EditorPtr>(ParentLayer->CurrentApp);
        if (!app->RenderPipeline || !app->RenderPipeline->SceneRenderer)
        {
            return;
        }

        // Fetch latest scene render output — updated every render frame
        m_scene_texture = app->RenderPipeline->SceneRenderer->GetFrameOutput();
        if (!m_scene_texture.Valid())
        {
            return;
        }

        float   sw             = RegionW > 0 ? RegionW : (float) ctx->ScreenW * 0.60f;
        float   sh             = RegionW > 0 ? RegionH : (float) ctx->ScreenH * 0.72f;
        float   sx             = RegionW > 0 ? RegionX : (float) ctx->ScreenW * 0.19f;
        float   sy             = RegionW > 0 ? RegionY : 28.f; // below menu bar

        ZUIBox* panel          = ZUIBeginColumn(ctx, "##zui_vp_panel", ZPx(sw), ZPx(sh));
        panel->Flags           = panel->Flags | ZUI_DrawBorder | ZUI_FloatX | ZUI_FloatY;
        panel->FloatPos[0]     = sx;
        panel->FloatPos[1]     = sy;
        panel->BorderColor[0]  = ctx->Theme.PanelBorder[0];
        panel->BorderColor[1]  = ctx->Theme.PanelBorder[1];
        panel->BorderColor[2]  = ctx->Theme.PanelBorder[2];
        panel->BorderColor[3]  = 1.0f;
        panel->BorderThickness = 1.f;
        panel->EdgeSoftness    = 0.f;

        // Scene image fills the full panel — drag-drop target and viewport-hover source
        ZUIBox* img_box        = ZUIPushBox(ctx, "##scene_img", 11, ZUI_DrawBackground | ZUI_Clickable);
        img_box->Size[0]       = ZFill();
        img_box->Size[1]       = ZFill();
        img_box->TextureIndex  = m_scene_texture.Index;
        ZUIBoxSetColor(img_box, 1.f, 1.f, 1.f, 1.f);

        ZUISignal vp_sig = ZUISignalFromBox(ctx, img_box);
        ZUIPopBox(ctx);

        // Gap 3: viewport-hover → gate camera controller
        ctx->ViewportHovered = (vp_sig.Flags & ZUI_SignalHovered) != 0;

        // Gap 2: accept file drops
        char drop_buf[512]   = {};
        if (ZUIAcceptDrop(ctx, img_box, drop_buf, sizeof(drop_buf)) && ZEngine::Helpers::secure_strlen(drop_buf) > 0)
        {
            auto*       app = reinterpret_cast<EditorPtr>(ParentLayer->CurrentApp);
            const char* dot = strrchr(drop_buf, '.');
            if (dot && strcmp(dot, ".zescene") == 0)
            {
                Messengers::IMessenger::SendAsync<ZEngine::Applications::Layer, Messengers::GenericMessage<std::string>>(Tetragrama::EDITOR_COMPONENT_DOCKSPACE_REQUEST_OPENSCENE, Messengers::GenericMessage<std::string>(drop_buf));
            }
            else if (dot && strcmp(dot, ".zemesh") == 0)
            {
                Messengers::IMessenger::SendAsync<ZEngine::Applications::Layer, Messengers::GenericMessage<std::string>>(Tetragrama::EDITOR_COMPONENT_DOCKSPACE_REQUEST_OPENMESH, Messengers::GenericMessage<std::string>(drop_buf));
            }
            else if (app && app->Configuration && dot && (strcmp(dot, ".glb") == 0 || strcmp(dot, ".gltf") == 0 || strcmp(dot, ".fbx") == 0 || strcmp(dot, ".obj") == 0))
            {
                ZEngine::Helpers::secure_strncpy(app->Configuration->PendingImportPath, sizeof(app->Configuration->PendingImportPath), drop_buf, sizeof(app->Configuration->PendingImportPath) - 1);
                const char* name = strrchr(drop_buf, '/');
                name             = name ? name + 1 : drop_buf;
                ZEngine::Helpers::secure_strncpy(app->Configuration->PendingImportName, sizeof(app->Configuration->PendingImportName), name, sizeof(app->Configuration->PendingImportName) - 1);
                app->Configuration->ShowImporter  = true;
                app->Configuration->FocusImporter = true;
            }
        }

        // --- TRS toolbar overlay (top-left of viewport, floats over the scene image) ---
        {
            static const float kTransparent[4] = {0.f, 0.f, 0.f, 0.f};
            ZUIBox*            trs             = ZUIBeginRow(ctx, "##vp_trs_overlay", ZSPx(ctx, 108.f), ZSPx(ctx, 28.f));
            trs->Flags                         = trs->Flags | ZUI_FloatX | ZUI_FloatY;
            trs->FloatPos[0]                   = sx + 8.f;
            trs->FloatPos[1]                   = sy + 8.f;
            ZUIBoxSetColorArr(trs, kTransparent);
            ZUISmallButton(ctx, "T##gizmo"); // Translate
            ZUISameLine(ctx);
            ZUISmallButton(ctx, "R##gizmo"); // Rotate
            ZUISameLine(ctx);
            ZUISmallButton(ctx, "S##gizmo"); // Scale
            ZUIEndRow(ctx);
        }

        // --- FPS overlay (top-right of viewport) ---
        {
            static const float kTransparent[4] = {0.f, 0.f, 0.f, 0.f};
            static float       s_fps           = 0.f;
            if (ctx->DeltaTime > 0.f)
                s_fps = s_fps * 0.95f + (1.f / ctx->DeltaTime) * 0.05f;
            char fps_buf[32];
            snprintf(fps_buf, sizeof(fps_buf), "%.0f fps", (double) s_fps);
            ZUIBox* fps_box      = ZUIBeginRow(ctx, "##vp_fps_overlay", ZSPx(ctx, 120.f), ZSPx(ctx, 22.f));
            fps_box->Flags       = fps_box->Flags | ZUI_FloatX | ZUI_FloatY;
            fps_box->FloatPos[0] = sx + sw - 80.f;
            fps_box->FloatPos[1] = sy + 8.f;
            ZUIBoxSetColorArr(fps_box, kTransparent);
            ZUILabel(ctx, fps_buf, ctx->Theme.TextDefault);
            ZUIEndRow(ctx);
        }

        // Request resize if dimensions changed
        if ((uint32_t) sw != m_last_w || (uint32_t) sh != m_last_h)
        {
            m_last_w = (uint32_t) sw;
            m_last_h = (uint32_t) sh;
            if (app->State)
            {
                app->State->RenderTargetResizeRequests.Emplace({.Width = m_last_w, .Height = m_last_h});
            }
        }

        ZUIEndColumn(ctx);
    }
} // namespace Tetragrama::Components
