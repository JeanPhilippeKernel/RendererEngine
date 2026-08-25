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

    // A label + value row for a single named property
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

    // Three-float XYZ display row (read-only in Phase 9; editable widgets come in Phase 10)
    static void XYZRow(ZUIContext* ctx, const char* row_key, const char* label,
                       float x, float y, float z)
    {
        char val[64];
        snprintf(val, sizeof(val), "%.3f  %.3f  %.3f", (double)x, (double)y, (double)z);
        PropRow(ctx, row_key, label, val);
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

        // --- Outer panel — floated, right of hierarchy ---
        ZUIBox* panel      = ZUIBeginColumn(ctx, "##zui_insp_panel", ZPx(280.f), ZPx(600.f));
        panel->Flags       = panel->Flags | ZUI_DrawBackground | ZUI_FloatX | ZUI_FloatY;
        panel->FloatPos[0] = 760.f;
        panel->FloatPos[1] = 80.f;
        panel->BgColor[0]  = 0.12f;
        panel->BgColor[1]  = 0.12f;
        panel->BgColor[2]  = 0.14f;
        panel->BgColor[3]  = 0.96f;

        // Title bar
        ZUIBeginRow(ctx, "##insp_hdr", ZFill(), ZPx(26.f));
            ZUILabel(ctx, Name ? Name : "Inspector");
        ZUIEndRow(ctx);
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

                // Name (read-only display; edit comes in Phase 10)
                const char* actor_name = (nc && nc->Value[0]) ? nc->Value : "Actor";
                ZUILabel(ctx, actor_name);
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
                constexpr float kDeg = 180.f / 3.14159265f;
                XYZRow(ctx, "##loc", "Location",
                       tc->Position.x, tc->Position.y, tc->Position.z);
                XYZRow(ctx, "##rot", "Rotation",
                       tc->Rotation.x * kDeg, tc->Rotation.y * kDeg, tc->Rotation.z * kDeg);
                XYZRow(ctx, "##scl", "Scale",
                       tc->Scale.x, tc->Scale.y, tc->Scale.z);
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
