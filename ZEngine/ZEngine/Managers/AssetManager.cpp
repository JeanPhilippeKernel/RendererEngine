#include <ZEngine/Core/VFS/Meta/MetaFileIO.h>
#include <ZEngine/Core/VFS/Registry/AssetRegistry.h>
#include <ZEngine/Helpers/IntrusivePtr.h>
#include <ZEngine/Helpers/MemoryOperations.h>
#include <ZEngine/Importers/AssetCodec.h>
#include <ZEngine/Importers/ImportCoordinator.h>
#include <ZEngine/Managers/AssetManager.h>
#include <ZEngine/Rendering/Meshes/Mesh.h>
#include <ZEngine/Rendering/RenderResourceManager.h>
#include <algorithm>
#include <cmath>
#include <limits>
#include <new>
#include <random>
#include <type_traits>

using namespace ZEngine::Core::Containers;
using namespace ZEngine::Core::Memory;
using namespace ZEngine::Core::VFS;
using namespace ZEngine::Importers;

namespace ZEngine::Managers
{
    static AssetManager* s_Instance = nullptr;

    AssetManager*        AssetManager::Instance()
    {
        return s_Instance;
    }

    AssetHandle AssetManager::CreateHandle(uint32_t index, AssetType type)
    {
        return ((uint32_t(type) & 0xF) << 28) | (index & 0x0FFFFFFF);
    }

    uint32_t AssetManager::ReadAssetHandleIndex(AssetHandle h)
    {
        return h & 0x0FFFFFFF;
    }

    AssetType AssetManager::ReadAssetHandleType(AssetHandle h)
    {
        return static_cast<AssetType>((h >> 28) & 0xF);
    }

    void AssetManager::Initialize(Core::Memory::ArenaAllocator* arena, Hardwares::VulkanDevice* device, cstring working_space_path)
    {
        s_Instance                          = ZPushStructCtor<AssetManager>(arena);
        s_Instance->Arena                   = arena;

        s_Instance->Device                  = device;
        s_Instance->CurrentWorkingSpacePath = working_space_path;

        // Initialise the container slab. Growing containers (Meshes, NodeHierarchies,
        // Materials, UUIDToTextureHandle, UUIDToMaterialSlot) back into the slab so
        // realloc can extend in-place — zero dead-block accumulation on grow.
        s_Instance->ContainerSlab.Init(s_Instance->Arena, AssetManager::CONTAINER_SLAB_BYTES);
        auto* slab = &s_Instance->ContainerSlab;

        s_Instance->NodeHierarchies.init(slab, 5000);
        s_Instance->Meshes.init(slab, 5000);
        s_Instance->Materials.init(slab, 5000);
        s_Instance->GPUMeshMaterials.init(s_Instance->Arena, 5000);
        s_Instance->Textures.init(s_Instance->Arena, 5000);
        s_Instance->UUIDToTextureHandle.init(slab, 5000);
        s_Instance->MeshToHierarchySlot.init(s_Instance->Arena, 5000);
        s_Instance->UUIDToMaterialSlot.init(slab, 5000);
        s_Instance->MeshCacheAllocations.init(slab, 128);

        // Registry callbacks borrow their observers. A process-static registry
        // would retain those pointers after this manager and its arena retire.
        // Construct fresh registry state in the same bounded owner instead.
        s_Instance->Registry = ZPushStructCtor<Core::VFS::AssetRegistry>(s_Instance->Arena);
        s_Instance->Registry->Initialize(s_Instance->Arena);

        // Reserve slot 0 as an intentional fallback material: AppRenderPipeline defaults
        // an unresolved submesh to index 0, so without this it would silently alias
        // whichever real material happens to be ingested first.
        Importers::AssetMaterial fallback_mat = {};
        s_Instance->Materials.push(fallback_mat);
        Rendering::Meshes::MeshMaterial fallback_gpu = {};
        fallback_gpu.AlbedoColor                     = Core::Maths::Vec4f(1.0f, 0.078f, 0.576f, 1.0f); // matches FallbackTextureHandle's (255, 20, 147)
        s_Instance->GPUMeshMaterials.push(fallback_gpu);
    }

    void AssetManager::InitFallbackTexture()
    {
        if (!s_Instance || !s_Instance->Device || !s_Instance->Device->RRM)
            return;
        auto* rrm                         = static_cast<Rendering::RenderResourceManager*>(s_Instance->Device->RRM);
        s_Instance->FallbackTextureHandle = rrm->GetOrCreateFallbackTexture();
        ZENGINE_LOG_ASSET_INFO("Fallback texture ready")
    }

    void AssetManager::Shutdown()
    {
        if (s_Instance)
        {
            for (auto* allocation : s_Instance->MeshCacheAllocations)
                delete allocation;
            s_Instance->MeshCacheAllocations.clear();
            if (s_Instance->Registry)
            {
                s_Instance->Registry->~AssetRegistry();
                s_Instance->Registry = nullptr;
            }
            s_Instance->ContainerSlab.Shutdown();
        }
        s_Instance = nullptr;
    }

    AssetHandle AssetManager::RegisterAsset(AssetType type, const uuids::uuid& uuid, uint32_t slot_index, const Core::VFS::VFSPath& path, const Core::VFS::MetaFileData& meta)
    {
        AssetHandle handle = CreateHandle(slot_index, type);
        if (s_Instance->Registry)
        {
            Core::VFS::MetaFileData reg_meta = meta;
            reg_meta.AssetUUID               = uuid;
            s_Instance->Registry->RegisterLoaded(uuid, static_cast<AssetType>(type), path, reg_meta, handle);
        }
        return handle;
    }

    bool AssetManager::IsRegistered(const uuids::uuid& id)
    {
        return s_Instance && s_Instance->Registry && s_Instance->Registry->FindByUUID(id) != nullptr;
    }

    void AssetManager::IngestMesh(AssetMesh&& mesh, AssetNodeHierarchy&& hierarchy)
    {
        if (!s_Instance)
            return;
        std::lock_guard lock(s_Instance->IngestMutex);
        // Use MeshToHierarchySlot — populated only after data is actually ingested.
        // IsRegistered / GetAsset both give false positives because VFSScanner
        // pre-registers UUIDs with SlotHandle=0 before any mesh data exists.
        uint32_t*       existing_hier_slot = s_Instance->MeshToHierarchySlot.find(mesh.MeshUUID);
        bool            is_reload          = existing_hier_slot != nullptr;

        // Hot-reload (#762): overwrite the existing slots' data in place instead of
        // pushing new ones, so the mesh/hierarchy keep the same AssetHandle and
        // AssetRegistry::MarkStale below can hand RenderResourceManager's OnStaleCallback
        // a handle that already points at the fresh data.
        uint32_t        mesh_slot          = 0;
        AssetMesh*      mp                 = nullptr;
        if (is_reload)
        {
            mp = GetAsset<AssetMesh>(mesh.MeshUUID);
            ZENGINE_VALIDATE_ASSERT(mp != nullptr, "IngestMesh: MeshToHierarchySlot has this UUID but the mesh record is missing")
        }
        else
        {
            mesh_slot    = static_cast<uint32_t>(s_Instance->Meshes.size());
            mp           = &s_Instance->Meshes.push_use({});
            mp->MeshUUID = mesh.MeshUUID;
        }
        AssetMesh& m = *mp;
        m.SubMeshes.init(s_Instance->Arena, mesh.SubMeshes.size());
        m.Vertices.init(s_Instance->Arena, mesh.Vertices.size(), mesh.Vertices.size());
        m.Indices.init(s_Instance->Arena, mesh.Indices.size(), mesh.Indices.size());
        Helpers::secure_memcpy(m.Vertices.data(), m.Vertices.size() * sizeof(float), mesh.Vertices.data(), mesh.Vertices.size() * sizeof(float));
        Helpers::secure_memcpy(m.Indices.data(), m.Indices.size() * sizeof(uint32_t), mesh.Indices.data(), mesh.Indices.size() * sizeof(uint32_t));
        for (auto& sub : mesh.SubMeshes)
            m.SubMeshes.push(sub);

        // Runtime bounds are not part of the .zemesh codec. Reset on every reload
        // so an empty/malformed replacement cannot retain the previous box/sphere.
        // All submeshes use this vertex space and the same entity matrix in the
        // draw path; applying imported node transforms only here would disagree
        // with that renderer rather than fix its separate hierarchy limitation.
        m.BoundsCenter = {};
        m.BoundsRadius = 0.f;
        m.BoundsMin    = {};
        m.BoundsMax    = {};
        m.BoundsValid  = false;
        // Vertices are 8 floats each: pos(xyz) nrm(xyz) uv(uv).
        if (!m.Vertices.empty() && m.Vertices.size() % 8 == 0)
        {
            const size_t       vcount  = m.Vertices.size() / 8;
            Core::Maths::Vec3f minimum = {m.Vertices[0], m.Vertices[1], m.Vertices[2]};
            Core::Maths::Vec3f maximum = minimum;
            double             sum[3]  = {};
            bool               valid   = true;
            for (size_t vi = 0; vi < vcount && valid; ++vi)
            {
                for (uint32_t axis = 0; axis < 3; ++axis)
                {
                    const float value = m.Vertices[vi * 8 + axis];
                    if (!std::isfinite(value))
                    {
                        valid = false;
                        break;
                    }
                    minimum[axis]  = std::min(minimum[axis], value);
                    maximum[axis]  = std::max(maximum[axis], value);
                    sum[axis]     += double(value);
                }
            }
            if (valid)
            {
                for (uint32_t axis = 0; axis < 3; ++axis)
                    m.BoundsCenter[axis] = float(std::clamp(sum[axis] / double(vcount), double(minimum[axis]), double(maximum[axis])));
                double max_squared = 0.0;
                for (size_t vi = 0; vi < vcount; ++vi)
                {
                    double squared = 0.0;
                    for (uint32_t axis = 0; axis < 3; ++axis)
                    {
                        const double delta  = double(m.Vertices[vi * 8 + axis]) - m.BoundsCenter[axis];
                        squared            += delta * delta;
                    }
                    max_squared = std::max(max_squared, squared);
                }
                const double radius = std::sqrt(max_squared);
                if (radius <= std::numeric_limits<float>::max())
                {
                    m.BoundsRadius = float(radius);
                    if (double(m.BoundsRadius) < radius)
                        m.BoundsRadius = std::nextafter(m.BoundsRadius, std::numeric_limits<float>::infinity());
                }
                // An unrepresentable sphere keeps radius zero (always visible in
                // culling). The finite AABB is still available for decorations.
                m.BoundsMin   = minimum;
                m.BoundsMax   = maximum;
                m.BoundsValid = true;
            }
        }

        if (!is_reload)
            RegisterAsset(AssetType::MESH, m.MeshUUID, mesh_slot);

        // Hierarchy: same in-place-overwrite treatment as the mesh above. Deliberately
        // keep the existing NodeHierarchyUUID/MeshUUID on reload rather than the new
        // AssetNodeHierarchy's freshly-minted one — the registry entry (and this UUID's
        // identity) must stay stable across reloads, not just the slot index.
        uint32_t            hier_slot = is_reload ? *existing_hier_slot : static_cast<uint32_t>(s_Instance->NodeHierarchies.size());
        AssetNodeHierarchy& h         = is_reload ? s_Instance->NodeHierarchies[hier_slot] : s_Instance->NodeHierarchies.push_use({});
        if (!is_reload)
        {
            h.NodeHierarchyUUID = hierarchy.NodeHierarchyUUID;
            h.MeshUUID          = hierarchy.MeshUUID;
        }

        h.Hierarchies.init(s_Instance->Arena, hierarchy.Hierarchies.size(), hierarchy.Hierarchies.size());
        h.LocalTransforms.init(s_Instance->Arena, hierarchy.LocalTransforms.size(), hierarchy.LocalTransforms.size());
        h.GlobalTransforms.init(s_Instance->Arena, hierarchy.GlobalTransforms.size(), hierarchy.GlobalTransforms.size());
        h.Names.init(s_Instance->Arena, hierarchy.Names.size());
        h.MaterialNames.init(s_Instance->Arena, hierarchy.MaterialNames.size());
        h.NodeNames.init(s_Instance->Arena, hierarchy.NodeNames.size() > 32 ? hierarchy.NodeNames.size() * 2 : 64);
        h.NodeMeshes.init(s_Instance->Arena, hierarchy.NodeMeshes.size() > 32 ? hierarchy.NodeMeshes.size() * 2 : 64);
        h.NodeMaterials.init(s_Instance->Arena, hierarchy.NodeMaterials.size() > 32 ? hierarchy.NodeMaterials.size() * 2 : 64);

        Helpers::secure_memcpy(h.Hierarchies.data(), h.Hierarchies.size() * sizeof(Helpers::NodeHierarchy), hierarchy.Hierarchies.data(), hierarchy.Hierarchies.size() * sizeof(Helpers::NodeHierarchy));
        Helpers::secure_memcpy(h.LocalTransforms.data(), h.LocalTransforms.size() * sizeof(Core::Maths::Mat4f), hierarchy.LocalTransforms.data(), hierarchy.LocalTransforms.size() * sizeof(Core::Maths::Mat4f));
        Helpers::secure_memcpy(h.GlobalTransforms.data(), h.GlobalTransforms.size() * sizeof(Core::Maths::Mat4f), hierarchy.GlobalTransforms.data(), hierarchy.GlobalTransforms.size() * sizeof(Core::Maths::Mat4f));

        for (auto& name : hierarchy.Names)
        {
            auto& n = h.Names.push_use({});
            n.init(s_Instance->Arena, name.c_str());
        }
        for (auto& mat_name : hierarchy.MaterialNames)
        {
            auto& n = h.MaterialNames.push_use({});
            n.init(s_Instance->Arena, mat_name.c_str());
        }
        for (const auto& [k, v] : hierarchy.NodeNames)
            h.NodeNames.insert(k, v);
        for (const auto& [k, v] : hierarchy.NodeMeshes)
            h.NodeMeshes.insert(k, v);
        for (const auto& [k, v] : hierarchy.NodeMaterials)
            h.NodeMaterials.insert(k, v);

        if (!is_reload)
        {
            RegisterAsset(AssetType::MESH_HIERARCHY, h.NodeHierarchyUUID, hier_slot);
            s_Instance->MeshToHierarchySlot.insert(h.MeshUUID, hier_slot);
        }

        if (s_Instance->Registry)
        {
            if (is_reload)
            {
                // SetState(Loaded) would hit RenderResourceManager's OnReady callback,
                // which skips any UUID that already has a GPU buffer — it never reaches
                // ScheduleSwap. MarkStale is what actually drives the hot-reload swap.
                s_Instance->Registry->MarkStale(m.MeshUUID);
                s_Instance->Registry->MarkStale(h.NodeHierarchyUUID);
            }
            else
            {
                // Notify the registry that both assets are now loaded so hot-reload callbacks fire.
                s_Instance->Registry->SetState(m.MeshUUID, Core::VFS::AssetState::Loaded);
                s_Instance->Registry->SetState(h.NodeHierarchyUUID, Core::VFS::AssetState::Loaded);
            }
        }
    }

    Rendering::Textures::TextureHandle AssetManager::IngestTexture(const uuids::uuid& uuid, const Core::Containers::String& path)
    {
        if (!s_Instance)
            return {};
        std::lock_guard lock(s_Instance->IngestMutex);

        // Dedup — return existing handle if already uploaded.
        // Use UUIDToTextureHandle (not IsRegistered): VFSScanner pre-registers texture
        // UUIDs without uploading them, so IsRegistered gives a false positive.
        if (auto* h = s_Instance->UUIDToTextureHandle.find(uuid))
        {
            // Already known — this is a reimport signal (e.g. TextureImporter after a file
            // edit). The handle value doesn't change: RRM's reload path reconstructs the
            // existing GPU resource in place. Ask RRM to do that on the render thread.
            if (s_Instance->Device && s_Instance->Device->RRM)
                static_cast<Rendering::RenderResourceManager*>(s_Instance->Device->RRM)->ScheduleTextureReload(uuid);
            return *h;
        }

        auto  slot          = static_cast<uint32_t>(s_Instance->Textures.size());
        auto& new_tex       = s_Instance->Textures.push_use({});
        new_tex.TextureUUID = uuid;
        new_tex.Path.init(s_Instance->Arena, path.c_str());

        if (!new_tex.Path.empty() && s_Instance->Device && s_Instance->Device->RRM)
        {
            char full_path[MAX_FILE_PATH_COUNT] = {};
            snprintf(full_path, sizeof(full_path), "%s%c%s", s_Instance->CurrentWorkingSpacePath, PLATFORM_OS_BACKSLASH, new_tex.Path.c_str());
            auto* rrm      = static_cast<Rendering::RenderResourceManager*>(s_Instance->Device->RRM);
            new_tex.Handle = rrm->SubmitTextureFile(full_path);
            if (!new_tex.Handle.Valid())
            {
                ZENGINE_VALIDATE_ASSERT(s_Instance->FallbackTextureHandle.Valid(), "FallbackTextureHandle not initialized — InitFallbackTexture must be called before ingesting assets")
                ZENGINE_LOG_ASSET_WARN("Texture not found: '{}' — using fallback", full_path)
                new_tex.Handle = s_Instance->FallbackTextureHandle;
            }
        }
        else
        {
            ZENGINE_VALIDATE_ASSERT(s_Instance->FallbackTextureHandle.Valid(), "FallbackTextureHandle not initialized — InitFallbackTexture must be called before ingesting assets")
            ZENGINE_LOG_ASSET_WARN("Texture {} has no path — extraction may have failed, using fallback", uuids::to_string(uuid))
            new_tex.Handle = s_Instance->FallbackTextureHandle;
        }

        RegisterAsset(AssetType::TEXTURE, new_tex.TextureUUID, slot);
        s_Instance->UUIDToTextureHandle.insert(new_tex.TextureUUID, new_tex.Handle);
        return new_tex.Handle;
    }

    void AssetManager::IngestTextures(Core::Containers::Array<AssetTexture>&& textures)
    {
        if (!s_Instance)
            return;
        for (size_t i = 0; i < textures.size(); ++i)
            IngestTexture(textures[i].TextureUUID, textures[i].Path);
    }

    void AssetManager::IngestMaterial(AssetMaterial&& mat)
    {
        if (!s_Instance)
            return;
        std::lock_guard lock(s_Instance->IngestMutex);
        // Use UUIDToMaterialSlot — populated only after data is actually ingested.
        // GetAsset gives false positives: VFSScanner pre-registers all .zematerial UUIDs
        // with SlotHandle=0, so GetAsset<AssetMaterial>(uuid_N) returns Materials[0]
        // (the first ingested material) for every subsequent material → all skipped.
        if (s_Instance->UUIDToMaterialSlot.find(mat.MaterialUUID) != nullptr)
            return;

        auto slot = static_cast<uint32_t>(s_Instance->Materials.size());
        s_Instance->Materials.push(mat.Clone(s_Instance->Arena));
        RegisterAsset(AssetType::MATERIAL, mat.MaterialUUID, slot);
        s_Instance->UUIDToMaterialSlot.insert(mat.MaterialUUID, slot);

        Rendering::Meshes::MeshMaterial& gpu_mat = s_Instance->GPUMeshMaterials.push_use({});
        gpu_mat.AlbedoColor                      = {mat.AlbedoColor[0], mat.AlbedoColor[1], mat.AlbedoColor[2], mat.AlbedoColor[3]};
        gpu_mat.EmissiveColor                    = {mat.EmissiveColor[0], mat.EmissiveColor[1], mat.EmissiveColor[2], mat.EmissiveColor[3]};
        gpu_mat.RoughnessColor                   = {mat.RoughnessColor[0], mat.RoughnessColor[1], mat.RoughnessColor[2], mat.RoughnessColor[3]};
        gpu_mat.SpecularColor                    = {mat.SpecularColor[0], mat.SpecularColor[1], mat.SpecularColor[2], mat.SpecularColor[3]};
        gpu_mat.AmbientColor                     = {mat.AmbientColor[0], mat.AmbientColor[1], mat.AmbientColor[2], mat.AmbientColor[3]};
        gpu_mat.Factors                          = {mat.Factors[0], mat.Factors[1], mat.Factors[2], mat.Factors[3]};

        // Resolve handle for each texture slot: UUID lookup first, then fall back to
        // uploading from the stored path — handles scene-reload and dragged-.zmesh cases
        // where IngestTextures may not have run yet for this material's textures.
        gpu_mat.AlbedoMap                        = ResolveTextureMapIndex(mat.AlbedoTexUUID, mat.AlbedoTexPath);
        gpu_mat.EmissiveMap                      = ResolveTextureMapIndex(mat.EmissiveTexUUID, mat.EmissiveTexPath);
        gpu_mat.NormalMap                        = ResolveTextureMapIndex(mat.NormalTexUUID, mat.NormalTexPath);
        gpu_mat.OpacityMap                       = ResolveTextureMapIndex(mat.OpacityTexUUID, mat.OpacityTexPath);
        gpu_mat.SpecularMap                      = ResolveTextureMapIndex(mat.SpecularTexUUID, mat.SpecularTexPath);
    }

    bool AssetManager::IngestMaterialFromUUID(Core::Memory::ArenaAllocator* scratch, const uuids::uuid& material_uuid)
    {
        if (!s_Instance || !s_Instance->Registry || !scratch || material_uuid.is_nil())
            return false;
        {
            // The map is mutated by import workers. Its fast deduplication path
            // still needs the same ownership guard as the eventual insertion.
            std::lock_guard lock(s_Instance->IngestMutex);
            if (s_Instance->UUIDToMaterialSlot.find(material_uuid) != nullptr)
                return true;
        }

        AssetRecordSnapshot expected = {};
        auto*               registry = s_Instance->Registry;
        if (!registry->TryReadRecord(material_uuid, &expected) || expected.Record.Type != AssetType::MATERIAL || !expected.Record.Path.IsValid() || !expected.Record.Path.Extension().Equals(".zematerial"))
            return false;

        char native[MAX_FILE_PATH_COUNT] = {};
        expected.Record.Path.ResolveNative(s_Instance->CurrentWorkingSpacePath, native, sizeof(native));

        Importers::AssetMaterial mat = {};
        Importers::AssetCodec::DeserializeMaterialAssetFile(scratch, native, mat);
        if (mat.MaterialUUID != material_uuid)
            return false;
        IngestMaterial(std::move(mat)); // transitively ingests each referenced texture map
        std::lock_guard lock(s_Instance->IngestMutex);
        return s_Instance->UUIDToMaterialSlot.find(material_uuid) != nullptr;
    }

    uint32_t AssetManager::ResolveTextureMapIndex(const uuids::uuid& id, const Core::Containers::String& path)
    {
        if (!s_Instance || id.is_nil())
            return INVALID_MAP_HANDLE;
        auto* h = s_Instance->UUIDToTextureHandle.find(id);
        if (h)
            return h->Index;
        if (!path.empty())
            return IngestTexture(id, path).Index;
        return INVALID_MAP_HANDLE;
    }

    Rendering::Textures::TextureHandle AssetManager::FindTextureHandle(const uuids::uuid& uuid)
    {
        if (!s_Instance)
            return {};
        std::lock_guard lock(s_Instance->IngestMutex);
        auto*           h = s_Instance->UUIDToTextureHandle.find(uuid);
        return h ? *h : Rendering::Textures::TextureHandle{};
    }

    void AssetManager::ReleaseTexture(const uuids::uuid& uuid)
    {
        if (!s_Instance)
            return;
        std::lock_guard lock(s_Instance->PendingTextureReleaseMutex);
        if (s_Instance->PendingTextureReleaseCount >= MAX_PENDING_TEXTURE_RELEASES)
        {
            ZENGINE_LOG_ASSET_WARN("[AssetManager] Pending texture release queue full — dropping release for {}", uuids::to_string(uuid))
            return;
        }
        s_Instance->PendingTextureReleases[s_Instance->PendingTextureReleaseCount++] = uuid;
    }

    void AssetManager::FlushTextureReleases()
    {
        if (!s_Instance)
            return;

        uuids::uuid local[MAX_PENDING_TEXTURE_RELEASES];
        uint32_t    count = 0;
        {
            std::lock_guard lock(s_Instance->PendingTextureReleaseMutex);
            count = s_Instance->PendingTextureReleaseCount;
            Helpers::secure_memcpy(local, sizeof(local), s_Instance->PendingTextureReleases, count * sizeof(local[0]));
            s_Instance->PendingTextureReleaseCount = 0;
        }
        if (count == 0)
            return;

        // IngestMutex, not just PendingTextureReleaseMutex: Materials/GPUMeshMaterials are
        // unsynchronized and also written by IngestMaterial/IngestTexture under this lock.
        std::lock_guard lock(s_Instance->IngestMutex);
        for (uint32_t r = 0; r < count; ++r)
        {
            s_Instance->UUIDToTextureHandle.remove(local[r]);

            // Sentinel set directly, not via ResolveTextureMapIndex — its path fallback
            // would re-ingest (undo) the release for any material with a stored path.
            for (uint32_t i = 0; i < s_Instance->Materials.size(); ++i)
            {
                auto& mat     = s_Instance->Materials[i];
                auto& gpu_mat = s_Instance->GPUMeshMaterials[i];
                if (mat.AlbedoTexUUID == local[r])
                    gpu_mat.AlbedoMap = INVALID_MAP_HANDLE;
                if (mat.EmissiveTexUUID == local[r])
                    gpu_mat.EmissiveMap = INVALID_MAP_HANDLE;
                if (mat.NormalTexUUID == local[r])
                    gpu_mat.NormalMap = INVALID_MAP_HANDLE;
                if (mat.OpacityTexUUID == local[r])
                    gpu_mat.OpacityMap = INVALID_MAP_HANDLE;
                if (mat.SpecularTexUUID == local[r])
                    gpu_mat.SpecularMap = INVALID_MAP_HANDLE;
            }
        }
    }

    Importers::AssetMesh* AssetManager::GetMeshAsset(const uuids::uuid& id)
    {
        if (!Registry)
            return nullptr;
        AssetHandle slot = 0;
        if (!Registry->TryReadSlot(id, AssetType::MESH, &slot) || ReadAssetHandleType(slot) != AssetType::MESH)
            return nullptr;
        uint32_t index = ReadAssetHandleIndex(slot);
        return index < Meshes.size() && Meshes[index].MeshUUID == id ? &Meshes[index] : nullptr;
    }

    bool AssetManager::TryGetMeshDrawCount(const uuids::uuid& id, uint32_t* out_draws)
    {
        if (!out_draws || id.is_nil())
            return false;
        std::lock_guard lock(IngestMutex);
        return TryGetMeshDrawCountLocked(id, out_draws);
    }

    bool AssetManager::TryGetMeshDrawCountLocked(const uuids::uuid& id, uint32_t* out_draws)
    {
        const auto* mesh = GetMeshAsset(id);
        if (!mesh || mesh->Vertices.empty() || mesh->Vertices.size() % 8 != 0 || mesh->Indices.empty() || mesh->SubMeshes.empty() || mesh->SubMeshes.size() > UINT32_MAX)
            return false;
        const auto vertices = mesh->Vertices.size() / 8;
        for (const auto& sub : mesh->SubMeshes)
            if (!sub.VertexCount || !sub.IndexCount || sub.VertexOffset > vertices || sub.VertexCount > vertices - sub.VertexOffset || sub.IndexOffset > mesh->Indices.size() || sub.IndexCount > mesh->Indices.size() - sub.IndexOffset)
                return false;
        *out_draws = static_cast<uint32_t>(mesh->SubMeshes.size());
        return true;
    }

    bool AssetManager::TryGetMeshBounds(const uuids::uuid& id, Core::Maths::Vec3f& out_center, float& out_radius)
    {
        std::lock_guard lock(IngestMutex);
        auto*           mesh = GetMeshAsset(id);
        if (!mesh || !std::isfinite(mesh->BoundsRadius) || mesh->BoundsRadius <= 0.0f || !std::isfinite(mesh->BoundsCenter.x) || !std::isfinite(mesh->BoundsCenter.y) || !std::isfinite(mesh->BoundsCenter.z))
            return false;

        out_center = mesh->BoundsCenter;
        out_radius = mesh->BoundsRadius;
        return true;
    }

    bool AssetManager::TryGetMeshAABB(const uuids::uuid& id, Core::Maths::Vec3f& out_min, Core::Maths::Vec3f& out_max)
    {
        std::unique_lock lock(IngestMutex, std::try_to_lock);
        if (!lock.owns_lock())
            return false; // Optional per-frame decoration must not wait on a full ingestion scan.
        const auto* mesh = GetMeshAsset(id);
        if (!mesh || !mesh->BoundsValid)
            return false;
        for (uint32_t axis = 0; axis < 3; ++axis)
            if (!std::isfinite(mesh->BoundsMin[axis]) || !std::isfinite(mesh->BoundsMax[axis]) || mesh->BoundsMin[axis] > mesh->BoundsMax[axis])
                return false;
        out_min = mesh->BoundsMin;
        out_max = mesh->BoundsMax;
        return true;
    }

    Importers::AssetNodeHierarchy* AssetManager::GetMeshNodeHierarchy(const uuids::uuid& mesh_id)
    {
        if (!s_Instance)
            return nullptr;
        const uint32_t* slot = s_Instance->MeshToHierarchySlot.find(mesh_id);
        return (slot && *slot < s_Instance->NodeHierarchies.size()) ? &s_Instance->NodeHierarchies[*slot] : nullptr;
    }

    AssetHandle AssetManager::GetMeshNodeHierarchyHandle(const uuids::uuid& id)
    {
        if (!Registry)
            return 0;
        const Core::VFS::AssetRecord* rec = Registry->FindByUUID(id);
        return rec ? rec->SlotHandle : 0;
    }

    void AssetManager::ReloadFromDisk(Core::Memory::ArenaAllocator* scratch)
    {
        if (!s_Instance || !s_Instance->Registry || !scratch)
            return;
        // Materials — deserialize each .zematerial that has not been ingested yet.
        {
            auto result = s_Instance->Registry->Query({.Type = AssetType::MATERIAL}, scratch);
            for (uint32_t i = 0; i < result.Handles.size(); ++i)
            {
                auto* rec = s_Instance->Registry->Access(result.Handles[i]);
                if (!rec || rec->UUID.is_nil())
                    continue;
                if (s_Instance->UUIDToMaterialSlot.find(rec->UUID) != nullptr)
                    continue;

                char native[MAX_FILE_PATH_COUNT] = {};
                rec->Path.ResolveNative(s_Instance->CurrentWorkingSpacePath, native, sizeof(native));

                AssetMaterial mat = {};
                Importers::AssetCodec::DeserializeMaterialAssetFile(scratch, native, mat);
                if (!mat.MaterialUUID.is_nil())
                {
                    ZENGINE_LOG_ASSET_INFO("Reloading material from disk: {}", native)
                    IngestMaterial(std::move(mat));
                }
            }
        }

        // Textures — ingest each raster texture that has not been ingested yet.
        {
            auto result = s_Instance->Registry->Query({.Type = AssetType::TEXTURE}, scratch);
            for (uint32_t i = 0; i < result.Handles.size(); ++i)
            {
                auto* rec = s_Instance->Registry->Access(result.Handles[i]);
                if (!rec || rec->UUID.is_nil())
                    continue;
                if (s_Instance->UUIDToTextureHandle.find(rec->UUID) != nullptr)
                    continue;

                Core::Containers::String rel_path = {};
                rel_path.init(scratch, rec->Path.CStr());

                ZENGINE_LOG_ASSET_INFO("Reloading texture from disk: {}", rec->Path.CStr())
                IngestTexture(rec->UUID, rel_path);
            }
        }

        // Meshes — deserialize each .zemesh that has not been ingested yet.
        {
            auto result = s_Instance->Registry->Query({.Type = AssetType::MESH}, scratch);
            for (uint32_t i = 0; i < result.Handles.size(); ++i)
            {
                auto* rec = s_Instance->Registry->Access(result.Handles[i]);
                if (!rec || rec->UUID.is_nil())
                    continue;
                if (rec->State == Core::VFS::AssetState::Loaded)
                    continue;
                if (s_Instance->MeshToHierarchySlot.find(rec->UUID) != nullptr)
                    continue;

                char native[MAX_FILE_PATH_COUNT] = {};
                rec->Path.ResolveNative(s_Instance->CurrentWorkingSpacePath, native, sizeof(native));

                AssetMesh          mesh = {};
                AssetNodeHierarchy hier = {};
                // Use the AssetManager's own arena — meshes can be hundreds of MB,
                // far exceeding the caller's scratch. After IngestMesh copies the data
                // permanently, the deserialization buffers become dead weight but are
                // acceptable as a one-time startup cost.
                Importers::AssetCodec::DeserializeMeshAssetFile(s_Instance->Arena, native, mesh, hier);
                if (!mesh.MeshUUID.is_nil())
                {
                    ZENGINE_LOG_ASSET_INFO("Reloading mesh from disk: {}", native)
                    IngestMesh(std::move(mesh), std::move(hier));
                }
            }
        }
    }

    uuids::uuid AssetManager::GetOrCreateUUID(Core::VFS::IVFSContext& ctx, const Core::VFS::VFSPath& asset_path, const char* importer_name)
    {
        auto hash = Core::VFS::MetaFileIO::ComputeHash(ctx, asset_path);
        auto meta = Core::VFS::MetaFileIO::GetOrCreate(ctx, asset_path, importer_name, hash.Succeeded() ? hash.Value() : 0);
        if (meta.Succeeded())
            return meta.Value().AssetUUID;

        std::random_device           rd;
        std::mt19937                 generator(rd());
        uuids::uuid_random_generator gen{generator};
        return gen();
    }

    namespace
    {
        template <typename T>
        bool CopyArray(const Array<T>& source, Array<T>& destination, ArenaAllocator& arena)
        {
            static_assert(std::is_trivially_copyable_v<T>);
            destination.m_allocator = &arena;
            if (source.empty())
                return true;
            if (source.size() > SIZE_MAX / sizeof(T))
                return false;
            auto* data = static_cast<T*>(arena.AllocateNoZero(source.size() * sizeof(T), alignof(T)));
            if (!data || Helpers::secure_memcpy(data, source.size() * sizeof(T), source.data(), source.size() * sizeof(T)) != Helpers::MEMORY_OP_SUCCESS)
                return false;
            destination.m_data = data;
            destination.m_size = destination.m_capacity = source.size();
            return true;
        }

        bool CopyNames(const Array<String>& source, Array<String>& destination, ArenaAllocator& arena)
        {
            destination.m_allocator = &arena;
            if (source.empty())
                return true;
            if (source.size() > SIZE_MAX / sizeof(String))
                return false;
            auto* names = static_cast<String*>(arena.AllocateNoZero(source.size() * sizeof(String), alignof(String)));
            if (!names)
                return false;
            destination.m_data     = names;
            destination.m_capacity = source.size();
            for (const auto& source_name : source)
            {
                auto* name = new (&names[destination.m_size]) String{};
                ++destination.m_size;
                const auto bytes = source_name.size() + 1; // Checked reader bounded the string and validated its NUL.
                name->m_data     = static_cast<char*>(arena.AllocateNoZero(bytes, alignof(char)));
                if (!name->m_data || Helpers::secure_memcpy(name->m_data, bytes, source_name.c_str(), bytes) != Helpers::MEMORY_OP_SUCCESS)
                    return false;
                name->m_allocator = &arena;
                name->m_size      = source_name.size();
                name->m_capacity  = bytes;
            }
            return true;
        }

        bool CopyMap(const UnorderedHashMap<uint32_t, uint32_t>& source, UnorderedHashMap<uint32_t, uint32_t>& destination, ArenaAllocator& arena)
        {
            if (!destination.try_init(&arena, source.capacity()))
                return false;
            for (const auto& [key, value] : source)
                destination.insert(key, value);
            return true;
        }

        void Bounds(AssetMesh& mesh)
        {
            // Checked decode guarantees a nonempty, finite 8-float vertex stream.
            auto minimum = Core::Maths::Vec3f(mesh.Vertices[0], mesh.Vertices[1], mesh.Vertices[2]);
            auto maximum = minimum;
            for (size_t i = 8; i < mesh.Vertices.size(); i += 8)
                for (uint32_t axis = 0; axis < 3; ++axis)
                {
                    minimum[axis] = std::min(minimum[axis], mesh.Vertices[i + axis]);
                    maximum[axis] = std::max(maximum[axis], mesh.Vertices[i + axis]);
                }
            double squared = 0.0;
            for (uint32_t axis = 0; axis < 3; ++axis)
            {
                mesh.BoundsCenter[axis]  = float((double(minimum[axis]) + maximum[axis]) * 0.5);
                const double extent      = std::max(double(maximum[axis]) - mesh.BoundsCenter[axis], double(mesh.BoundsCenter[axis]) - minimum[axis]);
                squared                 += extent * extent;
            }
            const double radius = std::sqrt(squared);
            if (radius < std::numeric_limits<float>::max())
            {
                mesh.BoundsRadius = float(radius);
                if (double(mesh.BoundsRadius) < radius)
                    mesh.BoundsRadius = std::nextafter(mesh.BoundsRadius, std::numeric_limits<float>::infinity());
            }
            mesh.BoundsMin   = minimum;
            mesh.BoundsMax   = maximum;
            mesh.BoundsValid = true;
        }
    } // namespace

    VFSResult<uint32_t> AssetManager::LoadMeshAsset(IVFSContext& vfs, const uuids::uuid& uuid, const AssetCodec::MeshAssetReadLimits& limits, const std::atomic<bool>* cancelled)
    {
        const auto stopped = [&] { return cancelled && cancelled->load(std::memory_order_acquire); };
        if (stopped())
            return VFSResult<uint32_t>::Fail(VFSError::Cancelled);
        uint32_t draws = 0;
        if (TryGetMeshDrawCount(uuid, &draws))
            return VFSResult<uint32_t>::Ok(draws);
        std::unique_lock decode_lock(MeshLoadMutex, std::try_to_lock);
        if (!decode_lock.owns_lock())
            return VFSResult<uint32_t>::Fail(VFSError::AlreadyExists);
        if (stopped())
            return VFSResult<uint32_t>::Fail(VFSError::Cancelled);
        if (TryGetMeshDrawCount(uuid, &draws))
            return VFSResult<uint32_t>::Ok(draws);
        AssetRecordSnapshot expected = {};
        auto*               registry = Registry;
        if (!registry || !registry->TryReadRecord(uuid, &expected))
            return VFSResult<uint32_t>::Fail(VFSError::NotFound);
        if (expected.Record.Type != AssetType::MESH || !expected.Record.Path.IsValid() || !expected.Record.Path.Extension().Equals(".zemesh"))
            return VFSResult<uint32_t>::Fail(VFSError::InvalidPath);
        if (!limits.MaxDecodedBytes || limits.MaxDecodedBytes > ZMega(512ULL))
            return VFSResult<uint32_t>::Fail(VFSError::SizeLimitExceeded);

        ArenaAllocator scratch = {};
        scratch.Initialize(limits.MaxDecodedBytes, 0, "AssetManager/MeshRead");
        AssetMesh          source           = {};
        AssetNodeHierarchy source_hierarchy = {};
        const auto         read             = AssetCodec::ReadMeshAssetFileVFS(&scratch, vfs, expected.Record.Path, &source, &source_hierarchy, uuid, limits);
        if (read.Failed())
            return VFSResult<uint32_t>::Fail(read.Error());
        if (stopped())
            return VFSResult<uint32_t>::Fail(VFSError::Cancelled);
        Bounds(source);

        // A mesh is not independently renderable: each submesh references a
        // material which in turn owns the texture paths required by its GPU
        // record. Scene open uses this checked loader rather than ReloadFromDisk,
        // so ingest those dependencies before publishing the mesh draw records.
        // Each material decode is transient; its clone and texture work are
        // retained by AssetManager before the temporary range is released.
        for (const AssetSubMesh& submesh : source.SubMeshes)
        {
            if (submesh.MaterialUUID.is_nil())
                continue;
            ArenaScratch material_decode(&scratch);
            if (!IngestMaterialFromUUID(material_decode.Arena(), submesh.MaterialUUID))
                return VFSResult<uint32_t>::Fail(VFSError::NotFound);
            if (stopped())
                return VFSResult<uint32_t>::Fail(VFSError::Cancelled);
        }

        auto storage = ZEngine::Helpers::Scope<MeshCacheAllocation>(new (std::nothrow) MeshCacheAllocation{});
        if (!storage)
            return VFSResult<uint32_t>::Fail(VFSError::OutOfMemory);
        // Mirror the decoder's allocation order/capacities. Allow for the slab
        // base's different alignment without reserving/clearing unused capacity.
        if (scratch.m_current_offset > SIZE_MAX - 64)
            return VFSResult<uint32_t>::Fail(VFSError::SizeLimitExceeded);
        const auto bytes = scratch.m_current_offset + 64;
        storage->Slab    = &ContainerSlab;
        storage->Block   = ContainerSlab.TryRealloc(nullptr, bytes);
        if (!storage->Block || !storage->Arena.InitializeBorrowed(storage->Block, bytes, "AssetManager/LoadedMesh"))
            return VFSResult<uint32_t>::Fail(VFSError::OutOfMemory);
        auto&              arena     = storage->Arena;
        AssetMesh          mesh      = {};
        AssetNodeHierarchy hierarchy = {};
        if (!CopyArray(source.Vertices, mesh.Vertices, arena) || !CopyArray(source.Indices, mesh.Indices, arena) || !CopyArray(source.SubMeshes, mesh.SubMeshes, arena) || !CopyArray(source_hierarchy.Hierarchies, hierarchy.Hierarchies, arena) || !CopyArray(source_hierarchy.LocalTransforms, hierarchy.LocalTransforms, arena) || !CopyArray(source_hierarchy.GlobalTransforms, hierarchy.GlobalTransforms, arena) || !CopyNames(source_hierarchy.Names, hierarchy.Names, arena) || !CopyNames(source_hierarchy.MaterialNames, hierarchy.MaterialNames, arena) ||
            !CopyMap(source_hierarchy.NodeNames, hierarchy.NodeNames, arena) || !CopyMap(source_hierarchy.NodeMeshes, hierarchy.NodeMeshes, arena) || !CopyMap(source_hierarchy.NodeMaterials, hierarchy.NodeMaterials, arena))
            return VFSResult<uint32_t>::Fail(VFSError::OutOfMemory);
        mesh.MeshUUID               = source.MeshUUID;
        mesh.BoundsCenter           = source.BoundsCenter;
        mesh.BoundsRadius           = source.BoundsRadius;
        mesh.BoundsMin              = source.BoundsMin;
        mesh.BoundsMax              = source.BoundsMax;
        mesh.BoundsValid            = source.BoundsValid;
        hierarchy.MeshUUID          = source_hierarchy.MeshUUID;
        hierarchy.NodeHierarchyUUID = source_hierarchy.NodeHierarchyUUID;

        std::lock_guard lock(IngestMutex);
        if (stopped())
            return VFSResult<uint32_t>::Fail(VFSError::Cancelled);
        // A concurrent importer/second load can win while we decode. Placement
        // reuses that complete binding; it must never replace or mark it stale.
        if (TryGetMeshDrawCountLocked(uuid, &draws))
            return VFSResult<uint32_t>::Ok(draws);
        if (Registry != registry || MeshToHierarchySlot.contains(uuid))
            return VFSResult<uint32_t>::Fail(VFSError::Cancelled);
        const auto mesh_slot      = Meshes.size();
        const auto hierarchy_slot = NodeHierarchies.size();
        if (mesh_slot >= 0x10000000 || hierarchy_slot >= 0x10000000 || (MeshToHierarchySlot.size() + 1) * 4 > MeshToHierarchySlot.capacity() * 3)
            return VFSResult<uint32_t>::Fail(VFSError::SizeLimitExceeded);
        if (!Meshes.try_reserve(mesh_slot + 1) || !NodeHierarchies.try_reserve(hierarchy_slot + 1) || !MeshCacheAllocations.try_reserve(MeshCacheAllocations.size() + 1))
            return VFSResult<uint32_t>::Fail(VFSError::OutOfMemory);
        const auto mesh_handle      = CreateHandle(uint32_t(mesh_slot), AssetType::MESH);
        const auto hierarchy_handle = CreateHandle(uint32_t(hierarchy_slot), AssetType::MESH_HIERARCHY);
        if (!registry->TryBindLoadedMesh(expected, hierarchy.NodeHierarchyUUID, mesh_handle, hierarchy_handle))
            return VFSResult<uint32_t>::Fail(VFSError::Cancelled);
        draws = uint32_t(mesh.SubMeshes.size());
        // Capacity is established. No failure/IO/callback between paired registry
        // publication and these allocation-free, complete CPU descriptor appends.
        new (&Meshes.m_data[mesh_slot]) AssetMesh(std::move(mesh));
        ++Meshes.m_size;
        new (&NodeHierarchies.m_data[hierarchy_slot]) AssetNodeHierarchy(std::move(hierarchy));
        ++NodeHierarchies.m_size;
        MeshToHierarchySlot.insert(uuid, uint32_t(hierarchy_slot));
        MeshCacheAllocations.push(storage.release());
        (void) registry->SetState(uuid, AssetState::Loaded);
        (void) registry->SetState(source_hierarchy.NodeHierarchyUUID, AssetState::Loaded);
        return VFSResult<uint32_t>::Ok(draws);
    }
} // namespace ZEngine::Managers
