#include <pch.h>
#include <LogUIComponent.h>
#include <Helpers/UIComponentDrawerHelper.h>

namespace Tetragrama::Components
{
    LogUIComponent::LogUIComponent(std::string_view name, bool visibility) : UIComponent(name, visibility, false) {}

    LogUIComponent::~LogUIComponent() {}

    void LogUIComponent::Update(ZEngine::Core::TimeStep dt)
    {
        ZEngine::Logging::Logger::GetLogMessage(m_logger_message);
    }

    bool LogUIComponent::OnUIComponentRaised(ZEngine::Components::UI::Event::UIComponentEvent&)
    {
        return false;
    }

    void LogUIComponent::Render()
    {
        ImGui::Begin(m_name.c_str(), (m_can_be_closed ? &m_can_be_closed : NULL), ImGuiWindowFlags_NoCollapse);

        ImGui::SameLine();
        m_is_clear_button_pressed = ImGui::Button("Clear");

        ImGui::SameLine();
        m_is_copy_button_pressed = ImGui::Button("Copy");

        ImGui::Separator();
        ImGui::BeginChild("scrolling", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));

        if (!m_logger_message.Message.empty())
        {
            const char* line_start = m_logger_message.Message.data();
            const char* line_end   = (m_logger_message.Message.data() + m_logger_message.Message.size());
            Helpers::DrawColoredTextLine(line_start, line_end, ImVec4(m_logger_message.Color[0], m_logger_message.Color[1], m_logger_message.Color[2], m_logger_message.Color[3]));
        }

        ImGui::PopStyleVar();

        if (m_auto_scroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
        {
            ImGui::SetScrollHereY(1.0f);
        }

        ImGui::EndChild();

        ImGui::End();
    }
} // namespace Tetragrama::Components
