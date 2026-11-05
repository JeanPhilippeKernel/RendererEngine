#include <ZEngine/Cook/CookAdapter.h>
#include <ZEngine/Cook/CookCallbackStorage.h>
#include <ZEngine/Cook/CookServiceMemoryInternal.h>
#include <ZEngine/Cook/SceneCooker.h>
#include <ZEngine/ECS/SceneCompatibility.h>
#include <typeinfo>

namespace ZEngine::Cook
{
    namespace VFS = Core::VFS;

    namespace
    {
        struct ContextCoverage
        {
            const ICookCallbackStorage* Owner     = nullptr;
            bool                        Uncovered = false;

            void                        Check(const void* root) noexcept
            {
                if (root && !Uncovered && (!Owner || !Owner->Contains(root)))
                    Uncovered = true;
            }
        };

        VFS::VFSResult<CookServiceMemoryRequirements> QueryCallbacks(const SceneCookContext& context, const ICookAllocator& covered, const Detail::CookNativeServiceMemory* services) noexcept
        {
            using Result = VFS::VFSResult<CookServiceMemoryRequirements>;
            if (!context.Schemas || !context.Schemas->IsInitialized() || !context.Adapters || !context.Adapters->IsInitialized() || (context.Migrations && !context.Migrations->IsInitialized()))
                return Result::Fail(VFS::VFSError::InvalidPath);
            ContextCoverage coverage = {.Owner = context.Callbacks};
            context.Schemas->ForEachCanonical(
                [](void* state, const ECS::SceneComponentSchema& schema) {
                    auto& check = *static_cast<ContextCoverage*>(state);
                    check.Check(schema.Codecs.Context);
                    check.Check(schema.References.Context);
                },
                &coverage);
            for (const auto& entry : context.Adapters->Entries())
                coverage.Check(entry.Context);
            if (context.Migrations)
                context.Migrations->ForEachContext([](void* state, const void* root) { static_cast<ContextCoverage*>(state)->Check(root); }, &coverage);
            if (coverage.Uncovered)
                return Result::Fail(VFS::VFSError::Unsupported);
            if (!context.Callbacks)
                return Result::Ok({});
            // Only the final native owner promises that its arena is the entire
            // graph. A custom StorageArena declaration is not that promise and
            // must never bypass the owner's complete MemoryRequirements query.
            if (services && typeid(*context.Callbacks) == typeid(CookArenaCallbackStorage))
                return services->CallbackRequirements(context.Callbacks, sizeof(CookArenaCallbackStorage));
            return context.Callbacks->MemoryRequirements(context, covered);
        }
    } // namespace

    size_t Detail::CookNativeServiceMemory::ServiceQueryWorkingBytes() noexcept
    {
        // Native registry/callback/VFS projections share one requirements
        // payload and return path. Include the Ok argument, coverage visitor,
        // adapter view and VFS overlap/owner counters; native callbacks reuse
        // this snapshot instead of constructing another one.
        return 2 * sizeof(CookServiceMemoryRequirements) + sizeof(VFS::VFSResult<CookServiceMemoryRequirements>) + sizeof(ContextCoverage) + sizeof(Core::Containers::ArrayView<const CookAdapterFns>) + 10 * sizeof(uintptr_t);
    }

    VFS::VFSResult<CookServiceMemoryRequirements> QueryCookCallbackMemory(const SceneCookContext& context, const ICookAllocator& covered) noexcept
    {
        return QueryCallbacks(context, covered, nullptr);
    }

    VFS::VFSResult<CookServiceMemoryRequirements> Detail::CookNativeServiceMemory::CallbackRequirements(const SceneCookContext& context, const ICookAllocator& covered) const noexcept
    {
        return QueryCallbacks(context, covered, this);
    }

    bool CookArenaCallbackStorage::Contains(const void* context) const noexcept
    {
        return m_arena && m_arena->RetainsStorage(context, 1);
    }

    VFS::VFSResult<CookServiceMemoryRequirements> CookArenaCallbackStorage::MemoryRequirements(const SceneCookContext& context, const ICookAllocator&) const noexcept
    {
        using Result = VFS::VFSResult<CookServiceMemoryRequirements>;
        if (!m_arena)
            return Result::Fail(VFS::VFSError::Unsupported);
        Detail::CookNativeServiceMemory memory      = {};
        const auto                      initialized = memory.Initialize(context, m_arena);
        return initialized.Succeeded() ? memory.CallbackRequirements(this, sizeof(*this)) : Result::Fail(initialized.Error());
    }
} // namespace ZEngine::Cook
