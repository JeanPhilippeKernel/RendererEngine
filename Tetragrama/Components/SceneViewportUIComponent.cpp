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
                                // Strip working space prefix — remainder is VFS path
                                auto relative = ZEngine::Core::VFS::VFSPath::Parse(buf + ws_len);
                                if (relative.Succeeded())
                                {
                                    vfs_path = relative.Value();
                                    ZENGINE_CORE_INFO("SceneViewport: VFS path='{}'", vfs_path.CStr())
                                    ctx->ImportCoordinator->Enqueue(vfs_path, ZEngine::Importers::ImportPriority::Immediate);
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
