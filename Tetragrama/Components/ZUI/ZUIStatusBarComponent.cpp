// clang-format off
#include <Tetragrama/Components/ZUI/ZUIStatusBarComponent.h>
#include <Tetragrama/Editor.h>
#include <ZEngine/ECS/ActorManager.h>
#include <ZEngine/ECS/Components/NameComponent.h>
#include <ZEngine/Engine.h>
#include <ZEngine/UI/ZUIWidgets.h>
#include <cstdio>
// clang-format on

using namespace ZEngine::UI;
using namespace ZEngine::ECS;
using namespace ZEngine::ECS::Components;

namespace Tetragrama::Components
{
    void ZUIStatusBarComponent::Initialize(Tetragrama::Layers::ZUILayer* parent, cstring name, bool visibility)
    {
        ParentLayer = parent;
        Name        = name;
        Visible     = visibility;
    }

    // HOT PATH — runs every frame, no heap allocation allowed.
    void ZUIStatusBarComponent::BuildUI(ZUIContext* ctx)
    {
        if (!Visible || !ParentLayer || !ParentLayer->CurrentApp)
            return;

        auto* app                = reinterpret_cast<EditorPtr>(ParentLayer->CurrentApp);

        m_frame_times[m_ft_head] = ctx->DeltaTime;
        m_ft_head                = (m_ft_head + 1) % kFtSamples;
        float sum                = 0.f;
        for (int i = 0; i < kFtSamples; ++i)
            sum += m_frame_times[i];
        m_smoothed_dt    = sum / (float) kFtSamples;

        float   sw       = RegionW > 0 ? RegionW : (float) ctx->ScreenW;
        float   sy       = RegionW > 0 ? RegionY : (float) ctx->ScreenH - kBarH;

        ZUIBox* bar      = ZUIBeginRow(ctx, "##status_bar", ZPx(sw), ZPx(kBarH));
        bar->Flags       = bar->Flags | ZUI_DrawBackground | ZUI_DrawBorder | ZUI_FloatX | ZUI_FloatY;
        bar->FloatPos[0] = RegionW > 0 ? RegionX : 0.f;
        bar->FloatPos[1] = sy;
        // Background and border follow active theme
        ZUIBoxSetColorArr(bar, ctx->Theme.TitleBarBg);
        bar->BorderColor[0]  = ctx->Theme.PanelBorder[0];
        bar->BorderColor[1]  = ctx->Theme.PanelBorder[1];
        bar->BorderColor[2]  = ctx->Theme.PanelBorder[2];
        bar->BorderColor[3]  = ctx->Theme.PanelBorder[3];
        bar->BorderThickness = 0.5f;
        bar->EdgeSoftness    = 0.f;

        // Vertically center children
        float fh             = ZUIGetFrameHeight(ctx);
        float vpad           = fmaxf(0.f, (kBarH - fh) * 0.5f);
        bar->Padding[1]      = vpad;
        bar->Padding[3]      = vpad;

        ZUISpacer(ctx, 6.f);

        // Toggle helper — reads/writes panel visibility via ShellPanelManager
        auto toggle = [&](const char* key, const char* panel_name) {
            bool      on = ShellPanelManager && ShellPanelManager->IsPanelVisible(panel_name);
            ZUISignal s  = ZUIButton(ctx, key);
            if ((s.Flags & ZUI_SignalClicked) && ShellPanelManager)
                ShellPanelManager->SetPanelVisible(panel_name, !on);
        };

        toggle("Console##sb_con", "Console");
        ZUISpacer(ctx, 4.f);
        toggle("Browser##sb_bro", "Project");
        ZUISpacer(ctx, 4.f);
        toggle("Importer##sb_imp", "Importer");

        ZUISpacer(ctx, 8.f);
        ZUILabel(ctx, "|", ctx->Theme.TextDim);
        ZUISpacer(ctx, 8.f);

        // Scene name
        ZUILabel(ctx, "Scene:", ctx->Theme.TextDim);
        ZUISpacer(ctx, 4.f);
        const char* scene_name = (app->Configuration && !app->Configuration->ActiveSceneName.empty()) ? app->Configuration->ActiveSceneName.c_str() : "-";
        ZUILabel(ctx, scene_name, ctx->Theme.TextDefault);

        ZUISpacer(ctx, 10.f);
        ZUILabel(ctx, "|", ctx->Theme.TextDim);
        ZUISpacer(ctx, 10.f);

        // Selected actor name + actor count
        {
            auto*       eng        = ZEngine::Engine::GetContext();
            auto*       edit_scene = app->CurrentScene ? reinterpret_cast<EditorScenePtr>(app->CurrentScene) : nullptr;
            const char* sel        = nullptr;

            if (edit_scene && edit_scene->SelectedActorHandle.Valid() && eng && eng->ActorManager)
            {
                Actor* actor = eng->ActorManager->Access(edit_scene->SelectedActorHandle);
                if (actor)
                {
                    auto* nc = actor->GetComponent<NameComponent>();
                    sel      = (nc && nc->Value[0]) ? nc->Value : "Actor";
                }
            }

            if (sel)
                ZUILabel(ctx, sel, ctx->Theme.TextDefault);
            else
                ZUILabel(ctx, "Nothing selected", ctx->Theme.TextDim);
        }

        ZUISpacer(ctx, 10.f);
        ZUILabel(ctx, "|", ctx->Theme.TextDim);
        ZUISpacer(ctx, 10.f);

        // Camera position
        if (app->CameraController)
        {
            auto pos = app->CameraController->GetPosition();
            char cam_buf[64];
            snprintf(cam_buf, sizeof(cam_buf), "X:%.1f  Y:%.1f  Z:%.1f", (double) pos.x, (double) pos.y, (double) pos.z);
            ZUILabel(ctx, cam_buf, ctx->Theme.TextDim);
            ZUISpacer(ctx, 10.f);
            ZUILabel(ctx, "|", ctx->Theme.TextDim);
            ZUISpacer(ctx, 10.f);
        }

        // FPS
        {
            float fps = m_smoothed_dt > 0.f ? 1.f / m_smoothed_dt : 0.f;
            char  fps_buf[32];
            snprintf(fps_buf, sizeof(fps_buf), "FPS: %.0f  %.2f ms", (double) fps, (double) (m_smoothed_dt * 1000.f));
            ZUILabel(ctx, fps_buf, ctx->Theme.TextDim);
        }

        ZUIEndRow(ctx);
    }

} // namespace Tetragrama::Components
