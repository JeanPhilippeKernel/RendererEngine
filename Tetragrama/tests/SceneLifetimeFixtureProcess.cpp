#include <ZEngine/Core/Memory/MemoryManager.h>
#include <ZEngine/Core/VFS/VFSContext.h>
#include <ZEngine/Core/VFS/VFSDiskBackend.h>
#include <ZEngine/ECS/BuiltInSceneComponentSchemas.h>
#include <ZEngine/ECS/Components/LightComponent.h>
#include <ZEngine/ECS/Components/MeshComponent.h>
#include <ZEngine/ECS/Components/TransformComponent.h>
#include <ZEngine/ECS/Components/UUIDComponent.h>
#include <ZEngine/ECS/Scene.h>
#include <ZEngine/ECS/SceneDocument.h>
#include <ZEngine/Importers/AssetCodec.h>
#include <ZEngine/Rendering/Scenes/RenderScene.h>
#include <nlohmann/json.hpp>
#include <array>
#include <cstdio>
#include <filesystem>

using namespace ZEngine::Core::VFS;
using namespace ZEngine::Importers;
using namespace ZEngine::Importers::AssetCodec;
using ZEngine::Core::Memory::MemoryManager;
using ZEngine::ECS::Scene;
using ZEngine::ECS::SceneComponentSchemaRegistry;
using ZEngine::ECS::SceneDiagnostics;
using ZEngine::ECS::SceneDocument;
using ZEngine::ECS::SceneDocumentSettings;
namespace Components = ZEngine::ECS::Components;

namespace
{
    struct FixtureMemory
    {
        MemoryManager Value = {};
        ~FixtureMemory()
        {
            Value.Shutdown();
        }
    };
    const auto MeshId     = *uuids::uuid::from_string("419c0001-0000-4000-8000-000000000001");
    const auto MaterialId = *uuids::uuid::from_string("419c0002-0000-4000-8000-000000000001");

    bool       WriteJson(VFSContext& files, const char* name, const nlohmann::json& value)
    {
        const auto path = VFSPath::Parse(name);
        const auto text = value.dump(2);
        return path.Succeeded() && WriteFileAtomically(files, path.Value(), {reinterpret_cast<const uint8_t*>(text.data()), text.size()}).Succeeded();
    }

    bool Generate(const std::filesystem::path& root)
    {
        FixtureMemory owner  = {};
        auto&         memory = owner.Value;
        memory.Initialize(ZMega(256), {});
        auto*          arena  = &memory.MainArena;
        VFSDiskBackend disk   = {};
        VFSContext     files  = {};
        const auto     native = root.generic_u8string();
        disk.Initialize(reinterpret_cast<const char*>(native.c_str()), VFSBackendCaps::Read | VFSBackendCaps::Write, arena, 8);
        files.Initialize(arena);
        if (files.Mount(&disk, VFSPath::Root(), 0).Failed())
        {
            return false;
        }
        ImportConfiguration config = {};
        config.VFS                 = &files;
        config.AssetName.init(arena, "capacity");
        config.OutputAssetFile.init(arena, "triangle.zemesh");
        config.OutputAssetsPath.init(arena, "/Assets/Meshes");
        config.OutputMaterialPath.init(arena, "/Assets/Materials");
        AssetMaterial material = {};
        material.MaterialUUID  = MaterialId;
        material.Name.init(arena, "capacity");
        material.AlbedoColor[0]    = 1.f;
        material.AlbedoColor[1]    = .2f;
        material.AlbedoColor[2]    = .05f;
        material.AlbedoColor[3]    = 1.f;
        material.EmissiveColor[0]  = .05f;
        material.RoughnessColor[0] = .8f;
        material.AlphaMode         = ZEngine::Rendering::Materials::AlphaMode::Opaque;
        if (SerializeMaterialAssetFile(arena, material, config, 0).Failed())
        {
            return false;
        }
        AssetMesh mesh = {};
        mesh.MeshUUID  = MeshId;
        mesh.Vertices.init(arena, 24);
        const std::array<float, 24> vertices = {-.012f, -.012f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, .012f, -.012f, 0.f, 0.f, 0.f, 1.f, 1.f, 0.f, 0.f, .012f, 0.f, 0.f, 0.f, 1.f, .5f, 1.f};
        for (const auto value : vertices)
        {
            mesh.Vertices.push(value);
        }
        mesh.Indices.init(arena, 3);
        for (uint32_t index = 0; index < 3; ++index)
        {
            mesh.Indices.push(index);
        }
        mesh.SubMeshes.init(arena, 1);
        mesh.SubMeshes.push({.MaterialUUID = MaterialId, .VertexCount = 3, .IndexCount = 3, .VertexUnitStreamSize = 32, .IndexUnitStreamSize = 4, .TotalByteSize = 108});
        AssetNodeHierarchy hierarchy = {};
        hierarchy.MeshUUID           = MeshId;
        hierarchy.NodeHierarchyUUID  = *uuids::uuid::from_string("419c0003-0000-4000-8000-000000000001");
        hierarchy.Hierarchies.init(arena, 1);
        hierarchy.Hierarchies.push({.DepthLevel = 0});
        hierarchy.LocalTransforms.init(arena, 1);
        hierarchy.GlobalTransforms.init(arena, 1);
        hierarchy.LocalTransforms.push(ZEngine::Core::Maths::Identity<ZEngine::Core::Maths::Mat4f>());
        hierarchy.GlobalTransforms.push(ZEngine::Core::Maths::Identity<ZEngine::Core::Maths::Mat4f>());
        if (SerializeMeshAssetFile(arena, mesh, hierarchy, config).Failed())
        {
            return false;
        }
        AssetMesh          reopened_mesh      = {};
        AssetNodeHierarchy reopened_hierarchy = {};
        if (ReadMeshAssetFileVFS(arena, files, VFSPath::Parse("/Assets/Meshes/triangle.zemesh").Value(), &reopened_mesh, &reopened_hierarchy, MeshId).Failed() || reopened_mesh.SubMeshes.size() != 1 || reopened_mesh.Indices.size() != 3)
        {
            return false;
        }
        SceneComponentSchemaRegistry schemas     = {};
        SceneDiagnostics             diagnostics = {};
        schemas.Initialize(arena);
        if (!ZEngine::ECS::RegisterBuiltInSceneComponentSchemas(schemas, &diagnostics))
        {
            return false;
        }
        Scene scene = {};
        scene.Initialize(arena);
        const auto light    = scene.CreateEntity();
        const auto light_id = *uuids::uuid::from_string("419c0004-0000-4000-8000-000000000001");
        scene.AddComponent<Components::UUIDComponent>(light, {light_id});
        scene.AddComponent<Components::TransformComponent>(
            light,
            {
            .Rotation = {-.7f, .4f, 0.f}
        });
        scene.AddComponent<Components::LightComponent>(light, {.Intensity = 3.f});
        if (files.CreateDir(VFSPath::Parse("/Scenes").Value()).Failed())
        {
            return false;
        }
        const uint32_t cap = ZEngine::Rendering::Scenes::SceneData::MAX_DRAW_COMMANDS;
        for (uint32_t count = 0; count <= cap + 1; ++count)
        {
            if (count)
            {
                char identity[37] = {};
                std::snprintf(identity, sizeof(identity), "419c0005-0000-4000-8000-%012x", count);
                const auto entity = scene.CreateEntity();
                scene.AddComponent<Components::UUIDComponent>(entity, {*uuids::uuid::from_string(identity)});
                scene.AddComponent<Components::TransformComponent>(
                    entity,
                    {
                    .Position = {(float((count - 1) % 128) - 63.5f) * .035f, (float((count - 1) / 128) - 31.5f) * .035f, 0.f}
                });
                scene.AddComponent<Components::MeshComponent>(entity, {MeshId});
            }
            if (count != 16 && count != cap && count != cap + 1)
            {
                continue;
            }
            SceneDocumentSettings settings     = {};
            settings.UUID                      = *uuids::uuid::from_string(count == 16 ? "419c0006-0000-4000-8000-000000000001" : count == cap ? "419c0006-0000-4000-8000-000000000002" : "419c0006-0000-4000-8000-000000000003");
            settings.Name                      = count == 16 ? "small" : count == cap ? "near-limit" : "over-limit";
            settings.Grid.Enabled              = false;
            settings.Sky.Mode                  = ZEngine::Rendering::Scenes::SkyMode::SkySphere;
            settings.Sky.PrimaryCelestialLight = light_id;
            SceneDocument document             = {};
            const auto    path                 = VFSPath::Parse(("/Scenes/" + settings.Name + ".zescene").c_str());
            if (!SceneDocument::Capture(scene, settings, schemas, &document, &diagnostics) || document.EntityCount() != count + 1 || document.Save(files, path.Value(), &diagnostics).Failed())
            {
                for (uint32_t index = 0; index < diagnostics.Count; ++index)
                {
                    std::fprintf(stderr, "%s\n", diagnostics.Entries[index].Message);
                }
                return false;
            }
            SceneDocument reopened = {};
            if (!SceneDocument::Read(files, path.Value(), schemas, &reopened, &diagnostics) || reopened.EntityCount() != count + 1)
            {
                return false;
            }
        }
        const nlohmann::json project = {
            { "projectName",                                                                                                                                                                                               "Scene lifetime capacity"},
            {     "version",                                                                                                                                                                                                                 "1.0.0"},
            {"workingSpace",                                                                                                                                                                           reinterpret_cast<const char*>(native.c_str())},
            {    "sceneDir",                                                                                                                                                                                                                "Scenes"},
            {   "assetDirs", {{"meshDir", "Assets/Meshes"}, {"materialDir", "Assets/Materials"}, {"textureDir", "Assets/Textures"}, {"soundDir", "Assets/Sounds"}, {"spriteDir", "Assets/Sprites"}, {"environmentMapDir", "Assets/EnvironmentMaps"}}},
            {   "sceneList",                                                                                                                                                                              {{{"name", "small"}, {"isDefault", true}}}}
        };
        const bool written = WriteJson(files, "/project.json", project) && WriteJson(
                                                                               files,
                                                                               "/fixture.json",
                                                                               {
                                                                               {         "draw_capacity",                                                                 cap},
                                                                               {       "small_instances",                                                                  16},
                                                                               {  "near_limit_instances",                                                                 cap},
                                                                               {  "over_limit_instances",                                                             cap + 1},
                                                                               {"submeshes_per_instance",                                                                   1},
                                                                               {              "vertices",                                                                   3},
                                                                               {               "indices",                                                                   3},
                                                                               {         "material_uuid",                                        uuids::to_string(MaterialId)},
                                                                               {             "mesh_uuid",                                            uuids::to_string(MeshId)},
                                                                               {         "qualification", "Capacity fixture only; not a universal supported scene-size claim"}
        });
        scene.Shutdown();
        files.Shutdown();
        return written;
    }
} // namespace

int main(int argc, char** argv)
{
    if (argc != 2)
    {
        std::fprintf(stderr, "usage: SceneLifetimeFixtureProcess new-directory\n");
        return 2;
    }
    std::error_code error = {};
    const auto      root  = std::filesystem::absolute(argv[1], error);
    if (error || !std::filesystem::create_directory(root, error) || error)
    {
        std::fprintf(stderr, "refusing an existing or unavailable fixture directory\n");
        return 2;
    }
    return Generate(root) ? 0 : 1;
}
