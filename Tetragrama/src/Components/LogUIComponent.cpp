#include <pch.h>
#include <LogUIComponent.h>
#include <Helpers/UIComponentDrawerHelper.h>

namespace Tetragrama::Components {
    LogUIComponent::LogUIComponent(std::string_view name, bool visibility) : UIComponent(name, visibility, false) {}

    LogUIComponent::~LogUIComponent() {}

    void LogUIComponent::Update(ZEngine::Core::TimeStep dt) {}

    bool LogUIComponent::OnUIComponentRaised(ZEngine::Components::UI::Event::UIComponentEvent&) {
        return false;
    }

    void LogUIComponent::Render() {
        ImGui::Begin(m_name.c_str(), (m_can_be_closed ? &m_can_be_closed : NULL), ImGuiWindowFlags_NoCollapse);

        ImGui::SameLine();
        m_is_clear_button_pressed = ImGui::Button("Clear");

        ImGui::SameLine();
        m_is_copy_button_pressed = ImGui::Button("Copy");

        ImGui::Separator();
        ImGui::BeginChild("scrolling", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));

        if (m_content.size() > 0) {
            const char* buf     = m_content.begin();
            const char* buf_end = m_content.end();

            ImGuiListClipper clipper;
            clipper.Begin(m_content_line_offsets.Size);
            while (clipper.Step()) {
                for (int line_no = clipper.DisplayStart; line_no < clipper.DisplayEnd; line_no++) {
                    const char* line_start = buf + m_content_line_offsets[line_no];
                    const char* line_end   = (line_no + 1 < m_content_line_offsets.Size) ? (buf + m_content_line_offsets[line_no + 1] - 1) : buf_end;

                    if (strstr(line_start, "[error]")) {
                        Helpers::DrawColoredTextLine(line_start, line_end, ImVec4(1.0f, 0.4f, 0.4f, 1.0f));                       
                    }
                    if (strstr(line_start, "[warning]")) {
                        Helpers::DrawColoredTextLine(line_start, line_end, ImVec4(1.0f, 0.87f, 0.37f, 1.f));                       
                    }
                    if (strstr(line_start, "[debug]")) {
                        Helpers::DrawColoredTextLine(line_start, line_end, ImVec4(0.94f, 0.39f, 0.13f, 1.0f));
                    }                
                    if (strstr(line_start, "[info]")) {
                        Helpers::DrawColoredTextLine(line_start, line_end, ImVec4(0.46f, 0.96f, 0.46f, 1.f));
                    }
                    if (strstr(line_start, "[trace]")) {
                        Helpers::DrawColoredTextLine(line_start, line_end, ImVec4(1.0f, 1.0f, 1.0f, 1.f));
                    }
                    if (strstr(line_start, "[critical]")) {
                        Helpers::DrawColoredTextLine(line_start, line_end, ImVec4(0.47f, 0.47f, 0.69f, 0.40f));
                    }
                    if (strstr(line_start, "[log]")) {
                        Helpers::DrawColoredTextLine(line_start, line_end, ImVec4(1.f, 1.f, 1.f, 0.5f));                      
                    }                                                   
                }
            }
            clipper.End();         
        }
      
        ImGui::PopStyleVar();

        if (m_auto_scroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) {
            ImGui::SetScrollHereY(1.0f);
        }

        ImGui::EndChild();

        ImGui::End();
    }

    void LogUIComponent::ReceiveLogMessageMessageHandler(Messengers::GenericMessage<std::vector<std::string>>& message) {
        const auto& message_content = message.GetValue();
        if (!message_content.empty()) {
            for (const auto& content : message_content) {
                int old_content_size = m_content.size();
                m_content.append(content.c_str(), (content.c_str() + content.size()));

                for (int new_size = m_content.size(); old_content_size < new_size; old_content_size++) {
                    if (m_content[old_content_size] == '\n')
                        m_content_line_offsets.push_back(old_content_size + 1);
                }
                    
            }
        }
    }
} // namespace Tetragrama::Components
