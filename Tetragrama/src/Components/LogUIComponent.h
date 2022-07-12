#pragma once
#include <ZEngine/ZEngine.h>

namespace Tetragrama::Components {
    class LogUIComponent : public ZEngine::Components::UI::UIComponent {
    public:
        LogUIComponent(std::string_view name = "LogUIComponent", bool visibility = true);
        virtual ~LogUIComponent();

        void Update(ZEngine::Core::TimeStep dt) override;

        virtual void Render() override;

    protected:
        virtual bool OnUIComponentRaised(ZEngine::Components::UI::Event::UIComponentEvent&) override;


    private:
        bool            m_auto_scroll{true};
        bool            m_is_copy_button_pressed{false};
        bool            m_is_clear_button_pressed{false};
        ImGuiTextBuffer m_content;
        ImVector<int>   m_line_offsets;
    };
} // namespace Tetragrama::Components
