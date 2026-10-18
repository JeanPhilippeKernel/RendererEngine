#include <Tetragrama/Editor.h>
#include <Tetragrama/Panels/PanelHelpers.h>
#include <Tetragrama/Panels/ViewportPanel.h>
#include <ZEngine/Applications/AppRenderPipeline.h>
#include <ZEngine/Core/MainThreadScheduler.h>
#include <ZEngine/Core/VFS/VFSPath.h>
#include <ZEngine/ECS/Components/MeshComponent.h>
#include <ZEngine/ECS/Components/NameComponent.h>
#include <ZEngine/ECS/Components/TransformComponent.h>
#include <ZEngine/Engine.h>
#include <ZEngine/Helpers/MemoryOperations.h>
#include <ZEngine/Importers/AssetCodec.h>
#include <ZEngine/Managers/AssetManager.h>
#include <ZEngine/Rendering/Renderers/GraphicRenderer.h>
#include <ZEngine/UI/ZUIFont.h>
#include <ZEngine/UI/ZUIWidgets.h>
#include <cstdio>
#include <cstring>

using namespace ZEngine::UI;
using namespace ZEngine::Helpers;
using namespace ZEngine::Core::VFS;

namespace Tetragrama::Panels
{
    static constexpr int kGizmoNone      = -1;
    static constexpr int kGizmoTranslate = 0;
    static constexpr int kGizmoRotate    = 1;
    static constexpr int kGizmoScale     = 2;

    struct MeshLoadPayload
    {
        ZEngine::Core::Memory::ArenaAllocator* Arena                         = nullptr;
        std::atomic_bool*                      InFlight                      = nullptr;
        ZEngine::Importers::AssetMesh          Mesh                          = {};
        ZEngine::Importers::AssetNodeHierarchy Hierarchy                     = {};
        uuids::uuid                            MeshId                        = {};
        char                                   DropPath[MAX_FILE_PATH_COUNT] = {};
        char                                   MeshPath[MAX_FILE_PATH_COUNT] = {};
        void*                                  Layer                         = nullptr;
    };

    void DestroyMeshLoadPayload(MeshLoadPayload* payload)
    {
        if (!payload)
            return;
        payload->Arena->Clear();
        payload->InFlight->store(false, std::memory_order_release);
        delete payload;
    }

    void ViewportPanel::Initialize(Tetragrama::Layers::ZUILayer* layer)
    {
        m_layer       = layer;

        auto* context = ZEngine::Engine::GetContext();
        if (!context || !context->ImportPipelineArena.m_memory)
            return;

        context->ImportPipelineArena.CreateSubArena(DroppedMeshTaskArenaBytes, &m_dropped_mesh_task_arena, "ImportPipeline/EditorDroppedMeshTask");
    }

    void CompleteDroppedMeshLoad(void* context)
    {
        auto* payload = static_cast<MeshLoadPayload*>(context);
        auto* ctx     = ZEngine::Engine::GetContext();
        auto* app     = payload->Layer ? reinterpret_cast<EditorPtr>(reinterpret_cast<Tetragrama::Layers::ZUILayer*>(payload->Layer)->CurrentApp) : nullptr;
        auto* scene   = app ? reinterpret_cast<EditorScenePtr>(app->CurrentScene) : nullptr;

        if (ctx && scene && ctx->ActorManager)
        {
            ZEngine::Managers::AssetManager::IngestMesh(std::move(payload->Mesh), std::move(payload->Hierarchy));

            char iname[256] = {};
            auto path       = VFSPath::Parse(payload->DropPath);
            if (path.Succeeded())
            {
                const auto stem = path.Value().Stem();
                snprintf(iname, sizeof(iname), "%.*s", static_cast<int>(stem.Length), stem.Data);
            }

            const uint32_t render_id = scene->AddMeshInstance(payload->MeshId, iname);

            using namespace ZEngine::ECS::Components;
            const ZEngine::ECS::ActorHandle handle = ctx->ActorManager->Create();
            ZEngine::ECS::Actor* const      actor  = ctx->ActorManager->Access(handle);
            if (actor)
            {
                NameComponent name = {};
                secure_strncpy(name.Value, sizeof(name.Value), iname, secure_strlen(iname));
                actor->AddComponent<NameComponent>(name);
                actor->AddComponent<TransformComponent>({});

                MeshComponent mesh    = {};
                mesh.MeshUUID         = payload->MeshId;
                mesh.RenderInstanceId = render_id;
                actor->AddComponent<MeshComponent>(mesh);
            }
        }

        DestroyMeshLoadPayload(payload);
    }

    void DeserializeDroppedMeshLoad(void* context)
    {
        auto* payload = static_cast<MeshLoadPayload*>(context);
        ZEngine::Importers::AssetCodec::DeserializeMeshAssetFile(payload->Arena, payload->MeshPath, payload->Mesh, payload->Hierarchy);

        // Ingest material files with the mesh on the worker, then create the scene
        // objects on the main thread once all asset data is ready.
        auto* ctx = ZEngine::Engine::GetContext();
        if (ctx)
        {
            auto scratch = ZGetScratch(&ctx->AssetArena);
            for (uint32_t index = 0; index < payload->Mesh.SubMeshes.size(); ++index)
            {
                const uuids::uuid& material_id = payload->Mesh.SubMeshes[index].MaterialUUID;
                if (!material_id.is_nil())
                    ZEngine::Managers::AssetManager::IngestMaterialFromUUID(scratch.Arena, material_id);
            }
            ZReleaseScratch(scratch);
        }

        ZEngine::Core::MainThreadScheduler::Post(payload, &CompleteDroppedMeshLoad);
    }

    // HOT PATH — runs every frame, no heap allocation allowed.
    void ViewportPanel::BuildContent(ZUIContext* ctx, float rect[4])
    {
        static const float kDarkBg[4] = {0.09f, 0.09f, 0.095f, 1.f};

        if (!m_layer || !m_layer->CurrentApp)
        {
            EmptyPanelBg(ctx, "##vp_bg", kDarkBg, nullptr);
            return;
        }

        auto* app = reinterpret_cast<Tetragrama::EditorPtr>(m_layer->CurrentApp);
        if (!app->RenderPipeline || !app->RenderPipeline->SceneRenderer)
        {
            EmptyPanelBg(ctx, "##vp_bg", kDarkBg, nullptr);
            return;
        }

        m_scene_texture = app->RenderPipeline->SceneRenderer->GetFrameOutput();

        float   sw      = rect[2] - rect[0];
        float   sh      = rect[3] - rect[1];

        // Outer fill column — scene image fills the whole docked area, no chrome
        ZUIBox* bg      = ZUIBeginColumn(ctx, "##vp_bg", ZFill(), ZFill());
        bg->Flags       = bg->Flags | ZUI_DrawBackground;
        ZUIBoxSetColorArr(bg, kDarkBg);
        bg->EdgeSoftness  = 0.f;

        // Scene image — fills the column, drag-drop target
        ZUIBox* img       = ZUIPushBox(ctx, "##vp_img", 8, ZUI_DrawBackground | ZUI_Clickable | ZUI_NoKeyboardFocus);
        img->Size[0]      = ZFill();
        img->Size[1]      = ZFill();
        img->TextureIndex = m_scene_texture.Valid() ? (uint32_t) m_scene_texture.Index : 0xFFFFFFFFu;
        ZUIBoxSetColor(img, 1.f, 1.f, 1.f, m_scene_texture.Valid() ? 1.f : 0.f);
        ctx->ViewportInputKey = img->Key;

        ZUISignal img_sig     = ZUISignalFromBox(ctx, img);
        ZUIPopBox(ctx);

        // The camera consumes raw GLFW cursor positions, not UI-space positions.
        // Convert both the hover bounds and the extent used by camera rays/panning.
        if (app->CameraController)
        {
            app->CameraController->SetViewportFromUI(rect, ctx->InputScale);
        }

        // Keep ViewportHovered for ZUI-level concerns (drag-drop, scroll routing).
        ctx->ViewportHovered             = (img_sig.Flags & ZUI_SignalHovered) != 0;

        // Drag-drop: accept scene files and raw mesh formats
        char               drop_buf[512] = {};
        ZUIDragPayloadKind drop_kind     = ZUIDragPayloadKind::Opaque;
        if (ZUIAcceptDrop(ctx, img, drop_buf, (uint32_t) sizeof(drop_buf), &drop_kind) && drop_kind != ZUIDragPayloadKind::Opaque && secure_strlen(drop_buf) > 0)
        {
            const auto parsed        = ZEngine::Core::VFS::VFSPath::Parse(drop_buf);
            const auto ext           = parsed.Succeeded() ? parsed.Value().Extension() : ZEngine::Core::VFS::VFSPathComponent{};
            char       extension[12] = {};
            if (ext.Length < sizeof(extension))
                for (size_t i = 0; i < ext.Length; ++i)
                    extension[i] = ext.Data[i] >= 'A' && ext.Data[i] <= 'Z' ? ext.Data[i] + ('a' - 'A') : ext.Data[i];
            // Cooked assets are loaded through the VFS; native files are only accepted by importers.
            if (drop_kind == ZUIDragPayloadKind::VFSPath && ZEngine::Helpers::secure_strcmp(extension, ".zescene") == 0)
            {
                secure_strncpy(m_pending_scene_drop, sizeof(m_pending_scene_drop), drop_buf, sizeof(m_pending_scene_drop) - 1);
                ZEngine::Core::MainThreadScheduler::Post(this, [](void* ctx) { reinterpret_cast<ViewportPanel*>(ctx)->OpenDroppedScene(); });
            }
            else if (drop_kind == ZUIDragPayloadKind::VFSPath && ZEngine::Helpers::secure_strcmp(extension, ".zemesh") == 0)
            {
                secure_strncpy(m_pending_mesh_drop, sizeof(m_pending_mesh_drop), drop_buf, sizeof(m_pending_mesh_drop) - 1);
                ZEngine::Core::MainThreadScheduler::Post(this, [](void* ctx) { reinterpret_cast<ViewportPanel*>(ctx)->SpawnDroppedMesh(); });
            }
            else if (app->Configuration && (ZEngine::Helpers::secure_strcmp(extension, ".glb") == 0 || ZEngine::Helpers::secure_strcmp(extension, ".gltf") == 0 || ZEngine::Helpers::secure_strcmp(extension, ".fbx") == 0 || ZEngine::Helpers::secure_strcmp(extension, ".obj") == 0))
            {
                auto* eng = ZEngine::Engine::GetContext();
                if (ResolveDroppedFile(drop_buf, drop_kind, app->Configuration->WorkingSpacePath.c_str(), eng ? eng->EngineAssetsNativeRoot : nullptr, app->Configuration->PendingImportPath, sizeof(app->Configuration->PendingImportPath)))
                {
                    const auto name = parsed.Value().Filename();
                    secure_strncpy(app->Configuration->PendingImportName, sizeof(app->Configuration->PendingImportName), name.Data, name.Length < sizeof(app->Configuration->PendingImportName) ? name.Length : sizeof(app->Configuration->PendingImportName) - 1);
                    app->Configuration->ShowImporter  = true;
                    app->Configuration->FocusImporter = true;
                }
            }
        }

        // Resize: emit a resize request whenever the docked area changes
        const uint32_t requested_width  = static_cast<uint32_t>(sw);
        const uint32_t requested_height = static_cast<uint32_t>(sh);
        if (requested_width > 0 && requested_height > 0 && (requested_width != m_last_w || requested_height != m_last_h) && app->State)
        {
            if (app->State->RenderTargetResizeRequests.push({.Width = requested_width, .Height = requested_height}))
            {
                m_last_w = requested_width;
                m_last_h = requested_height;
            }
        }

        // Figma viewport overlays: display controls top-left, transform/snapping controls top-right,
        // scene info bottom-left, performance readout bottom-right.
        {
            ZUIBox* top      = ZUIBeginRow(ctx, "##vp_top", ZPx(sw - 16.f), ZPx(26.f));
            top->Flags       = top->Flags | ZUI_FloatX | ZUI_FloatY;
            top->FloatPos[0] = 8.f;
            top->FloatPos[1] = 8.f;
            top->MainAlign   = ZUIAlign::SpaceBetween;

            ZUIBox* left     = ZUIBeginRow(ctx, "##vp_display", ZFit(), ZPx(26.f));
            left->Gap        = 5.f;
            ZUIEditorControl(ctx, "##vp_menu", ZUIIcon::Menu, nullptr, false);
            ZUIEditorControl(ctx, "##vp_persp", ZUIIcon::Box, "Perspective", true);
            ZUIEditorControl(ctx, "##vp_lit", ZUIIcon::Sun, "Lit", true);
            ZUIEditorControl(ctx, "##vp_show", ZUIIcon::Count, "Show", true);
            ZUIEndRow(ctx);

            ZUIBox* right = ZUIBeginRow(ctx, "##vp_transform", ZFit(), ZPx(26.f));
            right->Gap    = 4.f;
            if (ZUIEditorControl(ctx, "##vp_sel", ZUIIcon::MousePointer2, nullptr, false, m_gizmo_op == kGizmoNone).Flags & ZUI_SignalClicked)
                m_gizmo_op = kGizmoNone;
            if (ZUIEditorControl(ctx, "##vp_move", ZUIIcon::Move, nullptr, false, m_gizmo_op == kGizmoTranslate).Flags & ZUI_SignalClicked)
                m_gizmo_op = kGizmoTranslate;
            if (ZUIEditorControl(ctx, "##vp_rot", ZUIIcon::Rotate3D, nullptr, false, m_gizmo_op == kGizmoRotate).Flags & ZUI_SignalClicked)
                m_gizmo_op = kGizmoRotate;
            if (ZUIEditorControl(ctx, "##vp_scale", ZUIIcon::Scaling, nullptr, false, m_gizmo_op == kGizmoScale).Flags & ZUI_SignalClicked)
                m_gizmo_op = kGizmoScale;
            ZUIEditorControl(ctx, "##vp_space", ZUIIcon::Globe, nullptr, false);
            if (ZUIEditorControl(ctx, "##vp_grid", ZUIIcon::Grid3x3, "10", true, m_grid_enabled).Flags & ZUI_SignalClicked)
                m_grid_enabled = !m_grid_enabled;
            ZUIEditorControl(ctx, "##vp_snaprot", ZUIIcon::RotateCw, "10\xB0", true);
            ZUIEditorControl(ctx, "##vp_snapscale", ZUIIcon::Maximize, "0.25", true);
            ZUIEditorControl(ctx, "##vp_cam", ZUIIcon::Camera, "4", true);
            ZUIEndRow(ctx);

            ZUIEndRow(ctx);
        }

        {
            // Scene information, bottom-left.
            const char* scene_name = (app->Configuration && !app->Configuration->ActiveSceneName.empty()) ? app->Configuration->ActiveSceneName.c_str() : "Untitled";
            char        info[200]  = {};
            snprintf(info, sizeof(info), "%s  •  Persistent Scene", scene_name);
            ZUIBox* row      = ZUIBeginRow(ctx, "##vp_scene", ZFit(), ZPx(13.f));
            row->Flags       = row->Flags | ZUI_FloatX | ZUI_FloatY;
            row->FloatPos[0] = 12.f;
            row->FloatPos[1] = sh - 12.f - 13.f;
            row->Gap         = 8.f;
            row->CrossAlign  = ZUIAlign::Center;
            ZUIIconBox(ctx, ZUIIcon::Map, 12.f);
            ZUILabelTight(ctx, info, ctx->Theme.TextDefault, ZUIFontSize::Body);
            ZUIEndRow(ctx);
        }

        {
            // Performance readout, bottom-right: render-thread SmoothedDeltaTime (includes vsync wait).
            const float smooth_dt   = ZEngine::Engine::GetContext()->SmoothedDeltaTime;
            const float fps         = smooth_dt > 0.f ? 1.f / smooth_dt : 0.f;
            char        fps_buf[24] = {};
            char        ms_buf[24]  = {};
            snprintf(fps_buf, sizeof(fps_buf), "%.1f FPS", (double) fps);
            snprintf(ms_buf, sizeof(ms_buf), "%.2f ms", (double) (smooth_dt * 1000.f));

            ZUIBox* row      = ZUIBeginRow(ctx, "##vp_perf", ZFit(), ZPx(12.f));
            row->Flags       = row->Flags | ZUI_FloatX | ZUI_FloatY;
            row->FloatPos[0] = sw - 14.f - 99.f;
            row->FloatPos[1] = sh - 12.f - 12.f;
            row->Gap         = 8.f;
            row->CrossAlign  = ZUIAlign::Center;
            ZUILabelTight(ctx, fps_buf, ctx->Theme.TextDefault, ZUIFontSize::Body);
            ZUIBox* sep  = ZUIPushBox(ctx, "##vp_perf_sep", 13, ZUI_DrawBackground);
            sep->Size[0] = ZPx(1.f);
            sep->Size[1] = ZPx(12.f);
            ZUIBoxSetColorArr(sep, ctx->Theme.TextDim);
            sep->EdgeSoftness = 0.f;
            ZUIPopBox(ctx);
            ZUILabelTight(ctx, ms_buf, ctx->Theme.TextDefault, ZUIFontSize::Body);
            ZUIEndRow(ctx);
        }

        ZUIEndColumn(ctx);
    }

    // SpawnDroppedMesh (main-thread only)

    void ViewportPanel::SpawnDroppedMesh()
    {
        auto* app   = m_layer ? reinterpret_cast<EditorPtr>(m_layer->CurrentApp) : nullptr;
        auto* scene = app ? reinterpret_cast<EditorScenePtr>(app->CurrentScene) : nullptr;
        auto* ctx   = ZEngine::Engine::GetContext();
        if (!scene || !ctx || !ctx->ActorManager || !app->Configuration)
            return;
        if (!m_dropped_mesh_task_arena.m_memory)
        {
            ZENGINE_CORE_ERROR("[Viewport] Dropped-mesh task arena was not initialized")
            return;
        }

        // m_pending_mesh_drop is a VFS-style path — resolve to native before file I/O.
        char native_path[MAX_FILE_PATH_COUNT] = {};
        auto vfs_pr                           = VFSPath::Parse(m_pending_mesh_drop);
        if (!vfs_pr.Succeeded())
            return;
        vfs_pr.Value().ResolveNative(app->Configuration->WorkingSpacePath.c_str(), native_path, sizeof(native_path));

        ZEngine::Importers::AssetCodec::AssetMeshFileHeader header{};
        if (!ZEngine::Importers::AssetCodec::ReadAssetMeshFileHeader(native_path, header))
            return;

        // Deserialize + material ingest run on a worker thread. A single, fixed
        // ImportPipeline lease outlives the task until its main-thread callback
        // consumes or discards the decoded mesh. This deliberately permits one
        // dropped mesh at a time rather than reserving an unbounded root arena.

        uint64_t file_bytes = 0;
        if (FILE* f = fopen(native_path, "rb"))
        {
            fseek(f, 0, SEEK_END);
            const long bytes = ftell(f);
            fclose(f);
            if (bytes < 0)
                return;
            file_bytes = static_cast<uint64_t>(bytes);
        }

        constexpr uint64_t task_overhead_bytes = ZMega(8);
        constexpr uint64_t task_multiplier     = 4;
        if (file_bytes > (DroppedMeshTaskArenaBytes - task_overhead_bytes) / task_multiplier)
        {
            ZENGINE_CORE_WARN("[Viewport] Dropped mesh is too large for the {} MiB ImportPipeline task budget: {}", DroppedMeshTaskArenaBytes / ZMega(1), native_path)
            return;
        }

        bool expected = false;
        if (!m_dropped_mesh_task_in_flight.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
        {
            ZENGINE_CORE_WARN("[Viewport] A dropped-mesh task is already in flight; wait for it to finish before dropping another mesh")
            return;
        }

        m_dropped_mesh_task_arena.Clear();

        auto* payload     = new MeshLoadPayload();
        payload->Arena    = &m_dropped_mesh_task_arena;
        payload->InFlight = &m_dropped_mesh_task_in_flight;
        payload->MeshId   = header.Id;
        payload->Layer    = m_layer;
        secure_strncpy(payload->MeshPath, sizeof(payload->MeshPath), native_path, secure_strlen(native_path));
        secure_strncpy(payload->DropPath, sizeof(payload->DropPath), m_pending_mesh_drop, secure_strlen(m_pending_mesh_drop));

        if (!ZEngine::Helpers::ThreadPoolHelper::Submit(payload, &DeserializeDroppedMeshLoad))
            DestroyMeshLoadPayload(payload);
    }

    // OpenDroppedScene (main-thread only)

    void ViewportPanel::OpenDroppedScene()
    {
        auto* app = m_layer ? reinterpret_cast<EditorPtr>(m_layer->CurrentApp) : nullptr;
        if (!app || !app->Configuration)
            return;

        // Resolve to native so OpenScene's contract matches the dialog call site.
        char native_path[MAX_FILE_PATH_COUNT] = {};
        auto vfs_pr                           = VFSPath::Parse(m_pending_scene_drop);
        if (!vfs_pr.Succeeded())
            return;
        vfs_pr.Value().ResolveNative(app->Configuration->WorkingSpacePath.c_str(), native_path, sizeof(native_path));
        app->OpenScene(native_path);
    }

} // namespace Tetragrama::Panels
