#include <pch.h>
#include <Helpers/SerializerCommonHelper.h>
#include <Helpers/ThreadPool.h>
#include <Importers/IAssetImporter.h>
#include <Serializers/EditorSceneSerializer.h>
#include <fmt/format.h>

using namespace ZEngine::Helpers;
using namespace Tetragrama::Helpers;
using namespace Tetragrama::Importers;

namespace Tetragrama::Serializers
{

    void EditorSceneSerializer::Serialize(const Ref<EditorScene>& scene)
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

            auto          full_scenename = fmt::format("{0}/{1}.zescene", m_default_output, scene->Name);
            std::ofstream out(full_scenename, std::ios::binary | std::ios::trunc | std::ios::out);
            if (!out.is_open())
            {
                out.close();

                if (m_error_callback)
                {
                    m_error_callback(Context, "Error: Unable to open file for writing.");
                }
            }

            out.seekp(std::ios::beg);

            size_t name_count = scene->Name.size();
            out.write(reinterpret_cast<const char*>(&name_count), sizeof(size_t));
            out.write(scene->Name.data(), name_count + 1);

            SerializeStringArrayData(out, scene->MeshFiles);
            REPORT_PROGRESS(Context, 0.25f)

            SerializeStringArrayData(out, scene->ModelFiles);
            REPORT_PROGRESS(Context, 0.5f)

            SerializeStringArrayData(out, scene->MaterialFiles);
            REPORT_PROGRESS(Context, 0.75f)

            std::vector<std::string> hashes = {};
            for (auto& [k, _] : scene->Data)
            {
                hashes.emplace_back(k);
            }
            SerializeStringArrayData(out, hashes);
            REPORT_PROGRESS(Context, 1.f)

            out.close();
            {
                std::unique_lock l(m_mutex);
                m_is_serializing = false;
            }

            if (m_complete_callback)
            {
                m_complete_callback(Context);
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

            EditorScene scene = {};

            if (scene_filename.empty())
            {
                {
                    std::unique_lock l(m_mutex);
                    m_is_deserializing = false;
                }

                if (m_deserialize_complete_callback)
                {
                    m_deserialize_complete_callback(Context, std::move(scene));
                }
                return;
            }

            std::ifstream in_stream(scene_filename.data(), std::ios::binary | std::ios::in);
            if (!in_stream.is_open())
            {
                in_stream.close();
                if (m_error_callback)
                {
                    m_error_callback(Context, "Error: Unable to open file for reading.");
                }
                return;
            }

            in_stream.seekg(0, std::ios::beg);

            size_t name_count;
            in_stream.read(reinterpret_cast<char*>(&name_count), sizeof(size_t));

            scene.Name.resize(name_count);
            in_stream.read(scene.Name.data(), name_count + 1);

            DeserializeStringArrayData(in_stream, scene.MeshFiles);
            REPORT_PROGRESS(Context, 0.25f)

            DeserializeStringArrayData(in_stream, scene.ModelFiles);
            REPORT_PROGRESS(Context, 0.5f)

            DeserializeStringArrayData(in_stream, scene.MaterialFiles);
            REPORT_PROGRESS(Context, 0.75f)

            std::vector<std::string> hashes = {};
            DeserializeStringArrayData(in_stream, hashes);

            for (auto& hash : hashes)
            {
                uint16_t indices[3] = {0};

                int      i          = 0;

                while (hash[i] != ':' && hash[i] != '\0')
                {
                    indices[0] = indices[0] * 10 + (hash[i] - '0');
                    i++;
                }
                i++; // Skip the colon

                while (hash[i] != ':' && hash[i] != '\0')
                {
                    indices[1] = indices[1] * 10 + (hash[i] - '0');
                    i++;
                }
                i++;

                while (hash[i] != '\0')
                {
                    indices[2] = indices[2] * 10 + (hash[i] - '0');
                    i++;
                }

                scene.Data[hash] = {.MeshFileIndex = indices[0], .ModelPathIndex = indices[1], .MaterialPathIndex = indices[2]};
            }

            REPORT_PROGRESS(Context, 1.f)

            in_stream.close();

            auto                                                  ctx    = reinterpret_cast<EditorContext*>(Context);
            const auto&                                           config = *ctx->ConfigurationPtr;

            std::vector<ZEngine::Rendering::Scenes::SceneRawData> scene_data;
            for (auto& [_, model] : scene.Data)
            {
                auto mesh_path     = fmt::format("{0}/{1}", config.WorkingSpacePath, scene.MeshFiles[model.MeshFileIndex]);
                auto model_path    = fmt::format("{0}/{1}", config.WorkingSpacePath, scene.ModelFiles[model.ModelPathIndex]);
                auto material_path = fmt::format("{0}/{1}", config.WorkingSpacePath, scene.MaterialFiles[model.MaterialPathIndex]);

#ifdef _WIN32
                std::replace(model_path.begin(), model_path.end(), '/', '\\');
                std::replace(mesh_path.begin(), mesh_path.end(), '/', '\\');
                std::replace(material_path.begin(), material_path.end(), '/', '\\');
#endif // _WIN32

                auto import_data = IAssetImporter::DeserializeImporterData(model_path, mesh_path, material_path);
                scene_data.push_back(import_data.Scene);
            }

            scene.RenderScene->SceneData->Vertices.clear();
            scene.RenderScene->SceneData->Indices.clear();
            scene.RenderScene->SceneData->Materials.clear();
            scene.RenderScene->SceneData->MaterialFiles.clear();
            scene.RenderScene->SceneData->DrawData.clear();

            scene.RenderScene->SetRootNodeName(scene.Name);
            scene.RenderScene->Merge(scene_data);
            scene.RenderScene->IsDrawDataDirty = true;

            {
                std::unique_lock l(m_mutex);
                m_is_deserializing = false;
            }

            if (m_deserialize_complete_callback)
            {
                m_deserialize_complete_callback(Context, std::move(scene));
            }
        });
    }
} // namespace Tetragrama::Serializers
