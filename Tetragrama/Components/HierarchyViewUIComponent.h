#pragma once
#include <Tetragrama/Components/UIComponent.h>
#include <Tetragrama/Editor.h>
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

    private:
        void RenderGuizmo(EditorPtr app, EditorScenePtr scene);

        int  m_gizmo_operation{-1};
        char m_filter_buf[128] = {};
    };
} // namespace Tetragrama::Components
