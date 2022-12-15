#include <pch.h>
#include <LogUIComponent.h>
#include <Helpers/UIComponentDrawerHelper.h>

namespace Tetragrama::Components
{
    LogUIComponent::LogUIComponent(std::string_view name, bool visibility) : UIComponent(name, visibility, false) {}

    LogUIComponent::~LogUIComponent() {}

    void LogUIComponent::Update(ZEngine::Core::TimeStep dt)
    {
        if (ZEngine::Logging::Logger::GetLogMessage(m_logger_message))
        {
            int old_size = m_content_buffer.size();
            m_content_buffer.append(m_logger_message.Message.data(), (m_logger_message.Message.data() + m_logger_message.Message.size()));
            for (int new_size = m_content_buffer.size(); old_size < new_size; old_size++)
            {
                if (m_content_buffer[old_size] == '\n')
                {
                    m_line_offset.push_back(old_size + 1);
                }
            }
        }
    }

    bool LogUIComponent::OnUIComponentRaised(ZEngine::Components::UI::Event::UIComponentEvent&)
    {
        return false;
    }

    void LogUIComponent::ClearLog()
    {
        m_content_buffer.clear();
        m_line_offset.clear();
        m_line_offset.push_back(0);

        m_logger_message.Message.clear();
    }

    void LogUIComponent::Render()
    {
        ImGui::Begin(m_name.c_str(), (m_can_be_closed ? &m_can_be_closed : NULL), ImGuiWindowFlags_NoCollapse);

        ImGui::SameLine();
        m_is_clear_button_pressed = ImGui::Button("Clear");

        ImGui::SameLine();
        m_is_copy_button_pressed = ImGui::Button("Copy");

        ImGui::Separator();
        if (ImGui::BeginChild("scrolling", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar))
        {
            if (m_is_copy_button_pressed)
            {
                ImGui::LogToClipboard();
            }

            if (m_is_clear_button_pressed)
            {
                ClearLog();
            }

            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));

            const char* buffer_start = m_content_buffer.begin();
            const char* buffer_end   = m_content_buffer.end();

            ImGuiListClipper clipper;
            clipper.Begin(m_line_offset.Size);
            while (clipper.Step())
            {
                for (int line_no = clipper.DisplayStart; line_no < clipper.DisplayEnd; line_no++)
                {
                    const char* line_start = buffer_start + m_line_offset[line_no];
                    const char* line_end   = (line_no + 1 < m_line_offset.Size) ? (buffer_start + m_line_offset[line_no + 1] - 1) : buffer_end;
                    Helpers::DrawColoredTextLine(
                        line_start, line_end, ImVec4(m_logger_message.Color[0], m_logger_message.Color[1], m_logger_message.Color[2], m_logger_message.Color[3]));
                }
            }
            clipper.End();


            if (m_is_copy_button_pressed)
            {
                ImGui::LogFinish();
            }

            if (m_auto_scroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
            {
                ImGui::SetScrollHereY(1.0f);
            }

            ImGui::PopStyleVar();
        }
        ImGui::EndChild();

        ImGui::End();
    }
} // namespace Tetragrama::Components
