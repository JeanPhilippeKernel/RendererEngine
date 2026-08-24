#include <Tetragrama/EditorScene.h>
#include <ZEngine/ECS/Components/LightComponent.h>
#include <ZEngine/ECS/Components/MeshComponent.h>
#include <ZEngine/ECS/Components/NameComponent.h>
#include <ZEngine/ECS/Components/TransformComponent.h>
#include <ZEngine/Engine.h>
#include <ZEngine/Helpers/MemoryOperations.h>
#include <ZEngine/Importers/AssetCodec.h>
#include <ZEngine/Managers/AssetManager.h>
#include <ZEngine/Rendering/BuiltinMeshes.h>
using namespace ZEngine::Core::Containers;
using namespace ZEngine::ECS::Components;
using namespace ZEngine::Managers;
using ZEngine::Core::VFS::VFSPath;

namespace Tetragrama
{
    void EditorScene::Initialize(ZEngine::Core::Memory::ArenaAllocator* arena, cstring name)
    {
        // 200 MB carved directly from MainArena — not part of UIContext budget.
        // Covers: AssetFiles list (500 entries), scene graph data, seqlock instance buffers,
        // material/texture path strings on reload, and sub-arenas (InstanceArena 4 MB).
        // No budget config entry: EditorScene is a scene-level system, not a UI component.
        arena->CreateSubArena(ZMega(200), &LocalArena);

        Name = name;
        Sky.Mode.init(&LocalArena, "atmosphere");

        AssetFiles.init(&LocalArena, 500);
        HashToAssetFile.init(&LocalArena, 500);

        // Allocate a sub-arena for the instance list.
        LocalArena.CreateSubArena(ZMega(4), &InstanceArena);
        Instances.init(&InstanceArena, 64);

        // Spawn a default directional light so new scenes are not dark.
        // Rotation: -60° pitch (mostly downward), 30° yaw (slight horizontal angle).
        auto* ctx = ZEngine::Engine::GetContext();
        if (ctx && ctx->ActorManager)
        {
            ZEngine::Rendering::RegisterBuiltinMeshes(&LocalArena);

            const uuids::uuid light_uuid = ZEngine::Rendering::BuiltinMeshUUIDParsed(ZEngine::Rendering::BuiltinMeshID::DirectionalLightIcon);
            if (light_uuid.is_nil())
                return;

            constexpr cstring         default_light_name = "DirectionalLight";

            ZEngine::ECS::ActorHandle handle             = ctx->ActorManager->Create();
            ZEngine::ECS::Actor*      actor              = ctx->ActorManager->Access(handle);
            if (!actor)
                return;

            NameComponent nc = {};
            ZEngine::Helpers::secure_strncpy(nc.Value, sizeof(nc.Value), default_light_name, ZEngine::Helpers::secure_strlen(default_light_name));
            actor->AddComponent<NameComponent>(nc);

            TransformComponent tc = {};
            tc.Rotation.x         = -1.047f;
            tc.Rotation.y         = 0.524f;
            actor->AddComponent<TransformComponent>(tc);

            LightComponent lc = {};
            lc.LightType      = LightComponent::Type::Directional;
            lc.Intensity      = 3.f;
            lc.Color[0]       = 1.f;
            lc.Color[1]       = 1.f;
            lc.Color[2]       = 1.f;
            actor->AddComponent<LightComponent>(lc);

            uint32_t      render_id = AddMeshInstance(light_uuid, default_light_name);
            MeshComponent mc        = {};
            mc.MeshUUID             = light_uuid;
            mc.RenderInstanceId     = render_id;
            actor->AddComponent<MeshComponent>(mc);
        }
    }

    bool EditorScene::HasPendingChange() const
    {
        return HasPendingChanges.value.load(std::memory_order_acquire);
    }

    void EditorScene::PushAssetFile(const ZEngine::Importers::AssetImporterOutput& data)
    {
        if (data.Type == ZEngine::Importers::AssetFileType::UNKNOWN)
        {
            ZENGINE_CORE_WARN("{} : Invalid operation, unknown asset file type", __FUNCTION__)
            return;
        }

        EditorAssetSceneFiles asset_file = {};
        asset_file.Type                  = data.Type;
        asset_file.Hash                  = ZEngine::Core::Containers::hash_compute(data.Path.c_str());
        asset_file.Path.init(&LocalArena, data.Path.c_str());
        asset_file.RootPath.init(&LocalArena, data.RootPath.c_str());

        if (HashToAssetFile.contains(asset_file.Hash))
        {
            ZENGINE_CORE_WARN("Asset file already exists at that location : {}", asset_file.Path.c_str())
            return;
        }

        auto index = AssetFiles.size();
        AssetFiles.push(asset_file);
        HashToAssetFile.insert(asset_file.Hash, index);

        HasPendingChanges.value.store(true, std::memory_order_release);
    }

    void EditorScene::MarkDirty(bool value)
    {
        Dirty.value.store(value, std::memory_order_release);
    }

    bool EditorScene::IsDirty()
    {
        return Dirty.value.load(std::memory_order_acquire);
    }

    void EditorScene::Reset()
    {
        AssetFiles.clear();
        HashToAssetFile.clear();

        SeqBeginWrite();
        Instances.clear();
        NextInstanceId = 1;
        SeqEndWrite();
        MarkInstancesDirty();

        Dirty.value.store(false, std::memory_order_release);
    }

    void EditorScene::ExtractAsync(const EditorScene& scene)
    {
        // Compact the global geometry buffers before ingesting a new scene so
        // orphaned data from the previous scene is reclaimed starting from offset 0.
        auto* ctx = ZEngine::Engine::GetContext();
        if (ctx && ctx->RenderResourceManager)
            ctx->RenderResourceManager->ResetGeometryBuffers();

        for (const auto& file : scene.AssetFiles)
        {
            auto& f = AssetFiles.push_use({});
            f.Hash  = file.Hash;
            f.Type  = file.Type;
            f.Path.init(&LocalArena, file.Path.c_str());
            f.RootPath.init(&LocalArena, file.RootPath.c_str());
        }

        // Re-ingest cooked assets on scene load.
        // Materials are processed before meshes so their texture handles are available
        // when the mesh submeshes reference them.
        for (const auto& file : AssetFiles)
        {
            if (file.Type == ZEngine::Importers::AssetFileType::MATERIAL)
            {
                ZEngine::Importers::AssetMaterial mat{};
                auto                              path                            = ZEngine::Core::Containers::String{};
                char                              native_buf[MAX_FILE_PATH_COUNT] = {};
                VFSPath::Parse(file.Path.c_str()).Value().ResolveNative(file.RootPath.c_str(), native_buf, sizeof(native_buf));
                path.init(&LocalArena, native_buf);
                ZEngine::Importers::AssetCodec::DeserializeMaterialAssetFile(&LocalArena, path.c_str(), mat);

                // Reconstruct AssetTexture entries from the inline path fields so
                // IngestTextures can upload them to the GPU.
                ZEngine::Core::Containers::Array<ZEngine::Importers::AssetTexture> textures{};
                textures.init(&LocalArena, 5);
                auto add_tex = [&](const uuids::uuid& uuid, const ZEngine::Core::Containers::String& tex_path) {
                    if (!uuid.is_nil() && !tex_path.empty())
                    {
                        auto& t       = textures.push_use({});
                        t.TextureUUID = uuid;
                        t.Path.init(&LocalArena, tex_path.c_str());
                    }
                };
                add_tex(mat.AlbedoTexUUID, mat.AlbedoTexPath);
                add_tex(mat.EmissiveTexUUID, mat.EmissiveTexPath);
                add_tex(mat.NormalTexUUID, mat.NormalTexPath);
                add_tex(mat.OpacityTexUUID, mat.OpacityTexPath);
                add_tex(mat.SpecularTexUUID, mat.SpecularTexPath);

                AssetManager::IngestTextures(std::move(textures));
                AssetManager::IngestMaterial(std::move(mat));
            }
        }

        for (const auto& file : AssetFiles)
        {
            if (file.Type == ZEngine::Importers::AssetFileType::MESH)
            {
                ZEngine::Importers::AssetMesh          mesh{};
                ZEngine::Importers::AssetNodeHierarchy hier{};
                auto                                   path                            = ZEngine::Core::Containers::String{};
                char                                   native_buf[MAX_FILE_PATH_COUNT] = {};
                VFSPath::Parse(file.Path.c_str()).Value().ResolveNative(file.RootPath.c_str(), native_buf, sizeof(native_buf));
                path.init(&LocalArena, native_buf);
                ZEngine::Importers::AssetCodec::DeserializeMeshAssetFile(&LocalArena, path.c_str(), mesh, hier);
                AssetManager::IngestMesh(std::move(mesh), std::move(hier));
            }
        }
    }

    ZEngine::ECS::ActorHandle EditorScene::SpawnMeshActor(const uuids::uuid& mesh_uuid, const char* name)
    {
        auto* ctx = ZEngine::Engine::GetContext();
        if (!ctx || !ctx->ActorManager)
            return {};

        // Register with the render scene first to get a stable instance ID.
        uint32_t                  render_id = AddMeshInstance(mesh_uuid, name);

        // Create the Actor and wire up components.
        ZEngine::ECS::ActorHandle handle    = ctx->ActorManager->Create();
        ZEngine::ECS::Actor*      actor     = ctx->ActorManager->Access(handle);
        if (!actor)
            return {};

        NameComponent nc = {};
        ZEngine::Helpers::secure_strncpy(nc.Value, sizeof(nc.Value), name, ZEngine::Helpers::secure_strlen(name));
        actor->AddComponent<NameComponent>(nc);
        actor->AddComponent<TransformComponent>({});

        MeshComponent mc    = {};
        mc.MeshUUID         = mesh_uuid;
        mc.RenderInstanceId = render_id;
        actor->AddComponent<MeshComponent>(mc);

        return handle;
    }

} // namespace Tetragrama
