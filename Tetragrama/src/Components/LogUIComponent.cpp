#include <pch.h>
#include <LogUIComponent.h>

using namespace ZEngine::Logging;

namespace Tetragrama::Components
{
    LogUIComponent::LogUIComponent(std::string_view name, bool visibility) : UIComponent(name, visibility, false)
    {
        Logger::AddEventHandler(std::bind(&LogUIComponent::OnLog, this, std::placeholders::_1));
    }

    LogUIComponent::~LogUIComponent()
    {
    }

    void LogUIComponent::Update(ZEngine::Core::TimeStep dt)
    {
    }

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

        ImGui::SameLine();
        m_is_clear_button_pressed = ImGui::Button("Clear");

        ImGui::SameLine();
        m_is_copy_button_pressed = ImGui::Button("Copy");

        ImGui::Separator();

        if (m_is_copy_button_pressed)
        {
            ImGui::LogToClipboard();
        }

        if (m_is_clear_button_pressed)
        {
            ClearLog();
        }

        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));

        if (ImGui::BeginTable("log_table", 1, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollX | ImGuiTableFlags_ScrollY))
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            {
                for (const ZEngine::Logging::LogMessage& message : m_log_queue)
                {
                    ImGui::TableNextRow();
                    ImGui::PushStyleColor(ImGuiCol_Text, {message.Color[0], message.Color[1], message.Color[2], message.Color[3]});
                    ImGui::TableSetColumnIndex(0);
                    ImGui::Text(message.Message.data());
                    ImGui::PopStyleColor();
                }
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
} // namespace Tetragrama::Components
