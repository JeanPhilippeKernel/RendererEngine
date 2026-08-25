#include <Tetragrama/Components/ZUI/ZUIStatusBarComponent.h>
#include <Tetragrama/Editor.h>
#include <ZEngine/UI/ZUIWidgets.h>
#include <cstdio>

using namespace ZEngine::UI;

namespace Tetragrama::Components
{
    static constexpr float k_dim[4]  = {0.55f, 0.55f, 0.60f, 1.f};
    static constexpr float k_text[4] = {0.90f, 0.90f, 0.90f, 1.f};
    static constexpr float k_on[4]   = {0.30f, 0.65f, 0.45f, 1.f};

    void ZUIStatusBarComponent::Initialize(Tetragrama::Layers::ZUILayer* parent,
                                           cstring name, bool visibility)
    {
        ParentLayer = parent;
        Name        = name;
        Visible     = visibility;
    }

    void ZUIStatusBarComponent::BuildUI(ZUIContext* ctx)
    {
        if (!Visible || !ParentLayer || !ParentLayer->CurrentApp) { return; }

        auto* app = reinterpret_cast<EditorPtr>(ParentLayer->CurrentApp);

        // Update smoothed frame time
        m_frame_times[m_ft_head] = ctx->DeltaTime;
        m_ft_head                = (m_ft_head + 1) % kFtSamples;
        float sum = 0.f;
        for (int i = 0; i < kFtSamples; ++i) { sum += m_frame_times[i]; }
        m_smoothed_dt = sum / (float)kFtSamples;

        float sw = RegionW > 0 ? RegionW : (float)ctx->ScreenW;
        float sy = RegionW > 0 ? RegionY : (float)ctx->ScreenH - kBarH;

        // Bar: full-width, bottom-anchored
        ZUIBox* bar    = ZUIBeginRow(ctx, "##status_bar", ZPx(sw), ZPx(kBarH));
        bar->Flags = bar->Flags | ZUI_DrawBackground | ZUI_DrawBorder | ZUI_FloatX | ZUI_FloatY;
        bar->FloatPos[0] = RegionW > 0 ? RegionX : 0.f;
        bar->FloatPos[1] = sy;
        bar->BgColor[0]  = 0.25f; bar->BgColor[1] = 0.25f;
        bar->BgColor[2]  = 0.28f; bar->BgColor[3]  = 1.f;
        bar->BorderColor[0] = 0.40f; bar->BorderColor[1] = 0.40f;
        bar->BorderColor[2] = 0.50f; bar->BorderColor[3] = 1.f;
        bar->BorderThickness = 1.f;

        ZUISpacer(ctx, 6.f);

        // Console toggle
        {
            bool on = app->Configuration->ShowConsole;
            ZUISignal s = ZUIButton(ctx, on ? "Console##on" : "Console##off");
            if (on) {
                ZUIBox* btn = ctx->Current ? ctx->Current->LastChild : nullptr;
                (void)btn; // future: tint button with k_on color
            }
            if (s.Flags & ZUI_SignalClicked) {
                app->Configuration->ShowConsole  = !on;
                app->Configuration->FocusConsole = !on;
            }
        }

        ZUISpacer(ctx, 4.f);

        // Browser toggle
        {
            bool on = app->Configuration->ShowContentBrowser;
            ZUISignal s = ZUIButton(ctx, on ? "Browser##on" : "Browser##off");
            if (s.Flags & ZUI_SignalClicked) {
                app->Configuration->ShowContentBrowser  = !on;
                app->Configuration->FocusContentBrowser = !on;
            }
        }

        ZUISpacer(ctx, 4.f);

        // Importer toggle
        {
            bool on = app->Configuration->ShowImporter;
            ZUISignal s = ZUIButton(ctx, on ? "Importer##on" : "Importer##off");
            if (s.Flags & ZUI_SignalClicked) {
                app->Configuration->ShowImporter  = !on;
                app->Configuration->FocusImporter = !on;
            }
        }

        ZUISpacer(ctx, 8.f);
        ZUILabel(ctx, "|", k_dim);
        ZUISpacer(ctx, 8.f);

        // Scene name
        ZUILabel(ctx, "Scene:", k_dim);
        ZUISpacer(ctx, 4.f);
        const char* scene_name = (app->Configuration && !app->Configuration->ActiveSceneName.empty())
                                 ? app->Configuration->ActiveSceneName.c_str() : "-";
        ZUILabel(ctx, scene_name, k_text);

        ZUISpacer(ctx, 10.f);
        ZUILabel(ctx, "|", k_dim);
        ZUISpacer(ctx, 10.f);

        // Camera position
        if (app->CameraController)
        {
            auto pos = app->CameraController->GetPosition();
            char cam_buf[64];
            snprintf(cam_buf, sizeof(cam_buf), "X:%.1f  Y:%.1f  Z:%.1f",
                     (double)pos.x, (double)pos.y, (double)pos.z);
            ZUILabel(ctx, cam_buf, k_dim);
        }

        ZUISpacer(ctx, 10.f);
        ZUILabel(ctx, "|", k_dim);
        ZUISpacer(ctx, 10.f);

        // FPS
        {
            float fps = m_smoothed_dt > 0.f ? 1.f / m_smoothed_dt : 0.f;
            char  fps_buf[32];
            snprintf(fps_buf, sizeof(fps_buf), "FPS: %.0f  %.2f ms",
                     (double)fps, (double)(m_smoothed_dt * 1000.f));
            ZUILabel(ctx, fps_buf, k_dim);
        }

        ZUIEndRow(ctx);
    }
} // namespace Tetragrama::Components
