// clang-format off
#include <Tetragrama/Components/HierarchyViewUIComponent.h>
#include <ImGuizmo/ImGuizmo.h>
#include <Tetragrama/Editor.h>

#include <ZEngine/Core/Maths/Matrix.h>
#include <ZEngine/ECS/Components/CameraComponent.h>
#include <ZEngine/ECS/Components/LightComponent.h>
#include <ZEngine/ECS/Components/MeshComponent.h>
#include <ZEngine/ECS/Components/NameComponent.h>
#include <ZEngine/ECS/Components/TransformComponent.h>
#include <ZEngine/Engine.h>
#include <ZEngine/Windows/Inputs/KeyCodeDefinition.h>
#include <ZEngine/Windows/Inputs/Keyboard.h>
// clang-format on

using namespace ZEngine;
using namespace ZEngine::ECS;
using namespace ZEngine::ECS::Components;
using namespace ZEngine::Windows::Inputs;
using namespace ZEngine::Core::Maths;

namespace Tetragrama::Components
{
    HierarchyViewUIComponent::HierarchyViewUIComponent() {}
    HierarchyViewUIComponent::~HierarchyViewUIComponent() {}

    void HierarchyViewUIComponent::Initialize(Layers::ImguiLayer* parent, const char* name, bool visibility, bool closed)
    {
        UIComponent::Initialize(parent, name, visibility, closed);
    }

    void HierarchyViewUIComponent::Update(ZEngine::Core::TimeStep /*dt*/)
    {
        if (!ParentLayer || !ParentLayer->CurrentApp)
            return;

        auto  window   = ParentLayer->CurrentApp->CurrentWindow;
        auto* keyboard = IDevice::As<Keyboard>();
        if (!keyboard)
            return;

        auto* app = reinterpret_cast<EditorPtr>(ParentLayer->CurrentApp);
        if (keyboard->IsKeyPressed(ZENGINE_KEY_T, window))
            app->Configuration->GizmoOperation = ImGuizmo::OPERATION::TRANSLATE;
        if (keyboard->IsKeyPressed(ZENGINE_KEY_R, window))
            app->Configuration->GizmoOperation = ImGuizmo::OPERATION::ROTATE;
        if (keyboard->IsKeyPressed(ZENGINE_KEY_S, window))
            app->Configuration->GizmoOperation = ImGuizmo::OPERATION::SCALE;
    }

    void HierarchyViewUIComponent::Render(ZEngine::Rendering::Renderers::GraphicRenderer* const /*renderer*/, ZEngine::Hardwares::CommandBuffer* const /*command_buffer*/)
    {
        if (!ParentLayer || !ParentLayer->CurrentApp)
            return;

        auto* app           = reinterpret_cast<EditorPtr>(ParentLayer->CurrentApp);
        auto* current_scene = reinterpret_cast<EditorScenePtr>(app->CurrentScene);
        auto* ctx           = Engine::GetContext();
        if (!current_scene || !ctx || !ctx->ActorManager)
            return;

        ImGui::Begin(Name, CanBeClosed ? &CanBeClosed : nullptr, ImGuiWindowFlags_NoCollapse);

        if (ImGui::IsMouseDown(0) && ImGui::IsWindowHovered())
            current_scene->SelectedActorHandle = {};

        {
            const float btn_sz  = 22.f;
            const float spacing = ImGui::GetStyle().ItemSpacing.x;
            const float avail   = ImGui::GetContentRegionAvail().x;
            const float box_w   = avail - btn_sz - spacing;

            ImGui::SetNextItemWidth(box_w);
            ImGui::InputText("##filter", m_filter_buf, sizeof(m_filter_buf));
            ImGui::SameLine();

            ImVec2 btn_pos = ImGui::GetCursorScreenPos();
            bool   clicked = ImGui::InvisibleButton("##new_collection", {btn_sz, btn_sz});
            bool   hovered = ImGui::IsItemHovered();
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("New Collection");

            {
                ImDrawList* dl  = ImGui::GetWindowDrawList();
                ImU32       col = ImGui::ColorConvertFloat4ToU32(hovered ? ImVec4(0.85f, 0.85f, 0.90f, 1.f) : ImVec4(0.55f, 0.58f, 0.65f, 1.f));

                float       x = btn_pos.x, y = btn_pos.y, s = btn_sz;
                float       m   = s * 0.12f;
                float       bx0 = x + m, by0 = y + s * 0.32f;
                float       bx1 = x + s - m, by1 = y + s - m;
                dl->AddRectFilled({bx0, by0}, {bx1, by1}, col, 2.f);

                float tx0 = bx0, ty0 = by0 - s * 0.14f;
                float tx1 = bx0 + (bx1 - bx0) * 0.45f, ty1 = by0 + 1.f;
                dl->AddRectFilled({tx0, ty0}, {tx1, ty1}, col, 2.f, ImDrawFlags_RoundCornersTopLeft | ImDrawFlags_RoundCornersTopRight);

                float cx = (bx0 + bx1) * 0.5f, cy = (by0 + by1) * 0.5f;
                float hl  = s * 0.18f;
                float thk = 1.5f;
                ImU32 bg  = ImGui::ColorConvertFloat4ToU32(ImVec4(0.14f, 0.15f, 0.18f, 1.f));
                dl->AddLine({cx - hl, cy}, {cx + hl, cy}, bg, thk + 1.5f);
                dl->AddLine({cx, cy - hl}, {cx, cy + hl}, bg, thk + 1.5f);
                dl->AddLine({cx - hl, cy}, {cx + hl, cy}, col, thk);
                dl->AddLine({cx, cy - hl}, {cx, cy + hl}, col, thk);
            }

            if (clicked)
                ZENGINE_CORE_INFO("Create collection — not yet implemented")
        }
        ImGui::Dummy({0, 3});

        static ImGuiTableFlags table_flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY;

        ImVec2                 table_size  = {0.f, ImGui::GetContentRegionAvail().y - 2.f};
        if (ImGui::BeginTable("##outliner", 3, table_flags, table_size))
        {
            ImGui::TableSetupColumn("##label", ImGuiTableColumnFlags_WidthStretch, 0.55f);
            ImGui::TableSetupColumn("##type", ImGuiTableColumnFlags_WidthStretch, 0.25f);
            ImGui::TableSetupColumn("##level", ImGuiTableColumnFlags_WidthStretch, 0.20f);

            ImGui::TableNextRow();
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.55f, 0.55f, 0.60f, 1.f));
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted("Label");
            ImGui::TableSetColumnIndex(1);
            ImGui::TextUnformatted("Type");
            ImGui::TableSetColumnIndex(2);
            ImGui::TextUnformatted("Level");
            ImGui::PopStyleColor();

            ActorHandle pending_delete = {};
            ctx->ActorManager->ForEach([&](ActorHandle h, Actor* actor) {
                const char* label = "Actor";
                auto*       nc    = actor->GetComponent<NameComponent>();
                if (nc && nc->Value[0])
                    label = nc->Value;

                if (m_filter_buf[0] != '\0' && !strstr(label, m_filter_buf))
                    return;

                const char* type = "Actor";
                if (actor->HasComponent<MeshComponent>())
                    type = "Static Mesh";
                else if (actor->HasComponent<LightComponent>())
                    type = "Light";
                else if (actor->HasComponent<CameraComponent>())
                    type = "Camera";

                bool selected = (current_scene->SelectedActorHandle.Index == h.Index && current_scene->SelectedActorHandle.Generation == h.Generation);

                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);

                ImGui::PushID((int) h.Index);
                if (ImGui::Selectable(label, selected, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap))
                    current_scene->SelectedActorHandle = h;

                if (ImGui::BeginPopupContextItem("##actor_ctx"))
                {
                    if (ImGui::MenuItem("Delete"))
                    {
                        auto* mc = actor->GetComponent<MeshComponent>();
                        if (mc && mc->RenderInstanceId != UINT32_MAX)
                            current_scene->RemoveMeshInstance(mc->RenderInstanceId);
                        if (selected)
                            current_scene->SelectedActorHandle = {};
                        pending_delete = h;
                    }
                    ImGui::EndPopup();
                }
                ImGui::PopID();

                ImGui::TableSetColumnIndex(1);
                ImGui::TextUnformatted(type);

                ImGui::TableSetColumnIndex(2);
                ImGui::TextUnformatted("Persistent");
            });

            if (pending_delete.Valid())
                ctx->ActorManager->Destroy(pending_delete);

            ImGui::EndTable();
        }

        RenderGuizmo(app, current_scene);

        ImGui::End();
    }

    void HierarchyViewUIComponent::RenderGuizmo(EditorPtr app, EditorScenePtr scene)
    {
        if (!app || !app->CameraController || !scene)
            return;

        auto* ctx = Engine::GetContext();
        if (!ctx || !ctx->ActorManager)
            return;

        ActorHandle h     = scene->SelectedActorHandle;
        Actor*      actor = ctx->ActorManager->Access(h);
        if (!actor)
            return;

        auto* tc = actor->GetComponent<TransformComponent>();
        if (!tc)
            return;

        auto camera = app->CameraController->GetCamera();
        if (!camera)
            return;

        const auto view       = camera->GetView();
        auto       projection = camera->GetProjection();
        // ImGuizmo expects OpenGL-style Y-up projection; un-flip the Vulkan Y scale.
        projection[1][1]      = -projection[1][1];

        Mat4f transform       = ComposeTransformMatrix(tc->Position, tc->Rotation, tc->Scale);

        int   gizmo_op        = app->Configuration->GizmoOperation;
        float snap_val        = 0.5f;
        bool  snapping        = IDevice::As<Keyboard>() && IDevice::As<Keyboard>()->IsKeyPressed(ZENGINE_KEY_LEFT_CONTROL, app->CurrentWindow);
        if (snapping && static_cast<ImGuizmo::OPERATION>(gizmo_op) == ImGuizmo::ROTATE)
            snap_val = 45.0f;
        float snap_arr[3] = {snap_val, snap_val, snap_val};

        if (gizmo_op > 0)
            ImGuizmo::Manipulate(value_ptr(view), value_ptr(projection), static_cast<ImGuizmo::OPERATION>(gizmo_op), ImGuizmo::MODE::WORLD, value_ptr(transform), nullptr, snapping ? snap_arr : nullptr);

        if (ImGuizmo::IsUsing())
        {
            Vec3f new_pos, new_rot, new_scale;
            if (DecomposeTransformComponent(transform, new_pos, new_rot, new_scale))
            {
                tc->PreviousPosition = tc->Position;
                tc->Position         = new_pos;
                tc->Rotation         = new_rot;
                tc->Scale            = new_scale;

                // Keep RenderScene in sync — TransformComponent and Instances are not auto-bridged yet.
                auto* mc             = actor->GetComponent<MeshComponent>();
                if (mc && mc->RenderInstanceId != UINT32_MAX)
                    scene->SetInstanceTransform(mc->RenderInstanceId, transform);
            }
        }
    }

} // namespace Tetragrama::Components
