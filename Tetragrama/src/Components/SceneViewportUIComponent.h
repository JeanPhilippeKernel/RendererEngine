#pragma once
#include <Components/Events/SceneViewportFocusedEvent.h>
#include <Components/Events/SceneViewportResizedEvent.h>
#include <Components/Events/SceneViewportUnfocusedEvent.h>
#include <Messengers/Message.h>
#include <ZEngine/ZEngine.h>
#include <vulkan/vulkan.h>

namespace Tetragrama::Components
{
    class SceneViewportUIComponent : public ZEngine::Components::UI::UIComponent
    {
    public:
        SceneViewportUIComponent(std::string_view name = "Scene", bool visibility = true);
        virtual ~SceneViewportUIComponent();

        void         Update(ZEngine::Core::TimeStep dt) override;
        virtual void Render() override;

    public:
        std::future<void> SceneViewportClickedMessageHandlerAsync(Messengers::ArrayValueMessage<int, 2>&);
        std::future<void> SceneViewportFocusedMessageHandlerAsync(Messengers::GenericMessage<bool>&);
        std::future<void> SceneViewportUnfocusedMessageHandlerAsync(Messengers::GenericMessage<bool>&);

    private:
        bool                  m_is_window_focused{false};
        bool                  m_is_window_hovered{false};
        bool                  m_is_window_clicked{false};
        bool                  m_refresh_texture_handle{false};
        bool                  m_request_renderer_resize{false};
        ImVec2                m_viewport_size{0.f, 0.f};
        ImVec2                m_content_region_available_size{0.f, 0.f};
        std::array<ImVec2, 2> m_viewport_bounds;
        VkDescriptorSet       m_scene_texture{VK_NULL_HANDLE};
    };
} // namespace Tetragrama::Components
