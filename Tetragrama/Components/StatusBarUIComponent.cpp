#include <Tetragrama/Components/StatusBarUIComponent.h>
#include <Tetragrama/Editor.h>
#include <imgui.h>
#include <cstdio>

namespace Tetragrama::Components
{
    static constexpr float kStatusBarHeight = 28.0f;

    void                   StatusBarUIComponent::Initialize(Layers::ImguiLayer* parent, cstring name, bool visibility, bool closed)
    {
        UIComponent::Initialize(parent, name, visibility, closed);
    }

    void StatusBarUIComponent::Update(ZEngine::Core::TimeStep /*dt*/) {}

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

        ImGui::SetNextWindowPos({vp->Pos.x, vp->Pos.y + vp->Size.y - kStatusBarHeight});
        ImGui::SetNextWindowSize({vp->Size.x, kStatusBarHeight});
        ImGui::SetNextWindowViewport(vp->ID);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 4.0f));
        ImGui::PushStyleColor(ImGuiCol_WindowBg, dark ? ImVec4{0.13f, 0.13f, 0.14f, 1.0f} : ImVec4{0.83f, 0.83f, 0.83f, 1.0f});

        ImGui::Begin(Name, nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBringToFrontOnFocus);
        ImGui::PopStyleColor();
        ImGui::PopStyleVar(3);

        auto*                  app         = reinterpret_cast<EditorPtr>(ParentLayer->CurrentApp);

        // Neutral button backgrounds (theme-aware)
        ImVec4                 btn_off     = dark ? ImVec4{0.22f, 0.22f, 0.24f, 0.85f} : ImVec4{0.72f, 0.72f, 0.72f, 1.0f};
        ImVec4                 btn_on      = dark ? ImVec4{0.28f, 0.28f, 0.32f, 1.0f} : ImVec4{0.60f, 0.60f, 0.62f, 1.0f};

        // Console — teal-green signature
        static constexpr ImU32 kConsoleOn  = IM_COL32(55, 210, 150, 255);
        static constexpr ImU32 kConsoleOff = IM_COL32(55, 110, 85, 180);

        // Content Browser — amber signature
        static constexpr ImU32 kBrowserOn  = IM_COL32(255, 185, 50, 255);
        static constexpr ImU32 kBrowserOff = IM_COL32(140, 100, 30, 180);

        // Console button
        {
            bool on = app->Configuration->ShowConsole;
            ImGui::PushStyleColor(ImGuiCol_Button, on ? btn_on : btn_off);
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f);
            if (ImGui::SmallButton("     Console "))
            {
                app->Configuration->ShowConsole  = !on;
                app->Configuration->FocusConsole = !on;
            }
            ImGui::PopStyleVar();
            ImGui::PopStyleColor();
            ImVec2      bmin = ImGui::GetItemRectMin();
            ImVec2      bmax = ImGui::GetItemRectMax();
            float       cy   = (bmin.y + bmax.y) * 0.5f;
            const float isz  = 10.0f;
            ImVec2      ip   = {bmin.x + 4.0f, cy - isz * 0.5f};
            ImDrawList* dl   = ImGui::GetWindowDrawList();
            ImU32       ic   = on ? kConsoleOn : kConsoleOff;
            dl->AddRect(ip, {ip.x + isz, ip.y + isz}, ic, 1.0f, 0, 1.2f);
            for (int li = 0; li < 3; ++li)
                dl->AddLine({ip.x + 1.5f, ip.y + 2.0f + li * 2.5f}, {ip.x + isz - 1.5f, ip.y + 2.0f + li * 2.5f}, ic, 1.0f);
        }

        ImGui::SameLine(0.0f, 6.0f);

        // Content Browser button
        {
            bool on = app->Configuration->ShowContentBrowser;
            ImGui::PushStyleColor(ImGuiCol_Button, on ? btn_on : btn_off);
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f);
            if (ImGui::SmallButton("     Content Browser "))
            {
                app->Configuration->ShowContentBrowser  = !on;
                app->Configuration->FocusContentBrowser = !on;
            }
            ImGui::PopStyleVar();
            ImGui::PopStyleColor();
            ImVec2      bmin = ImGui::GetItemRectMin();
            ImVec2      bmax = ImGui::GetItemRectMax();
            float       cy   = (bmin.y + bmax.y) * 0.5f;
            const float isz  = 10.0f;
            ImVec2      ip   = {bmin.x + 4.0f, cy - isz * 0.5f};
            ImDrawList* dl   = ImGui::GetWindowDrawList();
            ImU32       ic   = on ? kBrowserOn : kBrowserOff;
            float       tw = isz * 0.45f, th = isz * 0.28f;
            dl->AddRectFilled({ip.x, ip.y + th}, {ip.x + isz, ip.y + isz}, ic, 1.0f);
            dl->AddRectFilled({ip.x, ip.y + th - 1.5f}, {ip.x + tw, ip.y + th + 1.5f}, ic);
        }

        ImGui::SameLine(0.0f, 10.0f);
        ImGui::TextDisabled("|");
        ImGui::SameLine(0.0f, 10.0f);

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
