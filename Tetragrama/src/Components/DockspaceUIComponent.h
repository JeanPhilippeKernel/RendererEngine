#pragma once
#include <ZEngine/ZEngine.h>

namespace Tetragrama::Components {
    class DockspaceUIComponent : public ZEngine::Components::UI::UIComponent {
    public:
        DockspaceUIComponent(std::string_view name = "Dockspace UI Component", bool visibility = true);
        virtual ~DockspaceUIComponent();

        void Update(ZEngine::Core::TimeStep dt) override;

        virtual void Render() override;

    protected:
        virtual bool OnUIComponentRaised(ZEngine::Components::UI::Event::UIComponentEvent&) override;

    private:
        ImGuiDockNodeFlags m_dockspace_node_flag;
        ImGuiWindowFlags   m_window_flags;

        bool OnRequestCreateScene(ZEngine::Components::UI::Event::UIComponentEvent&);
        bool OnRequestOpenScene(ZEngine::Components::UI::Event::UIComponentEvent&);
        bool OnRequestSave(ZEngine::Components::UI::Event::UIComponentEvent&);
        bool OnRequestSaveAs(ZEngine::Components::UI::Event::UIComponentEvent&);

        bool OnRequestExit(ZEngine::Event::WindowClosedEvent&);
    };
} // namespace Tetragrama::Components
