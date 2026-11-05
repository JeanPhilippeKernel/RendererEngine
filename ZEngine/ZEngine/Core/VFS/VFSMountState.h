#pragma once
#include <ZEngine/Core/Containers/FixedArray.h>
#include <ZEngine/Core/Memory/NativePages.h>
#include <ZEngine/Core/VFS/VFSMountTable.h>
#include <mutex>

namespace ZEngine::Core::VFS
{
    // Independent of the context facade and arena. Shutdown closes the domain to
    // new operations; leases defer release of the retained mapping/provider state.
    struct VFSMountState final : Helpers::RefCounted, Memory::NativePageObject<VFSMountState>
    {
        static constexpr uint32_t MaxPins = 64;
        struct Pin
        {
            VFSPath Root   = {};
            bool    Active = false;
        };

        Containers::FixedArray<MountPoint, VFSMountTable::kMaxMountPoints> Mounts   = {};
        Containers::FixedArray<Pin, MaxPins>                               Pins     = {};
        size_t                                                             Count    = 0;
        bool                                                               Closed   = false;
        // Mount data is guarded by the table's mutex. Pin release may outlive that
        // table, so pin records use this independently retained synchronization.
        mutable std::mutex                                                 PinMutex = {};

        bool                                                               HasPins() const;
        bool                                                               OverlapsPin(const VFSPath&) const;
        void                                                               Retire();
        void                                                               ReleasePin(uint32_t index) noexcept;

    private:
        void ReleaseClosedMappings(); // PinMutex held; closed tables do not read Mounts.
    };
} // namespace ZEngine::Core::VFS
