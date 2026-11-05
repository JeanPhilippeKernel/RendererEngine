#include <ZEngine/Cook/CookCallbackStorage.h>
#include <ZEngine/Cook/CookServiceMemoryInternal.h>
#include <ZEngine/Cook/CookVFSServiceMemory.h>
#include <ZEngine/Cook/SceneCooker.h>

namespace ZEngine::Cook
{
    Core::VFS::VFSResult<CookServiceMemoryRequirements> QueryCookVFSServiceMemory(const SceneCookContext& context) noexcept
    {
        Detail::CookNativeServiceMemory memory      = {};
        const auto                      initialized = memory.Initialize(context, context.Callbacks ? context.Callbacks->StorageArena() : nullptr);
        return initialized.Succeeded() ? memory.VFSRequirements() : Core::VFS::VFSResult<CookServiceMemoryRequirements>::Fail(initialized.Error());
    }
} // namespace ZEngine::Cook
