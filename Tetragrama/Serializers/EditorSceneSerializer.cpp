#include <Tetragrama/Editor.h>
#include <Tetragrama/Serializers/EditorSceneSerializer.h>
#include <ZEngine/Core/Containers/Array.h>
#include <ZEngine/Core/VFS/IVFSContext.h>
#include <ZEngine/Engine.h>
#include <ZEngine/Helpers/SerializerCommonHelper.h>
#include <ZEngine/Helpers/ThreadPool.h>
#include <ZEngine/Importers/AssetCodec.h>
#include <ZEngine/Importers/IAssetImporter.h>
#include <ZEngine/Managers/AssetManager.h>
#include <fmt/format.h>
#include <filesystem>
#include <fstream>

using namespace ZEngine::Helpers;
using namespace ZEngine::Core::Containers;
using ZEngine::Core::VFS::VFSPath;

namespace Tetragrama::Serializers
{
    void EditorSceneSerializer::Serialize(EditorScene* const scene)
    {
        if (!scene || m_is_serializing.exchange(true, std::memory_order_acq_rel))
            return;

        m_pending_serialize_scene = scene;
        if (!ThreadPoolHelper::Submit(this, &EditorSceneSerializer::RunSerializeTask))
        {
            m_pending_serialize_scene = nullptr;
            m_is_serializing.store(false, std::memory_order_release);
        }
    }

    void EditorSceneSerializer::RunSerializeTask(void* context)
    {
        auto* serializer = static_cast<EditorSceneSerializer*>(context);
        serializer->SerializeOnWorker(serializer->m_pending_serialize_scene);
    }

    void EditorSceneSerializer::SerializeOnWorker(EditorScene* const scene)
    {
        std::unique_lock l(m_mutex);
        Arena.Clear();

        if (m_default_output.empty())
        {
            m_pending_serialize_scene = nullptr;
            m_is_serializing.store(false, std::memory_order_release);
            return;
        }

        std::string scene_filename                      = std::string(scene->Name) + ".zescene";
        char        full_scenename[MAX_FILE_PATH_COUNT] = {};
        VFSPath::Parse(scene_filename.c_str()).Value().ResolveNative(m_default_output.c_str(), full_scenename, sizeof(full_scenename));
        std::ofstream out(full_scenename, std::ios::binary | std::ios::trunc | std::ios::out);
        if (!out.is_open())
        {
            if (m_error_callback)
                m_error_callback(Context, "Error: Unable to open file for writing.");
            m_pending_serialize_scene = nullptr;
            m_is_serializing.store(false, std::memory_order_release);
            return;
        }

        out.seekp(std::ios::beg);

        // Lazy cook: find mesh instances that are in RAM (from drag-drop)
        // but have no cooked .zemesh artifact on disk. Cook them now so
        // the saved scene can be reloaded on the next session.
        {
            auto                                                                       scratch = ZGetScratch(&Arena);
            ZEngine::Core::Containers::Array<ZEngine::Rendering::Scenes::MeshInstance> instances;
            scene->GetInstancesSnapshot(scratch.Arena, instances);

            auto*       mgr = ZEngine::Managers::AssetManager::Instance();
            auto*       app = reinterpret_cast<EditorPtr>(Context);
            const auto& cfg = app ? *app->Configuration : EditorConfiguration{};

            for (uint32_t i = 0; i < instances.size(); ++i)
            {
                if (!mgr || !mgr->Registry)
                    break;
                const uuids::uuid& uid = instances[i].MeshUUID;
                const auto*        rec = mgr->Registry->FindByUUID(uid);
                if (!rec || rec->Meta.ArtifactPath[0] != '\0')
                    continue; // already cooked or not found

                // In-memory mesh with no .zemesh — cook it now
                auto* mesh      = mgr->GetMeshAsset(uid);
                auto* hierarchy = mgr->GetMeshNodeHierarchy(uid);
                if (!mesh || !hierarchy)
                    continue;

                std::string                                         asset_name  = instances[i].Name[0] ? instances[i].Name : "UnknownMesh";
                std::string                                         output_file = asset_name + ".zemesh";

                ZEngine::Importers::AssetCodec::ImportConfiguration cook_cfg    = {};
                cook_cfg.OutputWorkingSpacePath.init(scratch.Arena, cfg.WorkingSpacePath.c_str());
                cook_cfg.OutputTextureFilesPath.init(scratch.Arena, cfg.TexturePath.c_str());
                cook_cfg.OutputAssetsPath.init(scratch.Arena, cfg.MeshPath.c_str());
                cook_cfg.OutputMaterialPath.init(scratch.Arena, cfg.MaterialPath.c_str());
                cook_cfg.AssetName.init(scratch.Arena, asset_name.c_str());
                cook_cfg.OutputAssetFile.init(scratch.Arena, output_file.c_str());
                cook_cfg.InputBaseAssetFilePath.init(scratch.Arena, cfg.WorkingSpacePath.c_str());
                cook_cfg.VFS = reinterpret_cast<ZEngine::Core::VFS::IVFSContext*>(ZEngine::Engine::GetContext()->VFS);

                auto output  = ZEngine::Importers::AssetCodec::SerializeMeshAssetFile(scratch.Arena, *mesh, *hierarchy, cook_cfg);
                if (!output.Path.empty())
                {
                    // Register the artifact path so future saves don't re-cook
                    ZEngine::Helpers::secure_strncpy(const_cast<ZEngine::Core::VFS::AssetRecord*>(rec)->Meta.ArtifactPath, MAX_FILE_PATH_COUNT, output.Path.c_str(), output.Path.size());

                    scene->PushAssetFile(output);
                }
            }
            ZReleaseScratch(scratch);
        }

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

        // Sky configuration
        WriteBinaryString(out, scene->Sky.Mode.empty() ? "atmosphere" : scene->Sky.Mode.c_str());
        WriteBinaryString(out, scene->Sky.EnvironmentMap.empty() ? "" : scene->Sky.EnvironmentMap.c_str());

        // Serialize mesh instances (seqlock snapshot).
        {
            auto                                                                       scratch = ZGetScratch(&Arena);
            ZEngine::Core::Containers::Array<ZEngine::Rendering::Scenes::MeshInstance> instances;
            scene->GetInstancesSnapshot(scratch.Arena, instances);
            WriteBinary(out, static_cast<uint32_t>(instances.size()));
            for (uint32_t i = 0; i < instances.size(); ++i)
            {
                WriteBinary(out, instances[i].MeshUUID);
                WriteBinary(out, instances[i].Transform);
                WriteBinary(out, instances[i].Name);
            }
            ZReleaseScratch(scratch);
        }

        out.close();

        if (m_complete_callback)
        {
            m_complete_callback(Context);
        }

        m_pending_serialize_scene = nullptr;
        m_is_serializing.store(false, std::memory_order_release);
    }

    void EditorSceneSerializer::Deserialize(cstring filename)
    {
        if (m_is_deserializing.exchange(true, std::memory_order_acq_rel))
            return;

        ZEngine::Helpers::secure_strcpy(m_pending_deserialize_filename, sizeof(m_pending_deserialize_filename), filename ? filename : "");
        if (!ThreadPoolHelper::Submit(this, &EditorSceneSerializer::RunDeserializeTask))
            m_is_deserializing.store(false, std::memory_order_release);
    }

    void EditorSceneSerializer::RunDeserializeTask(void* context)
    {
        auto* serializer = static_cast<EditorSceneSerializer*>(context);
        serializer->DeserializeOnWorker(serializer->m_pending_deserialize_filename);
    }

    void EditorSceneSerializer::DeserializeOnWorker(cstring scene_filename)
    {
        std::unique_lock l(m_mutex);
        Arena.Clear();

        if (!scene_filename || scene_filename[0] == '\0')
        {
            if (m_deserialize_complete_callback)
            {
                m_deserialize_complete_callback(Context, std::make_unique<EditorScene>());
            }

            m_is_deserializing.store(false, std::memory_order_release);
            return;
        }

        std::ifstream in_stream(scene_filename, std::ios::binary | std::ios::in);
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

        in_stream.seekg(0, std::ios::end);
        const std::streamoff file_size = in_stream.tellg();
        in_stream.seekg(std::ios::beg);
        if (file_size < 0 || !in_stream.good())
        {
            in_stream.close();
            if (m_error_callback)
                m_error_callback(Context, "Error: Invalid scene file.");
            m_is_deserializing.store(false, std::memory_order_release);
            return;
        }

        const auto reject_file = [&](cstring message) {
            in_stream.close();
            if (m_error_callback)
                m_error_callback(Context, message);
            m_is_deserializing.store(false, std::memory_order_release);
        };
        constexpr size_t kMaxSceneStringLength = MAX_FILE_PATH_COUNT - 1;

        REPORT_LOG(Context, "Reading checksum information...")

        uint32_t scene_magic   = 0;
        uint32_t scene_version = 0;
        if (!ReadBinary(in_stream, scene_magic) || !ReadBinary(in_stream, scene_version))
        {
            reject_file("Error: Invalid or truncated scene file.");
            return;
        }

        if (scene_magic != ZESCENE_MAGIC || scene_version != SCENE_FILE_VERSION)
        {
            reject_file("Error: Invalid scene file, unknown format");
            return;
        }

        auto scene = std::make_unique<EditorScene>();
        if (!scene->InitializeDeserialized(Arena.m_mem_page_size))
        {
            if (m_error_callback)
                m_error_callback(Context, "Error: unable to allocate persistent scene storage.");
            m_is_deserializing.store(false, std::memory_order_release);
            return;
        }

        REPORT_LOG(Context, "Extracting scene asset files...")

        size_t           asset_file_count        = 0;
        constexpr size_t kMinimumAssetRecordSize = sizeof(ZEngine::Importers::AssetFileType) + sizeof(uint64_t) + (2 * sizeof(size_t));
        if (!ReadBinary(in_stream, asset_file_count) || asset_file_count > static_cast<size_t>(file_size) / kMinimumAssetRecordSize)
        {
            reject_file("Error: Invalid or truncated scene file.");
            return;
        }
        scene->AssetFiles.reserve(asset_file_count);
        if (asset_file_count > 0)
            scene->HashToAssetFile.reserve(asset_file_count * 2);

        for (size_t i = 0; i < asset_file_count; ++i)
        {
            EditorAssetSceneFiles file = {};
            if (!ReadBinary(in_stream, file.Type) || !ReadBinary(in_stream, file.Hash) || !ReadBinaryString(&scene->LocalArena, in_stream, file.Path, kMaxSceneStringLength) || !ReadBinaryString(&scene->LocalArena, in_stream, file.RootPath, kMaxSceneStringLength) || file.Type > ZEngine::Importers::AssetFileType::ENVIRONMENT_MAP)
            {
                reject_file("Error: Invalid or truncated scene file.");
                return;
            }
            scene->HashToAssetFile.insert(file.Hash, static_cast<uint32_t>(i));
            scene->AssetFiles.push(std::move(file));
        }

        REPORT_LOG(Context, "Extracting scene name...")

        String scene_name = {};
        if (!ReadBinaryCString(&scene->LocalArena, in_stream, scene_name, kMaxSceneStringLength))
        {
            reject_file("Error: Invalid or truncated scene file.");
            return;
        }
        scene->Name = scene_name.c_str();

        // Sky configuration
        if (!ReadBinaryCString(&scene->LocalArena, in_stream, scene->Sky.Mode, kMaxSceneStringLength) || !ReadBinaryCString(&scene->LocalArena, in_stream, scene->Sky.EnvironmentMap, kMaxSceneStringLength))
        {
            reject_file("Error: Invalid or truncated scene file.");
            return;
        }

        REPORT_LOG(Context, "Extracting mesh instances...")

        uint32_t         instance_count          = 0;
        constexpr size_t kSerializedInstanceSize = sizeof(uuids::uuid) + sizeof(ZEngine::Core::Maths::Mat4f) + sizeof(char[128]);
        if (!ReadBinary(in_stream, instance_count) || instance_count > static_cast<size_t>(file_size) / kSerializedInstanceSize)
        {
            reject_file("Error: Invalid or truncated scene file.");
            return;
        }
        if (instance_count > 0)
        {
            scene->Instances.reserve(instance_count);
            for (uint32_t i = 0; i < instance_count; ++i)
            {
                uuids::uuid                 uuid;
                ZEngine::Core::Maths::Mat4f transform;
                char                        name[128] = {};
                if (!ReadBinary(in_stream, uuid) || !ReadBinary(in_stream, transform) || !ReadBinary(in_stream, name))
                {
                    reject_file("Error: Invalid or truncated scene file.");
                    return;
                }

                uint32_t id = scene->AddMeshInstance(uuid, name);
                scene->SetInstanceTransform(id, transform);
            }
        }

        if (m_deserialize_complete_callback)
        {
            m_deserialize_complete_callback(Context, std::move(scene));
        }

        m_is_deserializing.store(false, std::memory_order_release);
    }
} // namespace Tetragrama::Serializers
