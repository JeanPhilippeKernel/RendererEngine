#include <ZEngine/Core/Containers/FixedArray.h>
#include <ZEngine/Core/Memory/TLSFSlab.h>
#include <ZEngine/Helpers/MemoryOperations.h>
#include <ZEngine/Helpers/ThreadPool.h>
#include <ZEngine/Importers/ImageCodec.h>
#include <algorithm>
#include <cstdlib>
#include <limits>

// One shared CPU implementation. Cooking installs its checked route before
// probing/decoding, including decoder-internal allocations and reallocations.
#define STB_IMAGE_IMPLEMENTATION
#ifdef __GNUC__
#define STBI_NO_SIMD
#endif
// stb defines its implementation in the global namespace.
#define STBI_MALLOC(bytes)                                ZEngine::Importers::ImageCodec::Allocate(bytes)
#define STBI_REALLOC_SIZED(pointer, old_bytes, new_bytes) ZEngine::Importers::ImageCodec::Reallocate(pointer, old_bytes, new_bytes)
#define STBI_FREE(pointer)                                ZEngine::Importers::ImageCodec::Free(pointer)
#include <stb/stb_image.h>

namespace ZEngine::Importers::ImageCodec
{
    namespace
    {
        thread_local IAllocationRoute* CurrentRoute = nullptr;

        namespace VFS                               = Core::VFS;

        // stb can supply padding after EOF. Record attempts to consume outside
        // the immutable capture, so a padded successful decode is not accepted.
        struct ImageSource
        {
            const AssetCodec::AssetByteSource*               Source       = nullptr;
            uint64_t                                         Offset       = 0;
            VFS::VFSError                                    Error        = VFS::VFSError::OK;
            Core::Containers::FixedArray<uint8_t, ZKilo(16)> Buffer       = {};
            uint64_t                                         BufferOffset = 0;
            size_t                                           BufferBytes  = 0;
            bool                                             Buffered     = false;

            static int                                       Read(void* context, char* destination, int requested)
            {
                auto& self = *static_cast<ImageSource*>(context);
                if (self.Error != VFS::VFSError::OK || requested <= 0)
                {
                    return 0;
                }
                const auto count = static_cast<size_t>(std::min<uint64_t>(static_cast<uint32_t>(requested), self.Source->Size - self.Offset));
                if (!count)
                {
                    self.Error = VFS::VFSError::Corrupted;
                    return 0;
                }
                size_t done = 0;
                while (done < count)
                {
                    // Large decoder reads already amortize the source call and
                    // should not acquire another encoded-data copy. Header
                    // probes stay unbuffered until native pixel limits pass.
                    if (!self.Buffered || count - done >= self.Buffer.size())
                    {
                        const auto read = AssetCodec::ReadAssetByteRange(*self.Source, self.Offset, {reinterpret_cast<uint8_t*>(destination + done), count - done});
                        if (read.Failed())
                        {
                            self.Error = read.Error();
                            return 0;
                        }
                        self.Offset += count - done;
                        break;
                    }
                    if (self.Offset < self.BufferOffset || self.Offset - self.BufferOffset >= self.BufferBytes)
                    {
                        self.BufferOffset = self.Offset;
                        self.BufferBytes  = static_cast<size_t>(std::min<uint64_t>(self.Buffer.size(), self.Source->Size - self.Offset));
                        const auto read   = AssetCodec::ReadAssetByteRange(*self.Source, self.Offset, {self.Buffer.data(), self.BufferBytes});
                        if (read.Failed())
                        {
                            self.Error       = read.Error();
                            self.BufferBytes = 0;
                            return 0;
                        }
                    }
                    const size_t within = static_cast<size_t>(self.Offset - self.BufferOffset);
                    const size_t copied = std::min(count - done, self.BufferBytes - within);
                    (void) Helpers::secure_memcpy(destination + done, count - done, self.Buffer.data() + within, copied);
                    self.Offset += copied;
                    done        += copied;
                }
                return static_cast<int>(count);
            }

            static void Skip(void* context, int count)
            {
                auto& self = *static_cast<ImageSource*>(context);
                if (self.Error != VFS::VFSError::OK)
                {
                    return;
                }
                if (count < 0 || static_cast<uint32_t>(count) > self.Source->Size - self.Offset)
                {
                    self.Error = VFS::VFSError::Corrupted;
                    return;
                }
                self.Offset += static_cast<uint32_t>(count);
            }

            static int End(void* context)
            {
                const auto& self = *static_cast<ImageSource*>(context);
                return self.Error != VFS::VFSError::OK || self.Offset == self.Source->Size;
            }

            void Start(stbi__context& decoder)
            {
                stbi_io_callbacks callbacks = {Read, Skip, End};
                Offset                      = 0;
                BufferBytes                 = 0;
                stbi__start_callbacks(&decoder, &callbacks, this);
            }
        };

        EncodedImageKind DetectImage(const uint8_t* bytes, size_t count)
        {
            // Only format tests run on this fixed prefix. Their speculative
            // probes must not poison the bounded source's real EOF status.
            stbi__context decoder = {};
            stbi__start_mem(&decoder, bytes, static_cast<int>(count));
            if (stbi__png_test(&decoder))
            {
                return EncodedImageKind::PNG;
            }
            if (stbi__bmp_test(&decoder))
            {
                return EncodedImageKind::BMP;
            }
            if (stbi__gif_test(&decoder))
            {
                return EncodedImageKind::GIF;
            }
            if (stbi__psd_test(&decoder))
            {
                return EncodedImageKind::PSD;
            }
            if (stbi__pic_test(&decoder))
            {
                return EncodedImageKind::PIC;
            }
            // stb's JPEG type probe allocates/zeros its full decoder merely
            // to read the SOI marker. Match that marker (including its allowed
            // 0xff fill bytes) directly; real header/decode validation below
            // still uses the shared implementation. TGA/unsupported probes
            // must not allocate JPEG state or inherit its allocation refusal.
            size_t marker = 0;
            while (marker < count && bytes[marker] == 0xff)
            {
                ++marker;
            }
            if (marker && marker < count && bytes[marker] == 0xd8)
            {
                return EncodedImageKind::JPEG;
            }
            // These formats are supported elsewhere in stb, but not by the
            // encoded-LDR-texture contract. Do not let TGA guess their headers.
            if (stbi__hdr_test(&decoder) || stbi__pnm_test(&decoder))
            {
                return EncodedImageKind::Unknown;
            }
            return stbi__tga_test(&decoder) ? EncodedImageKind::TGA : EncodedImageKind::Unknown;
        }

        int DescribeImage(EncodedImageKind kind, stbi__context& decoder, int* width, int* height, int* channels, uint32_t* bits)
        {
            *bits = 8;
            switch (kind)
            {
                case EncodedImageKind::PNG:
                {
                    stbi__png png    = {};
                    png.s            = &decoder;
                    const int result = stbi__png_info_raw(&png, width, height, channels);
                    *bits            = png.depth == 16 ? 16 : 8;
                    return result;
                }
                case EncodedImageKind::JPEG:
                    return stbi__jpeg_info(&decoder, width, height, channels);
                case EncodedImageKind::BMP:
                {
                    const int result = stbi__bmp_info(&decoder, width, height, channels);
                    // BMP stores top-down orientation as a negative height.
                    // Normalize like its shared loader, but reject INT_MIN
                    // before abs() can overflow inside that loader.
                    if (result && *height < 0)
                    {
                        if (*height == std::numeric_limits<int>::min())
                        {
                            return 0;
                        }
                        *height = -*height;
                    }
                    return result;
                }
                case EncodedImageKind::TGA:
                    return stbi__tga_info(&decoder, width, height, channels);
                case EncodedImageKind::GIF:
                    return stbi__gif_info(&decoder, width, height, channels);
                case EncodedImageKind::PSD:
                    return stbi__psd_info(&decoder, width, height, channels);
                case EncodedImageKind::PIC:
                    return stbi__pic_info(&decoder, width, height, channels);
                default:
                    return 0;
            }
        }

        void* DecodeImage(EncodedImageKind kind, stbi__context& decoder, int* width, int* height, int* channels, stbi__result_info& info)
        {
            // Dispatch into the shared implementation, not another parser or
            // stb implementation. Avoid speculative format probes at real EOF.
            switch (kind)
            {
                case EncodedImageKind::PNG:
                    return stbi__png_load(&decoder, width, height, channels, STBI_rgb_alpha, &info);
                case EncodedImageKind::JPEG:
                    return stbi__jpeg_load(&decoder, width, height, channels, STBI_rgb_alpha, &info);
                case EncodedImageKind::BMP:
                    return stbi__bmp_load(&decoder, width, height, channels, STBI_rgb_alpha, &info);
                case EncodedImageKind::TGA:
                    return stbi__tga_load(&decoder, width, height, channels, STBI_rgb_alpha, &info);
                case EncodedImageKind::GIF:
                    return stbi__gif_load(&decoder, width, height, channels, STBI_rgb_alpha, &info);
                case EncodedImageKind::PSD:
                    return stbi__psd_load(&decoder, width, height, channels, STBI_rgb_alpha, &info, 8);
                case EncodedImageKind::PIC:
                    return stbi__pic_load(&decoder, width, height, channels, STBI_rgb_alpha, &info);
                default:
                    return nullptr;
            }
        }
    } // namespace

    Core::VFS::VFSResult<void> ValidateEncodedImage(const AssetCodec::AssetByteSource& source, IAllocationRoute& route, const EncodedImageLimits& limits, EncodedImageDescription* output)
    {
        if (!source.Read || !output)
        {
            return VFS::VFSResult<void>::Fail(VFS::VFSError::InvalidPath);
        }
        if (!source.Size)
        {
            return VFS::VFSResult<void>::Fail(VFS::VFSError::Corrupted);
        }
        if (!limits.MaxDimension || !limits.MaxDecodedBytes || source.Size > limits.MaxEncodedBytes || source.Size > static_cast<uint64_t>(std::numeric_limits<int>::max()))
        {
            return VFS::VFSResult<void>::Fail(VFS::VFSError::SizeLimitExceeded);
        }
        using ImagePrefix                        = Core::Containers::FixedArray<uint8_t, 128>;
        Core::Memory::WorkingMemoryCharge fixed  = {};
        auto*                             budget = route.WorkingMemoryBudget();
        if (!fixed.TryAcquire(budget, sizeof(ImageSource) + sizeof(ImagePrefix) + sizeof(stbi__context) + sizeof(stbi__result_info) + sizeof(AllocationScope) + 3 * sizeof(fixed)))
        {
            return VFS::VFSResult<void>::Fail(VFS::VFSError::SizeLimitExceeded);
        }
        AllocationScope scope(route);
        ImageSource     input  = {&source};
        ImagePrefix     prefix = {};
        const int       count  = ImageSource::Read(&input, reinterpret_cast<char*>(prefix.data()), static_cast<int>(prefix.size()));
        if (input.Error != VFS::VFSError::OK)
        {
            return VFS::VFSResult<void>::Fail(input.Error);
        }
        const auto kind = DetectImage(prefix.data(), static_cast<size_t>(count));
        if (kind == EncodedImageKind::Unknown)
        {
            return VFS::VFSResult<void>::Fail(VFS::VFSError::Unsupported);
        }
        Core::Memory::WorkingMemoryCharge format_fixed = {};
        if (kind == EncodedImageKind::PNG && !format_fixed.TryAcquire(budget, sizeof(stbi__png)))
        {
            return VFS::VFSResult<void>::Fail(VFS::VFSError::SizeLimitExceeded);
        }
        stbi__context decoder = {};
        input.Start(decoder);
        int       width = 0, height = 0, channels = 0;
        uint32_t  bits      = 8;
        const int described = DescribeImage(kind, decoder, &width, &height, &channels, &bits);
        if (input.Error != VFS::VFSError::OK || !described || width <= 0 || height <= 0 || channels < 1 || channels > 4)
        {
            return VFS::VFSResult<void>::Fail(input.Error != VFS::VFSError::OK ? input.Error : VFS::VFSError::Corrupted);
        }
        if (static_cast<uint32_t>(width) > limits.MaxDimension || static_cast<uint32_t>(height) > limits.MaxDimension || uint64_t(width) * uint64_t(height) > limits.MaxDecodedBytes / (4 * (bits / 8)))
        {
            return VFS::VFSResult<void>::Fail(VFS::VFSError::SizeLimitExceeded);
        }
        // Header inspection has finished and released its routed temporary
        // state. GIF's large decode state and PNG's zlib state are inline, not
        // heap blocks; reserve their actual types only for this decode phase.
        Core::Memory::WorkingMemoryCharge decoder_fixed = {};
        const size_t                      decoder_bytes = kind == EncodedImageKind::GIF ? sizeof(stbi__gif) : kind == EncodedImageKind::PNG ? sizeof(stbi__zbuf) : 0;
        if (!decoder_fixed.TryAcquire(budget, decoder_bytes))
        {
            return VFS::VFSResult<void>::Fail(VFS::VFSError::SizeLimitExceeded);
        }
        input.Buffered = true;
        input.Start(decoder);
        int               decoded_width = 0, decoded_height = 0, decoded_channels = 0;
        stbi__result_info info = {};
        info.bits_per_channel  = 8;
        void*      pixels      = DecodeImage(kind, decoder, &decoded_width, &decoded_height, &decoded_channels, info);
        const bool valid       = pixels && decoded_width == width && decoded_height == height && decoded_channels == channels && info.bits_per_channel == static_cast<int>(bits);
        Free(pixels);
        if (input.Error != VFS::VFSError::OK || !valid)
        {
            return VFS::VFSResult<void>::Fail(input.Error != VFS::VFSError::OK ? input.Error : VFS::VFSError::Corrupted);
        }
        *output = {kind, static_cast<uint32_t>(width), static_cast<uint32_t>(height), static_cast<uint32_t>(channels), bits};
        return VFS::VFSResult<void>::Ok();
    }

    AllocationScope::AllocationScope(IAllocationRoute& route) noexcept : m_previous(CurrentRoute), m_current(&route)
    {
        CurrentRoute = &route;
    }

    AllocationScope::~AllocationScope()
    {
        ZENGINE_VALIDATE_ASSERT(CurrentRoute == m_current, "Image allocation scopes must unwind in reverse order on their creating thread.");
        CurrentRoute = m_previous;
    }

    void* Allocate(size_t bytes) noexcept
    {
        if (CurrentRoute)
        {
            return CurrentRoute->Allocate(bytes);
        }
        auto* slab = Helpers::GetWorkerSlab();
        return slab ? slab->TryRealloc(nullptr, bytes) : std::malloc(bytes);
    }

    void* Reallocate(void* pointer, size_t old_bytes, size_t new_bytes) noexcept
    {
        if (CurrentRoute)
        {
            return CurrentRoute->Reallocate(pointer, old_bytes, new_bytes);
        }
        auto* slab = Helpers::GetWorkerSlab();
        return slab ? slab->TryRealloc(pointer, new_bytes) : std::realloc(pointer, new_bytes);
    }

    void Free(void* pointer) noexcept
    {
        if (!pointer)
        {
            return;
        }
        if (CurrentRoute)
        {
            CurrentRoute->Free(pointer);
        }
        else if (auto* slab = Helpers::GetWorkerSlab())
        {
            slab->Free(pointer);
        }
        else
        {
            std::free(pointer);
        }
    }
} // namespace ZEngine::Importers::ImageCodec
