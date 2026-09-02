#pragma once
#include <ZEngine/Core/Maths/Vec.h>
#include <ZEngine/Core/Memory/TLSFSlab.h>
#include <cstddef>
#include <cstdint>

namespace ZEngine::Rendering::Buffers
{
    enum class BitmapType : uint8_t
    {
        Texture2D = 0,
        CubeMap   = 1,
    };

    enum class BitmapFormat : uint8_t
    {
        UnsignedByte = 0,
        Float        = 1,
    };

    struct Bitmap
    {
        Bitmap() = default;
        ~Bitmap();

        Bitmap(const Bitmap&)            = delete;
        Bitmap& operator=(const Bitmap&) = delete;

        Bitmap(Bitmap&& o) noexcept;
        Bitmap&                 operator=(Bitmap&& o) noexcept;

        /// @brief Allocate a zeroed buffer. layers=1 for Texture2D, layers=6 for CubeMap.
        static Bitmap           Create(int w, int h, int layers, int ch, BitmapFormat fmt, BitmapType type, Core::Memory::TLSFSlab* slab = nullptr);

        /// @brief Allocate and copy from data.
        static Bitmap           FromData(int w, int h, int layers, int ch, BitmapFormat fmt, BitmapType type, const void* data, Core::Memory::TLSFSlab* slab = nullptr);

        void                    SetPixel(int x, int y, const Core::Maths::Vec4f& pixel);
        Core::Maths::Vec4f      GetPixel(int x, int y) const;

        static int              BytePerChannel(BitmapFormat fmt);

        int                     Width      = 0;
        int                     Height     = 0;
        int                     Layers     = 1; ///< 1 for Texture2D; 6 for CubeMap.
        int                     Channel    = 3;
        BitmapType              Type       = BitmapType::Texture2D;
        BitmapFormat            Format     = BitmapFormat::UnsignedByte;
        uint8_t*                Buffer     = nullptr;
        size_t                  BufferSize = 0;
        Core::Memory::TLSFSlab* Slab       = nullptr;

    private:
        void Alloc(size_t n);
        void AllocNoZero(size_t n);
        void FreeBuffer();
    };

    namespace BitmapConvert
    {
        Bitmap EquirectToCross(const Bitmap& equirect, Core::Memory::TLSFSlab* slab = nullptr);
        Bitmap CrossToCubemap(const Bitmap& cross, Core::Memory::TLSFSlab* slab = nullptr);
    } // namespace BitmapConvert

} // namespace ZEngine::Rendering::Buffers
