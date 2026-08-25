// clang-format off
#include <Tetragrama/Components/ZUI/ZUIInspectorViewComponent.h>
#include <Tetragrama/Editor.h>
#include <ZEngine/ECS/ActorManager.h>
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
    static constexpr float k_dim[4]  = {0.55f, 0.55f, 0.60f, 1.f};
    static constexpr float k_text[4] = {0.90f, 0.90f, 0.90f, 1.f};
    static constexpr float kLabelW   = 72.f;
    static constexpr float kRowH     = 21.f;

    // Label + read-only value row
    static void PropRow(ZUIContext* ctx, const char* row_key, const char* label, const char* value)
    {
        ZUIBeginRow(ctx, row_key, ZFill(), ZPx(kRowH));
            ZUIBox* lbl = ZUIPushBox(ctx, label, (uint32_t)ZEngine::Helpers::secure_strlen(label), ZUI_DrawText);
            lbl->Size[0]      = ZPx(kLabelW);
            lbl->Size[1]      = ZPx(kRowH);
            lbl->TextColor[0] = k_dim[0]; lbl->TextColor[1] = k_dim[1];
            lbl->TextColor[2] = k_dim[2]; lbl->TextColor[3] = k_dim[3];
            ZUIPopBox(ctx);
            ZUILabel(ctx, value, k_text);
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
            lbl->TextColor[0] = k_dim[0]; lbl->TextColor[1] = k_dim[1];
            lbl->TextColor[2] = k_dim[2]; lbl->TextColor[3] = k_dim[3];
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
        panel->BgColor[0]  = 0.24f;
        panel->BgColor[1]  = 0.24f;
        panel->BgColor[2]  = 0.28f;
        panel->BgColor[3]  = 0.96f;
        panel->BorderColor[0] = 0.40f; panel->BorderColor[1] = 0.42f;
        panel->BorderColor[2] = 0.50f; panel->BorderColor[3] = 1.f;
        panel->BorderThickness = 1.f;

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
            ZUILabel(ctx, "No actor selected", k_dim);
            ZUIEndColumn(ctx);
            return;
        }

        // --- Actor header ---
        auto* nc = actor->GetComponent<NameComponent>();
        {
            ZUIBox* hdr = ZUIBeginColumn(ctx, "##actor_hdr_card", ZFill(), ZPx(42.f));
            hdr->Flags  = hdr->Flags | ZUI_DrawBackground;
            hdr->BgColor[0] = 0.18f; hdr->BgColor[1] = 0.18f;
            hdr->BgColor[2] = 0.22f; hdr->BgColor[3] = 1.f;

                if (nc)
                {
                    ZUITextField(ctx, "##actor_name_field", nc->Value, sizeof(nc->Value), 200.f);
                }
                else
                {
                    ZUILabel(ctx, "Actor");
                }
                ZUILabel(ctx, "Actor", k_dim);

            ZUIEndColumn(ctx);
        }

        ZUISpacer(ctx, 4.f);

        // --- Transform section ---
        auto* tc = actor->GetComponent<TransformComponent>();
        if (tc)
        {
            ZUITreeNode(ctx, "Transform##sec", &m_transform_open);
            if (m_transform_open)
            {
                XYZDragRow(ctx, "##loc", "Location",
                           &tc->Position.x, &tc->Position.y, &tc->Position.z, 0.05f);
                // Rotation stored in radians; speed 0.01 rad/px ≈ 0.57°/px
                XYZDragRow(ctx, "##rot", "Rotation (rad)",
                           &tc->Rotation.x, &tc->Rotation.y, &tc->Rotation.z, 0.01f);
                XYZDragRow(ctx, "##scl", "Scale",
                           &tc->Scale.x, &tc->Scale.y, &tc->Scale.z, 0.01f);
                ZUISpacer(ctx, 4.f);
            }
        }

        // --- Mesh section ---
        auto* mc = actor->GetComponent<MeshComponent>();
        if (mc)
        {
            ZUITreeNode(ctx, "Mesh##sec", &m_mesh_open);
            if (m_mesh_open)
            {
                std::string uuid_str = uuids::to_string(mc->MeshUUID);
                PropRow(ctx, "##mesh_uuid", "UUID", uuid_str.c_str());
                ZUISpacer(ctx, 4.f);
            }
        }

        ZUIEndColumn(ctx); // end panel
    }
} // namespace Tetragrama::Components
