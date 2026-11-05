#pragma once
#include <ZEngine/Cook/CookMemory.h>
#include <ZEngine/Cook/CookObject.h>
#include <ZEngine/Core/VFS/VFSRootLease.h>

namespace ZEngine::Cook
{
    // Borrowed read/checked-close authority only. Root and every open wrapper
    // outlive each operation. The caller holds the cooperating output writer
    // lock across verification, reuse and publication; no live VFS re-resolution.
    struct CookObjectReadFns
    {
        void* Context                                                                                = nullptr;
        Core::VFS::VFSResult<Core::VFS::IVFSFile*> (*Open)(void*, const Core::VFS::VFSRelativePath&) = nullptr;
        Core::VFS::VFSResult<void> (*CloseChecked)(void*, Core::VFS::IVFSFile*)                      = nullptr;
    };

    [[nodiscard]] CookObjectReadFns          CookObjectReader(Core::VFS::VFSPublicationRootLease&) noexcept;
    [[nodiscard]] CookObjectReadFns          CookObjectReader(Core::VFS::VFSReadRootLease&) noexcept;

    // Allocation-free byte verification using caller-charged streaming storage.
    // Principal hash/digest/token state is charged through the mandatory checked
    // allocator before stat/read work; unknown fixed-budget contracts fail closed.
    // This is not complete helper/compiler/hash-internal call-stack accounting.
    // Short reads are supported; early EOF, trailing bytes, changed size/type or
    // a digest mismatch fail. This does not decode/authorize an asset format.
    [[nodiscard]] Core::VFS::VFSResult<void> VerifyCookObjectFile(Core::VFS::IVFSFile&, const CookObjectDescriptor&, Core::Containers::ArrayView<uint8_t> stream, ICookAllocator&);

    // Verify a private staged file before its digest-derived name is installed.
    // The staging path is separate from the expected object's final descriptor.
    // Missing staging files are errors; every successful open is checked-closed.
    // Invalid arguments fail before budget queries; fixed state is charged before
    // Open and retained through checked-close, without a second nested charge.
    [[nodiscard]] Core::VFS::VFSResult<void> VerifyCookStagedObject(const CookObjectReadFns&, const Core::VFS::VFSRelativePath& staging, const CookObjectDescriptor&, Core::Containers::ArrayView<uint8_t> stream, ICookAllocator&);

    // Validate the digest-derived path before opening. False means ONLY Open's
    // NotFound; all read/stat/close errors are failures, never cache misses. Every
    // successful open is checked-closed, including failures. No own allocation;
    // backend wrapper/snapshot storage remains part of the run's VFS accounting.
    [[nodiscard]] Core::VFS::VFSResult<bool> VerifyCookObject(const CookObjectReadFns&, const CookObjectDescriptor&, Core::Containers::StringView extension, Core::Containers::ArrayView<uint8_t> stream, ICookAllocator&);
} // namespace ZEngine::Cook
