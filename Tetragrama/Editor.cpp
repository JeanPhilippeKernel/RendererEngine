// New panel-manager-based UI (replaces old per-component system)
#include <GLFW/glfw3.h>
#include <Tetragrama/Components/ZUI/ZUIDockspaceComponent.h>
#include <Tetragrama/Components/ZUI/ZUIStatusBarComponent.h>
#include <Tetragrama/Controllers/EditorCameraController.h>
#include <Tetragrama/Editor.h>
#include <Tetragrama/EditorSceneIO.h>
#include <Tetragrama/Gizmo/GizmoDebugFrame.h>
#include <Tetragrama/Panels/ZUIPanelManagerComponent.h>
#include <ZEngine/Core/CoreEvent.h>
#include <ZEngine/Core/VFS/Registry/AssetRecord.h>
#include <ZEngine/Engine.h>
#include <ZEngine/Helpers/MemoryOperations.h>
#include <ZEngine/Managers/AssetManager.h>
#include <ZEngine/UI/ZUIContext.h>
#include <ZEngine/UI/ZUIFont.h>
#include <ZEngine/UI/ZUIIcons.h>
#include <ZEngine/UI/ZUIInput.h>
#include <ZEngine/UI/ZUIKey.h>
#include <ZEngine/Windows/Events/KeyEvent.h>
#include <fmt/format.h>
#include <nlohmann/json.hpp>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <utility>

using namespace ZEngine;
using namespace ZEngine::Core::Containers;
using namespace ZEngine::Core::Memory;
using namespace ZEngine::Helpers;
using namespace Tetragrama::Layers;

namespace Tetragrama
{
    bool Editor::BeginObjectPick(void* context)
    {
        if (!context)
        {
            return false;
        }
        auto&    frame    = *static_cast<ObjectPickFrame*>(context);
        uint32_t expected = static_cast<uint32_t>(ObjectPickPhase::Queued);
        if (!frame.Phase.value.compare_exchange_strong(expected, static_cast<uint32_t>(ObjectPickPhase::Recording), std::memory_order_acq_rel, std::memory_order_acquire))
        {
            return false;
        }
        if (frame.Cancelled.value.load(std::memory_order_acquire) != 0)
        {
            frame.Phase.value.store(static_cast<uint32_t>(ObjectPickPhase::Free), std::memory_order_release);
            return false;
        }
        return true;
    }

    void Editor::RecordObjectPickToken(void* context, uint32_t token, ZEngine::ECS::EntityID entity)
    {
        if (!context || token == 0 || token > ZEngine::Rendering::Scenes::SceneData::MAX_DRAW_COMMANDS || !entity.IsValid())
        {
            return;
        }
        auto& frame = *static_cast<ObjectPickFrame*>(context);
        if (frame.Phase.value.load(std::memory_order_relaxed) == static_cast<uint32_t>(ObjectPickPhase::Recording))
        {
            frame.Entities[token] = entity;
        }
    }

    void Editor::SubmitObjectPick(void* context)
    {
        if (!context)
        {
            return;
        }
        auto&    frame    = *static_cast<ObjectPickFrame*>(context);
        uint32_t expected = static_cast<uint32_t>(ObjectPickPhase::Recording);
        (void) frame.Phase.value.compare_exchange_strong(expected, static_cast<uint32_t>(ObjectPickPhase::Submitted), std::memory_order_release, std::memory_order_relaxed);
    }

    void Editor::CompleteObjectPick(const void* data, size_t size, void* context)
    {
        if (!context)
        {
            return;
        }
        auto&    frame    = *static_cast<ObjectPickFrame*>(context);
        uint32_t expected = static_cast<uint32_t>(ObjectPickPhase::Submitted);
        if (!frame.Phase.value.compare_exchange_strong(expected, static_cast<uint32_t>(ObjectPickPhase::Resolving), std::memory_order_acq_rel, std::memory_order_acquire))
        {
            return;
        }
        uint32_t token = 0;
        if (data && size == sizeof(token))
        {
            (void) secure_memcpy(&token, sizeof(token), data, sizeof(token));
        }
        frame.Token = token;
        frame.Phase.value.store(static_cast<uint32_t>(ObjectPickPhase::Complete), std::memory_order_release);
    }

    void Editor::CancelSubmittedObjectPick(void* context)
    {
        if (!context)
        {
            return;
        }
        auto&    frame    = *static_cast<ObjectPickFrame*>(context);
        uint32_t expected = static_cast<uint32_t>(ObjectPickPhase::Submitted);
        (void) frame.Phase.value.compare_exchange_strong(expected, static_cast<uint32_t>(ObjectPickPhase::Free), std::memory_order_acq_rel, std::memory_order_acquire);
    }

    void Editor::UpdateCameraRevision(const ZEngine::Rendering::Cameras::CameraFrameData& camera)
    {
        const bool changed = !m_pick_camera_valid || std::memcmp(&m_last_pick_camera.View, &camera.View, sizeof(camera.View)) != 0 || std::memcmp(&m_last_pick_camera.Projection, &camera.Projection, sizeof(camera.Projection)) != 0 || std::memcmp(&m_last_pick_camera.Position, &camera.Position, sizeof(camera.Position)) != 0 || m_last_pick_camera.ProjectionType != camera.ProjectionType || m_last_pick_camera.UsesReverseZ != camera.UsesReverseZ;
        if (changed)
        {
            m_last_pick_camera  = camera;
            m_pick_camera_valid = true;
            if (++m_camera_revision == 0)
            {
                ++m_camera_revision;
            }
        }
    }

    void Editor::QueueViewportObjectPick(ZEngine::UI::ZUIContext* ctx, bool focused)
    {
        if (!ctx || !ctx->MouseReleased[0] || ctx->MouseReleaseSerial[0] == 0 || ctx->MouseReleaseSerial[0] == m_last_pick_release)
        {
            return;
        }
        // A serial is consumed even when its sequence is ineligible. Retained
        // ZUI input must never replay a click after its owner changed.
        m_last_pick_release = ctx->MouseReleaseSerial[0];
        // ActiveKey is retired by the overlay's release pass. Attribute the
        // click using the immutable press position instead, so a panel-originated
        // release cannot become a viewport selection.
        uint32_t press_x = 0, press_y = 0;
        if (!CanQueueViewportPick(ctx, focused) || ctx->MousePressAlt[0] || !GizmoViewport.ToRenderPixel(ctx->MousePressPos[0], 1, 1, press_x, press_y))
        {
            return;
        }

        uint32_t expected = static_cast<uint32_t>(ObjectPickPhase::Free);
        if (!m_object_pick.Phase.value.compare_exchange_strong(expected, static_cast<uint32_t>(ObjectPickPhase::Resolving), std::memory_order_acq_rel, std::memory_order_acquire))
        {
            return; // one outstanding GPU readback is deliberately bounded
        }
        m_object_pick.SceneEpoch           = Session.SceneEpoch();
        m_object_pick.ViewportRevision     = GizmoViewport.BindingRevision();
        m_object_pick.CameraRevision       = m_camera_revision;
        m_object_pick.RenderTargetRevision = m_render_target_revision;
        m_object_pick.Pointer[0]           = ctx->MouseReleasePos[0][0];
        m_object_pick.Pointer[1]           = ctx->MouseReleasePos[0][1];
        m_object_pick.Mode                 = ctx->MouseReleaseCtrl[0] ? SelectionMode::Toggle : SelectionMode::Replace;
        m_object_pick.Token                = 0;
        m_object_pick.Cancelled.value.store(0, std::memory_order_relaxed);

        m_object_pick.Phase.value.store(static_cast<uint32_t>(ObjectPickPhase::Queued), std::memory_order_release);
    }

    bool Editor::CanQueueViewportPick(const ZEngine::UI::ZUIContext* ctx, bool focused) const
    {
        return ctx && focused && Session.Ready() && !Session.Active() && ctx->ViewportSelectable && ctx->ViewportInputKey && !ctx->TextInputActive && !ctx->ActiveModalKey && !ctx->ModalBox && ctx->PopupStackSize == 0 && ctx->PendingPopupKey == 0 && ctx->DragSourceKey == 0 && !ctx->InteractionCapture.Pointer && GizmoViewport.Bound(ctx);
    }

    void Editor::CancelObjectPick()
    {
        m_object_pick.Cancelled.value.store(1, std::memory_order_release);
        uint32_t expected = static_cast<uint32_t>(ObjectPickPhase::Queued);
        (void) m_object_pick.Phase.value.compare_exchange_strong(expected, static_cast<uint32_t>(ObjectPickPhase::Free), std::memory_order_acq_rel, std::memory_order_acquire);
    }

    void Editor::CancelHoverPick()
    {
        m_hover_pick.Cancelled.value.store(1, std::memory_order_release);
        uint32_t expected = static_cast<uint32_t>(ObjectPickPhase::Queued);
        (void) m_hover_pick.Phase.value.compare_exchange_strong(expected, static_cast<uint32_t>(ObjectPickPhase::Free), std::memory_order_acq_rel, std::memory_order_acquire);
    }

    void Editor::ResolveObjectPick()
    {
        uint32_t expected = static_cast<uint32_t>(ObjectPickPhase::Complete);
        if (!m_object_pick.Phase.value.compare_exchange_strong(expected, static_cast<uint32_t>(ObjectPickPhase::Resolving), std::memory_order_acq_rel, std::memory_order_acquire))
        {
            return;
        }

        const uint32_t               token   = m_object_pick.Token;
        const ZEngine::ECS::EntityID entity  = token <= ZEngine::Rendering::Scenes::SceneData::MAX_DRAW_COMMANDS ? m_object_pick.Entities[token] : ZEngine::ECS::INVALID_ENTITY;
        const bool                   current = m_object_pick.Cancelled.value.load(std::memory_order_acquire) == 0 && m_object_pick.SceneEpoch == Session.SceneEpoch() && m_object_pick.ViewportRevision == GizmoViewport.BindingRevision() && m_object_pick.CameraRevision == m_camera_revision && m_object_pick.RenderTargetRevision == m_render_target_revision;
        if (current)
        {
            if (entity.IsValid() && Session.Scene() && Session.Scene()->IsAlive(entity))
            {
                (void) Session.RequestSelection(entity, m_object_pick.Mode);
            }
            else if (token == 0)
            {
                Session.ClearSelection();
            }
        }
        m_object_pick.Phase.value.store(static_cast<uint32_t>(ObjectPickPhase::Free), std::memory_order_release);
    }

    void Editor::ResolveHoverPick()
    {
        uint32_t expected = static_cast<uint32_t>(ObjectPickPhase::Complete);
        if (!m_hover_pick.Phase.value.compare_exchange_strong(expected, static_cast<uint32_t>(ObjectPickPhase::Resolving), std::memory_order_acq_rel, std::memory_order_acquire))
        {
            return;
        }

        const uint32_t               token   = m_hover_pick.Token;
        const ZEngine::ECS::EntityID entity  = token <= ZEngine::Rendering::Scenes::SceneData::MAX_DRAW_COMMANDS ? m_hover_pick.Entities[token] : ZEngine::ECS::INVALID_ENTITY;
        const bool                   current = m_hover_pick.Cancelled.value.load(std::memory_order_acquire) == 0 && m_hover_pick.SceneEpoch == Session.SceneEpoch() && m_hover_pick.ViewportRevision == GizmoViewport.BindingRevision() && m_hover_pick.CameraRevision == m_camera_revision && m_hover_pick.RenderTargetRevision == m_render_target_revision;
        if (current)
        {
            if (entity.IsValid() && Session.Scene() && Session.Scene()->IsAlive(entity))
            {
                (void) Session.SetHovered(entity);
            }
            else
            {
                Session.ClearHovered();
            }
        }
        m_hover_pick.Phase.value.store(static_cast<uint32_t>(ObjectPickPhase::Free), std::memory_order_release);
    }

    void Editor::PrepareObjectPick(ZEngine::Applications::RenderFrameState& state)
    {
        state.ObjectPick = {};
        if (m_object_pick.Phase.value.load(std::memory_order_acquire) != static_cast<uint32_t>(ObjectPickPhase::Queued))
        {
            return;
        }
        uint32_t x = 0, y = 0;
        if (m_object_pick.SceneEpoch != Session.SceneEpoch() || m_object_pick.ViewportRevision != GizmoViewport.BindingRevision() || m_object_pick.CameraRevision != m_camera_revision || m_object_pick.RenderTargetRevision != state.ResizeSequence || !GizmoViewport.ToRenderPixel(m_object_pick.Pointer, state.RenderTargetW, state.RenderTargetH, x, y))
        {
            uint32_t expected = static_cast<uint32_t>(ObjectPickPhase::Queued);
            (void) m_object_pick.Phase.value.compare_exchange_strong(expected, static_cast<uint32_t>(ObjectPickPhase::Free), std::memory_order_acq_rel, std::memory_order_acquire);
            return;
        }
        state.ObjectPick.X           = x;
        state.ObjectPick.Y           = y;
        state.ObjectPick.Context     = &m_object_pick;
        state.ObjectPick.Begin       = &Editor::BeginObjectPick;
        state.ObjectPick.RecordToken = &Editor::RecordObjectPickToken;
        state.ObjectPick.Submitted   = &Editor::SubmitObjectPick;
        state.ObjectPick.Complete    = &Editor::CompleteObjectPick;
        state.ObjectPick.Cancelled   = &Editor::CancelSubmittedObjectPick;
    }

    void Editor::QueueViewportHoverPick(ZEngine::UI::ZUIContext* ctx, bool focused)
    {
        if (!ctx)
        {
            return;
        }
        // ViewportHovered is written while the overlay is built, after this
        // input update. The image-to-render-target conversion below remains
        // the authoritative bounds check when the pointer first enters it.
        if (!CanQueueViewportPick(ctx, focused) || ctx->MouseDown[0] || ctx->MouseDown[1] || ctx->MouseDown[2] || ctx->MouseReleased[0] || ctx->MousePressAlt[0])
        {
            Session.ClearHovered();
            return;
        }
        if (ctx->MousePos[0] == ctx->PrevMousePos[0] && ctx->MousePos[1] == ctx->PrevMousePos[1])
        {
            return;
        }

        uint32_t expected = static_cast<uint32_t>(ObjectPickPhase::Free);
        if (!m_hover_pick.Phase.value.compare_exchange_strong(expected, static_cast<uint32_t>(ObjectPickPhase::Resolving), std::memory_order_acq_rel, std::memory_order_acquire))
        {
            return; // coalesce pointer movement while the previous sample is in flight
        }
        m_hover_pick.SceneEpoch           = Session.SceneEpoch();
        m_hover_pick.ViewportRevision     = GizmoViewport.BindingRevision();
        m_hover_pick.CameraRevision       = m_camera_revision;
        m_hover_pick.RenderTargetRevision = m_render_target_revision;
        m_hover_pick.Pointer[0]           = ctx->MousePos[0];
        m_hover_pick.Pointer[1]           = ctx->MousePos[1];
        m_hover_pick.Mode                 = SelectionMode::Replace;
        m_hover_pick.Token                = 0;
        m_hover_pick.Cancelled.value.store(0, std::memory_order_relaxed);

        m_hover_pick.Phase.value.store(static_cast<uint32_t>(ObjectPickPhase::Queued), std::memory_order_release);
    }

    void Editor::PrepareHoverPick(ZEngine::Applications::RenderFrameState& state)
    {
        state.HoverPick = {};
        if (m_hover_pick.Phase.value.load(std::memory_order_acquire) != static_cast<uint32_t>(ObjectPickPhase::Queued))
        {
            return;
        }
        uint32_t x = 0, y = 0;
        if (m_hover_pick.SceneEpoch != Session.SceneEpoch() || m_hover_pick.ViewportRevision != GizmoViewport.BindingRevision() || m_hover_pick.CameraRevision != m_camera_revision || m_hover_pick.RenderTargetRevision != state.ResizeSequence || !GizmoViewport.ToRenderPixel(m_hover_pick.Pointer, state.RenderTargetW, state.RenderTargetH, x, y))
        {
            CancelHoverPick();
            Session.ClearHovered();
            return;
        }
        state.HoverPick.X           = x;
        state.HoverPick.Y           = y;
        state.HoverPick.Context     = &m_hover_pick;
        state.HoverPick.Begin       = &Editor::BeginObjectPick;
        state.HoverPick.RecordToken = &Editor::RecordObjectPickToken;
        state.HoverPick.Submitted   = &Editor::SubmitObjectPick;
        state.HoverPick.Complete    = &Editor::CompleteObjectPick;
        state.HoverPick.Cancelled   = &Editor::CancelSubmittedObjectPick;
    }

    void Editor::PrepareSelectionOutline(ZEngine::Applications::RenderFrameState& state)
    {
        state.SelectionOutline = {};
        if (!Session.Ready())
        {
            return;
        }
        for (const EntityTarget& target : Session.Selection().Entities())
        {
            if (!state.SelectionOutline.Add(Session.Resolve(target)))
            {
                continue;
            }
        }
        state.SelectionOutline.SortAndUnique();
        state.SelectionOutline.Hovered = Session.Resolve(Session.Selection().Hovered());
    }

    void Editor::OnInitializing()
    {
        // Reserve the editor owner before creating configuration and workspace
        // state. EditorScene will later carve its 200 MiB local arena from here.
        Memory->CreateBudgetedArena(Memory->Budget.EditorContext, &EditorArena);
        Configuration = ZPushStructCtor<EditorConfiguration>(&EditorArena);

        if (ZEngine::Helpers::secure_strlen(ConfigFile))
        {
            Configuration->ReadConfig(&EditorArena, ConfigFile);
        }

        if (Configuration->ActiveSceneName.empty())
        {
            ZENGINE_CORE_WARN("Editor Scene name is empty")

            cstring active_scene = "<empty scene>";
            Configuration->ActiveSceneName.init(&EditorArena, active_scene);
        }
        WorkingSpacePath = Configuration->WorkingSpacePath.c_str();
        if (WorkingSpacePath && WorkingSpacePath[0] != '\0')
        {
            WorkingSpaceBackend.Initialize(WorkingSpacePath, ZEngine::Core::VFS::VFSBackendCaps::Read | ZEngine::Core::VFS::VFSBackendCaps::Write | ZEngine::Core::VFS::VFSBackendCaps::List, &EditorArena);
            VFSBackend = &WorkingSpaceBackend;
        }
    }

    void Editor::OverrideWindowConfiguration()
    {
        const char* project_name = Configuration->ProjectName.empty() ? "ZEngine Editor" : Configuration->ProjectName.c_str();
        const char* scene_name   = Configuration->ActiveSceneName.empty() ? "<empty scene>" : Configuration->ActiveSceneName.c_str();
        std::string title        = fmt::format("{0} - Active Scene : {1}", project_name, scene_name);
        WindowCfg.EnableVsync    = true;
        WindowCfg.Title.init(&EditorArena, title.c_str());
    }

    void Editor::OnInitialized()
    {
        auto* engine = ZEngine::Engine::GetContext();
        if (!engine || !MeshDrop.Initialize(engine->ImportPipelineArena))
            ZENGINE_CORE_ERROR("[Editor] Could not initialize the bounded mesh-drop workspace")
        auto editor_scene          = ZPushStructCtor<EditorScene>(&EditorArena);
        auto editor_cam_controller = ZPushStructCtor<Controllers::EditorCameraController>(&EditorArena);
        ZUIUILayer                 = ZPushStructCtor<ZUILayer>(&EditorArena);

        ZUIUILayer->Initialize(&EditorArena, this);

        // Single panel-manager component replaces all old per-panel components.
        // It owns the dock tree, tab bars, and all panel views.
        auto* pm = ZPushStructCtor<Tetragrama::Panels::ZUIPanelManagerComponent>(&EditorArena);
        pm->Initialize(ZUIUILayer, "PanelManager");
        AssetImporter = &pm->importer;
        ZUIUILayer->AddComponent(pm);

        // Editor shell: menu bar + floating overlays (settings, etc.)
        // Registered after PanelManager so it renders on top.
        auto* shell = ZPushStructCtor<Tetragrama::Components::ZUIDockspaceComponent>(&EditorArena);
        shell->Initialize(ZUIUILayer, "EditorShell");
        shell->ShellPanelManager = &pm->Manager;
        ZUIUILayer->AddComponent(shell);

        auto* sbar              = ZPushStructCtor<Tetragrama::Components::ZUIStatusBarComponent>(&EditorArena);
        sbar->ShellPanelManager = &pm->Manager;
        sbar->Initialize(ZUIUILayer, "StatusBar");
        ZUIUILayer->AddComponent(sbar);
        editor_cam_controller->Initialize(&EditorArena, CurrentWindow, ZEngine::Engine::GetContext()->InputManager, this);
        editor_scene->Initialize(&EditorArena, Configuration->ActiveSceneName.c_str(), Configuration->DefaultSky);

        CameraController = editor_cam_controller;
        CurrentScene     = editor_scene;

        if (!Session.Initialize(&EditorArena) || !Session.BeginMutationPhase())
        {
            ZENGINE_CORE_ERROR("[Editor] Could not initialize the shared editor session")
        }
        else
        {
            if (!Session.BindScene(ZEngine::Engine::GetContext()->Scene))
            {
                ZENGINE_CORE_ERROR("[Editor] Could not bind the authored scene to the editor session")
            }
            else if (!editor_scene->AttachSession(Session, *ZEngine::Engine::GetContext()))
                ZENGINE_CORE_ERROR("[Editor] Could not attach scene mutation adapters")
            Session.EndMutationPhase();

            // `sceneList.isDefault` names a persisted project scene.  The
            // bootstrap EditorScene only gives the editor a safe owner while
            // that source is loaded; it is not the configured scene itself.
            // Keep an absent default file as an empty-editor startup, but load
            // a present one through the normal transactional scene path.
            std::string directory = {};
            if (Configuration && !Configuration->ActiveSceneName.empty() && TrySceneDialogDirectory(Configuration->WorkingSpacePath.c_str(), Configuration->ScenePath.c_str(), {}, &directory))
            {
                const std::filesystem::path source = std::filesystem::path(directory) / (std::string(Configuration->ActiveSceneName.c_str()) + ".zescene");
                std::error_code             error  = {};
                if (std::filesystem::is_regular_file(source, error) && !error)
                {
                    const auto scene_path = SceneSourcePathFromNative(Configuration->WorkingSpacePath.c_str(), source.string());
                    if (scene_path.Succeeded() && !RequestSceneOpen(scene_path.Value(), Session.SceneEpoch()))
                    {
                        ZENGINE_CORE_WARN("[Editor] Could not queue configured default scene {}", source.string())
                    }
                }
            }
        }

        // Bake ZUI font atlases. Font sizes are chosen to be readable on high-resolution
        // displays where glfwGetWindowSize returns physical pixel counts (~3024px wide).
        // ImGui approach: fonts are baked at physical pixel density and all draw
        // coordinates are also in physical pixels — the NDC transform handles the rest.
        if (RenderPipeline && RenderPipeline->ZUICtx && RenderPipeline->ZUIRenderPass)
        {
            constexpr const char* kFontPath       = "/ZodiacEngine/Settings/Fonts/Inter/Inter-Regular.ttf";
            constexpr const char* kHeaderFontPath = "/ZodiacEngine/Settings/Fonts/Inter/Inter-SemiBold.ttf";
            constexpr const char* kIconFontPath   = "/ZodiacEngine/Settings/Fonts/Lucide/lucide.ttf";
            constexpr const char* kAssetIconsPath = "/ZodiacEngine/Settings/Icons/ContentBrowser/thumbnails.png";
            constexpr float       kIconLogical    = 16.f; // Figma icon slot
            auto*                 ctx             = RenderPipeline->ZUICtx;

            // Font atlas is always baked at 2× the logical base size so that
            // every display gets a 2× oversampled atlas — the same sharpness
            // advantage Retina screens had before, now available everywhere.
            //
            // kBase  = logical display size (matches ZUIStyle.FontSize)
            // kBake  = atlas physical size  (kBase * kOversample)
            // FontScale = 1/kOversample     (maps atlas px → logical px)
            //
            // UIScale (fb/win ratio) is set each frame by BeginOverlayFrame and
            // handles the physical pixel density independently of the font atlas.
            // Figma type ramp (Inter): caption 10, body / heading 11, large title 12.
            constexpr float       kBase           = 11.f;                // logical body size in px
            constexpr float       kSmallLogical   = 10.f;                // logical small size in px
            constexpr float       kHeaderLogical  = 12.f;                // large title, semibold
            constexpr float       kHeadingLogical = 11.f;                // section heading, semibold
            constexpr float       kTinyLogical    = 9.f;                 // logical caption size in px
            constexpr float       kLargeLogical   = 12.f;                // logical regular 12 px
            constexpr float       kOversample     = 2.f;                 // always 2× — sharp on every display
            const float           kBake           = kBase * kOversample; // 22 px atlas
            const float           kSmall          = kSmallLogical * kOversample;
            const float           kHeader         = kHeaderLogical * kOversample;
            const float           kFontScale      = 1.f / kOversample; // 0.5

            ZENGINE_CORE_INFO("[ZUI] FontBake body={:.0f} small={:.0f} header={:.0f}  FontScale={:.2f}", kBake, kSmall, kHeader, kFontScale);

            auto scratch = ZGetScratch(&EditorArena);
            ctx->Atlas   = ZEngine::UI::ZUIFontAtlasBake(&ctx->PersistentArena, scratch.Arena, RenderPipeline->Device, kFontPath, kSmall, kBake, kHeader, 32, 224, kHeaderFontPath, kIconFontPath, ZEngine::UI::kZUIIconCodepoints, (uint32_t) ZEngine::UI::ZUIIcon::Count, kIconLogical * kOversample, kTinyLogical * kOversample, kLargeLogical * kOversample, kHeadingLogical * kOversample, kAssetIconsPath);
            ZReleaseScratch(scratch);

            if (ctx->Atlas)
            {
                if (ctx->Atlas->Small)
                {
                    ctx->Atlas->Small->FontScale = kFontScale;
                }
                if (ctx->Atlas->Body)
                {
                    ctx->Atlas->Body->FontScale = kFontScale;
                }
                if (ctx->Atlas->Header)
                {
                    ctx->Atlas->Header->FontScale = kFontScale;
                }
                if (ctx->Atlas->Heading)
                {
                    ctx->Atlas->Heading->FontScale = kFontScale;
                }
                if (ctx->Atlas->Icon)
                {
                    ctx->Atlas->Icon->FontScale = kFontScale;
                }
                if (ctx->Atlas->Tiny)
                {
                    ctx->Atlas->Tiny->FontScale = kFontScale;
                }
                if (ctx->Atlas->Large)
                {
                    ctx->Atlas->Large->FontScale = kFontScale;
                }

                // Style.FontSize = logical body size → FrameHeight = 11 + 5.5*2 = 22 px (Figma field height)
                ctx->Style.FontSize = kBase;
                ZUIStyleUpdate(&ctx->Style);
                ZENGINE_CORE_INFO("[ZUI] Style.FontSize={:.0f}  FrameHeight={:.0f}", ctx->Style.FontSize, ctx->Style.FrameHeight);
            }
        }

        // Scene instance creation is handled directly in SceneViewportUIComponent::OnDrop
        // via ImportCoordinator::Enqueue's returned UUID — no callback needed here.
    }

    void Editor::ProcessEvent(ZEngine::Core::CoreEvent& e)
    {
        // Always route events to the window and ZUI layer
        if (CurrentWindow)
        {
            CurrentWindow->OnEvent(e);
        }
        if (ZUIUILayer)
        {
            ZUIUILayer->OnEvent(e);
        }

        if (e.GetType() == Core::EventType::WindowFocusLost || e.GetType() == Core::EventType::WindowMinimized)
        {
            // Event callbacks only retire input/layout. Authored rollback stays
            // at the exclusive mutation point, even if focus returns this poll.
            m_focus_lost_pending = true;
            if (!ZUIUILayer)
            {
                ZEngine::UI::ZUIFeedFocusLost(RenderPipeline ? RenderPipeline->ZUICtx : nullptr);
            }
            GizmoViewport.Unbind();
        }

        if (e.GetType() == Core::EventType::WindowResized)
        {
            GizmoViewport.Unbind(); // retained layout is no longer a valid viewport
        }

        if (e.GetType() == ZEngine::Windows::Events::KeyPressedEvent::GetStaticType())
        {
            const auto& key     = static_cast<const ZEngine::Windows::Events::KeyPressedEvent&>(e);
            const int   keycode = static_cast<int>(key.GetKeyCode());
            auto*       ctx     = RenderPipeline ? RenderPipeline->ZUICtx : nullptr;
            // Consume each scene-history edge after the UI modifier adapter,
            // not a pair of boolean flags on the next update. Counts, ordering
            // and ownership must survive an arbitrarily backpressured overlay.
            if (ctx && ctx->CtrlDown && !ctx->AltDown)
            {
                if (keycode == GLFW_KEY_Z)
                {
                    ProcessHistoryShortcut(ctx, ctx->ShiftDown);
                }
                else if (keycode == GLFW_KEY_Y)
                {
                    ProcessHistoryShortcut(ctx, true);
                }
            }
            if (key.GetRepeatCount() == 0)
            {
                QueueSceneFileShortcut(ctx, keycode);
                ZEngine::UI::ZUIKey shortcut = ZEngine::UI::ZUIKey::None;
                switch (keycode)
                {
                    case GLFW_KEY_Q:
                        shortcut = ZEngine::UI::ZUIKey::Q;
                        break;
                    case GLFW_KEY_W:
                        shortcut = ZEngine::UI::ZUIKey::W;
                        break;
                    case GLFW_KEY_E:
                        shortcut = ZEngine::UI::ZUIKey::E;
                        break;
                    case GLFW_KEY_R:
                        shortcut = ZEngine::UI::ZUIKey::R;
                        break;
                }
                (void) ProcessGizmoShortcut(ctx, shortcut);
            }
        }

        OnEvent(e);
    }

    void Editor::OnUpdate(float dt)
    {
        auto*      ctx     = RenderPipeline ? RenderPipeline->ZUICtx : nullptr;
        auto*      eng     = ZEngine::Engine::GetContext();
        const bool focused = (!eng || !eng->InputManager || eng->InputManager->IsWindowFocused()) && !m_focus_lost_pending;
        PollSceneSaveDialog();
        PollSceneOpenDialog();
        if (AssetImporter)
        {
            AssetImporter->Poll(eng);
        }
        UpdateSessionInput(ctx, focused, eng);

        if (ZUIUILayer)
        {
            ZUIUILayer->Update(dt);
        }

        UpdateGizmoInput(ctx, focused);

        if (CameraController)
        {
            const auto capture = ZEngine::UI::ZUIGetInputCapture(ctx);
            CameraController->SetInputCapture(capture.Pointer, capture.Keyboard);
        }
    }

    void Editor::UpdateSessionInput(ZEngine::UI::ZUIContext* ctx, bool focused, ZEngine::EngineContext* engine)
    {
        if (!engine)
        {
            engine = ZEngine::Engine::GetContext();
        }
        focused = focused && !m_focus_lost_pending;
        ProcessSceneChangeInput(ctx, focused);
        // Engine::MainThreadRun has joined runtime waves before App->Update. This
        // phase is never gated by BeginOverlayWrite, so lifetime/cancel and queued
        // editor commands continue even while presentation retains an old overlay.
        uint64_t    terminating_control = Session.ControlKey();
        const auto* owner               = static_cast<const EditorScene*>(CurrentScene);
        if (Session.BeginMutationPhase(!owner || !owner->SourceReadOnly))
        {
            const auto current_camera = CameraController && CameraController->GetCamera() ? CameraController->GetCamera()->CaptureFrameData() : ZEngine::Rendering::Cameras::CameraFrameData{};
            UpdateCameraRevision(current_camera);
            if (!focused)
            {
                CancelObjectPick();
                CancelHoverPick();
                Session.ClearHovered();
            }
            ResolveObjectPick();
            ResolveHoverPick();
            QueueViewportObjectPick(ctx, focused);
            QueueViewportHoverPick(ctx, focused);
            // Consume only after entering the phase; a failed/nested entry must
            // not lose the pending cancellation before a later successful update.
            m_focus_lost_pending       = false;
            const bool escape          = ctx && ctx->EscapePressed && (!ctx->TextInputActive || Session.Active());
            const bool discarded_edits = (escape || !focused) && Session.DiscardEditRequests();
            if (!focused || escape)
            {
                const bool had_interaction = Session.Active() || discarded_edits || m_scene_settings.Sky || m_scene_settings.Grid;
                DiscardSceneSettings();
                Session.Cancel();
                const bool cleared_pivot = focused && escape && !had_interaction && ctx && !ctx->TextInputActive && !ctx->ActiveModalKey && !ctx->ModalBox && !ctx->PopupStackSize && !ctx->PendingPopupKey && !ctx->DragSourceKey && Session.ClearCustomPivot();
                if (ctx && (had_interaction || cleared_pivot))
                {
                    // A retained UI payload must not rearm a cancelled source while
                    // its original pointer is still held or its press edge persists.
                    ctx->ActiveKey       = 0;
                    ctx->MousePressed[0] = false;
                    ctx->EscapePressed   = false;
                }
            }
            // Open may cancel only its captured physical owner, before any
            // queued edit or same-poll release can commit/replace that drag.
            ProcessSceneFileInput(ctx, focused && engine && engine->Scene == Session.Scene() && !engine->CloseRequested.value.load(std::memory_order_acquire), true);
            if (focused)
            {
                ProcessHistoryShortcuts(ctx);
                const bool pending_control = ctx && Session.HasPendingControl(ctx->ActiveKey);
                const auto previous_mode   = Session.GizmoSettings().Mode;
                const auto previous_space  = Session.GizmoSettings().Space;
                const bool processed       = Session.ProcessRequests();
                if (!processed)
                {
                    ZENGINE_CORE_WARN("[Editor] {}", Session.LastError())
                }
                if (pending_control && !Session.Active() && (!processed || previous_mode != Session.GizmoSettings().Mode || previous_space != Session.GizmoSettings().Space))
                {
                    // Retire a rejected or mode-cancelled press, including a
                    // delayed source that had not started its transaction yet.
                    ctx->ActiveKey       = 0;
                    ctx->MousePressed[0] = false;
                }
                if (Session.ControlKey())
                {
                    terminating_control         = Session.ControlKey();
                    const uint64_t serial       = Session.ControlPointerSerial();
                    const bool     released     = ctx && ctx->MouseReleaseSerial[0] == serial && (serial != 0 || ctx->MouseReleased[0]);
                    const bool     held         = ctx && ctx->MouseDown[0] && !released;
                    const bool     capture_lost = !ctx || (!released && (ctx->MousePressSerial[0] != serial || (held && ctx->ActiveKey != terminating_control)));
                    const float    pointer_x    = ctx ? (released ? ctx->MouseReleasePos[0][0] : ctx->MousePos[0]) : 0.f;
                    (void) Session.UpdateControlDrag(pointer_x, held, focused, false, capture_lost);
                }
            }
            else
            {
                // Do not replay a delayed click or shortcut when focus returns.
                Session.DiscardRequests();
                if (ZUIUILayer)
                {
                    ZUIUILayer->ResetInput();
                }
                else
                {
                    ZEngine::UI::ZUIFeedFocusLost(ctx);
                }
            }
            ProcessSceneFileInput(ctx, focused && engine && engine->Scene == Session.Scene() && !engine->CloseRequested.value.load(std::memory_order_acquire));
            if (engine && !engine->CloseRequested.value.load(std::memory_order_acquire))
            {
                if (m_scene_settings.Sky || m_scene_settings.Grid)
                {
                    ZEngine::ECS::SceneDiagnostics settings_diagnostics = {};
                    if (!ProcessSceneSettings(*engine, &settings_diagnostics))
                        for (uint32_t i = 0; i < settings_diagnostics.Count; ++i)
                            ZENGINE_CORE_WARN("[Editor] {}", settings_diagnostics.Entries[i].Message)
                }
                // Native save panels/minimized windows can temporarily lose
                // focus. Already queued save/close decisions must still finish.
                (void) ProcessCloseRequest(*engine);
                if (!engine->CloseRequested.value.load(std::memory_order_acquire))
                {
                    if (focused)
                    {
                        (void) ProcessNewScene(*engine);
                        (void) ProcessSceneOpen(*engine);
                        if (MeshDrop.Pending())
                        {
                            ZEngine::ECS::SceneDiagnostics diagnostics = {};
                            if (!MeshDrop.Process(*engine, static_cast<EditorScene*>(CurrentScene), Session, &diagnostics))
                                for (uint32_t i = 0; i < diagnostics.Count; ++i)
                                    ZENGINE_CORE_WARN("[Editor] {}", diagnostics.Entries[i].Message)
                        }
                    }
                    (void) ProcessSceneSave(*engine);
                }
            }
            if (EnableGizmoDebug)
            {
                GizmoViewport.Unbind();
            }
            const auto input_camera = CameraController && CameraController->GetCamera() ? CameraController->GetCamera()->CaptureFrameData() : ZEngine::Rendering::Cameras::CameraFrameData{};
            // Start and finish owned interactions before camera input on every
            // update, even when overlay backpressure prevents a UI rebuild.
            GizmoViewport.UpdateTransformInput(Session, input_camera, ctx, focused, true);
            Session.EndMutationPhase();
        }

        if (ctx && terminating_control && Session.ControlKey() != terminating_control && ctx->ActiveKey == terminating_control)
        {
            ctx->ActiveKey       = 0;
            ctx->MousePressed[0] = false;
        }

        if (ctx)
        {
            ctx->InteractionCapture = {Session.Active(), Session.Active()};
        }
    }

    Editor::SceneFileDrag Editor::SceneFileDragOwner(const ZEngine::UI::ZUIContext* ctx) const
    {
        if (!ctx || !Session.OnOwnerThread() || !Session.Active() || !ctx->ActiveKey || !ctx->MousePressSerial[0])
        {
            return SceneFileDrag::None;
        }
        if (Session.ControlKey() == ctx->ActiveKey && Session.ControlPointerSerial() == ctx->MousePressSerial[0])
        {
            return SceneFileDrag::Control;
        }
        return GizmoViewport.OwnsPointerCapture(Session, ctx) ? SceneFileDrag::Viewport : SceneFileDrag::None;
    }

    bool Editor::CanUseSceneFileShortcut(const ZEngine::UI::ZUIContext* ctx, SceneFileDrag drag) const
    {
        if (!ctx || ctx->TextInputActive || ctx->DragSourceKey || ctx->ActiveModalKey || ctx->ModalBox || ctx->PopupStackSize || ctx->PendingPopupKey || ctx->MouseDown[1] || ctx->MouseDown[2])
        {
            return false;
        }
        if (drag != SceneFileDrag::None)
        {
            if (SceneFileDragOwner(ctx) != drag || (!ctx->MouseDown[0] && ctx->MouseReleaseSerial[0] != ctx->MousePressSerial[0]))
            {
                return false;
            }
        }
        else if (ctx->ActiveKey || ctx->InteractionCapture.Keyboard || ctx->MouseDown[0] || Session.Active())
        {
            return false;
        }
        const auto* owner = static_cast<const EditorScene*>(CurrentScene);
        return Session.OnOwnerThread() && Session.Ready() && owner && owner->Session == &Session && !m_closing && !m_focus_lost_pending && !m_new_scene_pending && !m_editor_close_pending && !m_scene_save.Pending && !m_scene_save_picker.valid() && !m_scene_open_picker.valid() && !SceneOpenPending() && !SceneChangePromptState().Visible();
    }

    void Editor::QueueSceneFileShortcut(const ZEngine::UI::ZUIContext* ctx, int keycode)
    {
        if (!ctx || !ctx->CtrlDown || ctx->AltDown || m_scene_file_input.Command != SceneFileCommand::None)
        {
            return;
        }
        SceneFileCommand command = SceneFileCommand::None;
        if (keycode == GLFW_KEY_S)
        {
            command = ctx->ShiftDown ? SceneFileCommand::SaveAs : SceneFileCommand::Save;
        }
        else if (!ctx->ShiftDown && keycode == GLFW_KEY_N)
        {
            command = SceneFileCommand::New;
        }
        else if (!ctx->ShiftDown && keycode == GLFW_KEY_O)
        {
            command = SceneFileCommand::Open;
        }
        const auto drag = command == SceneFileCommand::Open && ctx->MouseDown[0] ? SceneFileDragOwner(ctx) : SceneFileDrag::None;
        if (command == SceneFileCommand::None || !CanUseSceneFileShortcut(ctx, drag))
        {
            return;
        }
        const auto* owner = static_cast<const EditorScene*>(CurrentScene);
        if ((command == SceneFileCommand::Save || command == SceneFileCommand::SaveAs) && (!Session.Editable() || owner->SourceReadOnly))
        {
            return;
        }
        if (!m_scene_file_input_owner.Capture(*this))
        {
            return;
        }
        m_scene_file_input = {.Epoch = Session.SceneEpoch(), .PointerKey = drag != SceneFileDrag::None ? ctx->ActiveKey : 0, .PointerSerial = drag != SceneFileDrag::None ? ctx->MousePressSerial[0] : 0, .Command = command, .Drag = drag};
    }

    void Editor::ProcessSceneFileInput(ZEngine::UI::ZUIContext* ctx, bool focused, bool before_requests)
    {
        // Drag interruption runs before authored requests/releases. Ordinary
        // file input still runs after edits, so Save cannot serialize a preview.
        // Both paths retire rejected edges; neither replays against a new owner.
        if ((m_scene_file_input.Drag != SceneFileDrag::None) != before_requests)
        {
            return;
        }
        const auto input = std::exchange(m_scene_file_input, {});
        if (input.Command == SceneFileCommand::None)
        {
            return; // Idle updates never scan/copy/clear the retained path buffer.
        }
        const bool matches = m_scene_file_input_owner.Matches(*this);
        m_scene_file_input_owner.Reset();
        if (!matches || !focused || input.Epoch != Session.SceneEpoch() || Session.PendingRequestCount() || !CanUseSceneFileShortcut(ctx, input.Drag))
        {
            return;
        }
        if (input.Drag != SceneFileDrag::None)
        {
            if (input.Command != SceneFileCommand::Open || ctx->ActiveKey != input.PointerKey || ctx->MousePressSerial[0] != input.PointerSerial)
            {
                return;
            }
            Session.Cancel();
            if (Session.Active())
            {
                return;
            }
            ctx->ActiveKey          = 0;
            ctx->MousePressed[0]    = false;
            ctx->InteractionCapture = {};
        }
        switch (input.Command)
        {
            case SceneFileCommand::New:
                (void) RequestNewScene();
                break;
            case SceneFileCommand::Open:
                OpenSceneDialog();
                break;
            case SceneFileCommand::Save:
                SaveScene();
                break;
            case SceneFileCommand::SaveAs:
                SaveSceneAs();
                break;
            case SceneFileCommand::None:
                break;
        }
    }

    void Editor::ProcessHistoryShortcuts(ZEngine::UI::ZUIContext* ctx)
    {
        ProcessHistoryShortcut(ctx, false);
        ProcessHistoryShortcut(ctx, true);
    }

    void Editor::ProcessHistoryShortcut(ZEngine::UI::ZUIContext* ctx, bool redo)
    {
        if (!ctx || ctx->TextInputActive || ctx->ActiveModalKey != 0 || ctx->ModalBox || ctx->PopupStackSize != 0 || ctx->PendingPopupKey)
        {
            return;
        }
        // Retire even rejected global edges immediately; they must not rearm
        // after an interaction, a full queue or a read-only scene becomes editable.
        if (!std::exchange(redo ? ctx->CtrlYPressed : ctx->CtrlZPressed, false))
        {
            return;
        }
        const auto* owner = static_cast<const EditorScene*>(CurrentScene);
        if (m_closing || m_focus_lost_pending || !Session.Editable() || (owner && owner->SourceReadOnly) || Session.Active() || ctx->InteractionCapture.Keyboard)
        {
            return;
        }
        // Earlier queued edits/shortcuts can change availability before this
        // command applies. Only skip a known empty history with no earlier work.
        if (!(redo ? Session.CanRedo() : Session.CanUndo()) && !Session.PendingRequestCount())
        {
            return;
        }
        if (!Session.Request(redo ? EditorCommand::Redo : EditorCommand::Undo))
            ZENGINE_CORE_WARN("[Editor] {}", Session.LastError())
    }

    bool Editor::ProcessGizmoShortcut(const ZEngine::UI::ZUIContext* ctx, ZEngine::UI::ZUIKey key)
    {
        // Event-time gating: no overlay rebuild/edge flag is needed, so a held
        // camera-flight key cannot replay a mode change when presentation resumes.
        if (m_focus_lost_pending || !ctx || !ctx->ViewportInputKey || ctx->TextInputActive || ctx->ActiveModalKey || ctx->ModalBox || ctx->PopupStackSize || ctx->PendingPopupKey || ctx->MouseDown[1] || ctx->CtrlDown || ctx->ShiftDown || ctx->AltDown)
        {
            return false;
        }
        Gizmo::Operation operation = Gizmo::Operation::Select;
        switch (key)
        {
            case ZEngine::UI::ZUIKey::Q:
                operation = Gizmo::Operation::Select;
                break;
            case ZEngine::UI::ZUIKey::W:
                operation = Gizmo::Operation::Translate;
                break;
            case ZEngine::UI::ZUIKey::E:
                operation = Gizmo::Operation::Rotate;
                break;
            case ZEngine::UI::ZUIKey::R:
                operation = Gizmo::Operation::Scale;
                break;
            default:
                return false;
        }
        return Session.RequestGizmoChange({.Kind = Gizmo::ChangeKind::Operation, .Mode = operation});
    }

    void Editor::OnEvent(Core::CoreEvent& /*e*/)
    {
        // Window and UI events are handled in ProcessEvent. Camera state is sampled
        // from InputManager during the application update.
    }

    void Editor::OnPreRender() {}

    void Editor::OnPostRender() {}

    void Editor::OnRenderUI()
    {
        if (ZUIUILayer)
        {
            ZUIUILayer->Render(nullptr, nullptr);
        }
        BuildSceneChangePrompt(RenderPipeline ? RenderPipeline->ZUICtx : nullptr);
    }

    void Editor::OnClosing()
    {
        DiscardSceneSettings();
        if (AssetImporter)
        {
            AssetImporter->Close();
        }
        CancelSceneOpen();
        MeshDrop.Cancel();
        m_scene_save.Pending = false;
        m_scene_file_input   = {};
        m_scene_file_input_owner.Reset();
        m_scene_save_picker_owner.Reset();
        m_new_scene_owner.Reset();
        m_editor_close_owner.Reset();
        m_new_scene_pending    = false;
        m_editor_close_pending = false;
        m_scene_change.Reset();
        Session.Shutdown(); // ECS targets are still alive; restore before device/scene teardown.
        GizmoViewport.Unbind();
        if (CurrentScene)
        {
            static_cast<EditorScene*>(CurrentScene)->Session = nullptr;
        }
        if (RenderPipeline && RenderPipeline->ZUICtx)
        {
            RenderPipeline->ZUICtx->InteractionCapture = {};
            SyncSceneChangeCapture(RenderPipeline->ZUICtx);
        }
    }

    void Editor::OnClosed() {}

    void Editor::PrepareScene(ZEngine::Applications::RenderFrameState& state)
    {
        GameApplication::PrepareScene(state);
        UpdateCameraRevision(state.Camera);
        m_render_target_revision = state.ResizeSequence;
        PrepareSelectionOutline(state);
        PrepareObjectPick(state);
        PrepareHoverPick(state);
        state.ShowGrid      = Session.GizmoSettings().ShowGrid;
        // Copy only U/V bitmap metrics from the existing Body font. Rendering
        // pairs these values with the Grid/camera retained in the same mailbox,
        // never this mutable UI context or a glyph pointer.
        state.GridLabelFont = {};
        const auto* ctx     = RenderPipeline ? RenderPipeline->ZUICtx : nullptr;
        const auto* atlas   = ctx ? ctx->Atlas : nullptr;
        const auto* font    = atlas ? atlas->Body : nullptr;
        if (font && font->Glyphs && atlas->Handle.Valid())
        {
            constexpr Core::Containers::FixedArray<uint32_t, 2> codepoints = {'U', 'V'};
            bool                                                complete   = true;
            Rendering::Renderers::GridLabelFontSnapshot         captured   = {};
            captured.Atlas                                                 = atlas->Handle;
            captured.LogicalViewportHeight                                 = GizmoLogicalViewportHeight;
            for (uint32_t i = 0; i < codepoints.size(); ++i)
            {
                if (codepoints[i] < font->FirstCodepoint || codepoints[i] - font->FirstCodepoint >= font->GlyphCount)
                {
                    complete = false;
                    break;
                }
                const auto& glyph  = font->Glyphs[codepoints[i] - font->FirstCodepoint];
                captured.Glyphs[i] = {
                    {glyph.U0, glyph.V0, glyph.U1, glyph.V1},
                    {glyph.Width * font->FontScale, glyph.Height * font->FontScale}
                };
            }
            if (complete)
            {
                state.GridLabelFont = captured;
            }
        }
        state.Gizmo.Reset();
        if (EnableGizmoDebug)
        {
            (void) Gizmo::BuildDebugFrame(state.Camera, GizmoLogicalViewportHeight, state.RenderTargetW, state.RenderTargetH, state.Gizmo);
        }
        else
        {
            GizmoViewport.BuildRender(Session, state.Camera, RenderPipeline ? RenderPipeline->ZUICtx : nullptr, m_gizmo_focused, state.RenderTargetW, state.RenderTargetH, state.Gizmo);
        }
    }

    void Editor::SaveScene()
    {
        if (!Session.OnOwnerThread())
        {
            return;
        }
        auto* scene = static_cast<EditorScene*>(CurrentScene);
        if (m_closing || !scene || !Session.Editable() || scene->SourceReadOnly)
        {
            return;
        }
        if (!scene->SourcePath.IsValid())
        {
            SaveSceneAs();
        }
        else if (!RequestSceneSave(scene->SourcePath, Session.SceneEpoch()))
            ZENGINE_CORE_WARN("[Editor] Scene save request rejected; finish the active edit first")
    }

    void Editor::SaveSceneAs()
    {
        if (!Session.OnOwnerThread())
        {
            return;
        }
        auto* scene = static_cast<EditorScene*>(CurrentScene);
        if (m_closing || !scene || !CurrentWindow || !Configuration || !Session.Editable() || scene->SourceReadOnly || Session.Active() || m_scene_save_picker.valid())
        {
            return;
        }

        std::string directory = {};
        if (!TrySceneDialogDirectory(Configuration->WorkingSpacePath.c_str(), Configuration->ScenePath.c_str(), scene->SourcePath, &directory))
        {
            ZENGINE_CORE_WARN("[Editor] Scene save directory is not a bounded UTF-8 native path")
            return;
        }
        std::string filename = "Scene.zescene";
        if (scene->SourcePath.IsValid())
        {
            const auto source_name = scene->SourcePath.Filename();
            filename.assign(source_name.Data, source_name.Length);
        }
        else
        {
            std::string_view name = {};
            if (TrySceneSourceName(scene->Name, &name) && !name.empty() && name.find_first_of("<>:\"/\\|?*\r\n") == std::string_view::npos && name != "." && name != "..")
            {
                filename = std::string(name) + ".zescene";
            }
        }
        std::string_view filters[] = {".zescene"};
        if (!m_scene_save_picker_owner.Capture(*this))
        {
            return;
        }
        m_scene_save_picker = CurrentWindow->SaveFileDialogAsync(filters, directory, filename, "Save scene");
    }

    void Editor::PollSceneSaveDialog()
    {
        if (!Session.OnOwnerThread())
        {
            return;
        }
        ZEngine::Windows::FileDialogResult result = {};
        if (!m_scene_save_picker.TryTake(&result))
        {
            return;
        }
        const auto epoch   = m_scene_save_picker_owner.Epoch;
        const bool matches = m_scene_save_picker_owner.Matches(*this);
        m_scene_save_picker_owner.Reset();
        // A stale picker owns neither current consent nor its saved checkpoint.
        if (!matches || m_closing)
        {
            return;
        }
        if (result.Outcome == ZEngine::Windows::FileDialogStatus::Failed)
        {
            m_scene_change.SaveFinished(epoch, false);
            ZENGINE_CORE_ERROR("[Editor] Scene save picker failed")
            return;
        }
        if (result.Outcome != ZEngine::Windows::FileDialogStatus::Selected || epoch != Session.SceneEpoch() || !Configuration)
        {
            m_scene_change.SaveCancelled(epoch);
            return;
        }
        const auto path = SceneSourcePathFromNative(Configuration->WorkingSpacePath.c_str(), result.Path);
        if (path.Failed() || !RequestSceneSave(path.Value(), epoch))
        {
            m_scene_change.SaveFinished(epoch, false);
            ZENGINE_CORE_WARN("[Editor] Save destination rejected; select a project .zescene path and finish the active edit")
        }
    }

    bool Editor::RequestSceneSave(const ZEngine::Core::VFS::VFSPath& path, uint64_t expected_epoch)
    {
        if (!Session.OnOwnerThread())
        {
            return false;
        }
        const auto* scene = static_cast<const EditorScene*>(CurrentScene);
        if (m_closing || !scene || scene->Session != &Session || !Session.Editable() || scene->SourceReadOnly || Session.Active() || expected_epoch != Session.SceneEpoch() || !IsSceneSourcePath(path) || m_scene_save.Pending)
        {
            return false;
        }
        if (!m_scene_save.Owner.Capture(*this))
        {
            return false;
        }
        m_scene_save.Path    = path;
        m_scene_save.Pending = true;
        return true;
    }

    bool Editor::ProcessSceneSave(ZEngine::EngineContext& engine)
    {
        if (!m_scene_save.Pending)
        {
            return true;
        }
        if (!Session.InMutationPhase())
        {
            return false;
        }
        if (!m_scene_save.Owner.Matches(*this))
        {
            m_scene_save.Pending = false;
            return false; // obsolete work cannot complete a replacement owner's consent
        }
        if (Session.PendingRequestCount() || !ProcessSceneSettings(engine))
        {
            return false;
        }
        m_scene_save.Pending = false;
        const auto complete  = [&](bool succeeded) {
            m_scene_change.SaveFinished(m_scene_save.Owner.Epoch, succeeded);
            return succeeded;
        };
        auto* scene = static_cast<EditorScene*>(CurrentScene);
        if (!scene || !engine.VFS || !engine.SceneSchemas || engine.Scene != Session.Scene())
        {
            return complete(false);
        }

        // Revalidate the project boundary at execution, not only when the native
        // picker completed. A queued save must not follow a newly retargeted symlink.
        if (!WorkingSpacePath || !WorkingSpacePath[0])
        {
            return complete(false);
        }
        char native_path[ZEngine::Core::VFS::VFS_MAX_PATH] = {};
        m_scene_save.Path.ResolveNative(WorkingSpacePath, native_path, sizeof(native_path));
        const auto resolved = SceneSourcePathFromNative(WorkingSpacePath, native_path);
        if (resolved.Failed() || resolved.Value() != m_scene_save.Path)
        {
            ZENGINE_CORE_WARN("[Editor] Scene destination changed or escaped the project; save cancelled")
            return complete(false);
        }

        ZEngine::ECS::SceneDiagnostics diagnostics = {};
        if (!SaveEditorSceneDocument(*scene, Session, *engine.SceneSchemas, *engine.VFS, m_scene_save.Path, &diagnostics))
        {
            for (uint32_t i = 0; i < diagnostics.Count; ++i)
                ZENGINE_CORE_ERROR("[Editor] {}", diagnostics.Entries[i].Message)
            return complete(false);
        }
        ZENGINE_CORE_INFO("[Editor] Saved scene {}", scene->SourcePath.CStr())
        return complete(true);
    }

    bool Editor::RequestNewScene()
    {
        if (!Session.OnOwnerThread())
        {
            return false;
        }
        auto* scene = static_cast<EditorScene*>(CurrentScene);
        if (m_closing || !scene || scene->Session != &Session || !Session.Ready() || m_new_scene_pending || m_editor_close_pending || SceneChangePromptState().Visible())
        {
            return false;
        }
        if (!m_new_scene_owner.Capture(*this))
        {
            return false;
        }
        m_new_scene_pending = true;
        MeshDrop.Cancel(); // Delayed placement must not author behind lifecycle consent.
        return true;
    }

    bool Editor::ProcessNewScene(ZEngine::EngineContext& engine)
    {
        if (!m_new_scene_pending)
        {
            return true;
        }
        if (!Session.InMutationPhase())
        {
            return false;
        }
        const auto finish = [&] {
            m_new_scene_pending = false;
            m_scene_change.Finish(SceneChangeKind::NewScene, m_new_scene_owner.Epoch);
            m_new_scene_owner.Reset();
        };
        // Consent belongs to the original scene/project/window, not merely its
        // epoch. Retire foreign work before draining settings or cancelling a
        // newer interaction; restoring the old owners must never revive it.
        if (m_closing || !m_new_scene_owner.Matches(*this) || !Configuration || !engine.Scene || engine.Scene != Session.Scene())
        {
            finish();
            return false;
        }
        if (Session.PendingRequestCount() || !ProcessSceneSettings(engine))
        {
            return false;
        }
        auto* scene = static_cast<EditorScene*>(CurrentScene);
        // A lifecycle request cancels a preview, never silently commits it. Test
        // dirty state only after restoring the captured authored before-values.
        Session.Cancel();
        const auto gate = GateSceneChange(SceneChangeKind::NewScene);
        if (gate == SceneChangeGate::Waiting)
        {
            return true;
        }
        finish();
        if (gate != SceneChangeGate::Ready)
        {
            return true;
        }

        // Unbind first: restore the outgoing preview and publish its derived frame
        // before actor OnDestroy or entity teardown can observe it.
        if (!Session.BindScene(nullptr))
        {
            return false;
        }
        DiscardSceneSettings();
        m_scene_save.Pending = false;
        if (engine.WorldCommands)
        {
            engine.WorldCommands->Clear();
        }
        if (engine.ActorManager)
        {
            engine.ActorManager->DestroyAll();
        }
        engine.Scene->ForEachEntity([&](ZEngine::ECS::EntityID entity) { engine.Scene->DestroyEntity(entity); });
        scene->Reset(Configuration->DefaultSky);
        scene->Name = "Untitled Scene";
        if (!Session.BindScene(engine.Scene, false) || !scene->AttachSession(Session, engine))
        {
            return false;
        }
        return true;
    }

    void EditorConfiguration::ReadConfig(ZEngine::Core::Memory::ArenaAllocator* arena, const char* file)
    {
        std::ifstream   f(file);
        nlohmann::json  config              = nlohmann::json::parse(f);
        std::error_code path_error          = {};
        const auto      config_path         = std::filesystem::absolute(std::filesystem::path(file), path_error);
        std::string     root_project_dir    = path_error ? std::filesystem::path(file).parent_path().string() : config_path.parent_path().string();

        // Helper: expand $(workingSpace) token in a json string field.
        // Result is a workspace-relative sub-path, e.g. "Assets/Meshes" (no leading slash).
        auto            strip_leading_slash = [](std::string& s) {
            if (!s.empty() && s[0] == '/')
            {
                s.erase(0, 1);
            }
        };
        auto expand = [&](nlohmann::json& node, std::string_view key) {
            std::string_view lookup("$(workingSpace)");
            auto             s = node[key].get<std::string>();
            auto             p = s.find(lookup);
            if (p != std::string::npos)
            {
                s = s.replace(p, lookup.size(), "");
                strip_leading_slash(s);
                node[key] = s;
            }
        };
        auto expand_nested = [&](nlohmann::json& node, std::string_view section, std::string_view key) {
            std::string_view lookup("$(workingSpace)");
            auto             s = node[section][key].get<std::string>();
            auto             p = s.find(lookup);
            if (p != std::string::npos)
            {
                s = s.replace(p, lookup.size(), "");
                strip_leading_slash(s);
                node[section][key] = s;
            }
        };

        std::string working_space_path = config["workingSpace"];
        if (working_space_path == ".")
        {
            expand(config, "sceneDir");
            // Expand all fields inside assetDirs (new format)
            if (config.contains("assetDirs"))
            {
                for (auto& [key, val] : config["assetDirs"].items())
                {
                    expand_nested(config, "assetDirs", key);
                }
            }
            // Backward compat: old defaultImportDir format
            if (config.contains("defaultImportDir"))
            {
                expand_nested(config, "defaultImportDir", "textureDir");
                expand_nested(config, "defaultImportDir", "soundDir");
            }
            config["workingSpace"] = root_project_dir;
        }

        auto ws = config["workingSpace"].get<std::string>();
        ProjectName.init(arena, config["projectName"].get<std::string>().c_str());
        WorkingSpacePath.init(arena, ws.c_str());
        ScenePath.init(arena, config["sceneDir"].get<std::string>().c_str());

        // Asset directories — new assetDirs format, fall back to defaultImportDir
        auto asset_path = [&](const char* asset_key, const char* legacy_section, const char* legacy_key, const char* default_suffix) -> std::string {
            if (config.contains("assetDirs") && config["assetDirs"].contains(asset_key))
            {
                return config["assetDirs"][asset_key].get<std::string>();
            }
            if (legacy_section && config.contains(legacy_section) && config[legacy_section].contains(legacy_key))
            {
                return config[legacy_section][legacy_key].get<std::string>();
            }
            return fmt::format("{}{}", ws, default_suffix);
        };

        TexturePath.init(arena, asset_path("textureDir", "defaultImportDir", "textureDir", "/Assets/Textures").c_str());
        SoundPath.init(arena, asset_path("soundDir", "defaultImportDir", "soundDir", "/Assets/Sounds").c_str());
        MeshPath.init(arena, asset_path("meshDir", nullptr, nullptr, "/Assets/Meshes").c_str());
        MaterialPath.init(arena, asset_path("materialDir", nullptr, nullptr, "/Assets/Materials").c_str());
        SpritePath.init(arena, asset_path("spriteDir", nullptr, nullptr, "/Assets/Sprites").c_str());
        EnvironmentMapImportPath.init(arena, asset_path("environmentMapDir", nullptr, nullptr, "/Assets/EnvironmentMaps").c_str());

        // Project sky data is a template for newly created scenes. It never
        // replaces the SkyConfig serialized by an existing scene.
        const nlohmann::json* sky_defaults = nullptr;
        if (config.contains("skyDefaults") && config["skyDefaults"].is_object())
        {
            sky_defaults = &config["skyDefaults"];
        }
        else if (config.contains("sky") && config["sky"].is_object())
        {
            sky_defaults = &config["sky"]; // compatibility with the initial project template
        }

        if (sky_defaults)
        {
            const auto& sky = *sky_defaults;
            if (sky.contains("mode") && sky["mode"].is_string())
            {
                const std::string mode = sky["mode"].get<std::string>();
                if (mode == "hdri")
                {
                    DefaultSky.Mode = ZEngine::Rendering::Scenes::SkyMode::HDRI;
                }
                else if (mode == "skySphere")
                {
                    DefaultSky.Mode = ZEngine::Rendering::Scenes::SkyMode::SkySphere;
                }
            }
            if (sky.contains("environmentMap") && sky["environmentMap"].is_string())
            {
                auto environment_uuid = uuids::uuid::from_string(sky["environmentMap"].get<std::string>());
                if (environment_uuid.has_value())
                {
                    DefaultSky.EnvironmentMap = environment_uuid.value();
                }
            }
            if (sky.contains("environmentIntensity") && sky["environmentIntensity"].is_number())
            {
                DefaultSky.EnvironmentIntensity = sky["environmentIntensity"].get<float>();
            }
            if (sky.contains("skyLuminanceFactor") && sky["skyLuminanceFactor"].is_number())
            {
                DefaultSky.SkyLuminanceFactor = sky["skyLuminanceFactor"].get<float>();
            }
            if (sky.contains("skyDiffuseIntensity") && sky["skyDiffuseIntensity"].is_number())
            {
                DefaultSky.SkyDiffuseIntensity = sky["skyDiffuseIntensity"].get<float>();
            }
            if (sky.contains("skySpecularIntensity") && sky["skySpecularIntensity"].is_number())
            {
                DefaultSky.SkySpecularIntensity = sky["skySpecularIntensity"].get<float>();
            }
            if (sky.contains("aerialPerspectiveStartDepthKilometers") && sky["aerialPerspectiveStartDepthKilometers"].is_number())
            {
                DefaultSky.AerialPerspectiveStartDepthKilometers = sky["aerialPerspectiveStartDepthKilometers"].get<float>();
            }
            if (sky.contains("aerialPerspectiveDistanceScale") && sky["aerialPerspectiveDistanceScale"].is_number())
            {
                DefaultSky.AerialPerspectiveDistanceScale = sky["aerialPerspectiveDistanceScale"].get<float>();
            }
            if (sky.contains("environmentYawRadians") && sky["environmentYawRadians"].is_number())
            {
                DefaultSky.EnvironmentYawRadians = sky["environmentYawRadians"].get<float>();
            }
            if (sky.contains("environmentTint") && sky["environmentTint"].is_array() && sky["environmentTint"].size() == 4)
            {
                bool valid_tint = true;
                for (uint32_t i = 0; i < 4; ++i)
                {
                    if (!sky["environmentTint"][i].is_number())
                    {
                        valid_tint = false;
                        break;
                    }
                }
                if (valid_tint)
                {
                    for (uint32_t i = 0; i < 4; ++i)
                    {
                        DefaultSky.EnvironmentTint[i] = sky["environmentTint"][i].get<float>();
                    }
                }
            }
            if (sky.contains("skyLowerHemisphereColor") && sky["skyLowerHemisphereColor"].is_array() && sky["skyLowerHemisphereColor"].size() == 3)
            {
                bool valid_color = true;
                for (uint32_t i = 0; i < 3; ++i)
                {
                    if (!sky["skyLowerHemisphereColor"][i].is_number())
                    {
                        valid_color = false;
                        break;
                    }
                }
                if (valid_color)
                {
                    for (uint32_t i = 0; i < 3; ++i)
                    {
                        DefaultSky.SkyLowerHemisphereColor[i] = sky["skyLowerHemisphereColor"][i].get<float>();
                    }
                }
            }
        }
        DefaultSky.Sanitize();

        /*
         * Retreiving the Active Scene
         */
        for (const auto& scene : config["sceneList"])
        {
            bool is_default = scene["isDefault"].get<bool>();
            if (!is_default)
            {
                continue;
            }
            ActiveSceneName.init(arena, scene["name"].get<std::string>().c_str());
            break;
        }
    }
} // namespace Tetragrama
