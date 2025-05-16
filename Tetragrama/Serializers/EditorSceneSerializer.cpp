#include <pch.h>
#include <Helpers/SerializerCommonHelper.h>
#include <Helpers/ThreadPool.h>
#include <Importers/IAssetImporter.h>
#include <Serializers/EditorSceneSerializer.h>
#include <ZEngine/Core/Containers/Array.h>
#include <fmt/format.h>

using namespace ZEngine::Helpers;
using namespace ZEngine::Core::Containers;
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

        ThreadPoolHelper::Submit([this, scene = scene] {
            std::unique_lock l(m_mutex);
            m_is_serializing.store(true, std::memory_order_release);
            Arena.Clear();

            if (m_default_output.empty())
            {
                m_is_serializing.store(false, std::memory_order_release);
                return;
            }

            auto          full_scenename = fmt::format("{0}{1}{2}.zescene", m_default_output, PLATFORM_OS_BACKSLASH, scene->Name);
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

            WriteBinary(out, ZESCENE_MAGIC);
            WriteBinary(out, SCENE_FILE_VERSION);

            WriteBinary(out, scene->AssetFiles.size());
            for (auto& file : scene->AssetFiles)
            {
                WriteBinary(out, file.Type);
                WriteBinary(out, file.Hash);
                WriteBinaryString(out, file.Path);
                WriteBinaryString(out, file.RootPath);
            }

            WriteBinaryString(out, scene->Name);
            WriteBinaryArray(out, ArrayView{scene->Names});
            WriteBinaryArray(out, ArrayView{scene->Hierarchies});
            WriteBinaryArray(out, ArrayView{scene->LocalTransforms});
            WriteBinaryArray(out, ArrayView{scene->GlobalTransforms});

            WriteBinaryHashMap(out, scene->NodeMeshes);
            WriteBinaryHashMap(out, scene->NodeNames);

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

            in_stream.seekg(std::ios::beg);

            REPORT_LOG(Context, "Reading checksum information...")

            uint32_t scene_magic;
            uint32_t scene_version;
            ReadBinary(in_stream, scene_magic);
            ReadBinary(in_stream, scene_version);

            if (scene_magic != ZESCENE_MAGIC && scene_version != SCENE_FILE_VERSION)
            {
                in_stream.close();
                if (m_error_callback)
                {
                    m_error_callback(Context, "Error: Invalid scene file, unknown format");
                }
                m_is_deserializing.store(false, std::memory_order_release);
                return;
            }

            REPORT_LOG(Context, "Extracting scene asset files...")

            size_t asset_file_count;
            ReadBinary(in_stream, asset_file_count);
            scene.AssetFiles.init(&Arena, asset_file_count);

            for (int i = 0; i < asset_file_count; ++i)
            {
                auto& file = scene.AssetFiles.push_use({});
                ReadBinary(in_stream, file.Type);
                ReadBinary(in_stream, file.Hash);
                ReadBinaryString(&Arena, in_stream, file.Path);
                ReadBinaryString(&Arena, in_stream, file.RootPath);
            }

            REPORT_LOG(Context, "Extracting scene name...")

            char buf[DEFAULT_STR_BUFFER] = {0};
            ReadBinaryCString(&Arena, in_stream, buf);
            scene.Name = buf;

            REPORT_LOG(Context, "Extracting scene's hierarchies info...")

            ReadBinaryArray(&Arena, in_stream, scene.Names);
            ReadBinaryArray(&Arena, in_stream, scene.Hierarchies);
            ReadBinaryArray(&Arena, in_stream, scene.LocalTransforms);
            ReadBinaryArray(&Arena, in_stream, scene.GlobalTransforms);

            ReadHashMap(&Arena, in_stream, scene.NodeMeshes);
            ReadHashMap(&Arena, in_stream, scene.NodeNames);

            if (m_deserialize_complete_callback)
            {
                m_deserialize_complete_callback(Context, std::move(scene));
            }

            m_is_deserializing.store(false, std::memory_order_release);
        });
    }
} // namespace Tetragrama::Serializers
