#include <ZEngine/Core/Maths/Matrix.h>
#include <ZEngine/Importers/IAssetImporter.h>

using namespace ZEngine::Core::Maths;

namespace ZEngine::Importers
{
    int AddNode(AssetNodeHierarchy& hierarchy, int parent, int depth)
    {
        if (depth < 0)
            return -1;

        int node_id = static_cast<int>(hierarchy.Hierarchies.size());
        hierarchy.Hierarchies.push({.Parent = parent});
        hierarchy.LocalTransforms.push(Identity<Mat4f>());
        hierarchy.GlobalTransforms.push(Identity<Mat4f>());

        if (parent > -1)
        {
            int first_child = hierarchy.Hierarchies[parent].FirstChild;
            if (first_child == -1)
            {
                hierarchy.Hierarchies[parent].FirstChild = node_id;
            }
            else
            {
                int rs = first_child;
                while (hierarchy.Hierarchies[rs].RightSibling != -1)
                    rs = hierarchy.Hierarchies[rs].RightSibling;
                hierarchy.Hierarchies[rs].RightSibling = node_id;
            }
        }

        hierarchy.Hierarchies[node_id].DepthLevel   = depth;
        hierarchy.Hierarchies[node_id].RightSibling = -1;
        hierarchy.Hierarchies[node_id].FirstChild   = -1;
        return node_id;
    }
} // namespace ZEngine::Importers
