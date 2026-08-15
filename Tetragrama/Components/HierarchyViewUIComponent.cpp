// clang-format off
#include <Tetragrama/Components/HierarchyViewUIComponent.h>
#include <ImGuizmo/ImGuizmo.h>
#include <Tetragrama/Editor.h>
#include <ZEngine/Core/Maths/Matrix.h>
#include <ZEngine/Engine.h>
#include <ZEngine/Windows/Inputs/KeyCodeDefinition.h>
#include <ZEngine/Windows/Inputs/Keyboard.h>
// clang-format on

using namespace ZEngine;
using namespace ZEngine::Windows::Inputs;
using namespace ZEngine::Rendering::Scenes;
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

        auto window   = ParentLayer->CurrentApp->CurrentWindow;
        auto keyboard = IDevice::As<Keyboard>();
        if (!keyboard)
            return;

        if (keyboard->IsKeyPressed(ZENGINE_KEY_T, window))
            m_gizmo_operation = ImGuizmo::OPERATION::TRANSLATE;
        if (keyboard->IsKeyPressed(ZENGINE_KEY_R, window))
            m_gizmo_operation = ImGuizmo::OPERATION::ROTATE;
        if (keyboard->IsKeyPressed(ZENGINE_KEY_S, window))
            m_gizmo_operation = ImGuizmo::OPERATION::SCALE;
    }

    void HierarchyViewUIComponent::Render(ZEngine::Rendering::Renderers::GraphicRenderer* const /*renderer*/, ZEngine::Hardwares::CommandBuffer* const /*command_buffer*/)
    {
        if (!ParentLayer || !ParentLayer->CurrentApp)
            return;

        auto* app           = reinterpret_cast<EditorPtr>(ParentLayer->CurrentApp);
        auto* current_scene = reinterpret_cast<EditorScenePtr>(app->CurrentScene);
        if (!current_scene)
            return;

        ImGui::Begin(Name, CanBeClosed ? &CanBeClosed : nullptr, ImGuiWindowFlags_NoCollapse);

        // Deselect on background click
        if (ImGui::IsMouseDown(0) && ImGui::IsWindowHovered())
            current_scene->SelectedInstanceId.value.store(-1, std::memory_order_release);

        // --- Instance list ---
        auto                                           scratch = ZGetScratch(&ParentLayer->LocalArena);
        ZEngine::Core::Containers::Array<MeshInstance> instances;
        current_scene->GetInstancesSnapshot(scratch.Arena, instances);

        for (uint32_t i = 0; i < instances.size(); ++i)
        {
            const auto& inst     = instances[i];
            bool        selected = (current_scene->SelectedInstanceId.value.load(std::memory_order_acquire) == (int32_t) inst.Id);

            ImGui::PushID((int) inst.Id);

            ImGuiSelectableFlags flags = ImGuiSelectableFlags_SpanAllColumns;
            if (ImGui::Selectable(inst.Name[0] ? inst.Name : "Unnamed Mesh", selected, flags))
                current_scene->SelectedInstanceId.value.store((int32_t) inst.Id, std::memory_order_release);

            if (ImGui::BeginPopupContextItem("##inst_ctx"))
            {
                if (ImGui::MenuItem("Delete"))
                    current_scene->RemoveMeshInstance(inst.Id);
                ImGui::EndPopup();
            }

            ImGui::PopID();
        }

        ZReleaseScratch(scratch);

        RenderGuizmo(app, current_scene);

        ImGui::End();
    }

    void HierarchyViewUIComponent::RenderGuizmo(EditorPtr app, EditorScenePtr scene)
    {
        if (!app || !app->CameraController || !scene)
            return;

        int32_t selected_id = scene->SelectedInstanceId.value.load(std::memory_order_acquire);
        if (selected_id < 0)
            return;

        // Find the selected instance by ID.
        auto                                           scratch = ZGetScratch(&ParentLayer->LocalArena);
        ZEngine::Core::Containers::Array<MeshInstance> instances;
        scene->GetInstancesSnapshot(scratch.Arena, instances);

        MeshInstance* target = nullptr;
        for (uint32_t i = 0; i < instances.size(); ++i)
        {
            if ((int32_t) instances[i].Id == selected_id)
            {
                target = &instances[i];
                break;
            }
        }

        if (!target)
        {
            ZReleaseScratch(scratch);
            return;
        }

        auto camera = app->CameraController->GetCamera();
        if (!camera)
        {
            ZReleaseScratch(scratch);
            return;
        }

        const auto view       = camera->GetView();
        auto       projection = camera->GetProjection();
        // ImGuizmo expects OpenGL-style Y-up projection. The engine uses Vulkan's Y-flipped
        // projection (negative Y scale). Un-flip it so the gizmo axes and position are correct.
        projection[1][1]      = -projection[1][1];

        auto  transform       = target->Transform;

        float snap_value      = 0.5f;
        bool  snapping        = IDevice::As<Keyboard>() && IDevice::As<Keyboard>()->IsKeyPressed(ZENGINE_KEY_LEFT_CONTROL, app->CurrentWindow);
        if (snapping && static_cast<ImGuizmo::OPERATION>(m_gizmo_operation) == ImGuizmo::ROTATE)
            snap_value = 45.0f;
        float snap_arr[3] = {snap_value, snap_value, snap_value};

        if (m_gizmo_operation > 0)
        {
            ImGuizmo::Manipulate(value_ptr(view), value_ptr(projection), static_cast<ImGuizmo::OPERATION>(m_gizmo_operation), ImGuizmo::MODE::WORLD, value_ptr(transform), nullptr, snapping ? snap_arr : nullptr);
        }

        if (ImGuizmo::IsUsing())
            scene->SetInstanceTransform(static_cast<uint32_t>(selected_id), transform);

        ZReleaseScratch(scratch);
    }

} // namespace Tetragrama::Components
