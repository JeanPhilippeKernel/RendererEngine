#include <ZEngine/Core/Maths/Matrix.h>
#include <ZEngine/Importers/IAssetImporter.h>
#include <algorithm>
#include <limits>

using namespace ZEngine::Core::Maths;

namespace ZEngine::Importers
{
    int AddNode(AssetNodeHierarchy& hierarchy, int parent, int depth)
    {
        const auto count = hierarchy.Hierarchies.size();
        if (count >= size_t(std::numeric_limits<int>::max()) || hierarchy.LocalTransforms.size() != count || hierarchy.GlobalTransforms.size() != count || depth < 0 || parent < -1 || (parent == -1 ? depth != 0 : size_t(parent) >= count || hierarchy.Hierarchies[parent].DepthLevel != depth - 1))
            return -1;

        int tail = -1;
        if (parent != -1)
        {
            const auto& owner = hierarchy.Hierarchies[parent];
            if (owner.FirstChild == -1)
            {
                if (owner.LastChild != -1)
                    return -1;
            }
            else
            {
                if (owner.FirstChild < 0 || size_t(owner.FirstChild) >= count || hierarchy.Hierarchies[owner.FirstChild].Parent != parent)
                    return -1;
                tail = owner.LastChild;
                if (tail == -1)
                {
                    // v1 files may lack the optional tail. Resolve it once;
                    // newly constructed hierarchies always append in O(1).
                    tail = owner.FirstChild;
                    for (size_t visited = 0;; ++visited)
                    {
                        if (tail < 0 || size_t(tail) >= count || visited >= count || hierarchy.Hierarchies[tail].Parent != parent)
                            return -1;
                        const auto next = hierarchy.Hierarchies[tail].RightSibling;
                        if (next == -1)
                            break;
                        tail = next;
                    }
                }
                if (tail < 0 || size_t(tail) >= count || hierarchy.Hierarchies[tail].Parent != parent || hierarchy.Hierarchies[tail].RightSibling != -1)
                    return -1;
            }
        }
        const auto reserve = [count](auto& array) { return count < array.capacity() || array.try_reserve(std::max(count + 1, array.capacity() ? array.capacity() * 2 : size_t{4})); };
        if (!reserve(hierarchy.Hierarchies) || !reserve(hierarchy.LocalTransforms) || !reserve(hierarchy.GlobalTransforms))
            return -1;

        int node_id = static_cast<int>(count);
        hierarchy.Hierarchies.push({.Parent = parent, .DepthLevel = depth});
        hierarchy.LocalTransforms.push(Identity<Mat4f>());
        hierarchy.GlobalTransforms.push(Identity<Mat4f>());

        if (parent > -1)
        {
            auto& owner = hierarchy.Hierarchies[parent];
            if (owner.FirstChild == -1)
            {
                owner.FirstChild = node_id;
            }
            else
            {
                // The append tail makes wide source hierarchies O(N), not O(N²).
                hierarchy.Hierarchies[tail].RightSibling = node_id;
            }
            owner.LastChild = node_id;
        }

        return node_id;
    }
} // namespace ZEngine::Importers
