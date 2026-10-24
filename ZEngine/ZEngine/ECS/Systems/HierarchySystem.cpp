#include <ZEngine/ECS/Systems/HierarchySystem.h>

namespace ZEngine::ECS::Systems
{
    void SyncHierarchy(ECS::Scene& scene)
    {
        scene.SynchronizeHierarchy();
    }
} // namespace ZEngine::ECS::Systems
