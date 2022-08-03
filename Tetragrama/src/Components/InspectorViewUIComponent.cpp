#include <pch.h>
#include <InspectorViewUIComponent.h>

using namespace ZEngine::Rendering::Components;

namespace Tetragrama::Components {
    InspectorViewUIComponent::InspectorViewUIComponent(std::string_view name, bool visibility) : UIComponent(name, visibility) {}

    InspectorViewUIComponent::~InspectorViewUIComponent() {}

    void InspectorViewUIComponent::Update(ZEngine::Core::TimeStep dt) {}

    bool InspectorViewUIComponent::OnUIComponentRaised(ZEngine::Components::UI::Event::UIComponentEvent&) {
        return false;
    }

    void InspectorViewUIComponent::SceneEntitySelectedMessageHandler(Messengers::PointerValueMessage<ZEngine::Rendering::Entities::GraphicSceneEntity>& message) {
        m_scene_entity = message.GetValue();
    }

    void InspectorViewUIComponent::SceneEntityUnSelectedMessageHandler(Messengers::EmptyMessage& message) {
        m_scene_entity = nullptr;
    }

    void InspectorViewUIComponent::Render() {
        ImGui::Begin(m_name.c_str(), &m_visibility, ImGuiWindowFlags_NoCollapse);

        if (m_scene_entity) {
            if (m_scene_entity->HasComponent<NameComponent>()) {

                if (ImGui::TreeNodeEx(reinterpret_cast<void*>(typeid(NameComponent).hash_code()), ImGuiTreeNodeFlags_DefaultOpen, "%s", "Name")) {
                    auto& component = m_scene_entity->GetComponent<NameComponent>();

                    char buffer[1024];
                    memset(buffer, 0, sizeof(buffer));
                    auto raw_entity_name = component.Name.c_str();
                    strncpy(buffer, raw_entity_name, strlen(raw_entity_name));

                    if (ImGui::InputText("Entity name", buffer, sizeof(buffer))) {
                        component.Name = std::string(buffer);
                    }

                    ImGui::TreePop();
                }
            }

            if (m_scene_entity->HasComponent<TransformComponent>()) {

                if (ImGui::TreeNodeEx(reinterpret_cast<void*>(typeid(TransformComponent).hash_code()), ImGuiTreeNodeFlags_DefaultOpen, "%s", "Transform")) {
                    auto& component = m_scene_entity->GetComponent<TransformComponent>();

                    auto position = component.GetPosition();
                    if (ImGui::DragFloat3("Position", glm::value_ptr(position), 0.1f)) {
                        component.SetPosition(position);
                    }

                    auto rotation_axis = component.GetRotationAxis();
                    if (ImGui::DragFloat3("Rotation", glm::value_ptr(rotation_axis), 0.1f)) {
                        component.SetRotationAxis(rotation_axis);
                    }

                    auto scale_size = component.GetScaleSize();
                    if (ImGui::DragFloat3("Scale", glm::value_ptr(scale_size), 0.1f)) {
                        component.SetScaleSize(scale_size);
                    }

                    ImGui::TreePop();
                }
            }

            // Mesh Renderer
            if (m_scene_entity->HasComponent<GeometryComponent>()) {

                if (ImGui::TreeNodeEx(reinterpret_cast<void*>(typeid(GeometryComponent).hash_code()), ImGuiTreeNodeFlags_DefaultOpen, "%s", "Mesh Geometry")) {
                    auto& component     = m_scene_entity->GetComponent<GeometryComponent>();
                    auto  geometry_type = component.GetGeometry()->GetGeometryType();

                    const char* geometry_type_value[] = {"Custom", "Cube", "Quad", "Square"};

                    char buffer[16];
                    memset(buffer, 0, sizeof(buffer));
                    auto type_name = geometry_type_value[(int) geometry_type];
                    strncpy(buffer, type_name, strlen(type_name));

                    ImGui::InputText("Mesh", buffer, sizeof(buffer), ImGuiInputTextFlags_ReadOnly);
                    ImGui::TreePop();
                }
            }

            if (m_scene_entity->HasComponent<MaterialComponent>()) {

                if (ImGui::TreeNodeEx(reinterpret_cast<void*>(typeid(MaterialComponent).hash_code()), ImGuiTreeNodeFlags_DefaultOpen, "%s", "Material")) {
                    ImGui::TreePop();
                }
            }

            // Camera
            if (m_scene_entity->HasComponent<CameraComponent>()) {

                if (ImGui::TreeNodeEx(reinterpret_cast<void*>(typeid(CameraComponent).hash_code()), ImGuiTreeNodeFlags_DefaultOpen, "%s", "Camera")) {
                    auto& component = m_scene_entity->GetComponent<CameraComponent>();

                    auto const camera_controller = component.GetCameraController();
                    auto       camera_type       = camera_controller->GetCamera()->GetCameraType();

                    bool is_primary_camera = component.IsPrimaryCamera;
                    if (ImGui::Checkbox("Main Camera", &is_primary_camera)) {
                        component.IsPrimaryCamera = is_primary_camera;
                    }

                    if (camera_type == ZEngine::Rendering::Cameras::CameraType::PERSPECTIVE) {
                        auto  perspective_controller = reinterpret_cast<ZEngine::Controllers::PerspectiveCameraController*>(camera_controller);
                        float camera_fov             = ZEngine::Maths::degrees(perspective_controller->GetFieldOfView());
                        if (ImGui::DragFloat("Field of View", &camera_fov, 0.2f, -180.0f, 180.f, "%.2f")) {
                            perspective_controller->SetFieldOfView(ZEngine::Maths::radians(camera_fov));
                        }

                        // Clipping space
                        if (ImGui::TreeNodeEx(reinterpret_cast<void*>(typeid(CameraComponent).hash_code() + 0x000000FF), ImGuiTreeNodeFlags_DefaultOpen, "%s", "Clipping Space")) {
                            float camera_near = perspective_controller->GetNear();
                            float camera_far  = perspective_controller->GetFar();

                            if (ImGui::DragFloat("Near", &camera_near, 1.0f, 0.0f, 0.0f, "%.2f")) {
                                perspective_controller->SetNear(camera_near);
                            }

                            if (ImGui::DragFloat("Far", &camera_far, 0.2f, 0.0f, 0.0f, "%.2f")) {
                                perspective_controller->SetFar(camera_far);
                            }
                            ImGui::TreePop();
                        }

                        // Camera Controller Type
                        if (auto orbit_controller = dynamic_cast<ZEngine::Controllers::OrbitCameraController*>(camera_controller)) {
                            if (ImGui::TreeNodeEx(reinterpret_cast<void*>(typeid(orbit_controller).hash_code()), ImGuiTreeNodeFlags_DefaultOpen, "%s", "Controller (Orbit)")) {
                                auto       camera       = orbit_controller->GetCamera();
                                auto const orbit_camera = reinterpret_cast<ZEngine::Rendering::Cameras::OrbitCamera*>(camera.get());
                                float      radius       = orbit_camera->GetRadius();
                                float      yaw_angle    = orbit_camera->GetYawAngle();
                                float      pitch_angle  = orbit_camera->GetPitchAngle();

                                ImGui::DragFloat("Radius", &radius);
                                ImGui::DragFloat("X-axis angle", &pitch_angle);
                                ImGui::DragFloat("Y-axis angle", &yaw_angle);
                                ImGui::TreePop();
                            }
                        }
                    }
                    ImGui::TreePop();
                }
            }
        }

        ImGui::End();
    }
} // namespace Tetragrama::Components
