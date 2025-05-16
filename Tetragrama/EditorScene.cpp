#include <pch.h>
#include <EditorScene.h>
#include <Managers/AssetManager.h>
#include <stack>

namespace Tetragrama
{
    void EditorScene::Initialize(ZEngine::Core::Memory::ArenaAllocator* arena, const char* name)
    {
        arena->CreateSubArena(ZMega(200), &LocalArena);

        Name = name;

        AssetFiles.init(&LocalArena, 500);
        HashToAssetFile.init(&LocalArena, 500);

        Hierarchies.init(&LocalArena, 1000);
        Names.init(&LocalArena, 1000);
        LocalTransforms.init(&LocalArena, 1000);
        GlobalTransforms.init(&LocalArena, 1000);
        NodeMeshes.init(&LocalArena, 1000);
        NodeNames.init(&LocalArena, 1000);

        /*
         * Root Scene node
         */
        Reset();

        InitRootNode();

        RenderScene                  = ZPushStructCtor(&LocalArena, ZEngine::Rendering::Scenes::GraphicScene);
        RenderScene->IsDrawDataDirty = true;
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

        int                    node_id = static_cast<int>(Hierarchies.size());

        // Create new node
        Helpers::NodeHierarchy new_node;
        new_node.Parent       = parent;
        new_node.DepthLevel   = depth;
        new_node.FirstChild   = -1;
        new_node.LastChild    = -1;
        new_node.RightSibling = -1;

        Hierarchies.push(new_node);
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

    void EditorScene::CreateSceneNode(int parent, int depth)
    {
        int node_id = AddHierarchyNode(parent, depth);
        if (node_id < 0)
        {
            ZENGINE_CORE_ERROR("EditorScene::CreateSceneNode, failed to create scene node")
            return;
        }

        NodeNames[node_id] = Names.size();
        auto& name         = Names.push_use({});
        name.init(&LocalArena, "Empty entity");

        NodeMeshes.insert(node_id, uuids::uuid{});

        HasPendingChanges.store(true, std::memory_order_release);
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

    void EditorScene::PushAssetFile(const Importers::AssetImporterOutput& data)
    {
        if (data.Type == Importers::AssetFileType::UNKNOWN)
        {
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
        Names.clear();
        LocalTransforms.clear();
        GlobalTransforms.clear();
        NodeMeshes.clear();
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

        Hierarchies.push(EditorSceneNodeHierarchy{.Parent = -1, .FirstChild = -1, .DepthLevel = 0});
        NodeMeshes.insert(0, uuids::uuid{});
    }

    void EditorScene::ExtractAsync(EditorScene& scene)
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
        }

        for (const auto& lt : scene.LocalTransforms)
        {
            LocalTransforms.push(lt);
        }

        for (const auto& gt : scene.GlobalTransforms)
        {
            GlobalTransforms.push(gt);
        }

        auto node_mesh_view = scene.NodeMeshes.view();
        for (const auto& [k, v] : node_mesh_view)
        {
            NodeMeshes.insert(k, v);
        }

        auto node_name_view = scene.NodeNames.view();
        for (const auto& [k, v] : node_name_view)
        {
            NodeNames.insert(k, v);
        }

        auto asset_manager = Managers::AssetManager::Instance();

        for (const auto& file : AssetFiles)
        {
            asset_manager->LoadAssetFile(Importers::AssetImporterOutput{.Type = file.Type, .Path = file.Path.c_str(), .RootPath = file.RootPath.c_str()});
        }
    }
} // namespace Tetragrama