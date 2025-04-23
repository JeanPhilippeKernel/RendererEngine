#include <pch.h>
#include <Helpers/MemoryOperations.h>
#include <LogUIComponent.h>
#include <imgui.h>

using namespace ZEngine::Logging;
using namespace ZEngine::Helpers;

namespace Tetragrama::Components
{
    // KMP Preprocessing: builds the partial match table (prefix function)
    void buildKMPTable(const char* pattern, int* lps)
    {
        int m  = std::strlen(pattern);
        int j  = 0; // length of previous longest prefix suffix

        lps[0] = 0; // lps[0] is always 0
        for (int i = 1; i < m; ++i)
        {
            while (j > 0 && std::tolower(pattern[i]) != std::tolower(pattern[j]))
            {
                j = lps[j - 1];
            }
            if (std::tolower(pattern[i]) == std::tolower(pattern[j]))
            {
                ++j;
            }
            lps[i] = j;
        }
    }

    bool KMPSearch(const char* text, const char* pattern)
    {
        int n = std::strlen(text);
        int m = std::strlen(pattern);

        // Edge case: empty pattern
        if (m == 0)
            return true;

        int* lps = new int[m]; // longest prefix suffix table
        buildKMPTable(pattern, lps);

        int i = 0; // index for text[]
        int j = 0; // index for pattern[]

        while (i < n)
        {
            if (std::tolower(text[i]) == std::tolower(pattern[j]))
            {
                ++i;
                ++j;
            }

            if (j == m)
            { // found a match
                delete[] lps;
                return true;
            }
            else if (i < n && std::tolower(text[i]) != std::tolower(pattern[j]))
            {
                if (j != 0)
                {
                    j = lps[j - 1];
                }
                else
                {
                    ++i;
                }
            }
        }

        delete[] lps;
        return false; // no match found
    }

    LogUIComponent::LogUIComponent() {}

    LogUIComponent::~LogUIComponent()
    {
        Logger::RemoveEventHandler(m_handler_cookie);
    }

    void LogUIComponent::Initialize(Layers::ImguiLayer* parent, const char* name, bool visibility, bool closed)
    {
        UIComponent::Initialize(parent, name, visibility, closed);
        m_log_queue.init(&(parent->LayerArena), m_maxCount, m_maxCount);
        m_handler_cookie = Logger::AddEventHandler(std::bind(&LogUIComponent::OnLog, this, std::placeholders::_1));
    }

    void LogUIComponent::Update(ZEngine::Core::TimeStep dt) {}

    void LogUIComponent::ClearLog()
    {
        m_currentCount.store(0, std::memory_order_release);
    }

    void LogUIComponent::Render(ZEngine::Rendering::Renderers::GraphicRenderer* const renderer, ZEngine::Hardwares::CommandBuffer* const command_buffer)
    {
        ImGui::Begin(Name, (CanBeClosed ? &CanBeClosed : NULL), ImGuiWindowFlags_NoCollapse);

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

        char search_buffer_tolower[256] = {0};
        if (auto len = secure_strlen(m_search_buffer))
        {
            for (unsigned i = 0; i < len; ++i)
            {
                search_buffer_tolower[i] = ::tolower(m_search_buffer[i]);
            }
        }

        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
        if (ImGui::BeginTable("log_table", 1, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollX | ImGuiTableFlags_ScrollY))
        {
            auto count = m_currentCount.load(std::memory_order_acquire);
            for (unsigned i = 0; i < count; ++i)
            {
                auto& message = m_log_queue[i];

                if (current_item != 0)
                {
                    auto type = GetMessageType(message);

                    if (0 != secure_strcmp(type, items[current_item]))
                    {
                        continue;
                    }
                }

                if (secure_strlen(search_buffer_tolower) > 0)
                {

                    if (!KMPSearch(message.Message.c_str(), search_buffer_tolower))
                    {
                        continue;
                    }
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
            auto count = m_currentCount.load(std::memory_order_acquire);

            if (count > m_maxCount)
            {
                m_currentCount.store(0, std::memory_order_release);
                m_log_queue.clear();
            }

            m_log_queue[m_currentCount] = std::move(message);
            m_currentCount.store(++count, std::memory_order_release);
        }
    }

    const char* LogUIComponent::GetMessageType(const ZEngine::Logging::LogMessage& message)
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
