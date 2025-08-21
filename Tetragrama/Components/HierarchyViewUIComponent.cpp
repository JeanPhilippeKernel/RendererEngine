#include <pch.h>
#include <Controllers/EditorCameraController.h>
#include <Editor.h>
#include <HierarchyViewUIComponent.h>
#include <ImGuizmo/ImGuizmo.h>
#include <ZEngine/Managers/AssetManager.h>
#include <ZEngine/Rendering/Scenes/GraphicScene.h>
#include <ZEngine/Windows/Inputs/KeyCodeDefinition.h>
#include <ZEngine/Windows/Inputs/Keyboard.h>
#include <ZEngine/Windows/Inputs/Mouse.h>
#include <glm/glm.hpp>
#include <gtc/type_ptr.hpp>

using namespace ZEngine;
using namespace ZEngine::Helpers;
using namespace ZEngine::Windows::Inputs;
using namespace ZEngine::Rendering::Scenes;

namespace Tetragrama::Components
{
    HierarchyViewUIComponent::HierarchyViewUIComponent() {}

    HierarchyViewUIComponent::~HierarchyViewUIComponent() {}

    void HierarchyViewUIComponent::Initialize(Layers::ImguiLayer* parent, const char* name, bool visibility, bool closed)
    {
        UIComponent::Initialize(parent, name, visibility, closed);
        m_node_flag = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth /* | ImGuiTreeNodeFlags_DefaultOpen*/;
    }

    void HierarchyViewUIComponent::Update(ZEngine::Core::TimeStep dt)
    {
        if (!ParentLayer)
        {
            return;
        }

        if (ParentLayer->CurrentApp)
        {
            auto window   = ParentLayer->CurrentApp->CurrentWindow;
            auto keyboard = IDevice::As<Keyboard>();

            if (keyboard && keyboard->IsKeyPressed(ZENGINE_KEY_T, window))
            {
                m_gizmo_operation = ImGuizmo::OPERATION::TRANSLATE;
            }

            if (keyboard && keyboard->IsKeyPressed(ZENGINE_KEY_R, window))
            {
                m_gizmo_operation = ImGuizmo::OPERATION::ROTATE;
            }

            if (keyboard && keyboard->IsKeyPressed(ZENGINE_KEY_S, window))
            {
                m_gizmo_operation = ImGuizmo::OPERATION::SCALE;
            }
        }

        if (ParentLayer->CurrentApp)
        {
            auto                                app           = reinterpret_cast<EditorPtr>(ParentLayer->CurrentApp);
            auto                                current_scene = reinterpret_cast<EditorScenePtr>(app->CurrentScene);

            Managers::AssetManager::AssetHandle handle        = {};
            if (current_scene->PendingOnLoadHierarchies->Pop(handle))
            {
                Importers::AssetNodeHierarchy* mesh_node_hierarchy = Managers::AssetManager::GetAsset<Importers::AssetNodeHierarchy>(handle);
                if (!mesh_node_hierarchy)
                {
                    return;
                }

                struct StackEntry
                {
                    int Parent  = -1;
                    int Depth   = -1;
                    int node_id = -1;
                };

                auto                  asset_mgr  = ZEngine::Managers::AssetManager::Instance();

                Importers::AssetMesh* mesh       = asset_mgr->GetMeshAsset(mesh_node_hierarchy->MeshUUID);
                const auto&           mesh_alloc = current_scene->CreateOrGetMeshAllocation(mesh);

                for (int i = 0; i < mesh_node_hierarchy->Hierarchies.size(); ++i)
                {
                    if (mesh_node_hierarchy->Hierarchies[i].Parent == -1)
                    {
                        std::stack<StackEntry> stack;
                        stack.push({0, 1, i});

                        while (!stack.empty())
                        {
                            auto entry = stack.top();
                            stack.pop();

                            if (entry.node_id == -1)
                            {
                                continue;
                            }

                            auto node_ref = Importers::AssetNodeRef{.NodeHierarchyIndex = entry.node_id, .AssetNodeHandle = handle, .AssetMeshUUID = mesh_node_hierarchy->MeshUUID};
                            if (mesh_node_hierarchy->NodeNames.contains(entry.node_id))
                            {
                                auto name_idx = mesh_node_hierarchy->NodeNames[entry.node_id];
                                node_ref.Name = mesh_node_hierarchy->Names[name_idx].c_str();
                            }

                            int scene_node_id = current_scene->CreateSceneNode(entry.Parent, entry.Depth, node_ref);

                            if (mesh_node_hierarchy->NodeMeshes.contains(entry.node_id))
                            {
                                const auto& submesh_id                                               = mesh_node_hierarchy->NodeMeshes.at(entry.node_id);
                                const auto& sub_mesh                                                 = mesh->SubMeshes[submesh_id];
                                auto        material_handle                                          = asset_mgr->GetMaterialHandleFromUUID(sub_mesh.MaterialUUID);
                                auto        material_index                                           = Managers::AssetManager::ReadAssetHandleIndex(material_handle);

                                current_scene->NodeSubMeshesAllocations[scene_node_id].TransformId   = scene_node_id;
                                current_scene->NodeSubMeshesAllocations[scene_node_id].MaterialId    = material_index;
                                current_scene->NodeSubMeshesAllocations[scene_node_id].IndexOffset   = mesh_alloc.IndexOffset + sub_mesh.IndexOffset;
                                current_scene->NodeSubMeshesAllocations[scene_node_id].VertexOffset  = mesh_alloc.VertexOffset + sub_mesh.VertexOffset;
                                current_scene->NodeSubMeshesAllocations[scene_node_id].VertexCount   = sub_mesh.VertexCount;
                                current_scene->NodeSubMeshesAllocations[scene_node_id].IndexCount    = sub_mesh.IndexCount;
                                current_scene->NodeSubMeshesAllocations[scene_node_id].InstanceCount = mesh_alloc.InstanceCount;
                            }

                            const auto& node         = mesh_node_hierarchy->Hierarchies[entry.node_id];
                            bool        has_children = (node.FirstChild != -1);

                            if (has_children)
                            {
                                // Push TreePop manually
                                stack.push({-1}); // Marker for TreePop

                                // Push children in reverse order
                                auto                                  scratch = ZGetScratch(&ParentLayer->LocalArena);
                                ZEngine::Core::Containers::Array<int> children;
                                children.init(scratch.Arena, 5);

                                for (int child = node.FirstChild; child != -1; child = mesh_node_hierarchy->Hierarchies[child].RightSibling)
                                {
                                    children.push(child);
                                }

                                for (int i = static_cast<int>(children.size()) - 1; i >= 0; --i)
                                {
                                    stack.push({scene_node_id, (entry.Depth + 1), children[i]});
                                }

                                ZReleaseScratch(scratch);
                            }
                        }
                    }
                }
            }
        }
    }

    void HierarchyViewUIComponent::Render(ZEngine::Rendering::Renderers::GraphicRenderer* const renderer, ZEngine::Hardwares::CommandBuffer* const command_buffer)
    {
        auto app           = reinterpret_cast<EditorPtr>(ParentLayer->CurrentApp);
        auto current_scene = reinterpret_cast<EditorScenePtr>(app->CurrentScene);

        ImGui::Begin(Name, (CanBeClosed ? &CanBeClosed : NULL), ImGuiWindowFlags_NoCollapse);
        if (ImGui::BeginPopupContextWindow(Name))
        {
            if (ImGui::MenuItem("Create Empty"))
            {
                current_scene->CreateSceneNode();
            }
            ImGui::EndPopup();
        }

        RenderTreeNodes();

        // 0 means left buttom
        if (ImGui::IsMouseDown(0) && ImGui::IsWindowHovered())
        {
            current_scene->SelectedSceneNode.store(-1, std::memory_order_release);
            // Messengers::IMessenger::SendAsync<Components::UIComponent,
            // Messengers::EmptyMessage>(EDITOR_COMPONENT_HIERARCHYVIEW_NODE_UNSELECTED, Messengers::EmptyMessage{});
        }

        RenderGuizmo();

        ImGui::End();
    }

    void HierarchyViewUIComponent::RenderTreeNodes()
    {
        auto app           = reinterpret_cast<EditorPtr>(ParentLayer->CurrentApp);
        auto current_scene = reinterpret_cast<EditorScenePtr>(app->CurrentScene);

        if (current_scene->IsDirty())
        {
            return;
        }

        for (int i = 0; i < (int) current_scene->Hierarchies.size(); ++i)
        {
            if (!current_scene->IsSceneNodeDeleted(i) && current_scene->Hierarchies[i].Parent == -1)
            {
                RenderNode(current_scene, i, current_scene->SelectedSceneNode);
            }
        }
    }

    void HierarchyViewUIComponent::RenderGuizmo()
    {
        auto app           = reinterpret_cast<EditorPtr>(ParentLayer->CurrentApp);
        auto current_scene = reinterpret_cast<EditorScenePtr>(app->CurrentScene);

        if (current_scene->IsDirty())
        {
            return;
        }

        int selected_node = current_scene->SelectedSceneNode.load(std::memory_order_acquire);
        if (selected_node == -1)
        {
            return;
        }

        if (app->CameraController)
        {
            auto       camera             = app->CameraController->GetCamera();
            const auto camera_projection  = camera->GetPerspectiveMatrix();
            const auto camera_view_matrix = camera->GetViewMatrix();

            auto&      global_transform   = current_scene->GlobalTransforms[selected_node];
            auto       initial_transform  = global_transform;
            auto&      local_transform    = current_scene->LocalTransforms[selected_node];

            if (camera && IDevice::As<Keyboard>()->IsKeyPressed(ZENGINE_KEY_F, app->CurrentWindow))
            {
                auto active_editor_camera = reinterpret_cast<Controllers::EditorCameraControllerPtr>(app->CameraController);
                active_editor_camera->SetTarget(glm::vec3(global_transform[0][3], global_transform[1][3], global_transform[2][3]));
            }

            // snapping
            float snap_value        = 0.5f;
            bool  is_snap_operation = IDevice::As<Keyboard>()->IsKeyPressed(ZENGINE_KEY_LEFT_CONTROL, app->CurrentWindow);
            if (is_snap_operation && static_cast<ImGuizmo::OPERATION>(m_gizmo_operation) == ImGuizmo::ROTATE)
            {
                snap_value = 45.0f;
            }
            float snap_array[3] = {snap_value, snap_value, snap_value};

            if (m_gizmo_operation > 0)
            {
                ImGuizmo::Manipulate(glm::value_ptr(camera_view_matrix), glm::value_ptr(camera_projection), (ImGuizmo::OPERATION) m_gizmo_operation, ImGuizmo::MODE::WORLD, glm::value_ptr(global_transform), nullptr, is_snap_operation ? snap_array : nullptr);
            }

            auto delta_transform = glm::inverse(initial_transform) * global_transform;
            local_transform      = local_transform * delta_transform;
            // current_scene->MarkSceneNodeAsChanged(m_selected_node_identifier);

            if (ImGuizmo::IsUsing())
            {
                // ZEngine::Maths::Vector3 translation, rotation, scale;
                // ZEngine::Maths::DecomposeTransformComponent(transform, translation, rotation, scale);

                // entity_transform_component.SetPosition(translation);
                // entity_transform_component.SetScaleSize(scale);
                // entity_transform_component.SetRotation(rotation);
            }
        }
    }

    void HierarchyViewUIComponent::RenderNode(EditorScene* scene, int root_id, std::atomic_int& selected_node)
    {
        struct StackEntry
        {
            int  node_id;
            bool open;
        };

        std::stack<StackEntry> stack;
        stack.push({root_id, false});

        while (!stack.empty())
        {
            auto entry = stack.top();
            stack.pop();

            // Handle manual TreePop marker
            if (entry.node_id == -1)
            {
                ImGui::TreePop();
                continue;
            }

            if (scene->IsSceneNodeDeleted(entry.node_id))
            {
                continue;
            }

            const auto&        node         = scene->Hierarchies[entry.node_id];
            auto               name_id      = scene->NodeNames.at(entry.node_id);
            bool               is_selected  = (selected_node == entry.node_id);
            bool               has_children = (node.FirstChild != -1);

            ImGuiTreeNodeFlags flags        = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanFullWidth;
            if (!has_children)
            {
                flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
            }

            if (is_selected)
            {
                flags |= ImGuiTreeNodeFlags_Selected;
            }

            auto node_id = fmt::format("SceneNode_{0}", entry.node_id);
            bool open    = ImGui::TreeNodeEx(node_id.c_str(), flags, "%s", scene->Names[name_id].c_str());

            if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
            {
                selected_node.store(entry.node_id, std::memory_order_release);
            }

            // === Drag source ===
            if (ImGui::BeginDragDropSource())
            {
                ImGui::SetDragDropPayload("NODE_DRAG", &entry.node_id, sizeof(int));
                ImGui::EndDragDropSource();
            }

            // === Drop target ===
            if (ImGui::BeginDragDropTarget())
            {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("NODE_DRAG"))
                {
                    int dragged_id = *(const int*) payload->Data;
                    if (dragged_id != entry.node_id && !scene->IsSceneNodeDeleted(dragged_id))
                    {
                        // prevent making it a child of itself or its descendants
                        int  test          = entry.node_id;
                        bool is_descendant = false;
                        while (test != -1)
                        {
                            if (test == dragged_id)
                            {
                                is_descendant = true;
                                break;
                            }
                            test = scene->Hierarchies[test].Parent;
                        }

                        if (!is_descendant)
                        {
                            scene->ReparentNode(dragged_id, entry.node_id);
                        }
                    }
                }
                ImGui::EndDragDropTarget();
            }

            if (ImGui::BeginPopupContextItem(node_id.c_str()))
            {
                if (ImGui::MenuItem("Create Empty child"))
                {
                    scene->CreateSceneNode(entry.node_id, node.DepthLevel + 1);
                }

                if (ImGui::MenuItem("Rename"))
                {
                }

                if (ImGui::MenuItem("Delete"))
                {
                    scene->RemoveSceneNode(entry.node_id);
                }
                ImGui::EndPopup();
            }

            if (open && has_children)
            {
                // Push TreePop manually
                stack.push({-1, 0}); // Marker for TreePop

                // Push children in reverse order
                auto                                  scratch = ZGetScratch(&ParentLayer->LocalArena);
                ZEngine::Core::Containers::Array<int> children;
                children.init(scratch.Arena, 5);

                for (int child = node.FirstChild; child != -1; child = scene->Hierarchies[child].RightSibling)
                {
                    if (!scene->IsSceneNodeDeleted(child))
                    {
                        children.push(child);
                    }
                }

                for (int i = static_cast<int>(children.size()) - 1; i >= 0; --i)
                {
                    stack.push({children[i], false});
                }

                ZReleaseScratch(scratch);
            }
            else if (!has_children && !(flags & ImGuiTreeNodeFlags_NoTreePushOnOpen))
            {
                // Only pop if not marked as Leaf
                ImGui::TreePop();
            }
        }
    }
} // namespace Tetragrama::Components
