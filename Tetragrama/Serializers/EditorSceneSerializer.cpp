#include <pch.h>
#include <Helpers/SerializerCommonHelper.h>
#include <Helpers/ThreadPool.h>
#include <Importers/IAssetImporter.h>
#include <Serializers/EditorSceneSerializer.h>
#include <ZEngine/Core/Container/Array.h>
#include <fmt/format.h>

using namespace ZEngine::Helpers;
using namespace ZEngine::Core::Container;
using namespace Tetragrama::Helpers;
using namespace Tetragrama::Importers;

namespace Tetragrama::Serializers
{
    void EditorSceneSerializer::Serialize(ZRawPtr(EditorScene) const scene)
    {
        if (!scene)
        {
            return;
        }

        ThreadPoolHelper::Submit([this, scene] {
            std::unique_lock l(m_mutex);
            m_is_serializing.store(true, std::memory_order_release);
            Arena.Clear();

            if (m_default_output.empty())
            {
                m_is_serializing.store(false, std::memory_order_release);
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

            size_t name_count = ZEngine::Helpers::secure_strlen(scene->Name);
            out.write(reinterpret_cast<const char*>(&name_count), sizeof(size_t));
            out.write(scene->Name, name_count + 1);

            SerializeStringArrayData(out, ArrayView<String>(scene->MeshFiles));
            REPORT_PROGRESS(Context, 0.25f)

            SerializeStringArrayData(out, ArrayView<String>(scene->ModelFiles));
            REPORT_PROGRESS(Context, 0.5f)

            SerializeStringArrayData(out, ArrayView<String>(scene->MaterialFiles));
            REPORT_PROGRESS(Context, 0.75f)

            Array<String> hashes = {};
            hashes.init(&Arena, scene->Data.size());

            for (auto& [k, _] : scene->Data)
            {
                String s;
                s.init(&Arena, k);
                hashes.push(s);
            }
            SerializeStringArrayData(out, ArrayView<String>(hashes));
            REPORT_PROGRESS(Context, 1.f)

            out.close();

            if (m_complete_callback)
            {
                m_complete_callback(Context);
            }

            m_is_serializing.store(false, std::memory_order_release);
        });
    }

    void EditorSceneSerializer::Deserialize(std::string_view filename)
    {
        ThreadPoolHelper::Submit([this, scene_filename = std::string(filename)] {
            std::unique_lock l(m_mutex);

            m_is_deserializing.store(true, std::memory_order_release);
            Arena.Clear();

            EditorScene scene = {};
            scene.Initialize(&Arena);

            if (scene_filename.empty())
            {
                if (m_deserialize_complete_callback)
                {
                    m_deserialize_complete_callback(Context, std::move(scene));
                }

                m_is_deserializing.store(false, std::memory_order_release);
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
                m_is_deserializing.store(false, std::memory_order_release);
                return;
            }

            in_stream.seekg(0, std::ios::beg);

            String name = {};
            size_t name_count;
            in_stream.read(reinterpret_cast<char*>(&name_count), sizeof(size_t));
            name.init(&Arena, name_count + 1);
            in_stream.read(name.data(), name_count + 1);

            scene.Name = name.data();

            DeserializeStringArrayData(&Arena, in_stream, scene.MeshFiles);
            REPORT_PROGRESS(Context, 0.25f)

            DeserializeStringArrayData(&Arena, in_stream, scene.ModelFiles);
            REPORT_PROGRESS(Context, 0.5f)

            DeserializeStringArrayData(&Arena, in_stream, scene.MaterialFiles);
            REPORT_PROGRESS(Context, 0.75f)

            Array<String> hashes = {};
            DeserializeStringArrayData(&Arena, in_stream, hashes);

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

                scene.Data[hash.c_str()] = {.MeshFileIndex = indices[0], .ModelPathIndex = indices[1], .MaterialPathIndex = indices[2]};
            }

            REPORT_PROGRESS(Context, 1.f)

            in_stream.close();

            auto                                                  ctx    = reinterpret_cast<EditorContext*>(Context);
            const auto&                                           config = *ctx->ConfigurationPtr;

            std::vector<ZEngine::Rendering::Scenes::SceneRawData> scene_data;
            for (auto& [_, model] : scene.Data)
            {
                auto mesh_path     = fmt::format("{0}/{1}", config.WorkingSpacePath, scene.MeshFiles[model.MeshFileIndex].c_str());
                auto model_path    = fmt::format("{0}/{1}", config.WorkingSpacePath, scene.ModelFiles[model.ModelPathIndex].c_str());
                auto material_path = fmt::format("{0}/{1}", config.WorkingSpacePath, scene.MaterialFiles[model.MaterialPathIndex].c_str());

#ifdef _WIN32
                std::replace(model_path.begin(), model_path.end(), '/', '\\');
                std::replace(mesh_path.begin(), mesh_path.end(), '/', '\\');
                std::replace(material_path.begin(), material_path.end(), '/', '\\');
#endif // _WIN32

                auto import_data = AssetImporter->DeserializeImporterData(&Arena, model_path, mesh_path, material_path);
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

            if (m_deserialize_complete_callback)
            {
                m_deserialize_complete_callback(Context, std::move(scene));
            }

            m_is_deserializing.store(false, std::memory_order_release);
        });
    }
} // namespace Tetragrama::Serializers
