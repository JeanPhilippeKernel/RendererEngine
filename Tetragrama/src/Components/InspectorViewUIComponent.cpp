#include <pch.h>
#include <InspectorViewUIComponent.h>
#include <UIComponentDrawerHelper.h>

using namespace ZEngine::Rendering::Materials;
using namespace ZEngine::Rendering::Components;

namespace Tetragrama::Components {
    InspectorViewUIComponent::InspectorViewUIComponent(std::string_view name, bool visibility) : UIComponent(name, visibility) {
        m_node_flag =
            ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_AllowItemOverlap | ImGuiTreeNodeFlags_Framed | ImGuiTreeNodeFlags_FramePadding;
    }

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

    void InspectorViewUIComponent::SceneEntityDeletedMessageHandler(Messengers::EmptyMessage&) {
        m_scene_entity = nullptr;
    }

    void InspectorViewUIComponent::Render() {
        ImGui::Begin(m_name.c_str(), &m_visibility, ImGuiWindowFlags_NoCollapse);

        if (m_scene_entity) {
            Helpers::DrawEntityComponentControl<NameComponent>("Name", *m_scene_entity, m_node_flag, false, [](NameComponent& component) {
                ImGui::Dummy(ImVec2(0, 3));
                Helpers::DrawInputTextControl("Entity name", component.Name, [&component](std::string_view value) { component.Name = value; });
            });

            Helpers::DrawEntityComponentControl<TransformComponent>("Transform", *m_scene_entity, m_node_flag, false, [](TransformComponent& component) {
                ImGui::Dummy(ImVec2(0, 3));
                auto position = component.GetPosition();
                Helpers::DrawVec3Control("Position", position, [&component](ZEngine::Maths::Vector3& value) { component.SetPosition(value); });

                auto rotation_axis = component.GetRotationAxis();
                Helpers::DrawVec3Control("Rotation", rotation_axis, [&component](ZEngine::Maths::Vector3& value) { component.SetRotationAxis(value); });

                auto scale_size = component.GetScaleSize();
                Helpers::DrawVec3Control(
                    "Scale", scale_size, [&component](ZEngine::Maths::Vector3& value) { component.SetScaleSize(value); }, 1.0f);
            });

            // Mesh Renderer
            Helpers::DrawEntityComponentControl<GeometryComponent>("Mesh Geometry", *m_scene_entity, m_node_flag, true, [](GeometryComponent& component) {
                auto geometry_type = component.GetGeometry()->GetGeometryType();

                const char* geometry_type_value[] = {"Custom", "Cube", "Quad", "Square"};
                auto        type_name             = geometry_type_value[(int) geometry_type];

                ImGui::Dummy(ImVec2(0, 3));
                Helpers::DrawInputTextControl("Mesh", type_name, nullptr, true);
            });

            Helpers::DrawEntityComponentControl<MaterialComponent>("Materials", *m_scene_entity, m_node_flag, true, [](MaterialComponent& component) {
                auto material             = component.GetMaterial();
                auto material_shader_type = material->GetShaderBuiltInType();

                const char* built_in_shader_type[] = {"Basic", "Standard"};

                auto material_name = fmt::format("{0} Material", built_in_shader_type[(int) material_shader_type]);
                ImGui::Dummy(ImVec2(0, 3));
                Helpers::DrawInputTextControl("Name", material_name, nullptr, true);

                if (material_shader_type == ZEngine::Rendering::Shaders::ShaderBuiltInType::STANDARD) {
                    auto standard_material = reinterpret_cast<ZEngine::Rendering::Materials::StandardMaterial*>(material.get());

                    ImGui::Dummy(ImVec2(0, 0.5f));

                    float tile_factor = standard_material->GetTileFactor();
                    Helpers::DrawDragFloatControl(
                        "Tile Factor", tile_factor, 0.2f, 0.0f, 0.0f, "%.2f", [standard_material](float value) { standard_material->SetTileFactor(value); });
                    ImGui::Dummy(ImVec2(0, 0.5f));

                    float shininess = standard_material->GetShininess();
                    Helpers::DrawDragFloatControl("Shininess", shininess, 0.2f, 0.0f, 0.0f, "%.2f", [standard_material](float value) { standard_material->SetShininess(value); });
                    ImGui::Dummy(ImVec2(0, 0.5f));

                    auto tint_color      = standard_material->GetTintColor();
                    auto diffuse_texture = standard_material->GetDiffuseMap();
                    Helpers::DrawTextureColorControl("Diffuse Map", reinterpret_cast<ImTextureID>(diffuse_texture->GetIdentifier()), tint_color, true, nullptr,
                        [standard_material](auto& value) { standard_material->SetTintColor(value); });
                    ImGui::Dummy(ImVec2(0, 0.5f));

                    auto specular_texture    = standard_material->GetSpecularMap();
                    auto specular_tint_color = ZEngine::Maths::Vector4{1, 1, 1, 1};
                    Helpers::DrawTextureColorControl("Specular Map", reinterpret_cast<ImTextureID>(specular_texture->GetIdentifier()), specular_tint_color);
                    ImGui::Dummy(ImVec2(0, 0.5f));
                }
            });

            // Camera
            Helpers::DrawEntityComponentControl<CameraComponent>("Camera", *m_scene_entity, m_node_flag, true, [this](CameraComponent& component) {
                auto const camera_controller = component.GetCameraController();
                auto       camera_type       = camera_controller->GetCamera()->GetCameraType();

                ImGui::Dummy(ImVec2(0, 3));

                bool is_primary_camera = component.IsPrimaryCamera;
                if (ImGui::Checkbox("Main Camera", &is_primary_camera)) {
                    component.IsPrimaryCamera = is_primary_camera;
                }

                if (camera_type == ZEngine::Rendering::Cameras::CameraType::PERSPECTIVE) {
                    auto  perspective_controller = reinterpret_cast<ZEngine::Controllers::PerspectiveCameraController*>(camera_controller);
                    float camera_fov             = ZEngine::Maths::degrees(perspective_controller->GetFieldOfView());

                    Helpers::DrawDragFloatControl(
                        "Field Of View", camera_fov, 0.2f, -180.0f, 180.f, "%.2f",
                        [&perspective_controller](float value) { perspective_controller->SetFieldOfView(ZEngine::Maths::radians(value)); }, 120.f);

                    ImGui::Dummy(ImVec2(0, 3));

                    // Clipping space
                    if (ImGui::TreeNodeEx(reinterpret_cast<void*>(typeid(CameraComponent).hash_code() + 0x000000FF), m_node_flag, "%s", "Clipping Space")) {
                        float camera_near = perspective_controller->GetNear();
                        float camera_far  = perspective_controller->GetFar();

                        Helpers::DrawDragFloatControl(
                            "Near", camera_near, 0.2f, 0.0f, 0.0f, "%.2f", [&perspective_controller](float value) { perspective_controller->SetNear(value); });
                        ImGui::Dummy(ImVec2(0, 0.5f));

                        Helpers::DrawDragFloatControl(
                            "Far", camera_far, 0.2f, 0.0f, 0.0f, "%.2f", [&perspective_controller](float value) { perspective_controller->SetFar(value); });

                        ImGui::TreePop();
                    }

                    ImGui::Dummy(ImVec2(0, 3));

                    // Camera Controller Type
                    if (auto orbit_controller = dynamic_cast<ZEngine::Controllers::OrbitCameraController*>(camera_controller)) {
                        if (ImGui::TreeNodeEx(reinterpret_cast<void*>(typeid(orbit_controller).hash_code()), m_node_flag, "%s", "Controller (Orbit)")) {
                            auto       camera       = orbit_controller->GetCamera();
                            auto const orbit_camera = reinterpret_cast<ZEngine::Rendering::Cameras::OrbitCamera*>(camera.get());
                            float      radius       = orbit_camera->GetRadius();
                            float      yaw_angle    = orbit_camera->GetYawAngle();
                            float      pitch_angle  = orbit_camera->GetPitchAngle();

                            Helpers::DrawDragFloatControl("Radius", radius);
                            ImGui::Dummy(ImVec2(0, 0.5f));

                            Helpers::DrawDragFloatControl("X-axis angle", pitch_angle);
                            ImGui::Dummy(ImVec2(0, 0.5f));

                            Helpers::DrawDragFloatControl("Y-axis angle", yaw_angle);
                            ImGui::TreePop();
                        }
                    }
                }
            });

            // Add Components
            ImGui::Dummy(ImVec2(0, 5));

            bool should_open_popup = false;
            Helpers::DrawCenteredButtonControl("Add Components", [&should_open_popup]() { should_open_popup = true; });

            if (should_open_popup) {
                ImGui::OpenPopup("PopupAddComponent");
            }

            if (ImGui::BeginPopup("PopupAddComponent")) {
                if (ImGui::MenuItem("Geometry")) {
                    m_scene_entity->AddComponent<GeometryComponent>(ZEngine::CreateRef<ZEngine::Rendering::Geometries::CubeGeometry>());
                    ImGui::CloseCurrentPopup();
                }

                if (ImGui::MenuItem("Material")) {
                    ZEngine::Ref<StandardMaterial> material = ZEngine::CreateRef<StandardMaterial>();
                    material->SetTileFactor(20.f);
                    material->SetShininess(10.0f);
                    material->SetDiffuseMap(ZEngine::Rendering::Textures::CreateTexture(1, 1));
                    material->SetSpecularMap(ZEngine::Rendering::Textures::CreateTexture(1, 1));

                    m_scene_entity->AddComponent<MaterialComponent>(std::move(material));
                    ImGui::CloseCurrentPopup();
                }

                ImGui::EndPopup();
            }
        }

        ImGui::End();
    }
} // namespace Tetragrama::Components
