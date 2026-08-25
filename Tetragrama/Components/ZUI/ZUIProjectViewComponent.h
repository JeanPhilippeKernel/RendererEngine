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
        struct CachedEntry
        {
            char name[256]      = {};
            char full_path[512] = {};
            bool is_dir         = false;
        };

        static constexpr uint32_t kMaxEntries = 256;

        ZEngine::Core::Memory::ArenaAllocator m_arena        = {};
        ZEngine::Core::VFS::VFSPath           m_current_path = {};
        ZEngine::Core::VFS::VFSPath           m_listed_path  = {};
        CachedEntry*                          m_entries      = nullptr;
        uint32_t                              m_entry_count  = 0;
        bool                                  m_initialized  = false;

        // Re-lists only when m_current_path != m_listed_path — NOT every frame
        void RefreshIfNeeded();
    };
    ZDEFINE_PTR(ZUIProjectViewComponent);
} // namespace Tetragrama::Components
