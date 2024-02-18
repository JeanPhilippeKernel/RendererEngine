#pragma once
#include <ZEngine/ZEngine.h>
#include <Message.h>

namespace Tetragrama::Components
{
    class DockspaceUIComponent : public ZEngine::Components::UI::UIComponent
    {
    public:
        DockspaceUIComponent(std::string_view name = "Dockspace", bool visibility = true);
        virtual ~DockspaceUIComponent();

        void         Update(ZEngine::Core::TimeStep dt) override;
        virtual void Render() override;

        void RenderMenuBar();

        std::future<void> OnNewSceneAsync();
        std::future<void> OnOpenSceneAsync();
        std::future<void> OnImportAssetAsync();
        std::future<void> OnSaveAsync();
        std::future<void> OnSaveAsAsync();
        std::future<void> OnExitAsync();
        std::future<void> RequestExitMessageHandlerAsync(Messengers::GenericMessage<ZEngine::Event::WindowClosedEvent>&);

    private:
        ImGuiDockNodeFlags m_dockspace_node_flag;
        ImGuiWindowFlags   m_window_flags;
    };
} // namespace Tetragrama::Components
