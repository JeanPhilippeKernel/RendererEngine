#include <pch.h>
#include <UserInterfaceLayer.h>

namespace Tetragrama::Layers {

	void UserInterfaceLayer::Initialize() {
		ImguiLayer::Initialize();

		m_dockspace_component.reset(new Components::DockspaceUIComponent{});
		m_scene_component.reset(new Components::SceneViewportUIComponent{});
		m_demo_component.reset(new Components::DemoUIComponent{});

		m_dockspace_component->AddChild(m_demo_component);
		m_dockspace_component->AddChild(m_scene_component);

		this->AddUIComponent(m_dockspace_component);
	}

	bool UserInterfaceLayer::OnSceneTextureAvailableRaised(Components::Event::SceneTextureAvailableEvent& e) {
		auto scene_ui_component = dynamic_cast<Components::SceneViewportUIComponent*>(m_scene_component.get());
		scene_ui_component->SetSceneTexture(e.GetSceneTexture());
		return true;
	}
}