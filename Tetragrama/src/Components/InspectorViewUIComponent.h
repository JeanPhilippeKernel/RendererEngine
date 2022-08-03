#pragma once
#include <string>
#include <ZEngine/ZEngine.h>
#include <Message.h>

namespace Tetragrama::Components {
    class InspectorViewUIComponent : public ZEngine::Components::UI::UIComponent {
    public:
        InspectorViewUIComponent(std::string_view name = "InspectorViewUIComponent", bool visibility = true);
        virtual ~InspectorViewUIComponent();

        void Update(ZEngine::Core::TimeStep dt) override;

        virtual void Render() override;

    public:
        void SceneEntitySelectedMessageHandler(Messengers::PointerValueMessage<ZEngine::Rendering::Entities::GraphicSceneEntity>&);
        void SceneEntityUnSelectedMessageHandler(Messengers::EmptyMessage&);

    protected:
        virtual bool OnUIComponentRaised(ZEngine::Components::UI::Event::UIComponentEvent&) override;

    private:
        ZEngine::Rendering::Entities::GraphicSceneEntity* m_scene_entity{nullptr};
    };
} // namespace Tetragrama::Components
