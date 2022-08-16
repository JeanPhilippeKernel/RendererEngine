#include <pch.h>
#include <HierarchyViewUIComponent.h>
#include <Messenger.h>
#include <MessageToken.h>

using namespace ZEngine::Rendering::Components;
using namespace ZEngine::Rendering::Entities;
using namespace ZEngine::Inputs;

namespace Tetragrama::Components {
    HierarchyViewUIComponent::HierarchyViewUIComponent(std::string_view name, bool visibility) : UIComponent(name, visibility, false) {
        m_node_flag = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
    }

    HierarchyViewUIComponent::~HierarchyViewUIComponent() {}

    void HierarchyViewUIComponent::Update(ZEngine::Core::TimeStep dt) {

        if (auto scene = m_active_scene.lock()) {
            if (auto scene_active_window = scene->GetWindowParent()) {
                if (ZEngine::Inputs::IDevice::As<ZEngine::Inputs::Keyboard>()->IsKeyPressed(ZENGINE_KEY_W, scene_active_window)) {
                    m_gizmo_operation = ImGuizmo::OPERATION::TRANSLATE;
                }

                if (ZEngine::Inputs::IDevice::As<ZEngine::Inputs::Keyboard>()->IsKeyPressed(ZENGINE_KEY_E, scene_active_window)) {
                    m_gizmo_operation = ImGuizmo::OPERATION::ROTATE;
                }

                if (ZEngine::Inputs::IDevice::As<ZEngine::Inputs::Keyboard>()->IsKeyPressed(ZENGINE_KEY_R, scene_active_window)) {
                    m_gizmo_operation = ImGuizmo::OPERATION::SCALE;
                }
            }
        }
    }

    void HierarchyViewUIComponent::SceneAvailableMessageHandler(Messengers::GenericMessage<ZEngine::Ref<ZEngine::Rendering::Scenes::GraphicScene>>& message) {
        {
            std::unique_lock lock(m_mutex);
            m_active_scene = message.GetValue();
        }
    }

    void HierarchyViewUIComponent::EditorCameraAvailableMessageHandler(Messengers::GenericMessage<ZEngine::Ref<EditorCameraController>>& message) {
        {
            std::unique_lock lock(m_mutex);
            m_active_editor_camera = message.GetValue();
        }
    }

    void HierarchyViewUIComponent::RequestStartOrPauseRenderMessageHandler(Messengers::GenericMessage<bool>& message) {
        {
            std::unique_lock lock(m_mutex);
            m_is_allowed_to_render = message.GetValue();
        }
    }

    bool HierarchyViewUIComponent::OnUIComponentRaised(ZEngine::Components::UI::Event::UIComponentEvent&) {
        return false;
    }

    void HierarchyViewUIComponent::Render() {
        CHECK_IF_ALLOWED_TO_RENDER()

        ImGui::Begin(m_name.c_str(), (m_can_be_closed ? &m_can_be_closed : NULL), ImGuiWindowFlags_NoCollapse);

        if (!m_active_scene.expired()) {
            auto scene_ptr = m_active_scene.lock();
            scene_ptr->GetRegistry()->view<NameComponent>().each([&scene_ptr, this](entt::entity handle, NameComponent&) {
                ZEngine::Rendering::Entities::GraphicSceneEntity scene_entity(handle, scene_ptr->GetRegistry());
                RenderEntityNode(std::move(scene_entity));
            });
        }

        if (ImGui::BeginPopupContextWindow(0, 1, false)) {
            if (ImGui::MenuItem("Create Empty")) {
                if (auto scene_ptr = m_active_scene.lock()) {
                    scene_ptr->CreateEntity();
                }
            }
            ImGui::EndPopup();
        }

        // 0 means left buttom
        if (ImGui::IsMouseDown(0) && ImGui::IsWindowHovered()) {
            m_selected_scene_entity = GraphicSceneEntity(entt::null, nullptr);
            Messengers::IMessenger::SendAsync<ZEngine::Components::UI::UIComponent, Messengers::EmptyMessage>(
                EDITOR_COMPONENT_HIERARCHYVIEW_NODE_UNSELECTED, Messengers::EmptyMessage{});
        }
        ImGui::End();
    }

    void HierarchyViewUIComponent::RenderEntityNode(ZEngine::Rendering::Entities::GraphicSceneEntity&& scene_entity) {
        auto component    = scene_entity.GetComponent<NameComponent>();
        auto seleted_flag = (m_selected_scene_entity == scene_entity) ? ImGuiTreeNodeFlags_Selected : 0;
        m_is_node_opened  = ImGui::TreeNodeEx(reinterpret_cast<void*>((uint32_t) scene_entity), (m_node_flag | seleted_flag), "%s", component.Name.c_str());

        if (ImGui::IsItemClicked()) {
            m_selected_scene_entity = std::move(scene_entity);
            Messengers::IMessenger::SendAsync<ZEngine::Components::UI::UIComponent, Messengers::PointerValueMessage<GraphicSceneEntity>>(
                EDITOR_COMPONENT_HIERARCHYVIEW_NODE_SELECTED, Messengers::PointerValueMessage<GraphicSceneEntity>{&m_selected_scene_entity});
        }

        if (m_selected_scene_entity) {
            auto primary_scene_camera = m_active_editor_camera.lock()->GetCamera();
            if (primary_scene_camera) {
                const auto& camera_projection  = primary_scene_camera->GetProjectionMatrix();
                auto        camera_view_matrix = primary_scene_camera->GetViewMatrix();

                auto& entity_transform_component = m_selected_scene_entity.GetComponent<TransformComponent>();

                auto transform = entity_transform_component.GetTransform();

                // snapping
                float snap_value = 0.5f;
                bool  is_snap_operation =
                    ZEngine::Inputs::IDevice::As<ZEngine::Inputs::Keyboard>()->IsKeyPressed(ZENGINE_KEY_LEFT_CONTROL, m_active_scene.lock()->GetWindowParent());
                if (is_snap_operation && static_cast<ImGuizmo::OPERATION>(m_gizmo_operation) == ImGuizmo::ROTATE) {
                    snap_value = 45.0f;
                }
                float snap_array[3] = {snap_value, snap_value, snap_value};

                if (m_gizmo_operation > 0) {
                    ImGuizmo::Manipulate(ZEngine::Maths::value_ptr(camera_view_matrix), ZEngine::Maths::value_ptr(camera_projection), (ImGuizmo::OPERATION) m_gizmo_operation,
                        ImGuizmo::MODE::LOCAL, ZEngine::Maths::value_ptr(transform), nullptr, is_snap_operation ? snap_array : nullptr);
                }

                if (ImGuizmo::IsUsing()) {
                    ZEngine::Maths::Vector3 translation, rotation, scale;
                    ZEngine::Maths::DecomposeTransformComponent(transform, translation, rotation, scale);

                    entity_transform_component.SetPosition(translation);
                    entity_transform_component.SetScaleSize(scale);
                    entity_transform_component.SetRotation(rotation);
                }
            }
        }

        bool request_entity_removal = false;
        if (ImGui::BeginPopupContextItem()) {
            if (ImGui::MenuItem("Delete")) {
                if (m_selected_scene_entity == scene_entity) {
                    Messengers::IMessenger::SendAsync<ZEngine::Components::UI::UIComponent, Messengers::EmptyMessage>(
                        EDITOR_COMPONENT_HIERARCHYVIEW_NODE_DELETED, Messengers::EmptyMessage{});

                    m_selected_scene_entity = {entt::null, nullptr};
                }
                request_entity_removal = true;
            }
            ImGui::EndPopup();
        }

        if (m_is_node_opened) {
            ImGui::TreePop();
        }

        if (request_entity_removal) {
            if (auto scene_ptr = m_active_scene.lock()) {
                scene_ptr->RemoveEntity(scene_entity);
            }
        }
    }
} // namespace Tetragrama::Components
