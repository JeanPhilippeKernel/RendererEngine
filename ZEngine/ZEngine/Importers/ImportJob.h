#pragma once
#include <ZEngine/Core/VFS/Meta/MetaFileData.h>
#include <ZEngine/Core/VFS/VFSPath.h>
#include <cstdint>
#include <cstring>

namespace ZEngine::Importers
{
    enum class ImportPriority : uint8_t
    {
        Background = 0, // deferred; runs when frame budget allows
        Normal     = 1, // standard import triggered by VFSScanner
        Immediate  = 2, // user-initiated or FileWatcher::Modified; runs next Tick
    };

    // Callback fired after Flush() applies the import result.
    // Plain fn-ptr + context to avoid std::function heap allocation.
    struct ImportCallback
    {
        void* Context                   = nullptr;
        void (*Fn)(void*, bool success) = nullptr;

        bool IsValid() const
        {
            return Fn != nullptr;
        }
        void Invoke(bool success) const
        {
            if (Fn)
                Fn(Context, success);
        }
    };

    static constexpr uint32_t IMPORT_DIAGNOSTIC_MAX = 256;

    struct ImportJob
    {
        Core::VFS::VFSPath      Path;
        Core::VFS::MetaFileData Meta;
        ImportPriority          Priority                                 = ImportPriority::Normal;
        ImportCallback          Callback                                 = {};
        uint32_t                RequeueCount                             = 0; // incremented on dependency stall; cap at 3
        char                    DiagnosticMessage[IMPORT_DIAGNOSTIC_MAX] = {};
    };
} // namespace ZEngine::Importers
