#pragma once
#include <EditorCameraController.h>
#include <Message.h>
#include <UIComponent.h>
#include <future>
#include <mutex>
#include <string>
#include <imgui.h>

namespace Tetragrama::Components
{
    class HierarchyViewUIComponent : public UIComponent
    {
    public:
        HierarchyViewUIComponent(std::string_view name = "Hierarchy", bool visibility = true);
        virtual ~HierarchyViewUIComponent();

        void         Update(ZEngine::Core::TimeStep dt) override;
        virtual void Render() override;

        void RenderTreeNodes();
        void RenderGuizmo();
        void RenderSceneNodeTree(int node_identifier);

        std::future<void> EditorCameraAvailableMessageHandlerAsync(Messengers::GenericMessage<ZEngine::Ref<Controllers::EditorCameraController>>&);

    private:
        ImGuiTreeNodeFlags                                    m_node_flag;
        bool                                                  m_is_node_opened{false};
        int                                                   m_selected_node_identifier{-1};
        int                                                   m_gizmo_operation{-1};
        std::mutex                                            m_mutex;
        ZEngine::WeakRef<Controllers::EditorCameraController> m_active_editor_camera;
    };
} // namespace Tetragrama::Components
