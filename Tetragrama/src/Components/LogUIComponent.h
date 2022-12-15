#pragma once
#include <string>
#include <mutex>
#include <regex>
#include <condition_variable>
#include <ZEngine/ZEngine.h>
#include <Message.h>
#include <ZEngine/Logging/Logger.h>

namespace Tetragrama::Components
{
    class LogUIComponent : public ZEngine::Components::UI::UIComponent
    {
    public:
        LogUIComponent(std::string_view name = "Log", bool visibility = true);
        virtual ~LogUIComponent();

        void Update(ZEngine::Core::TimeStep dt) override;

        virtual void Render() override;

    protected:
        virtual bool OnUIComponentRaised(ZEngine::Components::UI::Event::UIComponentEvent&) override;

        void ClearLog();

    private:
        bool                            m_auto_scroll{true};
        bool                            m_is_copy_button_pressed{false};
        bool                            m_is_clear_button_pressed{false};
        ImGuiTextBuffer                 m_content_buffer;
        ImVector<int>                   m_line_offset;
        ZEngine::Logging::LoggerMessage m_logger_message;
    };
} // namespace Tetragrama::Components
