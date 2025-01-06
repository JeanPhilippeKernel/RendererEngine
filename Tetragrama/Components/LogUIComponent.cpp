#include <pch.h>
#include <Helpers/MemoryOperations.h>
#include <LogUIComponent.h>
#include <imgui.h>
#include <algorithm>

using namespace ZEngine::Logging;
using namespace ZEngine::Helpers;

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

    void LogUIComponent::Render(ZEngine::Rendering::Renderers::GraphicRenderer* const renderer, ZEngine::Rendering::Buffers::CommandBuffer* const command_buffer)
    {
        ImGui::Begin(Name.c_str(), (CanBeClosed ? &CanBeClosed : NULL), ImGuiWindowFlags_NoCollapse);

        static const char* items[]      = {"All", "info", "error", "warn", "critical", "trace"};
        static int         current_item = 0;

        ImGui::SameLine();
        ImGui::SetNextItemWidth(70);
        if (ImGui::BeginCombo("##Dropdown", items[current_item]))
        {
            for (int n = 0; n < IM_ARRAYSIZE(items); n++)
            {
                if (ImGui::Selectable(items[n], current_item == n))
                {
                    current_item = n;
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }

        ImGui::SameLine();
        if (ImGui::Button("Clear"))
        {
            ClearLog();
            m_search_buffer[0] = '\0';
        }

        ImGui::SameLine();
        if (m_is_copy_button_pressed = ImGui::Button("Copy"))
        {
            ImGui::LogToClipboard();
        }

        ImGui::SameLine();
        ImGui::InputTextWithHint("##Search", "Search logs...", m_search_buffer, IM_ARRAYSIZE(m_search_buffer));
        ImGui::Separator();

        std::string search_term;
        if (secure_strlen(m_search_buffer) > 0)
        {
            search_term = m_search_buffer;
            std::transform(search_term.begin(), search_term.end(), search_term.begin(), ::tolower);
        }

        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
        if (ImGui::BeginTable("log_table", 1, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollX | ImGuiTableFlags_ScrollY))
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            for (const auto& message : m_log_queue)
            {
                if (current_item != 0)
                {
                    if (GetMessageType(message) != items[current_item])
                        continue;
                }
                if (secure_strlen(m_search_buffer) > 0)
                {
                    std::string message_lower = message.Message;
                    std::transform(message_lower.begin(), message_lower.end(), message_lower.begin(), ::tolower);
                    if (message_lower.find(search_term) == std::string::npos)
                        continue;
                }
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
