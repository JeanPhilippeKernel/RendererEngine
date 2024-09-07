#include <pch.h>
#include <Helpers/ThreadPool.h>
#include <Serializers/EditorSceneSerializer.h>
#include <fmt/format.h>

using namespace ZEngine::Helpers;

namespace Tetragrama::Serializers
{

    void EditorSceneSerializer::Serialize(const ZEngine::Ref<EditorScene>& scene)
    {
        ThreadPoolHelper::Submit([this, scene] {
            {
                std::unique_lock l(m_mutex);
                m_is_serializing = true;
            }

            if (m_default_output.empty())
            {
                {
                    std::unique_lock l(m_mutex);
                    m_is_serializing = false;
                }
                return;
            }

            auto          scene_name     = scene->GetName();
            const auto&   scene_models   = scene->GetModels();
            auto          full_scenename = fmt::format("{0}/{1}.zescene", m_default_output, scene_name);
            std::ofstream out(full_scenename, std::ios::binary | std::ios::trunc | std::ios::out);
            if (!out.is_open())
            {
                out.close();

                if (m_error_callback)
                {
                    m_error_callback("Error: Unable to open file for writing.");
                }
            }

            size_t total_size = sizeof(size_t) * 2;
            for (const auto& [name, model] : scene_models)
            {
                // For each model: name size, model properties sizes, and name itself
                total_size += sizeof(size_t) + (model.MeshesPath.size() + 1) + (model.MaterialsPath.size() + 1) + (model.ModelPath.size() + 1) + model.Name.size();
            }

            size_t byte_written = 0;

            size_t name_size = scene_name.size();
            {
                out.write(reinterpret_cast<const char*>(&name_size), sizeof(size_t));
                byte_written += sizeof(size_t);
                REPORT_PROGRESS(static_cast<float>(byte_written) / total_size * 100.0f)

                out.write(scene_name.data(), name_size + 1);
                byte_written += (name_size + 1);
                REPORT_PROGRESS(static_cast<float>(byte_written) / total_size * 100.0f)
            }

            // Write number of models
            size_t models_size = scene_models.size();
            out.write(reinterpret_cast<const char*>(&models_size), sizeof(size_t));
            byte_written += sizeof(size_t);
            REPORT_PROGRESS(static_cast<float>(byte_written) / total_size * 100.0f)

            // Write models
            for (const auto& [name, model] : scene_models)
            {
                // Write model name
                size_t model_name_size = model.Name.size();
                {
                    out.write(reinterpret_cast<const char*>(&model_name_size), sizeof(size_t));
                    byte_written += sizeof(size_t);
                    REPORT_PROGRESS(static_cast<float>(byte_written) / total_size * 100.0f)

                    out.write(model.Name.data(), model_name_size + 1);
                    byte_written += (model_name_size + 1);
                    REPORT_PROGRESS(static_cast<float>(byte_written) / total_size * 100.0f)
                }

                // Write model properties
                size_t mesh_path_size = model.MeshesPath.size();
                {
                    out.write(reinterpret_cast<const char*>(&mesh_path_size), sizeof(size_t));
                    byte_written += sizeof(size_t);
                    REPORT_PROGRESS(static_cast<float>(byte_written) / total_size * 100.0f)

                    out.write(model.MeshesPath.data(), mesh_path_size + 1);
                    byte_written += (mesh_path_size + 1);
                    REPORT_PROGRESS(static_cast<float>(byte_written) / total_size * 100.0f)
                }

                size_t material_path_size = model.MaterialsPath.size();
                {
                    out.write(reinterpret_cast<const char*>(&material_path_size), sizeof(size_t));
                    byte_written += sizeof(size_t);
                    REPORT_PROGRESS(static_cast<float>(byte_written) / total_size * 100.0f)

                    out.write(model.MaterialsPath.data(), material_path_size + 1);
                    byte_written += (material_path_size + 1);
                    REPORT_PROGRESS(static_cast<float>(byte_written) / total_size * 100.0f)
                }

                size_t model_path_size = model.ModelPath.size();
                {
                    out.write(reinterpret_cast<const char*>(&model_path_size), sizeof(size_t));
                    byte_written += sizeof(size_t);
                    REPORT_PROGRESS(static_cast<float>(byte_written) / total_size * 100.0f)

                    out.write(model.ModelPath.data(), model_path_size + 1);
                    byte_written += (model_path_size + 1);
                    REPORT_PROGRESS(static_cast<float>(byte_written) / total_size * 100.0f)
                }
            }

            out.close();
            {
                std::unique_lock l(m_mutex);
                m_is_serializing = false;
            }

            if (m_complete_callback)
            {
                m_complete_callback();
            }
        });
    }

    void EditorSceneSerializer::Deserialize(std::string_view filename)
    {
        ThreadPoolHelper::Submit([this, scene_filename = std::string(filename)] {
            {
                std::unique_lock l(m_mutex);
                m_is_deserializing = true;
            }

            EditorScene deserialized_scene;
            if (scene_filename.empty())
            {
                {
                    std::unique_lock l(m_mutex);
                    m_is_deserializing = false;
                }

                if (m_deserialize_complete_callback)
                {
                    m_deserialize_complete_callback(std::move(deserialized_scene));
                }
                return;
            }

            std::ifstream in_stream(scene_filename.data(), std::ios::binary | std::ios::in);
            if (!in_stream.is_open())
            {
                in_stream.close();
                if (m_error_callback)
                {
                    m_error_callback("Error: Unable to open file for reading.");
                }
                return;
            }

            size_t total_size = 0;
            in_stream.seekg(0, std::ios::end);
            total_size = in_stream.tellg();
            in_stream.seekg(0, std::ios::beg);

            size_t byte_read = 0;

            size_t      scene_name_size = 0;
            std::string scene_name;
            {
                in_stream.read(reinterpret_cast<char*>(&scene_name_size), sizeof(size_t));
                byte_read += sizeof(size_t);
                REPORT_PROGRESS(static_cast<float>(byte_read) / total_size * 100.0f)

                scene_name.resize(scene_name_size);
                in_stream.read(scene_name.data(), scene_name_size + 1);
                byte_read += (scene_name_size + 1);
                REPORT_PROGRESS(static_cast<float>(byte_read) / total_size * 100.0f)
            }
            deserialized_scene.SetName(scene_name);

            size_t model_count = 0;
            {
                in_stream.read(reinterpret_cast<char*>(&model_count), sizeof(size_t));
                byte_read += sizeof(size_t);
                REPORT_PROGRESS(static_cast<float>(byte_read) / total_size * 100.0f)

                for (size_t i = 0; i < model_count; ++i)
                {
                    EditorScene::Model model = {};

                    size_t model_name_size{0};
                    {
                        in_stream.read(reinterpret_cast<char*>(&model_name_size), sizeof(size_t));
                        byte_read += sizeof(size_t);
                        REPORT_PROGRESS(static_cast<float>(byte_read) / total_size * 100.0f)

                        model.Name.resize(model_name_size);
                        in_stream.read(model.Name.data(), model_name_size + 1);
                        byte_read += (model_name_size + 1);
                        REPORT_PROGRESS(static_cast<float>(byte_read) / total_size * 100.0f)
                    }

                    size_t mesh_path_size{0};
                    {
                        in_stream.read(reinterpret_cast<char*>(&mesh_path_size), sizeof(size_t));
                        byte_read += sizeof(size_t);
                        REPORT_PROGRESS(static_cast<float>(byte_read) / total_size * 100.0f)

                        model.MeshesPath.resize(mesh_path_size);
                        in_stream.read(model.MeshesPath.data(), mesh_path_size + 1);
                        byte_read += (mesh_path_size + 1);
                        REPORT_PROGRESS(static_cast<float>(byte_read) / total_size * 100.0f)
                    }

                    size_t material_path_size{0};
                    {
                        in_stream.read(reinterpret_cast<char*>(&material_path_size), sizeof(size_t));
                        byte_read += sizeof(size_t);
                        REPORT_PROGRESS(static_cast<float>(byte_read) / total_size * 100.0f)

                        model.MaterialsPath.resize(material_path_size);
                        in_stream.read(model.MaterialsPath.data(), material_path_size + 1);
                        byte_read += (material_path_size + 1);
                        REPORT_PROGRESS(static_cast<float>(byte_read) / total_size * 100.0f)
                    }

                    size_t model_path_size{0};
                    {
                        in_stream.read(reinterpret_cast<char*>(&model_path_size), sizeof(size_t));
                        byte_read += sizeof(size_t);
                        REPORT_PROGRESS(static_cast<float>(byte_read) / total_size * 100.0f)

                        model.ModelPath.resize(model_path_size);
                        in_stream.read(model.ModelPath.data(), model_path_size + 1);
                        byte_read += (model_path_size + 1);
                        REPORT_PROGRESS(static_cast<float>(byte_read) / total_size * 100.0f)
                    }

                    deserialized_scene.m_models[model.Name] = std::move(model);
                }
            }

            in_stream.close();
            {
                std::unique_lock l(m_mutex);
                m_is_deserializing = false;
            }

            if (m_deserialize_complete_callback)
            {
                m_deserialize_complete_callback(std::move(deserialized_scene));
            }
        });
    }
} // namespace Tetragrama::Serializers
