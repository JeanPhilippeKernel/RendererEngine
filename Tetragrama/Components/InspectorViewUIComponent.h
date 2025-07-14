#pragma once
#include <Message.h>
#include <UIComponent.h>
#include <ZEngine/ZEngineDef.h>
#include <imgui.h>
#include <future>
#include <mutex>
#include <string>

namespace Tetragrama::Components
{
    class InspectorViewUIComponent : public UIComponent
    {
    public:
        InspectorViewUIComponent();
        virtual ~InspectorViewUIComponent();

        void         Initialize(Layers::ImguiLayer* parent = nullptr, const char* name = "Inspector", bool visibility = true, bool closed = false) override;

        void         Update(ZEngine::Core::TimeStep dt) override;

        virtual void Render(ZEngine::Rendering::Renderers::GraphicRenderer* const renderer, ZEngine::Hardwares::CommandBuffer* const command_buffer) override;

    private:
        ImGuiTreeNodeFlags m_node_flag;
    };
} // namespace Tetragrama::Components
