#include <Tetragrama/Editor.h>
#include <Tetragrama/Serializers/EditorSceneSerializer.h>
#include <ZEngine/Core/Containers/Array.h>
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

                    auto output = ZEngine::Importers::AssetCodec::SerializeMeshAssetFile(scratch.Arena, *mesh, *hierarchy, cook_cfg);
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

            m_is_serializing.store(false, std::memory_order_release);
        });
    }

    void EditorSceneSerializer::Deserialize(cstring filename)
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
            scene.Name                            = buf;

            // Sky configuration
            char sky_mode_buf[DEFAULT_STR_BUFFER] = {0};
            char sky_env_buf[DEFAULT_STR_BUFFER]  = {0};
            ReadBinaryCString(&Arena, in_stream, sky_mode_buf);
            ReadBinaryCString(&Arena, in_stream, sky_env_buf);
            scene.Sky.Mode.init(&Arena, sky_mode_buf);
            if (sky_env_buf[0] != '\0')
                scene.Sky.EnvironmentMap.init(&Arena, sky_env_buf);

            REPORT_LOG(Context, "Extracting mesh instances...")

            uint32_t instance_count = 0;
            ReadBinary(in_stream, instance_count);
            if (instance_count > 0)
            {
                // Initialize instance storage in the serializer's scratch arena.
                Arena.CreateSubArena(ZMega(4), &scene.InstanceArena);
                scene.Instances.init(&scene.InstanceArena, instance_count);
                for (uint32_t i = 0; i < instance_count; ++i)
                {
                    uuids::uuid                 uuid;
                    ZEngine::Core::Maths::Mat4f transform;
                    char                        name[128] = {};
                    ReadBinary(in_stream, uuid);
                    ReadBinary(in_stream, transform);
                    ReadBinary(in_stream, name);

                    uint32_t id = scene.AddMeshInstance(uuid, name);
                    scene.SetInstanceTransform(id, transform);
                }
            }

            if (m_deserialize_complete_callback)
            {
                m_deserialize_complete_callback(Context, std::move(scene));
            }

            m_is_deserializing.store(false, std::memory_order_release);
        });
    }
} // namespace Tetragrama::Serializers
