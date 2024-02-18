#pragma once
#include <string>
#include <ZEngine/ZEngine.h>
#include <Message.h>
#include <EditorCameraController.h>
#include <mutex>

namespace Tetragrama::Components {
    class HierarchyViewUIComponent : public ZEngine::Components::UI::UIComponent
    {
    public:
        HierarchyViewUIComponent(std::string_view name = "Hierarchy", bool visibility = true);
        virtual ~HierarchyViewUIComponent();

        void         Update(ZEngine::Core::TimeStep dt) override;
        virtual void Render() override;

    public:
        std::future<void> EditorCameraAvailableMessageHandlerAsync(Messengers::GenericMessage<ZEngine::Ref<EditorCameraController>>&);

    protected:
        void         RenderSceneNodeTree(int32_t node_identifier);
        void         RenderSceneNodeTrees(const std::vector<int32_t>& node_identifie_collection);

    private:
        ImGuiTreeNodeFlags                       m_node_flag;
        bool                                     m_is_node_opened{false};
        int                                      m_selected_node_identifier{-1};
        int                                      m_gizmo_operation{-1};
        std::mutex                               m_mutex;
        ZEngine::WeakRef<EditorCameraController> m_active_editor_camera;
    };
} // namespace Tetragrama::Components
