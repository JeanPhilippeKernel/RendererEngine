#include <Tetragrama/Components/LogUIComponent.h>
#include <Tetragrama/Editor.h>
#include <ZEngine/Helpers/MemoryOperations.h>
#include <imgui.h>
#include <cstdio>

namespace Tetragrama::Components
{
    LogUIComponent::~LogUIComponent()
    {
        if (m_cookie)
            ZEngine::Logging::Logger::RemoveEventHandler(m_cookie);
    }

    void LogUIComponent::Initialize(Layers::ImguiLayer* parent, cstring name, bool visibility, bool closed)
    {
        UIComponent::Initialize(parent, name, visibility, closed);
        m_filter_level = 5;
        m_cookie       = ZEngine::Logging::Logger::AddEventHandler({OnLogEntry, this});
    }

    void LogUIComponent::Update(ZEngine::Core::TimeStep /*dt*/) {}

    void LogUIComponent::OnLogEntry(void* ctx, const ZEngine::Logging::LogMessage& msg)
    {
        auto*    self = static_cast<LogUIComponent*>(ctx);
        LogEntry e;
        ZEngine::Helpers::secure_strncpy(e.Text, sizeof(e.Text), msg.Message ? msg.Message : "", sizeof(e.Text) - 1);
        e.Color[0] = msg.Color[0];
        e.Color[1] = msg.Color[1];
        e.Color[2] = msg.Color[2];
        e.Color[3] = msg.Color[3];
        e.Level    = static_cast<uint8_t>(msg.Level);
        self->PushEntry(e);
    }

    void LogUIComponent::PushEntry(const LogEntry& e)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_ring[m_head] = e;
        m_head         = (m_head + 1) % kMaxEntries;
        if (m_count < kMaxEntries)
            ++m_count;
        m_scroll_to_bottom = true;
    }

    void LogUIComponent::Render(ZEngine::Rendering::Renderers::GraphicRenderer* const, ZEngine::Hardwares::CommandBuffer* const)
    {
        if (!ParentLayer || !ParentLayer->CurrentApp)
            return;

        auto* app = reinterpret_cast<Tetragrama::EditorPtr>(ParentLayer->CurrentApp);
        if (!app || !app->Configuration->ShowConsole)
            return;

        if (app->Configuration->FocusConsole)
        {
            ImGui::SetNextWindowFocus();
            app->Configuration->FocusConsole = false;
        }

        const bool dark = ImGui::GetStyle().Colors[ImGuiCol_WindowBg].x < 0.5f;
        ImGui::PushStyleColor(ImGuiCol_WindowBg, dark ? ImVec4{0.10f, 0.10f, 0.11f, 1.0f} : ImVec4{0.96f, 0.96f, 0.96f, 1.0f});
        bool open = ImGui::Begin(Name, &app->Configuration->ShowConsole, ImGuiWindowFlags_NoCollapse);
        ImGui::PopStyleColor();

        if (open)
        {
            static constexpr cstring kLevelItems[] = {"Trace", "Info", "Warn", "Error", "Critical", "All"};
            ImGui::SetNextItemWidth(180.0f);
            ImGui::InputTextWithHint("##search", "Search logs...", m_search_buf, sizeof(m_search_buf));
            ImGui::SameLine();
            ImGui::SetNextItemWidth(80.0f);
            ImGui::Combo("##level", &m_filter_level, kLevelItems, 6);
            ImGui::SameLine();
            if (ImGui::SmallButton("Copy"))
                m_copy_requested = true;
            ImGui::SameLine();
            if (ImGui::SmallButton("Clear"))
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_count = 0;
                m_head  = 0;
            }
            ImGui::Separator();

            ImGui::BeginChild("##log_scroll", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                int                         count = m_count < kMaxEntries ? m_count : kMaxEntries;
                int                         start = (m_count >= kMaxEntries) ? m_head : 0;

                static char                 clip_buf[kMaxEntries * 260];
                int                         clip_pos = 0;
                if (m_copy_requested)
                    clip_buf[0] = '\0';

                for (int i = 0; i < count; ++i)
                {
                    const auto& e = m_ring[(start + i) % kMaxEntries];

                    if (m_filter_level < 5 && e.Level != static_cast<uint8_t>(m_filter_level))
                        continue;

                    if (m_search_buf[0] != '\0')
                    {
                        bool found = false;
                        for (int si = 0; e.Text[si] && !found; ++si)
                        {
                            int j = 0;
                            for (; m_search_buf[j] && e.Text[si + j]; ++j)
                                if (::tolower(e.Text[si + j]) != ::tolower(m_search_buf[j]))
                                    break;
                            if (!m_search_buf[j])
                                found = true;
                        }
                        if (!found)
                            continue;
                    }

                    ImGui::TextColored({e.Color[0], e.Color[1], e.Color[2], e.Color[3]}, "%s", e.Text);

                    if (m_copy_requested && clip_pos < (int) sizeof(clip_buf) - 2)
                    {
                        int n = snprintf(clip_buf + clip_pos, sizeof(clip_buf) - clip_pos, "%s\n", e.Text);
                        if (n > 0)
                            clip_pos += n;
                    }
                }

                if (m_copy_requested)
                {
                    ImGui::SetClipboardText(clip_buf);
                    m_copy_requested = false;
                }

                if (m_scroll_to_bottom)
                {
                    ImGui::SetScrollHereY(1.0f);
                    m_scroll_to_bottom = false;
                }
            }
            ImGui::EndChild();
        }
        ImGui::End();
    }
} // namespace Tetragrama::Components
