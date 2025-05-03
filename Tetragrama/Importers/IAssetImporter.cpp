#include <pch.h>
#include <IAssetImporter.h>

namespace Tetragrama::Importers
{
    int AddNode(AssetNodeHierarchy& hierarchy, int parent, int depth)
    {
        if (depth < 0)
        {
            return -1;
        }

        int node_id = (int) hierarchy.Hierarchies.size();

        hierarchy.Hierarchies.push({.Parent = parent});
        hierarchy.LocalTransforms.push(glm::mat4(1.0f));
        hierarchy.GlobalTransforms.push(glm::mat4(1.0f));

        if (parent > -1)
        {
            int first_child = hierarchy.Hierarchies[parent].FirstChild;

            if (first_child == -1)
            {
                hierarchy.Hierarchies[parent].FirstChild = node_id;
            }
            else
            {
                int right_sibling = hierarchy.Hierarchies[first_child].RightSibling;
                if (right_sibling > -1)
                {
                    // iterate nextSibling_ indices
                    for (right_sibling = first_child; hierarchy.Hierarchies[right_sibling].RightSibling != -1; right_sibling = hierarchy.Hierarchies[right_sibling].RightSibling)
                    {
                    }
                    hierarchy.Hierarchies[right_sibling].RightSibling = node_id;
                }
                else
                {
                    hierarchy.Hierarchies[first_child].RightSibling = node_id;
                }
            }
        }
        hierarchy.Hierarchies[node_id].DepthLevel   = depth;
        hierarchy.Hierarchies[node_id].RightSibling = -1;
        hierarchy.Hierarchies[node_id].FirstChild   = -1;

        return node_id;
    }
} // namespace Tetragrama::Importers