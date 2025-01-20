#include <pch.h>
#include <MessageToken.h>
#include <Messengers/Messenger.h>
#include <SceneViewportUIComponent.h>
#include <ZEngine/Logging/LoggerDefinition.h>
#include <ZEngine/Rendering/Renderers/GraphicRenderer.h>
#include <ZEngine/Windows/Inputs/KeyCodeDefinition.h>
/**/
#include <Editor.h>
#include <ImGuizmo/ImGuizmo.h>

using namespace Tetragrama::Components::Event;
using namespace ZEngine::Rendering::Renderers;
using namespace ZEngine::Hardwares;
using namespace ZEngine::Rendering;
using namespace ZEngine;

namespace Tetragrama::Components
{
    SceneViewportUIComponent::SceneViewportUIComponent(Layers::ImguiLayer* parent, std::string_view name, bool visibility) : UIComponent(parent, name, visibility, false)
    {
        // ImGuizmo configuration
        ImGuizmo::AllowAxisFlip(false);
        ImGuizmo::SetOrthographic(false);
    }

    SceneViewportUIComponent::~SceneViewportUIComponent() {}

    void SceneViewportUIComponent::Update(ZEngine::Core::TimeStep dt)
    {
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
            if (m_idle_frame_count >= m_idle_frame_threshold)
            {
                m_is_resizing             = false;
                m_request_renderer_resize = true;
            }
        }

        auto ctx = reinterpret_cast<EditorContext*>(ParentLayer->ParentContext);
        if (m_request_renderer_resize)
        {
            ctx->CameraControllerPtr->SetViewport(m_viewport_size.x, m_viewport_size.y);
        }

        if (m_is_window_hovered && m_is_window_focused)
        {
            ctx->CameraControllerPtr->ResumeEventProcessing();
        }
        else
        {
            ctx->CameraControllerPtr->PauseEventProcessing();
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
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::Begin(Name.c_str(), (CanBeClosed ? &CanBeClosed : NULL), ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove);

        auto viewport_offset            = ImGui::GetCursorPos();
        m_content_region_available_size = ImGui::GetContentRegionAvail();
        m_is_window_focused             = ImGui::IsWindowFocused();
        m_is_window_hovered             = ImGui::IsWindowHovered();
        m_is_window_clicked             = ImGui::IsMouseClicked(static_cast<int>(ZENGINE_KEY_MOUSE_LEFT));

        // Scene texture representation
        if (!m_scene_texture || m_refresh_texture_handle)
        {
            m_scene_texture          = renderer->GetImguiFrameOutput();
            m_refresh_texture_handle = false;
        }

        ImGui::Image(m_scene_texture, m_viewport_size, ImVec2(0, 1), ImVec2(1, 0));
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

        ImGui::End();

        ImGui::PopStyleVar();

        if (m_request_renderer_resize)
        {
            renderer->EnqueuedResizeRequests.Emplace({.Width = (uint32_t) m_viewport_size.x, .Height = (uint32_t) m_viewport_size.y});
            m_refresh_texture_handle  = true;
            m_request_renderer_resize = false;
        }
    }

    // std::future<void> SceneViewportUIComponent::SceneViewportClickedMessageHandlerAsync(Messengers::ArrayValueMessage<int, 2>& e)
    //{
    //     // Messengers::IMessenger::Send<ZEngine::Layers::Layer, Messengers::GenericMessage<std::pair<int, int>>>(
    //     //     EDITOR_RENDER_LAYER_SCENE_REQUEST_SELECT_ENTITY_FROM_PIXEL, Messengers::GenericMessage<std::pair<int, int>>{e});
    //     co_return;
    // }

    // std::future<void> SceneViewportUIComponent::SceneViewportFocusedMessageHandlerAsync(Messengers::GenericMessage<bool>& e)
    //{
    //     co_return;
    //     //co_await Messengers::IMessenger::SendAsync<Windows::Layers::Layer, Messengers::GenericMessage<bool>>(EDITOR_RENDER_LAYER_SCENE_REQUEST_FOCUS, Messengers::GenericMessage<bool>{e});
    // }

    // std::future<void> SceneViewportUIComponent::SceneViewportUnfocusedMessageHandlerAsync(Messengers::GenericMessage<bool>& e)
    //{
    //     co_return;
    //     //co_await Messengers::IMessenger::SendAsync<Windows::Layers::Layer, Messengers::GenericMessage<bool>>(EDITOR_RENDER_LAYER_SCENE_REQUEST_UNFOCUS, Messengers::GenericMessage<bool>{e});
    // }
} // namespace Tetragrama::Components
