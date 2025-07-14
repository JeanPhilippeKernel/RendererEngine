#pragma once
#include <UIComponent.h>
#include <imgui.h>

namespace Tetragrama::Components
{
    class HierarchyViewUIComponent : public UIComponent
    {
    public:
        HierarchyViewUIComponent();
        virtual ~HierarchyViewUIComponent();

        void         Initialize(Layers::ImguiLayer* parent = nullptr, const char* name = "Hierarchy", bool visibility = true, bool closed = false) override;

        void         Update(ZEngine::Core::TimeStep dt) override;
        virtual void Render(ZEngine::Rendering::Renderers::GraphicRenderer* const renderer, ZEngine::Hardwares::CommandBuffer* const command_buffer) override;

        void         RenderTreeNodes();
        void         RenderGuizmo();
        void         RenderNode(EditorScene* scene, int root_id, std::atomic_int& selected);

    private:
        ImGuiTreeNodeFlags m_node_flag;
        bool               m_is_node_opened{false};
        int                m_gizmo_operation{-1};
    };
} // namespace Tetragrama::Components
