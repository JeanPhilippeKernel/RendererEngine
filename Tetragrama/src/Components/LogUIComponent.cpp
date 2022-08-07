#include <pch.h>
#include <LogUIComponent.h>

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
            ImGui::TextUnformatted(buf, buf_end);
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
                m_content.append(content.c_str(), (content.c_str() + content.size()));
            }
        }
    }
} // namespace Tetragrama::Components
