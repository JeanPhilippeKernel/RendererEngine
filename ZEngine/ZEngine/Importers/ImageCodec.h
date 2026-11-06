#pragma once
#include <ZEngine/Core/Memory/AllocationRoute.h>
#include <ZEngine/Importers/AssetByteSource.h>
#include <cstddef>

namespace ZEngine::Importers::ImageCodec
{
    using IAllocationRoute = Core::Memory::IAllocationRoute;

    // Thread-local, nestable, and allocation-free. Decode, release all returned
    // pixels, and complete all decoder work within the scope. Do not pass blocks
    // from another route into it. The borrowed route outlives the scope, which
    // must be destroyed on its creating thread in reverse construction order.
    class AllocationScope
    {
    public:
        explicit AllocationScope(IAllocationRoute& route) noexcept;
        ~AllocationScope();
        AllocationScope(const AllocationScope&)            = delete;
        AllocationScope& operator=(const AllocationScope&) = delete;

    private:
        IAllocationRoute* m_previous = nullptr;
        IAllocationRoute* m_current  = nullptr;
    };

    enum class EncodedImageKind : uint8_t
    {
        Unknown = 0,
        PNG,
        JPEG,
        BMP,
        TGA,
        GIF,
        PSD,
        PIC,
    };

    struct EncodedImageLimits
    {
        uint64_t MaxEncodedBytes = ZMega(512);
        uint64_t MaxDecodedBytes = ZMega(512);
        uint32_t MaxDimension    = 16384;
    };

    struct EncodedImageDescription
    {
        EncodedImageKind Kind           = EncodedImageKind::Unknown;
        uint32_t         Width          = 0;
        uint32_t         Height         = 0;
        uint32_t         SourceChannels = 0;
        uint32_t         BitsPerChannel = 0;
    };

    // Supported encoded LDR formats only; GIF validates its first frame, as the
    // runtime decoder does. Recognize format bytes without allocating decoder
    // state, then preflight dimensions/native RGBA
    // storage, then fully decode and release pixels before returning. No encoded
    // file copy, native I/O, renderer, global decode-option mutation, or retained
    // pixels. Every decoder allocation uses the required borrowed route. Caller
    // must also check any sticky allocation error recorded by that route.
    // A supplied working budget additionally charges principal common/PNG/zlib/
    // GIF fixed objects before their phase, not a complete compiler-frame or
    // recursive/helper stack bound. Ordinary editor routes may be budgetless;
    // bounded cook routes must supply their actual budget.
    // Failure preserves the previous description. The source must be immutable.
    // A fixed 16 KiB, allocation-free read-ahead buffer amortizes tiny decoder
    // reads only after dimension/native-storage preflight. It is included in
    // principal fixed accounting; larger reads bypass this buffer.
    [[nodiscard]] Core::VFS::VFSResult<void> ValidateEncodedImage(const AssetCodec::AssetByteSource&, IAllocationRoute&, const EncodedImageLimits&, EncodedImageDescription*);

    // Outside an explicit scope, preserve the existing worker-slab/system-heap
    // route. These also serve short-lived encoded buffers alongside stb pixels.
    [[nodiscard]] void*                      Allocate(size_t bytes) noexcept;
    [[nodiscard]] void*                      Reallocate(void* pointer, size_t old_bytes, size_t new_bytes) noexcept;
    void                                     Free(void* pointer) noexcept;
} // namespace ZEngine::Importers::ImageCodec
