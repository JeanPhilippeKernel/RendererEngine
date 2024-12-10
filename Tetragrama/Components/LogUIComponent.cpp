#include <pch.h>
#include <LogUIComponent.h>
#include <imgui.h>
#include <algorithm>

using namespace ZEngine::Logging;

namespace Tetragrama::Components
{
    LogUIComponent::LogUIComponent(std::string_view name, bool visibility) : UIComponent(name, visibility, false)
    {
        Logger::AddEventHandler(std::bind(&LogUIComponent::OnLog, this, std::placeholders::_1));
    }

    LogUIComponent::~LogUIComponent() {}

    void LogUIComponent::Update(ZEngine::Core::TimeStep dt) {}

    void LogUIComponent::ClearLog()
    {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_log_queue.clear();
            m_log_queue.shrink_to_fit();
        }
    }

    void LogUIComponent::Render()
    {
        ImGui::Begin(m_name.c_str(), (m_can_be_closed ? &m_can_be_closed : NULL), ImGuiWindowFlags_NoCollapse);

        const char* items[]      = {"All", "info", "trace", "warn", "critical", "error"};
        static int  current_item = 0;

        ImGui::SameLine();
        ImGui::SetNextItemWidth(70);
        if (ImGui::BeginCombo("##Dropdown", items[current_item]))
        {
            for (int n = 0; n < IM_ARRAYSIZE(items); n++)
            {
                const bool is_selected = (current_item == n);
                if (ImGui::Selectable(items[n], is_selected))
                    current_item = n;
                if (is_selected)
                    ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }

        ImGui::SameLine();
        m_is_clear_button_pressed = ImGui::Button("Clear");

        ImGui::SameLine();
        m_is_copy_button_pressed = ImGui::Button("Copy");
        ImGui::SameLine();

        ImGui::InputTextWithHint("##Search", "Search logs...", m_search_buffer, IM_ARRAYSIZE(m_search_buffer));

        ImGui::Separator();

        if (m_is_copy_button_pressed)
        {
            ImGui::LogToClipboard();
        }
        if (m_is_clear_button_pressed)
        {
            ClearLog();
            m_search_buffer[0] = '\0';
        }

        std::vector<ZEngine::Logging::LogMessage> filtered_logs;
        {
            std::lock_guard<std::mutex> lock(m_mutex);

            for (const auto& message : m_log_queue)
            {
                bool matches_search = true;
                bool matches_type   = true;

                if (std::strlen(m_search_buffer) > 0)
                {
                    std::string message_lower = message.Message;
                    std::string search_term   = m_search_buffer;
                    std::transform(message_lower.begin(), message_lower.end(), message_lower.begin(), ::tolower);
                    std::transform(search_term.begin(), search_term.end(), search_term.begin(), ::tolower);
                    matches_search = (message_lower.find(search_term) != std::string::npos);
                }

                if (current_item != 0) // 0 is "All"
                {

                    std::string msg_type = GetMessageType(message);
                    matches_type         = (msg_type == items[current_item]);
                }

                if (matches_search && matches_type)
                {
                    filtered_logs.push_back(message);
                }
            }
        }

        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
        if (ImGui::BeginTable("log_table", 1, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollX | ImGuiTableFlags_ScrollY))
        {
            for (const auto& message : filtered_logs)
            {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextColored({message.Color[0], message.Color[1], message.Color[2], message.Color[3]}, message.Message.data());
            }
            ImGui::EndTable();
        }

        if (m_is_copy_button_pressed)
        {
            ImGui::LogFinish();
        }

        // if (m_auto_scroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
        // {
        //     ImGui::SetScrollHereY(1.0f);
        // }

        ImGui::PopStyleVar();

        ImGui::End();
    }

    void LogUIComponent::OnLog(ZEngine::Logging::LogMessage message)
    {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (m_currentCount > m_maxCount)
            {
                m_currentCount = 0;
                m_log_queue.clear();
                m_log_queue.shrink_to_fit();
            }

            m_log_queue.push_back(std::move(message));
            m_currentCount++;
        }
    }

    std::string LogUIComponent::GetMessageType(const ZEngine::Logging::LogMessage& message)
    {
        if (message.Color[0] == 0.0f && message.Color[1] == 1.0f && message.Color[2] == 0.0f && message.Color[3] == 1.0f)
            return "info";
        if (message.Color[0] == 1.0f && message.Color[1] == 0.5f && message.Color[2] == 0.0f && message.Color[3] == 1.0f)
            return "warn";
        if (message.Color[0] == 1.0f && message.Color[1] == 0.0f && message.Color[2] == 0.0f && message.Color[3] == 1.0f)
            return "error";
        if (message.Color[0] == 1.0f && message.Color[1] == 0.0f && message.Color[2] == 1.0f && message.Color[3] == 0.0f)
            return "critical";
        return "trace";
    }
} // namespace Tetragrama::Components
