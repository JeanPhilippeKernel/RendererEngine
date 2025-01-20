#pragma once
#include <EditorCameraController.h>
#include <Message.h>
#include <UIComponent.h>
#include <imgui.h>
#include <future>
#include <mutex>
#include <string>

namespace Tetragrama::Components
{
    class HierarchyViewUIComponent : public UIComponent
    {
    public:
        HierarchyViewUIComponent(Layers::ImguiLayer* parent = nullptr, std::string_view name = "Hierarchy", bool visibility = true);
        virtual ~HierarchyViewUIComponent();

        void         Update(ZEngine::Core::TimeStep dt) override;
        virtual void Render(ZEngine::Rendering::Renderers::GraphicRenderer* const renderer, ZEngine::Hardwares::CommandBuffer* const command_buffer) override;

        void         RenderTreeNodes();
        void         RenderGuizmo();
        void         RenderSceneNodeTree(int node_identifier);

    private:
        ImGuiTreeNodeFlags m_node_flag;
        bool               m_is_node_opened{false};
        int                m_selected_node_identifier{-1};
        int                m_gizmo_operation{-1};
    };
} // namespace Tetragrama::Components
