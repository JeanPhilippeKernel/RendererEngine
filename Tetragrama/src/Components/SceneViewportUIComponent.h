#pragma once
#include <ZEngine/ZEngine.h>
#include <Components/Events/SceneViewportResizedEvent.h>
#include <Components/Events/SceneViewportFocusedEvent.h>
#include <Components/Events/SceneViewportUnfocusedEvent.h>

namespace Tetragrama::Components {
    class SceneViewportUIComponent : public ZEngine::Components::UI::UIComponent {
    public:
        SceneViewportUIComponent(std::string_view name = "Scene viewport UI Component", bool visibility = true);
        virtual ~SceneViewportUIComponent();

        void Update(ZEngine::Core::TimeStep dt) override;

        virtual void Render() override;
        
        void SetSceneTexture(const uint32_t scene_texture) { m_scene_texture_identifier = scene_texture;  }

    protected:
        virtual bool OnUIComponentRaised(ZEngine::Components::UI::Event::UIComponentEvent&) override;
    
    private:
        bool OnSceneViewportResized(Event::SceneViewportResizedEvent&);
        bool OnSceneViewportFocused(Event::SceneViewportFocusedEvent&);
        bool OnSceneViewportUnfocused(Event::SceneViewportUnfocusedEvent&);

    private:
        bool m_is_window_focused{ false };
        bool m_is_window_hovered{ false };
        ImVec2 m_viewport_size{ 0.f, 0.f };
        ImVec2 m_content_region_available_size{ 0.f, 0.f };
        uint32_t m_scene_texture_identifier{ 0 };
    };
}