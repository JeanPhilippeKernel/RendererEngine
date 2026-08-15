#include <Tetragrama/Components/StatusBarUIComponent.h>
#include <Tetragrama/Editor.h>
#include <ZEngine/Helpers/MemoryOperations.h>
#include <ZEngine/Logging/Logger.h>
#include <imgui.h>
#include <cstdio>

namespace Tetragrama::Components
{
    static constexpr float kStatusBarHeight = 22.0f;
    static constexpr float kOverlayHeight   = 200.0f;

    StatusBarUIComponent::~StatusBarUIComponent()
    {
        if (m_log_cookie)
            ZEngine::Logging::Logger::RemoveEventHandler(m_log_cookie);
    }

    void StatusBarUIComponent::Initialize(Layers::ImguiLayer* parent, cstring name, bool visibility, bool closed)
    {
        UIComponent::Initialize(parent, name, visibility, closed);
        m_log_cookie = ZEngine::Logging::Logger::AddEventHandler({OnLogEntry, this});
    }

    void StatusBarUIComponent::Update(ZEngine::Core::TimeStep /*dt*/) {}

    void StatusBarUIComponent::OnLogEntry(void* ctx, const ZEngine::Logging::LogMessage& msg)
    {
        auto*        self = static_cast<StatusBarUIComponent*>(ctx);
        OverlayEntry e;
        ZEngine::Helpers::secure_strncpy(e.Text, sizeof(e.Text), msg.Message ? msg.Message : "", sizeof(e.Text) - 1);
        e.Color[0] = msg.Color[0];
        e.Color[1] = msg.Color[1];
        e.Color[2] = msg.Color[2];
        e.Color[3] = msg.Color[3];
        self->PushEntry(e);
    }

    void StatusBarUIComponent::PushEntry(const OverlayEntry& e)
    {
        std::lock_guard<std::mutex> lock(m_log_mutex);
        m_log_ring[m_log_head] = e;
        m_log_head             = (m_log_head + 1) % kMaxEntries;
        if (m_log_count < kMaxEntries)
            ++m_log_count;
        m_scroll_to_bottom = true;
    }

    void StatusBarUIComponent::Render(ZEngine::Rendering::Renderers::GraphicRenderer* const, ZEngine::Hardwares::CommandBuffer* const)
    {
        if (!ParentLayer || !ParentLayer->CurrentApp)
            return;

        float raw_dt             = ImGui::GetIO().DeltaTime;
        m_frame_times[m_ft_head] = raw_dt;
        m_ft_head                = (m_ft_head + 1) % kFtSamples;
        float sum                = 0.0f;
        for (int i = 0; i < kFtSamples; ++i)
            sum += m_frame_times[i];
        m_smoothed_dt             = sum / static_cast<float>(kFtSamples);

        const ImGuiViewport* vp   = ImGui::GetMainViewport();
        const bool           dark = ImGui::GetStyle().Colors[ImGuiCol_WindowBg].x < 0.5f;

        if (m_console_open)
        {
            ImGui::SetNextWindowPos({vp->Pos.x, vp->Pos.y + vp->Size.y - kStatusBarHeight - kOverlayHeight});
            ImGui::SetNextWindowSize({vp->Size.x, kOverlayHeight});
            ImGui::SetNextWindowViewport(vp->ID);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6.0f, 4.0f));
            ImGui::PushStyleColor(ImGuiCol_WindowBg, dark ? ImVec4{0.10f, 0.10f, 0.11f, 0.96f} : ImVec4{0.96f, 0.96f, 0.96f, 0.96f});

            ImGui::Begin("##ConsoleOverlay", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
            ImGui::PopStyleColor();
            ImGui::PopStyleVar(3);

            ImGui::TextDisabled("Console");
            ImGui::SameLine();
            if (ImGui::SmallButton("Clear"))
            {
                std::lock_guard<std::mutex> lock(m_log_mutex);
                m_log_count = 0;
                m_log_head  = 0;
            }
            ImGui::SameLine();
            if (ImGui::SmallButton(" X "))
                m_console_open = false;
            ImGui::Separator();

            ImGui::BeginChild("##log_scroll", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);
            {
                std::lock_guard<std::mutex> lock(m_log_mutex);
                int                         count = m_log_count < kMaxEntries ? m_log_count : kMaxEntries;
                int                         start = (m_log_count >= kMaxEntries) ? m_log_head : 0;
                for (int i = 0; i < count; ++i)
                {
                    const auto& e = m_log_ring[(start + i) % kMaxEntries];
                    ImGui::TextColored({e.Color[0], e.Color[1], e.Color[2], e.Color[3]}, "%s", e.Text);
                }
                if (m_scroll_to_bottom)
                {
                    ImGui::SetScrollHereY(1.0f);
                    m_scroll_to_bottom = false;
                }
            }
            ImGui::EndChild();
            ImGui::End();
        }

        ImGui::SetNextWindowPos({vp->Pos.x, vp->Pos.y + vp->Size.y - kStatusBarHeight});
        ImGui::SetNextWindowSize({vp->Size.x, kStatusBarHeight});
        ImGui::SetNextWindowViewport(vp->ID);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 3.0f));
        ImGui::PushStyleColor(ImGuiCol_WindowBg, dark ? ImVec4{0.13f, 0.13f, 0.14f, 1.0f} : ImVec4{0.83f, 0.83f, 0.83f, 1.0f});

        ImGui::Begin(Name, nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBringToFrontOnFocus);
        ImGui::PopStyleColor();
        ImGui::PopStyleVar(3);

        // Console toggle button
        ImGui::PushStyleColor(ImGuiCol_Button, m_console_open ? ImVec4{0.35f, 0.35f, 0.40f, 1.0f} : ImVec4{0.20f, 0.20f, 0.22f, 0.80f});
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f);
        if (ImGui::SmallButton(m_console_open ? " ^ Console " : " v Console "))
            m_console_open = !m_console_open;
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();

        ImGui::SameLine(0.0f, 10.0f);
        ImGui::TextDisabled("|");
        ImGui::SameLine(0.0f, 10.0f);

        auto*   app        = reinterpret_cast<EditorPtr>(ParentLayer->CurrentApp);
        auto*   scene      = app->CurrentScene ? reinterpret_cast<EditorScenePtr>(app->CurrentScene) : nullptr;

        cstring scene_name = (app->Configuration && app->Configuration->ActiveSceneName.c_str()) ? app->Configuration->ActiveSceneName.c_str() : "-";
        ImGui::TextDisabled("Scene:");
        ImGui::SameLine(0.0f, 4.0f);
        ImGui::TextUnformatted(scene_name);
        ImGui::SameLine(0.0f, 10.0f);
        ImGui::TextDisabled("|");
        ImGui::SameLine(0.0f, 10.0f);

        if (scene)
        {
            int32_t sel_id = scene->SelectedInstanceId.value.load(std::memory_order_acquire);
            if (sel_id > 0)
            {
                cstring sel_name = nullptr;
                for (uint32_t i = 0; i < scene->Instances.size(); ++i)
                    if ((int32_t) scene->Instances[i].Id == sel_id)
                    {
                        sel_name = scene->Instances[i].Name;
                        break;
                    }
                ImGui::TextUnformatted((sel_name && sel_name[0]) ? sel_name : "Unnamed");
            }
            else
            {
                ImGui::TextDisabled("Nothing selected");
            }
            ImGui::SameLine(0.0f, 10.0f);
            ImGui::TextDisabled("|");
            ImGui::SameLine(0.0f, 10.0f);
            char ibuf[32];
            snprintf(ibuf, sizeof(ibuf), "Instances: %u", (uint32_t) scene->Instances.size());
            ImGui::TextUnformatted(ibuf);
        }
        else
        {
            ImGui::TextDisabled("No scene");
        }

        float fps         = m_smoothed_dt > 0.0f ? 1.0f / m_smoothed_dt : 0.0f;
        float dt_ms       = m_smoothed_dt * 1000.0f;
        char  cam_buf[64] = "-";
        char  fps_buf[48];
        if (app->CameraController)
        {
            auto pos = app->CameraController->GetPosition();
            snprintf(cam_buf, sizeof(cam_buf), "X: %.1f  Y: %.1f  Z: %.1f", pos.x, pos.y, pos.z);
        }
        snprintf(fps_buf, sizeof(fps_buf), "FPS: %.0f  %.2f ms", fps, dt_ms);

        float right_w = ImGui::CalcTextSize(cam_buf).x + ImGui::CalcTextSize("  |  ").x + ImGui::CalcTextSize(fps_buf).x + 24.0f;
        ImGui::SameLine(ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX() - right_w);
        ImGui::TextUnformatted(cam_buf);
        ImGui::SameLine(0.0f, 10.0f);
        ImGui::TextDisabled("|");
        ImGui::SameLine(0.0f, 10.0f);
        ImGui::TextUnformatted(fps_buf);

        ImGui::End();
    }
} // namespace Tetragrama::Components
