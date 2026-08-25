#include <Tetragrama/Components/ZUI/ZUISceneViewportComponent.h>
#include <Tetragrama/Editor.h>
#include <Tetragrama/MessageToken.h>
#include <Tetragrama/Messengers/Messenger.h>
#include <ZEngine/Applications/AppRenderPipeline.h>
#include <ZEngine/Helpers/MemoryOperations.h>
#include <ZEngine/Logging/LoggerDefinition.h>
#include <ZEngine/Rendering/Renderers/GraphicRenderer.h>
#include <ZEngine/UI/ZUIWidgets.h>
#include <cstring>
#include <filesystem>

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

        // Scene image fills the remaining space; used as drop target
        ZUIBox* img_box = ZUIPushBox(ctx, "##scene_img", 11,
                                     ZUI_DrawBackground | ZUI_Clickable);
        img_box->Size[0]      = ZFill();
        img_box->Size[1]      = ZFill();
        img_box->TextureIndex = m_scene_texture.Index;
        img_box->BgColor[0]   = img_box->BgColor[1] = img_box->BgColor[2] = img_box->BgColor[3] = 1.f;

        ZUISignal vp_sig = ZUISignalFromBox(ctx, img_box);
        ZUIPopBox(ctx);

        // Gap 3: expose hover state so Editor can gate camera-controller routing
        ctx->ViewportHovered = (vp_sig.Flags & ZUI_SignalHovered) != 0;

        // Gap 2: accept file drops from ZUIProjectViewComponent
        char drop_buf[512] = {};
        if (ZUIAcceptDrop(ctx, img_box, drop_buf, sizeof(drop_buf)) &&
            ZEngine::Helpers::secure_strlen(drop_buf) > 0)
        {
            auto* app = reinterpret_cast<EditorPtr>(ParentLayer->CurrentApp);
            std::string file_ext = std::filesystem::path(drop_buf).extension().string();
            if (file_ext == ".zescene")
            {
                Messengers::IMessenger::SendAsync<ZEngine::Applications::Layer,
                    Messengers::GenericMessage<std::string>>(
                        Tetragrama::EDITOR_COMPONENT_DOCKSPACE_REQUEST_OPENSCENE,
                        Messengers::GenericMessage<std::string>(drop_buf));
            }
            else if (file_ext == ".zemesh")
            {
                Messengers::IMessenger::SendAsync<ZEngine::Applications::Layer,
                    Messengers::GenericMessage<std::string>>(
                        Tetragrama::EDITOR_COMPONENT_DOCKSPACE_REQUEST_OPENMESH,
                        Messengers::GenericMessage<std::string>(drop_buf));
            }
            else if (file_ext == ".glb"  || file_ext == ".gltf" ||
                     file_ext == ".fbx"  || file_ext == ".obj")
            {
                if (app && app->Configuration)
                {
                    ZEngine::Helpers::secure_strncpy(
                        app->Configuration->PendingImportPath,
                        sizeof(app->Configuration->PendingImportPath),
                        drop_buf, sizeof(app->Configuration->PendingImportPath) - 1);
                    const char* name = strrchr(drop_buf, '/');
                    name             = name ? name + 1 : drop_buf;
                    ZEngine::Helpers::secure_strncpy(
                        app->Configuration->PendingImportName,
                        sizeof(app->Configuration->PendingImportName),
                        name, sizeof(app->Configuration->PendingImportName) - 1);
                    app->Configuration->ShowImporter  = true;
                    app->Configuration->FocusImporter = true;
                    ZENGINE_CORE_INFO("SceneViewport: queued '{}' for import", drop_buf)
                }
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
