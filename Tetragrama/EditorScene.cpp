#include <pch.h>
#include <EditorScene.h>
#include <ZEngine/Managers/AssetManager.h>
#include <stack>

using namespace ZEngine::Rendering::Meshes;
using namespace ZEngine::Core::Containers;
using namespace ZEngine::Managers;
using namespace ZEngine::Helpers;

namespace Tetragrama
{
    void EditorScene::Initialize(ZEngine::Core::Memory::ArenaAllocator* arena, cstring name)
    {
        arena->CreateSubArena(ZMega(200), &LocalArena);

        Name                     = name;

        PendingOnLoadHierarchies = CreateRef<ThreadSafeQueue<AssetManager::AssetHandle>>();

        AssetFiles.init(&LocalArena, 500);
        HashToAssetFile.init(&LocalArena, 500);

        Hierarchies.init(&LocalArena, 1000);
        HierarchiesNodeRef.init(&LocalArena, 1000);
        Names.init(&LocalArena, 1000);
        LocalTransforms.init(&LocalArena, 1000);
        GlobalTransforms.init(&LocalArena, 1000);
        NodeNames.init(&LocalArena, 1000);
        MeshAllocations.init(&LocalArena, 1000);
        NodeSubMeshesAllocations.init(&LocalArena, 1000);

        Vertices.init(&LocalArena, 40000, 40000);
        Indices.init(&LocalArena, 10000, 10000);
        ZEngine::Helpers::secure_memset(Vertices.data(), 0, sizeof(float) * Vertices.size(), sizeof(float) * Vertices.size());
        ZEngine::Helpers::secure_memset(Indices.data(), 0, sizeof(uint32_t) * Indices.size(), sizeof(uint32_t) * Indices.size());

        /*
         * Root Scene node
         */
        Reset();

        InitRootNode();
    }

    bool EditorScene::HasPendingChange() const
    {
        return HasPendingChanges.load(std::memory_order_acquire);
    }

    int EditorScene::AddHierarchyNode(int parent, int depth)
    {
        if (depth < 0)
        {
            return -1;
        }

        int                             node_id  = static_cast<int>(Hierarchies.size());

        // Create new node
        ZEngine::Helpers::NodeHierarchy new_node = {};
        new_node.Parent                          = parent;
        new_node.DepthLevel                      = depth;

        Hierarchies.push(new_node);
        HierarchiesNodeRef.push({});
        LocalTransforms.push(glm::mat4(1.0f));
        GlobalTransforms.push(glm::mat4(1.0f));

        if (parent >= 0)
        {
            auto& parent_node = Hierarchies[parent];

            if (parent_node.FirstChild == -1)
            {
                // First child
                parent_node.FirstChild = node_id;
                parent_node.LastChild  = node_id;
            }
            else
            {
                // Append to last child's sibling list in O(1)
                Hierarchies[parent_node.LastChild].RightSibling = node_id;
                parent_node.LastChild                           = node_id;
            }
        }

        HasPendingChanges.store(true, std::memory_order_release);

        return node_id;
    }

    int EditorScene::CreateSceneNode(int parent, int depth, const ZEngine::Importers::AssetNodeRef& metadata)
    {
        int node_id = AddHierarchyNode(parent, depth);
        if (node_id < 0)
        {
            ZENGINE_CORE_ERROR("{}, failed to create scene node", __FUNCTION__)
            return node_id;
        }

        HierarchiesNodeRef[node_id] = metadata;

        NodeNames[node_id]          = Names.size();
        auto&   name                = Names.push_use({});
        cstring name_val            = "Empty entity";

        if (HierarchiesNodeRef[node_id].IsValid())
        {
            if (ZEngine::Helpers::secure_strlen(metadata.Name) > 0)
            {
                name_val = metadata.Name;
            }
        }
        name.init(&LocalArena, name_val);

        HasPendingChanges.store(true, std::memory_order_release);

        TransformBufferDirty[0].store(true, std::memory_order_release);
        TransformBufferDirty[1].store(true, std::memory_order_release);
        TransformBufferDirty[2].store(true, std::memory_order_release);
        return node_id;
    }

    void EditorScene::RemoveSceneNode(int node_id)
    {
        if (node_id < 0 || node_id >= static_cast<int>(Hierarchies.size()))
            return;

        if (IsSceneNodeDeleted(node_id))
            return;

        auto& node = Hierarchies[node_id];

        // Unlink from parent's child list
        if (node.Parent >= 0)
        {
            auto& parent  = Hierarchies[node.Parent];
            int   prev    = -1;
            int   current = parent.FirstChild;

            while (current != -1)
            {
                if (current == node_id)
                {
                    if (prev == -1)
                        parent.FirstChild = node.RightSibling;
                    else
                        Hierarchies[prev].RightSibling = node.RightSibling;
                    break;
                }

                prev    = current;
                current = Hierarchies[current].RightSibling;
            }
        }

        std::stack<int> to_delete;
        to_delete.push(node_id);

        while (!to_delete.empty())
        {
            int current_id = to_delete.top();
            to_delete.pop();

            auto& current_node = Hierarchies[current_id];

            // Push children to delete stack
            int   child        = current_node.FirstChild;
            while (child != -1)
            {
                int next_sibling = Hierarchies[child].RightSibling;
                to_delete.push(child);
                child = next_sibling;
            }

            // Mark as deleted
            current_node.Parent       = -2;
            current_node.FirstChild   = -1;
            current_node.RightSibling = -1;
            current_node.DepthLevel   = -1;

            Names[current_id].clear();
            Names[current_id].append("(deleted)");
        }

        HasPendingChanges.store(true, std::memory_order_release);
    }

    void EditorScene::ReparentNode(int node_id, int new_parent)
    {
        auto& node       = Hierarchies[node_id];
        int   old_parent = node.Parent;

        // unlink from old parent
        if (old_parent >= 0)
        {
            auto& parent  = Hierarchies[old_parent];
            int   prev    = -1;
            int   current = parent.FirstChild;

            while (current != -1)
            {
                if (current == node_id)
                {
                    if (prev == -1)
                        parent.FirstChild = node.RightSibling;
                    else
                        Hierarchies[prev].RightSibling = node.RightSibling;
                    break;
                }

                prev    = current;
                current = Hierarchies[current].RightSibling;
            }
        }

        // link to new parent
        node.Parent                        = new_parent;
        node.RightSibling                  = Hierarchies[new_parent].FirstChild;
        Hierarchies[new_parent].FirstChild = node_id;
        node.DepthLevel                    = Hierarchies[new_parent].DepthLevel + 1;
    }

    bool EditorScene::IsSceneNodeDeleted(int node)
    {
        if (node < 0)
        {
            return true;
        }
        return Hierarchies[node].Parent == -2;
    }

    const ZEngine::Rendering::Meshes::MeshAllocation& EditorScene::CreateOrGetMeshAllocation(ZEngine::Importers::AssetMesh* const mesh)
    {
        if (MeshAllocations.contains(mesh->MeshUUID))
        {
            MeshAllocations[mesh->MeshUUID].InstanceCount++;
        }
        else
        {
            auto vert_buff_dst_write_offset = Vertices.data() + CurrentVertexOffset;
            auto vert_buff_dst_byte_size    = sizeof(float) * Vertices.size();

            auto vert_buff_src_write_offset = mesh->Vertices.data();
            auto vert_buff_src_byte_size    = sizeof(float) * mesh->Vertices.size();

            auto indx_buff_dst_write_offset = Indices.data() + CurrentIndexOffset;
            auto indx_buff_dst_byte_size    = sizeof(uint32_t) * Indices.size();

            auto indx_buff_src_write_offset = mesh->Indices.data();
            auto indx_buff_src_byte_size    = sizeof(uint32_t) * mesh->Indices.size();

            ZEngine::Helpers::secure_memcpy(vert_buff_dst_write_offset, vert_buff_dst_byte_size, vert_buff_src_write_offset, vert_buff_src_byte_size);
            ZEngine::Helpers::secure_memcpy(indx_buff_dst_write_offset, indx_buff_dst_byte_size, indx_buff_src_write_offset, indx_buff_src_byte_size);

            MeshAllocations[mesh->MeshUUID].VertexOffset            = CurrentVertexOffset;
            MeshAllocations[mesh->MeshUUID].IndexOffset             = CurrentIndexOffset;
            MeshAllocations[mesh->MeshUUID].VertexCount             = mesh->Vertices.size();
            MeshAllocations[mesh->MeshUUID].IndexCount              = mesh->Indices.size();
            MeshAllocations[mesh->MeshUUID].SubMeshAllocationCount  = mesh->SubMeshes.size();
            MeshAllocations[mesh->MeshUUID].InstanceCount           = 1;

            CurrentVertexOffset                                    += mesh->Vertices.size();
            CurrentIndexOffset                                     += mesh->Indices.size();
        }

        MeshAllocationDirty[0].store(true, std::memory_order_release);
        MeshAllocationDirty[1].store(true, std::memory_order_release);
        MeshAllocationDirty[2].store(true, std::memory_order_release);

        return MeshAllocations.at(mesh->MeshUUID);
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
        asset_file.Path.init(&(LocalArena), data.Path.c_str());
        asset_file.RootPath.init(&(LocalArena), data.RootPath.c_str());

        if (HashToAssetFile.contains(asset_file.Hash))
        {
            ZENGINE_CORE_WARN("Asset file already exist at that location : {}", asset_file.Path.c_str())
            return;
        }

        auto index = AssetFiles.size();
        AssetFiles.push(asset_file);
        HashToAssetFile.insert(asset_file.Hash, index);

        HasPendingChanges.store(true, std::memory_order_release);
    }

    void EditorScene::MarkDirty(bool value)
    {
        Dirty.store(value, std::memory_order_release);
    }

    bool EditorScene::IsDirty()
    {
        return Dirty.load(std::memory_order_acquire);
    }

    void EditorScene::Reset()
    {
        AssetFiles.clear();
        HashToAssetFile.clear();

        Hierarchies.clear();
        HierarchiesNodeRef.clear();
        Names.clear();
        LocalTransforms.clear();
        GlobalTransforms.clear();
        NodeNames.clear();

        Dirty.store(false, std::memory_order_release);
    }

    void EditorScene::InitRootNode()
    {
        NodeNames.insert(0, 0);
        auto& root_name = Names.push_use({});
        root_name.init(&LocalArena, Name);

        LocalTransforms.push(glm::mat4(1.0f));
        GlobalTransforms.push(glm::mat4(1.0f));

        auto& node = Hierarchies.push_use({});
        HierarchiesNodeRef.push({});
        node.DepthLevel = 0;
    }

    void EditorScene::ExtractAsync(const EditorScene& scene)
    {
        for (const auto& file : scene.AssetFiles)
        {
            auto& f = AssetFiles.push_use({});
            f.Hash  = file.Hash;
            f.Type  = file.Type;
            f.Path.init(&(LocalArena), file.Path.c_str());
            f.RootPath.init(&(LocalArena), file.RootPath.c_str());
        }

        for (const auto& name : scene.Names)
        {
            auto& n = Names.push_use({});
            n.init(&(LocalArena), name.c_str());
        }

        for (const auto& h : scene.Hierarchies)
        {
            Hierarchies.push(h);
            HierarchiesNodeRef.push({});
        }

        for (const auto& lt : scene.LocalTransforms)
        {
            LocalTransforms.push(lt);
        }

        for (const auto& gt : scene.GlobalTransforms)
        {
            GlobalTransforms.push(gt);
        }

        for (const auto& [k, v] : scene.NodeNames)
        {
            NodeNames.insert(k, v);
        }

        auto asset_manager = AssetManager::Instance();

        for (const auto& file : AssetFiles)
        {
            asset_manager->LoadAssetFile(ZEngine::Importers::AssetImporterOutput{.Type = file.Type, .Path = file.Path.c_str(), .RootPath = file.RootPath.c_str()});
        }
    }
} // namespace Tetragrama