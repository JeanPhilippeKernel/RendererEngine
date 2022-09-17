#pragma once
#include <ZEngine/ZEngine.h>
#include <Components/Events/SceneViewportResizedEvent.h>
#include <Components/Events/SceneViewportFocusedEvent.h>
#include <Components/Events/SceneViewportUnfocusedEvent.h>
#include <Messengers/Message.h>

namespace Tetragrama::Components
{
    class SceneViewportUIComponent : public ZEngine::Components::UI::UIComponent
    {
    public:
        SceneViewportUIComponent(std::string_view name = "Scene Viewport", bool visibility = true);
        virtual ~SceneViewportUIComponent();

        void Update(ZEngine::Core::TimeStep dt) override;

        virtual void Render() override;

        void SetSceneTexture(uint32_t scene_texture);

        virtual bool OnUIComponentRaised(ZEngine::Components::UI::Event::UIComponentEvent&) override
        {
            return false;
        }

    public:
        void SceneTextureAvailableMessageHandler(Messengers::GenericMessage<uint32_t>&);

        void SceneViewportResizedMessageHandler(Messengers::GenericMessage<std::pair<float, float>>&);
        void SceneViewportClickedMessageHandler(Messengers::GenericMessage<std::pair<int, int>>&);
        void SceneViewportFocusedMessageHandler(Messengers::GenericMessage<bool>&);
        void SceneViewportUnfocusedMessageHandler(Messengers::GenericMessage<bool>&);
        void SceneViewportRequestRecomputationMessageHandler(Messengers::EmptyMessage&);

    private:
        bool                  m_is_window_focused{false};
        bool                  m_is_window_hovered{false};
        bool                  m_is_window_clicked{false};
        ImVec2                m_viewport_size{0.f, 0.f};
        ImVec2                m_content_region_available_size{0.f, 0.f};
        std::array<ImVec2, 2> m_viewport_bounds;
        uint32_t              m_scene_texture_identifier{0};
    };
} // namespace Tetragrama::Components
