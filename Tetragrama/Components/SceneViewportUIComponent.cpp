#include <Tetragrama/Components/Events/UIComponentEvent.h>
#include <Tetragrama/Components/SceneViewportUIComponent.h>
#include <Tetragrama/MessageToken.h>
#include <Tetragrama/Messengers/Messenger.h>
#include <ZEngine/Engine.h>
#include <ZEngine/Importers/ImportJob.h>
#include <ZEngine/Logging/LoggerDefinition.h>
#include <ZEngine/Rendering/Renderers/GraphicRenderer.h>
#include <ZEngine/Windows/Inputs/KeyCodeDefinition.h>
/**/
#include <ImGuizmo/ImGuizmo.h>
#include <Tetragrama/Editor.h>
#include <cstring>
#include <filesystem>

using namespace Tetragrama::Components::Event;
using namespace ZEngine::Rendering::Renderers;
using namespace ZEngine::Hardwares;
using namespace ZEngine::Rendering;
using namespace ZEngine;

namespace Tetragrama::Components
{
    SceneViewportUIComponent::SceneViewportUIComponent() {}

    SceneViewportUIComponent::~SceneViewportUIComponent() {}

    void SceneViewportUIComponent::Initialize(Layers::ImguiLayer* parent, const char* name, bool visibility, bool closed)
    {
        UIComponent::Initialize(parent, name, visibility, closed);

        // ImGuizmo configuration
        ImGuizmo::AllowAxisFlip(false);
        ImGuizmo::SetOrthographic(false);
    }

    void SceneViewportUIComponent::Update(ZEngine::Core::TimeStep dt)
    {
        auto app = reinterpret_cast<EditorPtr>(ParentLayer->CurrentApp);

        if ((m_viewport_size.x != m_content_region_available_size.x) || (m_viewport_size.y != m_content_region_available_size.y))
        {
            if (!m_is_resizing)
            {
                m_is_resizing = true;
            }

            m_viewport_size    = m_content_region_available_size;
            m_idle_frame_count = 0;
        }
        else if (m_is_resizing)
        {
            m_idle_frame_count++;
            if (m_idle_frame_count >= app->RenderPipeline->Device->SwapchainPtr->IdleFrameThreshold)
            {
                m_is_resizing             = false;
                m_request_renderer_resize = true;
            }
        }

        auto* camera_controller = app->CameraController;

        camera_controller->SetViewportOrigin(m_viewport_bounds[0].x, m_viewport_bounds[0].y);

        if (m_request_renderer_resize)
        {
            camera_controller->SetViewport(m_viewport_size.x, m_viewport_size.y);
        }

        if (m_is_window_hovered && m_is_window_focused)
        {
            camera_controller->ResumeEventProcessing();
        }
        else
        {
            camera_controller->PauseEventProcessing();
        }

        if (m_is_window_clicked && m_is_window_hovered && m_is_window_focused)
        {
            auto mouse_position   = ImGui::GetMousePos();
            mouse_position.x     -= m_viewport_bounds[0].x;
            mouse_position.y     -= m_viewport_bounds[0].y;

            auto mouse_bounded_x  = static_cast<int>(mouse_position.x);
            auto mouse_bounded_y  = static_cast<int>(mouse_position.y);
            // Todo : We should store mouse position...
        }
    }

    void SceneViewportUIComponent::Render(ZEngine::Rendering::Renderers::GraphicRenderer* const renderer, ZEngine::Hardwares::CommandBuffer* const command_buffer)
    {
        auto app = reinterpret_cast<EditorPtr>(ParentLayer->CurrentApp);

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::Begin(Name, (CanBeClosed ? &CanBeClosed : NULL), ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove);

        auto viewport_offset            = ImGui::GetCursorPos();
        m_content_region_available_size = ImGui::GetContentRegionAvail();
        m_is_window_focused             = ImGui::IsWindowFocused();
        m_is_window_hovered             = ImGui::IsWindowHovered();
        m_is_window_clicked             = ImGui::IsMouseClicked(static_cast<int>(ZENGINE_KEY_MOUSE_LEFT));

        // Scene texture representation
        if (!m_scene_texture || m_refresh_texture_handle)
        {
            m_scene_texture          = app->RenderPipeline->SceneRenderer->GetFrameOutput();
            m_refresh_texture_handle = false;
        }

        if (m_scene_texture.Valid())
        {
            ImGui::Image((ImTextureID) m_scene_texture.Index, m_viewport_size, ImVec2(0, 0), ImVec2(1, 1));
        }

        // ViewPort bound computation
        ImVec2 viewport_windows_size  = ImGui::GetWindowSize();
        ImVec2 minimum_bound          = ImGui::GetWindowPos();
        minimum_bound.x              += viewport_offset.x;
        minimum_bound.y              += viewport_offset.y;

        ImVec2 maximum_bound          = {minimum_bound.x + viewport_windows_size.x, minimum_bound.y + viewport_windows_size.y};

        m_viewport_bounds[0]          = minimum_bound;
        m_viewport_bounds[1]          = maximum_bound;

        // ImGuizmo configuration
        ImGuizmo::SetRect(minimum_bound.x, minimum_bound.y, m_viewport_size.x, m_viewport_size.y);

        ImGuizmo::SetDrawlist();

        if (ImGui::BeginDragDropTarget())
        {
            char buf[DEFAULT_STR_BUFFER] = {0};
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_FILE_DRAG_OP"))
            {
                ZEngine::Helpers::secure_memcpy(buf, DEFAULT_STR_BUFFER, payload->Data, payload->DataSize);
                if (ZEngine::Helpers::secure_strlen(buf) > 0)
                {
                    auto file_ext = std::filesystem::path(buf).extension().string();
                    if (file_ext == ".zescene")
                    {
                        Messengers::IMessenger::SendAsync<ZEngine::Applications::Layer, Messengers::GenericMessage<std::string>>(EDITOR_COMPONENT_DOCKSPACE_REQUEST_OPENSCENE, Messengers::GenericMessage<std::string>(buf));
                    }
                    else if (file_ext == ".zemesh")
                    {
                        Messengers::IMessenger::SendAsync<ZEngine::Applications::Layer, Messengers::GenericMessage<std::string>>(EDITOR_COMPONENT_DOCKSPACE_REQUEST_OPENMESH, Messengers::GenericMessage<std::string>(buf));
                    }
                    else if (file_ext == ".glb" || file_ext == ".gltf" || file_ext == ".fbx" || file_ext == ".obj")
                    {
                        ZENGINE_CORE_INFO("SceneViewport: dropped native='{}'", buf)
                        auto* ctx = ZEngine::Engine::GetContext();
                        if (!ctx || !ctx->ImportCoordinator || !ctx->VFS)
                        {
                            ZENGINE_CORE_ERROR("SceneViewport: missing engine context, coordinator, or VFS")
                        }
                        else
                        {
                            // buf is a native absolute path. Strip WorkingSpacePath prefix to get
                            // the VFS-relative path that the VFS backend is mounted on.
                            auto                        app = reinterpret_cast<Tetragrama::EditorPtr>(ParentLayer->CurrentApp);
                            const char*                 ws  = app ? app->WorkingSpacePath : "";

                            ZEngine::Core::VFS::VFSPath vfs_path;
                            size_t                      ws_len = ZEngine::Helpers::secure_strlen(ws);
                            if (ws_len > 0 && std::strncmp(buf, ws, ws_len) == 0)
                            {
                                auto relative = ZEngine::Core::VFS::VFSPath::Parse(buf + ws_len);
                                if (relative.Succeeded())
                                {
                                    vfs_path = relative.Value();
                                    ZENGINE_CORE_INFO("SceneViewport: VFS path='{}'", vfs_path.CStr())

                                    // Enqueue returns the asset UUID from the meta file — valid
                                    // regardless of whether the import runs now (first drop) or
                                    // was already cached (re-drop). Create the scene instance
                                    // immediately; the render system shows it once the mesh uploads.
                                    auto uuid = ctx->ImportCoordinator->Enqueue(vfs_path, ZEngine::Importers::ImportPriority::Immediate);
                                    if (uuid != uuids::uuid{})
                                    {
                                        auto* scene = reinterpret_cast<Tetragrama::EditorScenePtr>(app->CurrentScene);
                                        if (scene)
                                        {
                                            const char* p    = vfs_path.CStr();
                                            const char* name = strrchr(p, '/');
                                            name             = name ? name + 1 : p;
                                            scene->AddMeshInstance(uuid, name);
                                        }
                                    }
                                }
                                else
                                {
                                    ZENGINE_CORE_ERROR("SceneViewport: failed to parse VFS path from '{}'", buf + ws_len)
                                }
                            }
                            else
                            {
                                ZENGINE_CORE_WARN("SceneViewport: native path '{}' is outside working space '{}'", buf, ws)
                            }
                        }
                    }
                }
            }
            ImGui::EndDragDropTarget();
        }

        // Viewport overlay toolbar — after drag-drop so Image stays the last item for drop target.
        {
            typedef void (*DrawIconFn)(ImDrawList*, ImVec2, float, ImU32);

            auto overlay_btn = [](cstring id, ImVec2 pos, float btn_sz, bool active, ImVec4 active_col, cstring tip, ImDrawList* dl, DrawIconFn icon_fn) -> bool {
                ImVec4 bg = active ? ImVec4{active_col.x * .25f, active_col.y * .25f, active_col.z * .25f, .92f} : ImVec4{.12f, .12f, .12f, .70f};
                ImGui::SetCursorScreenPos(pos);
                ImGui::PushStyleColor(ImGuiCol_Button, bg);
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4{.30f, .30f, .30f, .90f});
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, {active_col.x * .4f, active_col.y * .4f, active_col.z * .4f, 1.f});
                bool hit = ImGui::Button(id, {btn_sz, btn_sz});
                ImGui::PopStyleColor(3);
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("%s", tip);
                ImVec4 ic = active ? active_col : ImVec4{active_col.x * .5f, active_col.y * .5f, active_col.z * .5f, 1.f};
                icon_fn(dl, pos, btn_sz, ImGui::ColorConvertFloat4ToU32(ic));
                return hit;
            };

            static DrawIconFn icon_grid = [](ImDrawList* d, ImVec2 p, float sz, ImU32 c) {
                float  m  = 5.f;
                ImVec2 p0 = {p.x + m, p.y + m};
                float  s  = sz - m * 2.f;
                for (int i = 1; i <= 3; ++i)
                {
                    float tx = p0.x + s * i / 4.f, ty = p0.y + s * i / 4.f;
                    d->AddLine({tx, p0.y}, {tx, p0.y + s}, c, 1.2f);
                    d->AddLine({p0.x, ty}, {p0.x + s, ty}, c, 1.2f);
                }
            };
            static DrawIconFn icon_translate = [](ImDrawList* d, ImVec2 p, float sz, ImU32 c) {
                float cx = p.x + sz * .5f, cy = p.y + sz * .5f, r = sz * .28f, al = r * .9f, aw = r * .35f;
                d->AddLine({cx, cy - al}, {cx, cy + al}, c, 1.5f);
                d->AddLine({cx - al, cy}, {cx + al, cy}, c, 1.5f);
                d->AddTriangleFilled({cx, cy - al - aw}, {cx - aw * .6f, cy - al + aw * .5f}, {cx + aw * .6f, cy - al + aw * .5f}, c);
                d->AddTriangleFilled({cx, cy + al + aw}, {cx - aw * .6f, cy + al - aw * .5f}, {cx + aw * .6f, cy + al - aw * .5f}, c);
                d->AddTriangleFilled({cx - al - aw, cy}, {cx - al + aw * .5f, cy - aw * .6f}, {cx - al + aw * .5f, cy + aw * .6f}, c);
                d->AddTriangleFilled({cx + al + aw, cy}, {cx + al - aw * .5f, cy - aw * .6f}, {cx + al - aw * .5f, cy + aw * .6f}, c);
            };
            static DrawIconFn icon_rotate = [](ImDrawList* d, ImVec2 p, float sz, ImU32 c) {
                float cx = p.x + sz * .5f, cy = p.y + sz * .5f, r = sz * .28f, aw = r * .45f;
                d->AddCircle({cx, cy}, r, c, 24, 1.8f);
                d->AddTriangleFilled({cx + r, cy}, {cx + r - aw, cy - aw * .6f}, {cx + r - aw, cy + aw * .6f}, c);
            };
            static DrawIconFn icon_scale = [](ImDrawList* d, ImVec2 p, float sz, ImU32 c) {
                float cx = p.x + sz * .5f, cy = p.y + sz * .5f, r = sz * .28f, h = r * .75f, dot = r * .22f;
                d->AddRect({cx - h, cy - h}, {cx + h, cy + h}, c, 0, 0, 1.5f);
                for (int dx = -1; dx <= 1; dx += 2)
                    for (int dy = -1; dy <= 1; dy += 2)
                        d->AddCircleFilled({cx + dx * h, cy + dy * h}, dot, c, 6);
            };

            auto*       scene  = app->CurrentScene ? reinterpret_cast<EditorScenePtr>(app->CurrentScene) : nullptr;
            int&        gizmo  = app->Configuration->GizmoOperation;

            const float btn_sz = 28.0f, pad = 8.0f, gap = 4.0f;
            ImVec2      base  = ImGui::GetWindowPos();
            base.x           += viewport_offset.x + pad;
            base.y           += viewport_offset.y + pad;
            ImDrawList* dl    = ImGui::GetWindowDrawList();

            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);

            if (scene)
            {
                bool grid_on = scene->Grid.Enabled;
                if (overlay_btn("##grid", base, btn_sz, grid_on, {0.30f, 0.80f, 0.90f, 1.0f}, grid_on ? "Hide Grid" : "Show Grid", dl, icon_grid))
                {
                    scene->Grid.Enabled = !grid_on;
                    scene->GridDirty[0].value.store(true, std::memory_order_release);
                    scene->GridDirty[1].value.store(true, std::memory_order_release);
                    scene->GridDirty[2].value.store(true, std::memory_order_release);
                }
            }

            dl->AddLine({base.x + 4.f, base.y + btn_sz + gap}, {base.x + btn_sz - 4.f, base.y + btn_sz + gap}, IM_COL32(255, 255, 255, 40), 1.f);

            struct
            {
                cstring    id;
                int        op;
                ImVec4     col;
                cstring    tip;
                DrawIconFn fn;
            } kBtns[] = {
                {"##gt", ImGuizmo::OPERATION::TRANSLATE,  {.33f, .60f, 1.f, 1.f}, "Translate (T)", icon_translate},
                {"##gr",    ImGuizmo::OPERATION::ROTATE,  {1.f, .60f, .20f, 1.f},    "Rotate (R)",    icon_rotate},
                {"##gs",     ImGuizmo::OPERATION::SCALE, {.30f, .85f, .40f, 1.f},     "Scale (S)",     icon_scale},
            };
            float gy = base.y + btn_sz + gap * 3.f;
            for (int i = 0; i < 3; ++i)
            {
                bool on = (gizmo == kBtns[i].op);
                if (overlay_btn(kBtns[i].id, {base.x, gy + i * (btn_sz + gap)}, btn_sz, on, kBtns[i].col, kBtns[i].tip, dl, kBtns[i].fn))
                    gizmo = on ? -1 : kBtns[i].op;
            }

            ImGui::PopStyleVar();
        }

        ImGui::End();

        ImGui::PopStyleVar();

        if (m_request_renderer_resize)
        {
            app->State->RenderTargetResizeRequests.Emplace({.Width = (uint32_t) m_viewport_size.x, .Height = (uint32_t) m_viewport_size.y});
            m_refresh_texture_handle  = true;
            m_request_renderer_resize = false;
        }
    }

    // std::future<void>
    // SceneViewportUIComponent::SceneViewportClickedMessageHandlerAsync(Messengers::ArrayValueMessage<int, 2>& e)
    //{
    //     // Messengers::IMessenger::Send<ZEngine::Layers::Layer, Messengers::GenericMessage<std::pair<int, int>>>(
    //     //     EDITOR_RENDER_LAYER_SCENE_REQUEST_SELECT_ENTITY_FROM_PIXEL, Messengers::GenericMessage<std::pair<int,
    //     int>>{e}); co_return;
    // }

    // std::future<void>
    // SceneViewportUIComponent::SceneViewportFocusedMessageHandlerAsync(Messengers::GenericMessage<bool>& e)
    //{
    //     co_return;
    //     //co_await Messengers::IMessenger::SendAsync<Windows::Layers::Layer,
    //     Messengers::GenericMessage<bool>>(EDITOR_RENDER_LAYER_SCENE_REQUEST_FOCUS,
    //     Messengers::GenericMessage<bool>{e});
    // }

    // std::future<void>
    // SceneViewportUIComponent::SceneViewportUnfocusedMessageHandlerAsync(Messengers::GenericMessage<bool>& e)
    //{
    //     co_return;
    //     //co_await Messengers::IMessenger::SendAsync<Windows::Layers::Layer,
    //     Messengers::GenericMessage<bool>>(EDITOR_RENDER_LAYER_SCENE_REQUEST_UNFOCUS,
    //     Messengers::GenericMessage<bool>{e});
    // }
} // namespace Tetragrama::Components
