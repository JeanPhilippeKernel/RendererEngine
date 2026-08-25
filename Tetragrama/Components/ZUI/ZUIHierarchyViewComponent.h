#pragma once
#include <Tetragrama/Components/ZUI/ZUIComponent.h>
#include <ZEngine/Core/Containers/Array.h>
#include <ZEngine/Core/Memory/Allocator.h>
#include <ZEngine/ECS/EntityID.h>

namespace Tetragrama::Components
{
    class ZUIHierarchyViewComponent : public ZUIComponent
    {
    public:
        ZUIHierarchyViewComponent()          = default;
        ~ZUIHierarchyViewComponent() override = default;

        void Initialize(Tetragrama::Layers::ZUILayer* parent,
                        cstring name       = "Hierarchy",
                        bool    visibility = true) override;

        void BuildUI(ZEngine::UI::ZUIContext* ctx) override;

    private:
        // Arena for collapsed-set and scratch DFS allocations
        ZEngine::Core::Memory::ArenaAllocator                    m_arena      = {};
        ZEngine::Core::Containers::Array<ZEngine::ECS::EntityID> m_collapsed  = {};
        bool                                                     m_root_open  = true;

        bool IsCollapsed(ZEngine::ECS::EntityID eid) const;
        void ToggleCollapsed(ZEngine::ECS::EntityID eid);
    };
    ZDEFINE_PTR(ZUIHierarchyViewComponent);
} // namespace Tetragrama::Components
