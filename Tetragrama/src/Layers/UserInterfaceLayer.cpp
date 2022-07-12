#include <pch.h>
#include <UserInterfaceLayer.h>

namespace Tetragrama::Layers {

    void UserInterfaceLayer::Initialize() {
        ImguiLayer::Initialize();

        m_dockspace_component = std::make_shared<Components::DockspaceUIComponent>();
        m_scene_component     = std::make_shared<Components::SceneViewportUIComponent>();
        m_log_component       = std::make_shared<Components::LogUIComponent>();
        m_demo_component      = std::make_shared<Components::DemoUIComponent>();

        m_dockspace_component->AddChild(m_demo_component);
        m_dockspace_component->AddChild(m_scene_component);
        m_dockspace_component->AddChild(m_log_component);

        this->AddUIComponent(m_dockspace_component);
    }

    bool UserInterfaceLayer::OnSceneTextureAvailableRaised(Components::Event::SceneTextureAvailableEvent& e) {
        auto scene_ui_component = reinterpret_cast<Components::SceneViewportUIComponent*>(m_scene_component.get());
        scene_ui_component->SetSceneTexture(e.GetSceneTexture());
        return true;
    }
} // namespace Tetragrama::Layers
