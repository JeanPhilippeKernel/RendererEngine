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

        void              Initialize(Layers::ImguiLayer* parent = nullptr, const char* name = "Inspector", bool visibility = true, bool closed = false) override;

        void              Update(ZEngine::Core::TimeStep dt) override;

        virtual void      Render(ZEngine::Rendering::Renderers::GraphicRenderer* const renderer, ZEngine::Hardwares::CommandBuffer* const command_buffer) override;
        std::future<void> SceneEntitySelectedMessageHandlerAsync(Messengers::GenericMessage<ZEngine::Rendering::Scenes::SceneEntity>&);
        std::future<void> SceneEntityUnSelectedMessageHandlerAsync(Messengers::EmptyMessage&);
        std::future<void> SceneEntityDeletedMessageHandlerAsync(Messengers::EmptyMessage&);

    private:
        ImGuiTreeNodeFlags                      m_node_flag;
        bool                                    m_recieved_unselected_request{false};
        bool                                    m_recieved_deleted_request{false};
        ZEngine::Rendering::Scenes::SceneEntity m_scene_entity;
        std::mutex                              m_mutex;
    };
} // namespace Tetragrama::Components
