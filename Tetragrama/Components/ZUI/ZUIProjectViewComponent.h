#pragma once
#include <Tetragrama/Components/ZUI/ZUIComponent.h>
#include <ZEngine/Core/Memory/Allocator.h>
#include <ZEngine/Core/VFS/VFSPath.h>

namespace Tetragrama::Components
{
    class ZUIProjectViewComponent : public ZUIComponent
    {
    public:
        ZUIProjectViewComponent()          = default;
        ~ZUIProjectViewComponent() override = default;

        void Initialize(Tetragrama::Layers::ZUILayer* parent,
                        cstring name       = "Project",
                        bool    visibility = true) override;

        void BuildUI(ZEngine::UI::ZUIContext* ctx) override;

    private:
        ZEngine::Core::Memory::ArenaAllocator m_arena       = {};
        ZEngine::Core::VFS::VFSPath           m_current_path = {};
        bool                                   m_initialized  = false;
    };
    ZDEFINE_PTR(ZUIProjectViewComponent);
} // namespace Tetragrama::Components
