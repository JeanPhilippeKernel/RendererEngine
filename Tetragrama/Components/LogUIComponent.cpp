#include <Tetragrama/Components/LogUIComponent.h>
#include <Tetragrama/Helpers/SearchPatternAlgorithm.h>
#include <ZEngine/Core/Containers/Array.h>
#include <ZEngine/Helpers/MemoryOperations.h>
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
        UILogQueue.init(parent->Arena, m_maxCount, m_maxCount);

        parent->Arena->CreateSubArena(ZMega(2), &LocalArena);
        for (unsigned i = 0; i < m_maxCount; ++i)
        {
            UILogQueue[i].Color.init(&LocalArena, 4, 4);
            UILogQueue[i].Type.init(&LocalArena, 16);
            UILogQueue[i].Content.init(&LocalArena, 256);
        }

        m_handler_cookie = Logger::AddEventHandler(std::bind(&LogUIComponent::OnLog, this, std::placeholders::_1));
    }

    void LogUIComponent::Update(ZEngine::Core::TimeStep dt)
    {
        ZEngine::Logging::LogMessage engine_log_msg = {};
        if (EngineLogQueue.Pop(engine_log_msg))
        {
            if (m_currentCount >= UILogQueue.size())
            {
                m_currentCount = 0;
            }

            auto& lm    = UILogQueue[m_currentCount];

            lm.Color[0] = engine_log_msg.Color[0];
            lm.Color[1] = engine_log_msg.Color[1];
            lm.Color[2] = engine_log_msg.Color[2];
            lm.Color[3] = engine_log_msg.Color[3];

            lm.Content.clear();
            lm.Content.append(engine_log_msg.Message.c_str());

            lm.Type.clear();
            lm.Type.append(ZEngine::Logging::Logger::MessageTypeToString(engine_log_msg.Type));

            ++m_currentCount;
        }
    }

    void LogUIComponent::ClearLog()
    {
        m_currentCount.store(0, std::memory_order_release);
    }

    void LogUIComponent::Render(ZEngine::Rendering::Renderers::GraphicRenderer* const renderer, ZEngine::Hardwares::CommandBuffer* const command_buffer)
    {
        static const char* items[] = {
            "info",
            "error",
            "warn",
            "critical",
            "trace",
            "All",
        };
        static int current_item               = 0;

        char       search_buffer_tolower[256] = {0};
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
                auto& message = UILogQueue[i];

                if (current_item != (IM_ARRAYSIZE(items) - 1) /*Last item : All*/)
                {
                    if (0 != secure_strcmp(message.Type.c_str(), items[current_item]))
                    {
                        continue;
                    }
                }

                bool      search_succeeded = false;
                ArenaTemp scratch          = {};
                if (secure_strlen(search_buffer_tolower) > 0)
                {
                    scratch = ZGetScratch(&LocalArena);

                    if (!Helpers::KMPSearch(scratch.Arena, message.Content.c_str(), search_buffer_tolower))
                    {
                        ZReleaseScratch(scratch);
                        continue;
                    }
                    search_succeeded = true;
                }

                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextColored({message.Color[0], message.Color[1], message.Color[2], message.Color[3]}, message.Content.data());

                if (search_succeeded && scratch.Arena)
                {
                    ZReleaseScratch(scratch);
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
        EngineLogQueue.Emplace(std::move(message));
    }
} // namespace Tetragrama::Components
