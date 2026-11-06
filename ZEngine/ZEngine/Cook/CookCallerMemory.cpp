#include <ZEngine/Cook/CookCallbackStorage.h>
#include <ZEngine/Cook/CookCallerMemory.h>
#include <ZEngine/Cook/CookServiceMemoryInternal.h>
#include <ZEngine/Cook/SceneCooker.h>
#include <algorithm>

namespace ZEngine::Cook
{
    namespace VFS = Core::VFS;

    namespace
    {
        struct ValueRange
        {
            const void* Object = nullptr;
            size_t      Bytes  = 0;
        };
    } // namespace

    size_t Detail::CookNativeServiceMemory::CallerQueryWorkingBytes() noexcept
    {
        // Five-entry table, insertion's copied current range, union counters
        // and the returned scalar/result. No scene-count-sized workspace.
        return sizeof(Core::Containers::FixedArray<ValueRange, 5>) + sizeof(ValueRange) + 7 * sizeof(uintptr_t) + 3 * sizeof(size_t) + sizeof(uint64_t) + sizeof(VFS::VFSResult<uint64_t>);
    }

    VFS::VFSResult<uint64_t> Detail::CookNativeServiceMemory::CallerRequirements(const SceneCookRequest& request, const SceneCookContext& context, const SceneCookResult* result, const CookDiagnostics* diagnostics) const noexcept
    {
        using Result = VFS::VFSResult<uint64_t>;
        if (!result || !diagnostics || request.Sources.empty() || !request.Sources.data())
        {
            return Result::Fail(VFS::VFSError::InvalidPath);
        }
        if (request.Sources.size() > SIZE_MAX / sizeof(VFS::VFSPath))
        {
            return Result::Fail(VFS::VFSError::SizeLimitExceeded);
        }
        // Five values only, independent of scene count. Compact/insert by integer
        // address: at most ten shifts, no recursive sort or redundant endpoints.
        // Writes never reach beyond the current input element.
        Core::Containers::FixedArray<ValueRange, 5> ranges = {
            {
             {&request, sizeof(request)},
             {&context, sizeof(context)},
             {request.Sources.data(), request.Sources.size_bytes()},
             {result, sizeof(*result)},
             {diagnostics, sizeof(*diagnostics)},
             }
        };
        size_t count = 0;
        for (auto range : ranges)
        {
            const auto begin = reinterpret_cast<uintptr_t>(range.Object);
            if (range.Bytes > UINTPTR_MAX - begin)
            {
                return Result::Fail(VFS::VFSError::SizeLimitExceeded);
            }
            if (Contains(range.Object, range.Bytes))
            {
                continue;
            }
            size_t position = count++;
            while (position && reinterpret_cast<uintptr_t>(ranges[position - 1].Object) > begin)
            {
                ranges[position] = ranges[position - 1];
                --position;
            }
            ranges[position] = range;
        }
        uint64_t  bytes = 0;
        uintptr_t end   = 0;
        for (size_t index = 0; index < count; ++index)
        {
            const auto& range = ranges[index];
            const auto  begin = reinterpret_cast<uintptr_t>(range.Object);
            const auto  limit = begin + range.Bytes;
            // A nested range contributes nothing; avoid unsigned subtraction.
            if (limit <= end)
            {
                continue;
            }
            const auto addition = limit - std::max(end, begin);
            if (addition > UINT64_MAX - bytes)
            {
                return Result::Fail(VFS::VFSError::SizeLimitExceeded);
            }
            bytes += addition;
            end    = limit;
        }
        return Result::Ok(bytes);
    }

    VFS::VFSResult<uint64_t> QueryCookCallerMemory(const SceneCookRequest& request, const SceneCookContext& context, const SceneCookResult* result, const CookDiagnostics* diagnostics) noexcept
    {
        if (!result || !diagnostics || request.Sources.empty() || !request.Sources.data())
        {
            return VFS::VFSResult<uint64_t>::Fail(VFS::VFSError::InvalidPath);
        }
        if (request.Sources.size() > SIZE_MAX / sizeof(VFS::VFSPath))
        {
            return VFS::VFSResult<uint64_t>::Fail(VFS::VFSError::SizeLimitExceeded);
        }
        Detail::CookNativeServiceMemory services    = {};
        const auto                      initialized = services.Initialize(context, context.Callbacks ? context.Callbacks->StorageArena() : nullptr);
        return initialized.Succeeded() ? services.CallerRequirements(request, context, result, diagnostics) : VFS::VFSResult<uint64_t>::Fail(initialized.Error());
    }
} // namespace ZEngine::Cook
