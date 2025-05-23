#include <pch.h>
#include <Helpers/MemoryOperations.h>
#include <LogUIComponent.h>
#include <SearchPatternAlgorithm.h>
#include <ZEngine/Core/Containers/Array.h>
#include <imgui.h>

using namespace ZEngine::Logging;
using namespace ZEngine::Helpers;
using namespace ZEngine::Core::Containers;

namespace Tetragrama::Components
{
    LogUIComponent::LogUIComponent() {}

    LogUIComponent::~LogUIComponent()
    {
        Logger::RemoveEventHandler(m_handler_cookie);
    }

    void LogUIComponent::Initialize(Layers::ImguiLayer* parent, const char* name, bool visibility, bool closed)
    {
        UIComponent::Initialize(parent, name, visibility, closed);

        parent->LocalArena.CreateSubArena(ZMega(1), &m_local_arena);
        m_log_queue.init(&(m_local_arena), m_maxCount, m_maxCount);
        m_handler_cookie = Logger::AddEventHandler(std::bind(&LogUIComponent::OnLog, this, std::placeholders::_1));
    }

    void LogUIComponent::Update(ZEngine::Core::TimeStep dt) {}

    void LogUIComponent::ClearLog()
    {
        m_currentCount.store(0, std::memory_order_release);
    }

    void LogUIComponent::Render(ZEngine::Rendering::Renderers::GraphicRenderer* const renderer, ZEngine::Hardwares::CommandBuffer* const command_buffer)
    {
        static const char* items[]                    = {"All", "info", "error", "warn", "critical", "trace"};
        static int         current_item               = 0;

        auto               scratch                    = ZGetScratch(&m_local_arena);

        char               search_buffer_tolower[256] = {0};
        if (auto len = secure_strlen(m_search_buffer))
        {
            for (unsigned i = 0; i < len; ++i)
            {
                search_buffer_tolower[i] = ::tolower(m_search_buffer[i]);
            }
        }

        ImGui::Begin(Name, (CanBeClosed ? &CanBeClosed : NULL), ImGuiWindowFlags_NoCollapse);

        ImGui::SetNextItemWidth(150);
        ImGui::InputTextWithHint("##Search", "Search logs...", m_search_buffer, IM_ARRAYSIZE(m_search_buffer));

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
        if (m_is_copy_button_pressed = ImGui::Button("Copy"))
        {
            ImGui::LogToClipboard();
        }

        ImGui::SameLine();
        if (ImGui::Button("Clear"))
        {
            ClearLog();
            m_search_buffer[0] = '\0';
        }

        ImGui::Separator();

        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
        if (ImGui::BeginTable("#log_table", 1, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollX | ImGuiTableFlags_ScrollY))
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

                    if (!Helpers::KMPSearch(scratch.Arena, message.Message.c_str(), search_buffer_tolower))
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

        ZReleaseScratch(scratch);
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
