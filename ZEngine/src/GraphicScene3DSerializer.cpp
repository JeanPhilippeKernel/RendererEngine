#include <pch.h>
#include <Serializers/GraphicScene3DSerializer.h>
#include <Core/Coroutine.h>
#include <Rendering/Components/TransformComponent.h>
#include <Rendering/Components/NameComponent.h>
#include <Rendering/Components/GeometryComponent.h>
#include <Rendering/Components/MaterialComponent.h>
#include <Rendering/Components/LightComponent.h>
#include <Rendering/Components/UUIComponent.h>
#include <Rendering/Geometries/CubeGeometry.h>
#include <Rendering/Textures/Texture.h>
#include <Managers/TextureManager.h>
#include <Rendering/Materials/StandardMaterial.h>
#include <Rendering/Materials/BasicMaterial.h>
#include <Rendering/Components/CameraComponent.h>
#include <Controllers/PerspectiveCameraController.h>
#include <Controllers/OrbitCameraController.h>
#include <Rendering/Lights/DirectionalLight.h>

using namespace ZEngine::Rendering::Materials;
using namespace ZEngine::Rendering::Components;
using namespace ZEngine::Rendering::Scenes;
using namespace ZEngine::Rendering::Entities;
using namespace ZEngine::Core;

namespace YAML {
    template <>
    struct convert<glm::vec3> {
        static Node encode(const glm::vec3& value) {
            Node node;
            node.push_back(value.x);
            node.push_back(value.y);
            node.push_back(value.z);
        }

        static bool decode(const Node& node, glm::vec3& value) {
            if (!node.IsSequence() || node.size() != 3) {
                return false;
            }

            value.x = node[0].as<float>();
            value.y = node[1].as<float>();
            value.z = node[2].as<float>();
            return true;
        }
    };

    template <>
    struct convert<glm::vec4> {
        static Node encode(const glm::vec4& value) {
            Node node;
            node.push_back(value.x);
            node.push_back(value.y);
            node.push_back(value.z);
            node.push_back(value.w);
        }

        static bool decode(const Node& node, glm::vec4& value) {
            if (!node.IsSequence() || node.size() != 4) {
                return false;
            }

            value.x = node[0].as<float>();
            value.y = node[1].as<float>();
            value.z = node[2].as<float>();
            value.w = node[3].as<float>();
            return true;
        }
    };
} // namespace YAML


namespace ZEngine::Serializers {
    GraphicScene3DSerializer::GraphicScene3DSerializer(const Ref<GraphicScene>& scene) : GraphicSceneSerializer() {
        m_scene                        = scene;
        auto directory                 = fmt::format("{0}/{1}", std::filesystem::current_path().string(), "Scenes");
        m_default_scene_directory_path = std::filesystem::path(directory);
    }

    SerializeInformation GraphicScene3DSerializer::Serialize(std::string_view filename) {
        YAML::Emitter        output;
        SerializeInformation serialize_info{};

        if (auto scene = m_scene.lock()) {
            output << YAML::BeginMap;
            output << YAML::Key << "Scene" << YAML::Value << "Untitled";
            output << YAML::Key << "Entities" << YAML::Value << YAML::BeginSeq;

            scene->m_entity_registry->each([&scene, this, &output](entt::entity handle) {
                GraphicSceneEntity entity = {handle, scene->GetRegistry()};
                SerializeSceneEntity(output, entity);
            });
            output << YAML::EndSeq;
            output << YAML::EndMap;
        }

        if (!std::filesystem::exists(m_default_scene_directory_path)) {
            std::filesystem::create_directory(m_default_scene_directory_path);
        }

        if (!output.good()) {
            serialize_info.IsSuccess    = false;
            serialize_info.ErrorMessage = output.GetLastError();
        } else {
            const auto full_scene_file_path = fmt::format("{0}/{1}", m_default_scene_directory_path.string(), filename);

            std::ofstream file_stream;
            file_stream.open(full_scene_file_path, std::ios::trunc | std::ios::ate);
            if (file_stream.is_open()) {
                file_stream << output.c_str();
                file_stream.close();
            }
        }

        return serialize_info;
    }

    SerializeInformation GraphicScene3DSerializer::Deserialize(std::string_view filename) {
        std::ifstream file_stream;
        const auto    full_scene_file_path = fmt::format("{0}/{1}", m_default_scene_directory_path.string(), filename);
        file_stream.open(full_scene_file_path, std::ifstream::in);
        if (!file_stream.is_open()) {
            return SerializeInformation{false, "Failed to open Scene file"};
        }
        file_stream.seekg(std::ifstream::beg);

        std::stringstream content_stream;
        content_stream << file_stream.rdbuf();

        YAML::Node scene_data = YAML::Load(content_stream);
        if (!scene_data["Scene"]) {
            return SerializeInformation{false, "Unable to load the Scene file"};
        }

        if (m_scene.expired()) {
            return SerializeInformation{false, "Unable to continue the deserialization process because the scene ref is no longer valid"};
        }

        SerializeInformation deserialization_info{};
        try {
            auto scene = m_scene.lock();

            ZENGINE_CORE_INFO("Deserializing Scene file: {0}", filename)

            auto entities = scene_data["Entities"];
            if (entities) {
                for (auto entity : entities) {
                    GraphicSceneEntity scene_entity;

                    // UUIComponent
                    auto entity_uuid = entity["Entity"].as<std::string>();

                    // NameComponent
                    std::string name;
                    auto        name_component = entity["NameComponent"];
                    if (name_component) {
                        name = name_component["Name"].as<std::string>();
                    }

                    scene_entity = scene->CreateEntity(entity_uuid, name);

                    // TransformComponent
                    auto transform_component = entity["TransformComponent"];
                    if (transform_component) {
                        auto& component = scene_entity.GetComponent<TransformComponent>();
                        component.SetPosition(transform_component["Position"].as<Maths::Vector3>());
                        component.SetRotationEulerAngles(transform_component["Rotation"].as<Maths::Vector3>());
                        component.SetScaleSize(transform_component["Scale"].as<Maths::Vector3>());
                    }

                    // GeometryComponent
                    auto geometry_component = entity["GeometryComponent"];
                    if (geometry_component) {
                        auto geometry_type = geometry_component["GeometryType"].as<int>();
                        if (geometry_type == static_cast<int>(Rendering::Geometries::GeometryType::CUBE)) {
                            scene_entity.AddComponent<GeometryComponent>(CreateRef<Rendering::Geometries::CubeGeometry>());
                        }
                    }

                    // MaterialComponent
                    auto material_component = entity["MaterialComponent"];
                    if (material_component) {
                        auto material_shader_type = material_component["MaterialShaderType"].as<int>();
                        if (material_shader_type == static_cast<int>(Rendering::Shaders::ShaderBuiltInType::STANDARD)) {
                            Ref<StandardMaterial> standard_material = CreateRef<StandardMaterial>();
                            standard_material->SetShininess(material_component["Shininess"].as<float>());
                            standard_material->SetTileFactor(material_component["TileFactor"].as<float>());
                            standard_material->SetDiffuseTintColor(material_component["DiffuseTintColor"].as<Maths::Vector4>());
                            standard_material->SetSpecularTintColor(material_component["SpecularTintColor"].as<Maths::Vector4>());

                            auto diffuse_map = material_component["DiffuseMap"];
                            if (diffuse_map["FromFile"].as<bool>()) {
                                auto texture_path = diffuse_map["TexturePath"].as<std::string>();
                                auto texture      = ZEngine::Managers::TextureManager::Load(texture_path);
                                standard_material->SetDiffuseMap(std::move(texture));
                            } else {
                                auto texture_color = diffuse_map["TextureColor"].as<Maths::Vector4>();
                                auto texture       = Ref<Rendering::Textures::Texture>(Rendering::Textures::CreateTexture(1, 1));
                                texture->SetData(texture_color.r, texture_color.g, texture_color.b, texture_color.a);
                                standard_material->SetDiffuseMap(std::move(texture));
                            }

                            auto specular_map = material_component["SpecularMap"];
                            if (specular_map["FromFile"].as<bool>()) {
                                auto texture_path = specular_map["TexturePath"].as<std::string>();
                                auto texture      = ZEngine::Managers::TextureManager::Load(texture_path);
                                standard_material->SetSpecularMap(std::move(texture));
                            } else {
                                auto texture_color = specular_map["TextureColor"].as<Maths::Vector4>();
                                auto texture       = Ref<Rendering::Textures::Texture>(Rendering::Textures::CreateTexture(1, 1));
                                texture->SetData(texture_color.r, texture_color.g, texture_color.b, texture_color.a);
                                standard_material->SetSpecularMap(std::move(texture));
                            }

                            scene_entity.AddComponent<MaterialComponent>(std::move(standard_material));
                        } else if (material_shader_type == static_cast<int>(Rendering::Shaders::ShaderBuiltInType::BASIC)) {
                            scene_entity.AddComponent<MaterialComponent>(CreateRef<BasicMaterial>());
                        }
                    }

                    // LightComponent
                    auto light_component = entity["LightComponent"];
                    if (light_component) {
                        auto light_type = light_component["LightType"].as<int>();
                        if (light_type == static_cast<int>(ZEngine::Rendering::Lights::LightType::DIRECTIONAL_LIGHT)) {
                            auto direction      = light_component["Direction"].as<Maths::Vector3>();
                            auto ambient_color  = light_component["AmbientColor"].as<Maths::Vector3>();
                            auto diffuse_color  = light_component["DiffuseColor"].as<Maths::Vector3>();
                            auto specular_color = light_component["SpecularColor"].as<Maths::Vector3>();

                            auto directional_light = CreateRef<ZEngine::Rendering::Lights::DirectionalLight>();
                            directional_light->SetAmbientColor(ambient_color);
                            directional_light->SetDiffuseColor(diffuse_color);
                            directional_light->SetSpecularColor(specular_color);
                            directional_light->SetDirection(direction);
                            scene_entity.AddComponent<LightComponent>(std::move(directional_light));
                        }
                    }

                    // CameraComponent
                    auto camera_component = entity["CameraComponent"];
                    if (camera_component) {
                        auto is_primary  = camera_component["IsPrimary"].as<bool>();
                        auto camera_type = camera_component["CameraType"].as<int>();

                        if (camera_type == static_cast<int>(Rendering::Cameras::CameraType::PERSPECTIVE)) {
                            auto aspect_ratio         = camera_component["AspectRatio"].as<float>();
                            auto field_of_view_degree = camera_component["FielOfViewDegree"].as<float>();
                            auto near                 = camera_component["Near"].as<float>();
                            auto far                  = camera_component["Far"].as<float>();

                            auto camera_controller_type = camera_component["CameraControllerType"];
                            if (!camera_controller_type) {
                                // missing CameraControllerType .. report error
                            }

                            auto controller_type = camera_controller_type["ControllerType"].as<int>();
                            if (controller_type == static_cast<int>(ZEngine::Controllers::CameraControllerType::PERSPECTIVE_ORBIT_CONTROLLER)) {
                                auto radius             = camera_controller_type["Radius"].as<float>();
                                auto yaw_angle_degree   = camera_controller_type["YawAngleDegree"].as<float>();
                                auto pitch_angle_degree = camera_controller_type["PitchAngleDegree"].as<float>();
                                auto position           = camera_controller_type["Position"].as<Maths::Vector3>();

                                auto window_parent    = scene->m_parent_window.lock();
                                auto orbit_controller = CreateRef<ZEngine::Controllers::OrbitCameraController>(window_parent, position, yaw_angle_degree, pitch_angle_degree);
                                orbit_controller->SetAspectRatio(scene->m_last_scene_requested_size.first / scene->m_last_scene_requested_size.second);
                                orbit_controller->SetNear(near);
                                orbit_controller->SetFar(far);
                                orbit_controller->SetFieldOfView(Maths::radians(field_of_view_degree));

                                auto       camera       = orbit_controller->GetCamera();
                                auto const orbit_camera = reinterpret_cast<ZEngine::Rendering::Cameras::OrbitCamera*>(camera.get());
                                orbit_camera->SetRadius(radius);

                                auto& component           = scene_entity.AddComponent<CameraComponent>(std::move(orbit_controller));
                                component.IsPrimaryCamera = is_primary;
                            }
                        }
                    }
                }
            }
        } catch (const std::exception& e) {
            deserialization_info.IsSuccess    = false;
            deserialization_info.ErrorMessage = e.what();
        }

        return deserialization_info;
    }

    void GraphicScene3DSerializer::SerializeSceneEntity(YAML::Emitter& emitter, const GraphicSceneEntity& entity) {
        emitter << YAML::BeginMap;

        SerializeSceneEntityComponent<UUIComponent>(
            emitter, entity, [](YAML::Emitter& emitter, UUIComponent& component) { emitter << YAML::Key << "Entity" << YAML::Value << uuids::to_string(component.Identifier); });

        SerializeSceneEntityComponent<NameComponent>(emitter, entity, [](YAML::Emitter& emitter, NameComponent& component) {
            emitter << YAML::Key << "NameComponent";
            emitter << YAML::BeginMap;
            emitter << YAML::Key << "Name" << YAML::Value << component.Name;
            emitter << YAML::EndMap;
        });

        SerializeSceneEntityComponent<TransformComponent>(emitter, entity, [](YAML::Emitter& emitter, TransformComponent& component) {
            emitter << YAML::Key << "TransformComponent";
            emitter << YAML::BeginMap;
            emitter << YAML::Key << "Position" << YAML::Value << component.GetPosition();
            emitter << YAML::Key << "Rotation" << YAML::Value << component.GetRotationEulerAngles();
            emitter << YAML::Key << "Scale" << YAML::Value << component.GetScaleSize();
            emitter << YAML::EndMap;
        });

        SerializeSceneEntityComponent<GeometryComponent>(emitter, entity, [](YAML::Emitter& emitter, GeometryComponent& component) {
            auto geometry = component.GetGeometry();
            emitter << YAML::Key << "GeometryComponent";
            emitter << YAML::BeginMap;
            emitter << YAML::Key << "GeometryType" << YAML::Value << static_cast<int>(geometry->GetGeometryType());
            emitter << YAML::EndMap;
        });

        SerializeSceneEntityComponent<MaterialComponent>(emitter, entity, [](YAML::Emitter& emitter, MaterialComponent& component) {
            auto material             = component.GetMaterials()[0]; // Todo : need to change to consider the list
            auto material_shader_type = material->GetShaderBuiltInType();

            emitter << YAML::Key << "MaterialComponent";
            emitter << YAML::BeginMap;
            emitter << YAML::Key << "MaterialShaderType" << YAML::Value << static_cast<int>(material_shader_type);

            if (material_shader_type == ZEngine::Rendering::Shaders::ShaderBuiltInType::STANDARD) {
                auto standard_material   = reinterpret_cast<ZEngine::Rendering::Materials::StandardMaterial*>(material.get());
                auto shininess           = standard_material->GetShininess();
                auto tile_factor         = standard_material->GetTileFactor();
                auto diffuse_tint_color  = standard_material->GetDiffuseTintColor();
                auto specular_tint_color = standard_material->GetSpecularTintColor();
                auto diffuse_map         = standard_material->GetDiffuseMap();
                auto specular_map        = standard_material->GetSpecularMap();

                emitter << YAML::Key << "Shininess" << YAML::Value << shininess;
                emitter << YAML::Key << "TileFactor" << YAML::Value << tile_factor;
                emitter << YAML::Key << "DiffuseTintColor" << YAML::Value << diffuse_tint_color;
                emitter << YAML::Key << "SpecularTintColor" << YAML::Value << specular_tint_color;

                {
                    emitter << YAML::Key << "DiffuseMap";
                    emitter << YAML::BeginMap;
                    emitter << YAML::Key << "FromFile" << YAML::Value << diffuse_map->IsFromFile();
                    if (diffuse_map->IsFromFile()) {
                        emitter << YAML::Key << "TexturePath" << YAML::Value << std::string(diffuse_map->GetFilePath());
                    } else {
                        const auto color = diffuse_map->GetData();
                        emitter << YAML::Key << "TextureColor" << YAML::Value << ZEngine::Maths::Vector4(color[0], color[1], color[2], color[3]);
                    }
                    emitter << YAML::EndMap;
                }

                {
                    emitter << YAML::Key << "SpecularMap";
                    emitter << YAML::BeginMap;
                    emitter << YAML::Key << "FromFile" << YAML::Value << specular_map->IsFromFile();
                    if (specular_map->IsFromFile()) {
                        emitter << YAML::Key << "TexturePath" << YAML::Value << std::string(specular_map->GetFilePath());
                    } else {
                        const auto color = specular_map->GetData();
                        emitter << YAML::Key << "TextureColor" << YAML::Value << ZEngine::Maths::Vector4(color[0], color[1], color[2], color[3]);
                    }
                    emitter << YAML::EndMap;
                }
            }

            if (material_shader_type == ZEngine::Rendering::Shaders::ShaderBuiltInType::BASIC) {
                auto basic_material = reinterpret_cast<ZEngine::Rendering::Materials::BasicMaterial*>(material.get());
                auto texture        = basic_material->GetTexture();
                emitter << YAML::Key << "Texture";
                emitter << YAML::BeginMap;
                emitter << YAML::Key << "FromFile" << YAML::Value << texture->IsFromFile();
                if (texture->IsFromFile()) {
                    emitter << YAML::Key << "TexturePath" << YAML::Value << std::string(texture->GetFilePath());
                } else {
                    const auto color = texture->GetData();
                    emitter << YAML::Key << "TextureColor" << YAML::Value << ZEngine::Maths::Vector4(color[0], color[1], color[2], color[3]);
                }
                emitter << YAML::EndMap;
            }

            emitter << YAML::EndMap;
        });


        SerializeSceneEntityComponent<LightComponent>(emitter, entity, [](YAML::Emitter& emitter, LightComponent& component) {
            auto light      = component.GetLight();
            auto light_type = light->GetLightType();

            emitter << YAML::Key << "LightComponent";
            emitter << YAML::BeginMap;

            if (light_type == ZEngine::Rendering::Lights::LightType::DIRECTIONAL_LIGHT) {
                auto directional_light = reinterpret_cast<ZEngine::Rendering::Lights::DirectionalLight*>(light.get());
                emitter << YAML::Key << "LightType" << YAML::Value << static_cast<int>(light_type);
                emitter << YAML::Key << "Direction" << YAML::Value << directional_light->GetDirection();
                emitter << YAML::Key << "AmbientColor" << YAML::Value << directional_light->GetAmbientColor();
                emitter << YAML::Key << "DiffuseColor" << YAML::Value << directional_light->GetDiffuseColor();
                emitter << YAML::Key << "SpecularColor" << YAML::Value << directional_light->GetSpecularColor();
            }

            emitter << YAML::EndMap;
        });

        SerializeSceneEntityComponent<CameraComponent>(emitter, entity, [](YAML::Emitter& emitter, CameraComponent& component) {
            auto const camera_controller = component.GetCameraController();
            auto       camera            = component.GetCamera();
            auto       is_primary        = component.IsPrimaryCamera;
            auto       camera_type       = camera->GetCameraType();

            emitter << YAML::Key << "CameraComponent";
            emitter << YAML::BeginMap;
            emitter << YAML::Key << "IsPrimary" << YAML::Value << is_primary;
            emitter << YAML::Key << "CameraType" << YAML::Value << static_cast<int>(camera_type);

            if (camera_type == Rendering::Cameras::CameraType::PERSPECTIVE) {
                auto  perspective_controller = reinterpret_cast<ZEngine::Controllers::PerspectiveCameraController*>(camera_controller);
                float camera_fov             = ZEngine::Maths::degrees(perspective_controller->GetFieldOfView());
                float camera_near            = perspective_controller->GetNear();
                float camera_far             = perspective_controller->GetFar();
                float aspect_ratio           = perspective_controller->GetAspectRatio();

                emitter << YAML::Key << "AspectRatio" << YAML::Value << aspect_ratio;
                emitter << YAML::Key << "FielOfViewDegree" << YAML::Value << camera_fov;
                emitter << YAML::Key << "Near" << YAML::Value << camera_near;
                emitter << YAML::Key << "Far" << YAML::Value << camera_far;

                auto camera_controller_type = camera_controller->GetControllerType();
                emitter << YAML::Key << "CameraControllerType";
                if (camera_controller_type == ZEngine::Controllers::CameraControllerType::PERSPECTIVE_ORBIT_CONTROLLER) {
                    auto       orbit_controller = reinterpret_cast<ZEngine::Controllers::OrbitCameraController*>(perspective_controller);
                    auto const orbit_camera     = reinterpret_cast<ZEngine::Rendering::Cameras::OrbitCamera*>(camera.get());
                    float      radius           = orbit_camera->GetRadius();
                    float      yaw_angle        = orbit_camera->GetYawAngle();
                    float      pitch_angle      = orbit_camera->GetPitchAngle();
                    auto       position         = orbit_camera->GetPosition();
                    emitter << YAML::BeginMap;
                    emitter << YAML::Key << "ControllerType" << YAML::Value << static_cast<int>(camera_controller_type);
                    emitter << YAML::Key << "Radius" << YAML::Value << radius;
                    emitter << YAML::Key << "YawAngleDegree" << YAML::Value << yaw_angle;
                    emitter << YAML::Key << "PitchAngleDegree" << YAML::Value << pitch_angle;
                    emitter << YAML::Key << "Position" << YAML::Value << position;
                    emitter << YAML::EndMap;
                }
            }

            emitter << YAML::EndMap;
        });

        emitter << YAML::EndMap;
    }

} // namespace ZEngine::Serializers
