// clang-format off
#include <Tetragrama/Components/ZUI/ZUIInspectorViewComponent.h>
#include <Tetragrama/Editor.h>
#include <ZEngine/ECS/ActorManager.h>
#include <ZEngine/ECS/Components/LightComponent.h>
#include <ZEngine/ECS/Components/MeshComponent.h>
#include <ZEngine/ECS/Components/NameComponent.h>
#include <ZEngine/ECS/Components/TransformComponent.h>
#include <ZEngine/Engine.h>
#include <ZEngine/Helpers/MemoryOperations.h>
#include <ZEngine/UI/ZUIWidgets.h>
#include <cstdio>
#include <string>
// clang-format on

using namespace ZEngine::ECS;
using namespace ZEngine::ECS::Components;
using namespace ZEngine::UI;

namespace Tetragrama::Components
{
    static constexpr float kLabelW   = 72.f;
    static constexpr float kRowH     = 21.f;

    // Label + read-only value row
    static void PropRow(ZUIContext* ctx, const char* row_key, const char* label, const char* value)
    {
        ZUIBeginRow(ctx, row_key, ZFill(), ZPx(kRowH));
            ZUIBox* lbl = ZUIPushBox(ctx, label, (uint32_t)ZEngine::Helpers::secure_strlen(label), ZUI_DrawText);
            lbl->Size[0]      = ZPx(kLabelW);
            lbl->Size[1]      = ZPx(kRowH);
            lbl->TextColor[0] = ctx->Theme.TextDim[0]; lbl->TextColor[1] = ctx->Theme.TextDim[1];
            lbl->TextColor[2] = ctx->Theme.TextDim[2]; lbl->TextColor[3] = ctx->Theme.TextDim[3];
            ZUIPopBox(ctx);
            ZUILabel(ctx, value, ctx->Theme.TextDefault);
        ZUIEndRow(ctx);
    }

    // Label + three DragFloat fields on one row — editable XYZ
    static bool XYZDragRow(ZUIContext* ctx,
                            const char* row_key, const char* label,
                            float* x, float* y, float* z, float speed)
    {
        ZUIBeginRow(ctx, row_key, ZFill(), ZPx(kRowH));
            ZUIBox* lbl = ZUIPushBox(ctx, label, (uint32_t)ZEngine::Helpers::secure_strlen(label), ZUI_DrawText);
            lbl->Size[0]      = ZPx(kLabelW);
            lbl->Size[1]      = ZPx(kRowH);
            lbl->TextColor[0] = ctx->Theme.TextDim[0]; lbl->TextColor[1] = ctx->Theme.TextDim[1];
            lbl->TextColor[2] = ctx->Theme.TextDim[2]; lbl->TextColor[3] = ctx->Theme.TextDim[3];
            ZUIPopBox(ctx);

            // Build unique per-axis keys from the row key
            char kx[40], ky[40], kz[40];
            snprintf(kx, sizeof(kx), "##x_%s", row_key + 2);
            snprintf(ky, sizeof(ky), "##y_%s", row_key + 2);
            snprintf(kz, sizeof(kz), "##z_%s", row_key + 2);
            bool cx = ZUIDragFloat(ctx, kx, x, speed, 54.f);
            bool cy = ZUIDragFloat(ctx, ky, y, speed, 54.f);
            bool cz = ZUIDragFloat(ctx, kz, z, speed, 54.f);
        ZUIEndRow(ctx);
        return cx || cy || cz;
    }

    // ---------------------------------------------------------------

    void ZUIInspectorViewComponent::Initialize(Tetragrama::Layers::ZUILayer* parent,
                                               cstring name, bool visibility)
    {
        ParentLayer = parent;
        Name        = name;
        Visible     = visibility;
    }

    void ZUIInspectorViewComponent::BuildUI(ZUIContext* ctx)
    {
        if (!Visible || !ParentLayer || !ParentLayer->CurrentApp) { return; }

        auto* app           = reinterpret_cast<EditorPtr>(ParentLayer->CurrentApp);
        auto* current_scene = reinterpret_cast<EditorScenePtr>(app->CurrentScene);
        auto* eng           = ZEngine::Engine::GetContext();
        if (!current_scene || !eng || !eng->ActorManager) { return; }

        if (RegionW == 0) { RegionX = 760.f; RegionY = 80.f; RegionW = 280.f; RegionH = 600.f; }

        ZUIBox* panel      = ZUIBeginColumn(ctx, "##zui_insp_panel", ZPx(RegionW), ZPx(RegionH));
        panel->Flags = panel->Flags | ZUI_DrawBackground | ZUI_DrawBorder | ZUI_FloatX | ZUI_FloatY;
        panel->FloatPos[0] = RegionX;
        panel->FloatPos[1] = RegionY;
        ZUIBoxSetColorArr(panel, ctx->Theme.PanelBg);
        panel->BorderColor[0] = ctx->Theme.PanelBorder[0]; panel->BorderColor[1] = ctx->Theme.PanelBorder[1];
        panel->BorderColor[2] = ctx->Theme.PanelBorder[2]; panel->BorderColor[3] = ctx->Theme.PanelBorder[3];
        panel->BorderColor[3] = 1.0f;
        panel->BorderThickness = 1.f;
        panel->EdgeSoftness    = 0.f;
        ZUIBoxSetCornerRadius(panel, 0.f);

        // Title bar — draggable (Gap 4)
        ZUIBox* hdr = ZUIBeginRow(ctx, "##insp_hdr", ZFill(), ZPx(26.f));
        hdr->Flags  = hdr->Flags | ZUI_Clickable;
            ZUILabel(ctx, Name ? Name : "Inspector");
            ZUISignal drag_sig = ZUISignalFromBox(ctx, hdr);
        ZUIEndRow(ctx);
        if ((drag_sig.Flags & ZUI_SignalHeld) &&
            (drag_sig.DragDelta[0] != 0.f || drag_sig.DragDelta[1] != 0.f))
        {
            RegionX += drag_sig.DragDelta[0];
            RegionY += drag_sig.DragDelta[1];
            Detached = true;
            panel->FloatPos[0] = RegionX;
            panel->FloatPos[1] = RegionY;
        }
        if (drag_sig.Flags & ZUI_SignalDoubleClicked) { Detached = false; }
        ZUISeparator(ctx);

        // No selection guard
        ActorHandle h     = current_scene->SelectedActorHandle;
        Actor*      actor = eng->ActorManager->Access(h);
        if (!actor)
        {
            ZUISpacer(ctx, 8.f);
            ZUILabel(ctx, "No actor selected", ctx->Theme.TextDim);
            ZUIEndColumn(ctx);
            return;
        }

        // --- Actor header ---
        auto* nc = actor->GetComponent<NameComponent>();
        {
            ZUIBox* hdr = ZUIBeginColumn(ctx, "##actor_hdr_card", ZFill(), ZPx(42.f));
            hdr->Flags  = hdr->Flags | ZUI_DrawBackground;
            ZUIBoxSetColor(hdr, 0.18f, 0.18f, 0.22f, 1.f);

                if (nc)
                {
                    ZUITextField(ctx, "##actor_name_field", nc->Value, sizeof(nc->Value), 200.f);
                }
                else
                {
                    ZUILabel(ctx, "Actor");
                }
                ZUILabel(ctx, "Actor", ctx->Theme.TextDim);

            ZUIEndColumn(ctx);
        }

        ZUISpacer(ctx, 4.f);

        // --- Transform section ---
        auto* tc = actor->GetComponent<TransformComponent>();
        if (tc)
        {
            ZUICollapsingHeader(ctx, "Transform", &m_transform_open);
            if (m_transform_open)
            {
                float widths[2] = {80.f, 0.f};
                ZUIBeginTable(ctx, "##transform_tbl", 2, widths);

                    ZUITableNextRow(ctx);
                    ZUITableSetColumn(ctx, 0);
                    ZUITableSetColumn(ctx, 1);
                    XYZDragRow(ctx, "##loc", "Location",
                               &tc->Position.x, &tc->Position.y, &tc->Position.z, 0.05f);

                    ZUITableNextRow(ctx);
                    ZUITableSetColumn(ctx, 0);
                    ZUITableSetColumn(ctx, 1);
                    {
                        float deg[3] = {
                            tc->Rotation.x * 57.2957f,
                            tc->Rotation.y * 57.2957f,
                            tc->Rotation.z * 57.2957f
                        };
                        XYZDragRow(ctx, "##rot", "Rotation", &deg[0], &deg[1], &deg[2], 1.0f);
                        tc->Rotation.x = deg[0] / 57.2957f;
                        tc->Rotation.y = deg[1] / 57.2957f;
                        tc->Rotation.z = deg[2] / 57.2957f;
                    }

                    ZUITableNextRow(ctx);
                    ZUITableSetColumn(ctx, 0);
                    ZUITableSetColumn(ctx, 1);
                    XYZDragRow(ctx, "##scl", "Scale",
                               &tc->Scale.x, &tc->Scale.y, &tc->Scale.z, 0.01f);

                ZUIEndTable(ctx);
                ZUISpacer(ctx, 4.f);
            }
        }

        // --- Mesh section ---
        auto* mc = actor->GetComponent<MeshComponent>();
        if (mc)
        {
            ZUICollapsingHeader(ctx, "Mesh", &m_mesh_open);
            if (m_mesh_open)
            {
                std::string uuid_str = uuids::to_string(mc->MeshUUID);
                PropRow(ctx, "##mesh_uuid", "UUID", uuid_str.c_str());
                ZUISpacer(ctx, 4.f);
            }
        }

        // --- Light section ---
        auto* lc = actor->GetComponent<LightComponent>();
        if (lc)
        {
            ZUICollapsingHeader(ctx, "Light", &m_light_open);
            if (m_light_open)
            {
                ZUIBeginRow(ctx, "##light_intensity_row", ZFill(), ZPx(kRowH));
                    ZUILabel(ctx, "Intensity", ctx->Theme.TextDim);
                    ZUIDragFloat(ctx, "##light_intensity", &lc->Intensity, 0.1f, 120.f);
                ZUIEndRow(ctx);

                char type_buf[32];
                snprintf(type_buf, sizeof(type_buf), "%d", (int)lc->LightType);
                PropRow(ctx, "##light_type", "Type", type_buf);
                ZUISpacer(ctx, 4.f);
            }
        }

        ZUIEndColumn(ctx); // end panel
    }
} // namespace Tetragrama::Components
