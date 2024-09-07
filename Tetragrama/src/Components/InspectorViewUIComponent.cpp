#include <pch.h>
#include <InspectorViewUIComponent.h>
#include <UIComponentDrawerHelper.h>
#include <ZEngine/Helpers/MeshHelper.h>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/matrix_decompose.hpp>

using namespace ZEngine::Rendering::Materials;
using namespace ZEngine::Rendering::Components;
using namespace ZEngine::Rendering::Textures;
using namespace ZEngine::Rendering::Lights;

namespace Tetragrama::Components
{
    InspectorViewUIComponent::InspectorViewUIComponent(std::string_view name, bool visibility) : UIComponent(name, visibility, false)
    {
        m_node_flag =
            ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_AllowItemOverlap | ImGuiTreeNodeFlags_Framed | ImGuiTreeNodeFlags_FramePadding;
    }

    InspectorViewUIComponent::~InspectorViewUIComponent() {}

    void InspectorViewUIComponent::Update(ZEngine::Core::TimeStep dt) {}

    std::future<void> InspectorViewUIComponent::SceneAvailableMessageHandlerAsync(Messengers::GenericMessage<ZEngine::Ref<ZEngine::Rendering::Scenes::GraphicScene>>& message)
    {
        {
            std::lock_guard lock(m_mutex);
            m_active_scene = message.GetValue();
        }
        co_return;
    }

    std::future<void> InspectorViewUIComponent::SceneEntitySelectedMessageHandlerAsync(Messengers::GenericMessage<ZEngine::Rendering::Scenes::SceneEntity>& message)
    {
        {
            std::lock_guard lock(m_mutex);
            m_scene_entity = std::move(message.GetValue());
        }
        co_return;
    }

    std::future<void> InspectorViewUIComponent::SceneEntityUnSelectedMessageHandlerAsync(Messengers::EmptyMessage& message)
    {
        {
            std::lock_guard lock(m_mutex);
            m_recieved_unselected_request = true;
        }
        co_return;
    }

    std::future<void> InspectorViewUIComponent::SceneEntityDeletedMessageHandlerAsync(Messengers::EmptyMessage&)
    {
        {
            std::lock_guard lock(m_mutex);
            m_recieved_deleted_request = true;
        }
        co_return;
    }

    std::future<void> InspectorViewUIComponent::RequestStartOrPauseRenderMessageHandlerAsync(Messengers::GenericMessage<bool>& message)
    {
        {
            std::lock_guard lock(m_mutex);
            m_is_allowed_to_render = message.GetValue();
        }
        co_return;
    }

    void InspectorViewUIComponent::Render()
    {
        if (m_recieved_deleted_request || m_recieved_unselected_request)
        {
            m_scene_entity                = {};
            m_recieved_deleted_request    = false;
            m_recieved_unselected_request = false;
        }

        ImGui::Begin(m_name.c_str(), (m_can_be_closed ? &m_can_be_closed : NULL), ImGuiWindowFlags_NoCollapse);

        Helpers::DrawEntityControl("Name", m_scene_entity, m_node_flag, [this] {
            ImGui::Dummy(ImVec2(0, 3));
            Helpers::DrawInputTextControl("Entity name", m_scene_entity.GetName(), [this](std::string_view value) {
                m_scene_entity.SetName(value);
            });
        });

        Helpers::DrawEntityControl("Transform", m_scene_entity, m_node_flag, [this] {
            auto transform = m_scene_entity.GetTransform();

            glm::vec3       translation, scale, skew;
            glm::qua<float> rot_quat;
            glm::vec4       perspective;
            glm::decompose(transform, scale, rot_quat, translation, skew, perspective);

            ImGui::Dummy(ImVec2(0, 3));
            Helpers::DrawVec3Control("Position", translation, [&translation](glm::vec3& value) {
                translation = value;
            });

            glm::vec3 rotation = glm::eulerAngles(rot_quat);
            ImGui::Dummy(ImVec2(0, 0.5));
            Helpers::DrawVec3Control("Rotation", rotation, [&rotation](glm::vec3& value) {
                rotation = value;
            });

            ImGui::Dummy(ImVec2(0, 0.5));
            Helpers::DrawVec3Control(
                "Scale",
                scale,
                [&scale](glm::vec3& value) {
                    scale = value;
                },
                1.0f);
        });

        // Mesh Renderer
        Helpers::DrawEntityComponentControl<MeshComponent>("Mesh Geometry", m_scene_entity, m_node_flag, true, [this](MeshComponent& component) {
            std::string type_name;
            if (auto active_scene = m_active_scene.lock())
            {
                // const auto& mesh                  = active_scene->GetMesh(component.GetMeshID());
                // const char* geometry_type_value[] = {"Custom", "Cube", "Quad", "Square"};
                // type_name                         = geometry_type_value[(int) mesh.Type];
            }

            ImGui::Dummy(ImVec2(0, 3));
            Helpers::DrawInputTextControl("Mesh", type_name, nullptr, true);
        });

        Helpers::DrawEntityComponentControl<MaterialComponent>("Materials", m_scene_entity, m_node_flag, true, [](MaterialComponent& component) {
            auto material             = component.GetMaterials()[0]; // Todo : need to be refactor to consider the collection of materials
            auto material_shader_type = material->GetShaderBuiltInType();

            const char* built_in_shader_type[] = {"Basic", "BASIC_2", "Standard"};

            auto material_name = fmt::format("{0} Material", built_in_shader_type[(int) material_shader_type]);
            ImGui::Dummy(ImVec2(0, 3));
            Helpers::DrawInputTextControl("Name", material_name, nullptr, true);

            if (material_shader_type == ZEngine::Rendering::Shaders::ShaderBuiltInType::STANDARD)
            {
                auto standard_material = reinterpret_cast<ZEngine::Rendering::Materials::StandardMaterial*>(material.get());

                ImGui::Dummy(ImVec2(0, 0.5f));

                float tile_factor = standard_material->GetTileFactor();
                Helpers::DrawDragFloatControl("Tile Factor", tile_factor, 0.2f, 0.0f, 0.0f, "%.2f", [standard_material](float value) {
                    standard_material->SetTileFactor(value);
                });
                ImGui::Dummy(ImVec2(0, 0.5f));

                float shininess = standard_material->GetShininess();
                Helpers::DrawDragFloatControl("Shininess", shininess, 0.2f, 0.0f, 0.0f, "%.2f", [standard_material](float value) {
                    standard_material->SetShininess(value);
                });
                ImGui::Dummy(ImVec2(0, 0.5f));

                // auto diffuse_tint_color = standard_material->GetDiffuseTintColor();
                // auto diffuse_texture    = standard_material->GetDiffuseMap();
                // Helpers::DrawTextureColorControl("Diffuse Map", reinterpret_cast<ImTextureID>(diffuse_texture->GetIdentifier()), diffuse_tint_color, true, nullptr,
                //     [standard_material](auto& value) { standard_material->SetDiffuseTintColor(value); });
                // ImGui::Dummy(ImVec2(0, 0.5f));

                // auto specular_tint_color = standard_material->GetSpecularTintColor();
                // auto specular_texture    = standard_material->GetSpecularMap();
                // Helpers::DrawTextureColorControl("Specular Map", reinterpret_cast<ImTextureID>(specular_texture->GetIdentifier()), specular_tint_color, true, nullptr,
                //     [standard_material](auto& value) { standard_material->SetSpecularTintColor(value); });
                // ImGui::Dummy(ImVec2(0, 0.5f));
            }
        });

        Helpers::DrawEntityComponentControl<LightComponent>("Lighting", m_scene_entity, m_node_flag, true, [this](LightComponent& component) {
            auto                            light           = component.GetLight();
            auto                            light_type      = light->GetLightType();
            std::array<std::string_view, 3> light_type_name = {"Directional", "Point", "Spot"};

            ImGui::Dummy(ImVec2(0, 3));
            Helpers::DrawInputTextControl("Type", light_type_name[static_cast<int>(light_type)], nullptr, true);

            if (light_type == LightType::DIRECTIONAL)
            {
                auto light_ptr = reinterpret_cast<DirectionalLight*>(light.get());
                auto direction = light_ptr->Direction.As<glm::vec3>();
                auto ambient   = light_ptr->Ambient.As<glm::vec3>();
                auto diffuse   = light_ptr->Diffuse.As<glm::vec3>();
                auto specular  = light_ptr->Specular.As<glm::vec3>();

                ImGui::Dummy(ImVec2(0, 0.5f));
                {
                    Helpers::DrawVec3Control("Direction", direction, [light_ptr](glm::vec3& value) {
                        light_ptr->Direction = glm::vec4(value, 1.0f);
                    });
                    ImGui::Dummy(ImVec2(0, 0.5f));

                    Helpers::DrawColorEdit3Control("Ambient", ambient, [light_ptr](glm::vec3& value) {
                        light_ptr->Ambient = glm::vec4(value, 1.0f);
                    });
                    ImGui::Dummy(ImVec2(0, 0.5f));

                    Helpers::DrawColorEdit3Control("Diffuse", diffuse, [light_ptr](glm::vec3& value) {
                        light_ptr->Diffuse = glm::vec4(value, 1.0f);
                    });
                    ImGui::Dummy(ImVec2(0, 0.5f));

                    Helpers::DrawColorEdit3Control("Specular", specular, [light_ptr](glm::vec3& value) {
                        light_ptr->Specular = glm::vec4(value, 1.0f);
                    });
                    ImGui::Dummy(ImVec2(0, 0.5f));
                }
            }

            else if (light_type == LightType::POINT)
            {
                auto transform = m_scene_entity.GetTransform();
                auto light_ptr = reinterpret_cast<PointLight*>(light.get());

                auto position = glm::vec3(transform[3]);
                auto ambient  = light_ptr->Ambient.As<glm::vec3>();
                auto diffuse  = light_ptr->Diffuse.As<glm::vec3>();
                auto specular = light_ptr->Specular.As<glm::vec3>();

                ImGui::Dummy(ImVec2(0, 0.5f));
                {
                    Helpers::DrawVec3Control("Position", position, [light_ptr](glm::vec3& value) {
                        light_ptr->Position = glm::vec4(value, 1.0f);
                    });
                    ImGui::Dummy(ImVec2(0, 0.5f));

                    Helpers::DrawColorEdit3Control("Ambient", ambient, [light_ptr](glm::vec3& value) {
                        light_ptr->Ambient = glm::vec4(value, 1.0f);
                    });
                    ImGui::Dummy(ImVec2(0, 0.5f));

                    Helpers::DrawColorEdit3Control("Diffuse", diffuse, [light_ptr](glm::vec3& value) {
                        light_ptr->Diffuse = glm::vec4(value, 1.0f);
                    });
                    ImGui::Dummy(ImVec2(0, 0.5f));

                    Helpers::DrawColorEdit3Control("Specular", specular, [light_ptr](glm::vec3& value) {
                        light_ptr->Specular = glm::vec4(value, 1.0f);
                    });
                    ImGui::Dummy(ImVec2(0, 0.5f));

                    Helpers::DrawDragFloatControl("Constant", light_ptr->Constant, 0.2f, 0.0f, 0.0f, "%.2f", [light_ptr](float value) {
                        light_ptr->Constant = value;
                    });
                    ImGui::Dummy(ImVec2(0, 0.5f));

                    Helpers::DrawDragFloatControl("Linear", light_ptr->Linear, 0.01f, 0.0f, 1.0f, "%.2f", [light_ptr](float value) {
                        light_ptr->Linear = value;
                    });
                    ImGui::Dummy(ImVec2(0, 0.5f));

                    Helpers::DrawDragFloatControl("Quadratic", light_ptr->Quadratic, 0.0001f, 0.0f, 2.0f, "%.2f", [light_ptr](float value) {
                        light_ptr->Quadratic = value;
                    });
                    ImGui::Dummy(ImVec2(0, 0.5f));
                }
            }

            else if (light_type == LightType::SPOT)
            {
                auto transform = m_scene_entity.GetTransform();
                auto light_ptr = reinterpret_cast<Spotlight*>(light.get());

                auto         direction = light_ptr->Direction.As<glm::vec3>();
                auto         position  = glm::vec3(transform[3]);
                auto         ambient   = light_ptr->Ambient.As<glm::vec3>();
                auto         diffuse   = light_ptr->Diffuse.As<glm::vec3>();
                auto         specular  = light_ptr->Specular.As<glm::vec3>();
                static float phi_angle = 12.5f;

                ImGui::Dummy(ImVec2(0, 0.5f));
                {
                    Helpers::DrawVec3Control("Position", position, [light_ptr](glm::vec3& value) {
                        light_ptr->Position = glm::vec4(value, 1.0f);
                    });
                    ImGui::Dummy(ImVec2(0, 0.5f));

                    Helpers::DrawVec3Control("Direction", direction, [light_ptr](glm::vec3& value) {
                        light_ptr->Direction = glm::vec4(value, 1.0f);
                    });
                    ImGui::Dummy(ImVec2(0, 0.5f));

                    Helpers::DrawColorEdit3Control("Ambient", ambient, [light_ptr](glm::vec3& value) {
                        light_ptr->Ambient = glm::vec4(value, 1.0f);
                    });
                    ImGui::Dummy(ImVec2(0, 0.5f));

                    Helpers::DrawColorEdit3Control("Diffuse", diffuse, [light_ptr](glm::vec3& value) {
                        light_ptr->Diffuse = glm::vec4(value, 1.0f);
                    });
                    ImGui::Dummy(ImVec2(0, 0.5f));

                    Helpers::DrawColorEdit3Control("Specular", specular, [light_ptr](glm::vec3& value) {
                        light_ptr->Specular = glm::vec4(value, 1.0f);
                    });
                    ImGui::Dummy(ImVec2(0, 0.5f));

                    Helpers::DrawDragFloatControl("CutOff", phi_angle, 0.1f, 0.0f, 360.0f, "%.2f", [light_ptr](float value) {
                        phi_angle         = value;
                        light_ptr->CutOff = glm::cos(glm::radians(value));
                    });
                    ImGui::Dummy(ImVec2(0, 0.5f));

                    Helpers::DrawDragFloatControl("Constant", light_ptr->Constant, 0.2f, 0.0f, 0.0f, "%.2f", [light_ptr](float value) {
                        light_ptr->Constant = value;
                    });
                    ImGui::Dummy(ImVec2(0, 0.5f));

                    Helpers::DrawDragFloatControl("Linear", light_ptr->Linear, 0.01f, 0.0f, 1.0f, "%.2f", [light_ptr](float value) {
                        light_ptr->Linear = value;
                    });
                    ImGui::Dummy(ImVec2(0, 0.5f));

                    Helpers::DrawDragFloatControl("Quadratic", light_ptr->Quadratic, 0.0001f, 0.0f, 2.0f, "%.2f", [light_ptr](float value) {
                        light_ptr->Quadratic = value;
                    });
                    ImGui::Dummy(ImVec2(0, 0.5f));
                }
            }
        });

        // Camera
        Helpers::DrawEntityComponentControl<CameraComponent>("Camera", m_scene_entity, m_node_flag, true, [this](CameraComponent& component) {
            // auto const camera_controller = component.GetCameraController();
            // auto       camera_type       = camera_controller->GetCamera()->GetCameraType();

            // ImGui::Dummy(ImVec2(0, 3));

            // bool is_primary_camera = component.IsPrimaryCamera;
            // if (ImGui::Checkbox("Main Camera", &is_primary_camera))
            //{
            //     component.IsPrimaryCamera = is_primary_camera;
            // }

            // if (camera_type == ZEngine::Rendering::Cameras::CameraType::PERSPECTIVE)
            //{
            //     auto  perspective_controller = reinterpret_cast<ZEngine::Controllers::PerspectiveCameraController*>(camera_controller);
            //     float camera_fov             = ZEngine::Maths::degrees(perspective_controller->GetFieldOfView());

            //    Helpers::DrawDragFloatControl(
            //        "Field Of View", camera_fov, 0.2f, -180.0f, 180.f, "%.2f",
            //        [&perspective_controller](float value) { perspective_controller->SetFieldOfView(ZEngine::Maths::radians(value)); }, 120.f);

            //    ImGui::Dummy(ImVec2(0, 3));

            //    // Clipping space
            //    if (ImGui::TreeNodeEx(reinterpret_cast<void*>(typeid(CameraComponent).hash_code() + 0x000000FF), m_node_flag, "%s", "Clipping Space"))
            //    {
            //        float camera_near = perspective_controller->GetNear();
            //        float camera_far  = perspective_controller->GetFar();

            //        Helpers::DrawDragFloatControl(
            //            "Near", camera_near, 0.2f, 0.0f, 0.0f, "%.2f", [&perspective_controller](float value) { perspective_controller->SetNear(value); });
            //        ImGui::Dummy(ImVec2(0, 0.5f));

            //        Helpers::DrawDragFloatControl(
            //            "Far", camera_far, 0.2f, 0.0f, 0.0f, "%.2f", [&perspective_controller](float value) { perspective_controller->SetFar(value); });

            //        ImGui::TreePop();
            //    }

            //    ImGui::Dummy(ImVec2(0, 3));

            //    // Camera Controller Type
            //    if (camera_controller->GetControllerType() == ZEngine::Controllers::CameraControllerType::PERSPECTIVE_ORBIT_CONTROLLER)
            //    {
            //        auto orbit_controller = reinterpret_cast<ZEngine::Controllers::OrbitCameraController*>(camera_controller);
            //        if (ImGui::TreeNodeEx(reinterpret_cast<void*>(typeid(orbit_controller).hash_code()), m_node_flag, "%s", "Controller (Orbit)"))
            //        {
            //            auto       camera       = orbit_controller->GetCamera();
            //            auto const orbit_camera = reinterpret_cast<ZEngine::Rendering::Cameras::OrbitCamera*>(camera.get());
            //            float      radius       = orbit_camera->GetRadius();
            //            float      yaw_angle    = orbit_camera->GetYawAngle();
            //            float      pitch_angle  = orbit_camera->GetPitchAngle();

            //            Helpers::DrawDragFloatControl("Radius", radius);
            //            ImGui::Dummy(ImVec2(0, 0.5f));

            //            Helpers::DrawDragFloatControl("X-axis angle", pitch_angle);
            //            ImGui::Dummy(ImVec2(0, 0.5f));

            //            Helpers::DrawDragFloatControl("Y-axis angle", yaw_angle);
            //            ImGui::TreePop();
            //        }
            //    }
            //}
        });

        // Add Components
        ImGui::Dummy(ImVec2(0, 5));

        bool should_open_popup = false;
        Helpers::DrawCenteredButtonControl("Add Components", [&should_open_popup]() {
            should_open_popup = true;
        });

        if (should_open_popup)
        {
            ImGui::OpenPopup("PopupAddComponent");
        }

        if (ImGui::BeginPopup("PopupAddComponent"))
        {
            if (ImGui::MenuItem("Geometry"))
            {
                int32_t mesh_id{-1};
                if (auto active_scene = m_active_scene.lock())
                {
                    auto cube_mesh = ZEngine::Helpers::CreateBuiltInMesh(ZEngine::Rendering::Meshes::MeshType::CUSTOM);
                    // mesh_id = active_scene->AddMesh(std::move(cube_mesh));
                }

                // m_scene_entity->AddComponent<MeshComponent>(mesh_id);
                ImGui::CloseCurrentPopup();
            }

            if (ImGui::MenuItem("Material"))
            {
                ZEngine::Ref<StandardMaterial> material = ZEngine::CreateRef<StandardMaterial>();
                material->SetTileFactor(20.f);
                material->SetShininess(10.0f);
                material->SetDiffuseMap(ZEngine::Ref<Texture>(CreateTexture(1, 1)));
                material->SetSpecularMap(ZEngine::Ref<Texture>(CreateTexture(1, 1)));

                // m_scene_entity->AddComponent<MaterialComponent>(std::move(material));
                ImGui::CloseCurrentPopup();
            }

            if (ImGui::BeginMenu("Lights"))
            {
                if (ImGui::MenuItem("Directional Light"))
                {
                    m_scene_entity.AddComponent<LightComponent>(ZEngine::CreateRef<DirectionalLight>());
                    ImGui::CloseCurrentPopup();
                }

                if (ImGui::MenuItem("Point Light"))
                {
                    m_scene_entity.AddComponent<LightComponent>(ZEngine::CreateRef<PointLight>());
                    ImGui::CloseCurrentPopup();
                }

                if (ImGui::MenuItem("Spot Light"))
                {
                    m_scene_entity.AddComponent<LightComponent>(ZEngine::CreateRef<Spotlight>());
                    ImGui::CloseCurrentPopup();
                }

                ImGui::EndMenu();
            }
            ImGui::EndPopup();
        }

        ImGui::End();
    }
} // namespace Tetragrama::Components
