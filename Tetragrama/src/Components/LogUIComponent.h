#pragma once
#include <string>
#include <ZEngine/ZEngine.h>
#include <Message.h>

namespace Tetragrama::Components {
    class LogUIComponent : public ZEngine::Components::UI::UIComponent {
    public:
        LogUIComponent(std::string_view name = "Log", bool visibility = true);
        virtual ~LogUIComponent();

        void Update(ZEngine::Core::TimeStep dt) override;

        virtual void Render() override;

    public:
        void ReceiveLogMessageMessageHandler(Messengers::GenericMessage<std::vector<std::string>>&);

    protected:
        virtual bool OnUIComponentRaised(ZEngine::Components::UI::Event::UIComponentEvent&) override;


    private:
        bool            m_auto_scroll{true};
        bool            m_is_copy_button_pressed{false};
        bool            m_is_clear_button_pressed{false};
        ImGuiTextBuffer m_content;
    };
} // namespace Tetragrama::Components
