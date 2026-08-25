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

        // --- Viewport toolbar ---
        ZUIBeginRow(ctx, "##vp_toolbar", ZFill(), ZPx(28.f));
        ZUISmallButton(ctx, "T##gizmo"); // Translate stub
        ZUISameLine(ctx);
        ZUISmallButton(ctx, "R##gizmo"); // Rotate stub
        ZUISameLine(ctx);
        ZUISmallButton(ctx, "S##gizmo"); // Scale stub
        ZUISpacer(ctx, 8.f);
        {
            char fps_buf[32];
            static float s_fps = 0.f;
            if (ctx->DeltaTime > 0.f)
                s_fps = s_fps * 0.95f + (1.f / ctx->DeltaTime) * 0.05f;
            snprintf(fps_buf, sizeof(fps_buf), "%.0f fps", (double)s_fps);
            ZUILabel(ctx, fps_buf, ctx->Theme.TextDim);
        }
        ZUIEndRow(ctx);
        ZUISeparator(ctx);

        // Scene image — also the drag-drop target and viewport-hover source
        ZUIBox* img_box = ZUIPushBox(ctx, "##scene_img", 11,
                                     ZUI_DrawBackground | ZUI_Clickable);
        img_box->Size[0]      = ZFill();
        img_box->Size[1]      = ZFill();
        img_box->TextureIndex = m_scene_texture.Index;
        img_box->BgColor[0]   = img_box->BgColor[1] = img_box->BgColor[2] = img_box->BgColor[3] = 1.f;

        ZUISignal vp_sig = ZUISignalFromBox(ctx, img_box);
        ZUIPopBox(ctx);

        // Gap 3: viewport-hover → gate camera controller
        ctx->ViewportHovered = (vp_sig.Flags & ZUI_SignalHovered) != 0;

        // Gap 2: accept file drops
        char drop_buf[512] = {};
        if (ZUIAcceptDrop(ctx, img_box, drop_buf, sizeof(drop_buf)) &&
            ZEngine::Helpers::secure_strlen(drop_buf) > 0)
        {
            auto* app = reinterpret_cast<EditorPtr>(ParentLayer->CurrentApp);
            const char* dot = strrchr(drop_buf, '.');
            if (dot && strcmp(dot, ".zescene") == 0) {
                Messengers::IMessenger::SendAsync<ZEngine::Applications::Layer,
                    Messengers::GenericMessage<std::string>>(
                        Tetragrama::EDITOR_COMPONENT_DOCKSPACE_REQUEST_OPENSCENE,
                        Messengers::GenericMessage<std::string>(drop_buf));
            } else if (dot && strcmp(dot, ".zemesh") == 0) {
                Messengers::IMessenger::SendAsync<ZEngine::Applications::Layer,
                    Messengers::GenericMessage<std::string>>(
                        Tetragrama::EDITOR_COMPONENT_DOCKSPACE_REQUEST_OPENMESH,
                        Messengers::GenericMessage<std::string>(drop_buf));
            } else if (app && app->Configuration && dot &&
                       (strcmp(dot,".glb")==0 || strcmp(dot,".gltf")==0 ||
                        strcmp(dot,".fbx")==0 || strcmp(dot,".obj")==0)) {
                ZEngine::Helpers::secure_strncpy(app->Configuration->PendingImportPath,
                    sizeof(app->Configuration->PendingImportPath),
                    drop_buf, sizeof(app->Configuration->PendingImportPath)-1);
                const char* name = strrchr(drop_buf,'/'); name = name ? name+1 : drop_buf;
                ZEngine::Helpers::secure_strncpy(app->Configuration->PendingImportName,
                    sizeof(app->Configuration->PendingImportName),
                    name, sizeof(app->Configuration->PendingImportName)-1);
                app->Configuration->ShowImporter  = true;
                app->Configuration->FocusImporter = true;
            }
        }

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
