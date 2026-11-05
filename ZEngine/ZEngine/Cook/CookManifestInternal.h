#pragma once
#include <ZEngine/Cook/CookManifest.h>
#include <ZEngine/Cook/CookSort.h>

namespace ZEngine::Cook::Detail
{
    struct CookManifestSyntaxHeader
    {
        uint32_t RecipeRevision = 0;
    };

    // Full syntax/Unicode/decoded-key uniqueness before schema parsing or any
    // cache-miss decision. Unknown nested objects are checked too. One exact
    // temporary key-index allocation, released on every exit; no DOM. Reuses the
    // typed reader's decode buffer (at least 4097 bytes), without retaining it.
    [[nodiscard]] Core::VFS::VFSResult<CookManifestSyntaxHeader> CheckCookManifestSyntax(Core::Containers::ArrayView<const uint8_t>, CookTarget, ICookAllocator&, CookDiagnostics*, Core::Containers::ArrayView<char>);
    [[nodiscard]] CookDiagnosticCode                             ManifestResourceCode(Core::VFS::VFSError) noexcept;
    Core::VFS::VFSError                                          ManifestDiagnostic(CookDiagnostics*, CookDiagnosticCode, Core::VFS::VFSError, cstring, CookSeverity = CookSeverity::Error);
    // Descriptor spelling is checked separately. Both reader and emitter reuse
    // this allocation-free conflict check with caller-owned sorting workspace.
    template <typename ObjectAt>
    [[nodiscard]] Core::VFS::VFSResult<void> ValidateManifestObjectSizes(uint32_t count, Core::Containers::ArrayView<uint32_t> workspace, CookSortState<uint32_t>& sort, const ObjectAt& object)
    {
        if (workspace.size() && !workspace.data())
        {
            return Core::VFS::VFSResult<void>::Fail(Core::VFS::VFSError::InvalidPath);
        }
        if (workspace.size() < count)
        {
            return Core::VFS::VFSResult<void>::Fail(Core::VFS::VFSError::SizeLimitExceeded);
        }
        for (uint32_t i = 0; i < count; ++i)
        {
            workspace[i] = i;
        }
        HeapSort<uint32_t>({workspace.data(), count}, sort, [&](uint32_t left, uint32_t right) { return CookDependencySlotBefore(object(left).Path.View(), object(right).Path.View()); });
        for (uint32_t i = 1; i < count; ++i)
        {
            if (object(workspace[i - 1]).Path == object(workspace[i]).Path && object(workspace[i - 1]).Size != object(workspace[i]).Size)
            {
                return Core::VFS::VFSResult<void>::Fail(Core::VFS::VFSError::Corrupted);
            }
        }
        return Core::VFS::VFSResult<void>::Ok();
    }
} // namespace ZEngine::Cook::Detail
