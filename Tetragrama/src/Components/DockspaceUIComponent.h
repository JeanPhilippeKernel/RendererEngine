#pragma once
#include <ZEngine/ZEngine.h>
#include <Message.h>

namespace Tetragrama::Components {
    class DockspaceUIComponent : public ZEngine::Components::UI::UIComponent {
    public:
        DockspaceUIComponent(std::string_view name = "Dockspace", bool visibility = true);
        virtual ~DockspaceUIComponent();

        void Update(ZEngine::Core::TimeStep dt) override;

        virtual void Render() override;

    public:
        std::future<void> RequestExitMessageHandlerAsync(Messengers::GenericMessage<ZEngine::Event::WindowClosedEvent>&);

    protected:
        virtual bool OnUIComponentRaised(ZEngine::Components::UI::Event::UIComponentEvent&) override;

    private:
        ImGuiDockNodeFlags m_dockspace_node_flag;
        ImGuiWindowFlags   m_window_flags;
    };
} // namespace Tetragrama::Components
