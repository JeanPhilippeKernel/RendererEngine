#pragma once
#include <ZEngine/Core/Maths/MathUtils.h>
#include <ZEngine/Core/Maths/Matrix.h>
#include <ZEngine/Core/Memory/TLSFSlab.h>
#include <ZEngine/Helpers/MemoryOperations.h>
#include <ZEngine/ZEngineDef.h>
#include <cmath>

namespace ZEngine::Rendering::Buffers
{

    enum BitmapType
    {
        TEXTURE_2D,
        CUBE
    };

    enum BitmapFormat
    {
        UNSIGNED_BYTE,
        FLOAT
    };

    struct BitmapPixel
    {
        /*
         * Mapping pixel coordinates on a specific face of a cubemap to 3D Cartesian coordinates
         *
         * The A and B values are normalized coordinates in the range [-1, 1], calculated from pixel coordinates (i, j)
         * and the face size.
         *
         * Reference: "Real-Time Rendering, Fourth Edition" by Tomas Akenine-M?ller, Eric Haines, Naty Hoffman
         */
        static ZEngine::Core::Maths::Vec3f FaceCoordToXYZ(int i, int j, int face_id, int face_size)
        {
            const float A = 2.0f * float(i) / face_size;
            const float B = 2.0f * float(j) / face_size;

            if (face_id == 0)
                return ZEngine::Core::Maths::Vec3f(-1.0f, A - 1.0f, B - 1.0f);
            if (face_id == 1)
                return ZEngine::Core::Maths::Vec3f(A - 1.0f, -1.0f, 1.0f - B);
            if (face_id == 2)
                return ZEngine::Core::Maths::Vec3f(1.0f, A - 1.0f, 1.0f - B);
            if (face_id == 3)
                return ZEngine::Core::Maths::Vec3f(1.0f - A, 1.0f, 1.0f - B);
            if (face_id == 4)
                return ZEngine::Core::Maths::Vec3f(B - 1.0f, A - 1.0f, 1.0f);
            if (face_id == 5)
                return ZEngine::Core::Maths::Vec3f(1.0f - B, A - 1.0f, -1.0f);
            return ZEngine::Core::Maths::Vec3f{};
        }
    };

    struct Bitmap
    {
        Bitmap() = default;

        /// @brief Allocate a zeroed buffer. When slab is non-null the buffer is slab-backed
        ///        and freed via slab on destruction; otherwise heap-allocated (new[]).
        Bitmap(int width, int height, int channel, BitmapFormat format, Core::Memory::TLSFSlab* slab = nullptr) : Width(width), Height(height), Channel(channel), Format(format), Slab(slab)
        {
            Alloc((size_t) (width * height * channel * BytePerChannel(format)));
        }

        /// @brief Cubemap / depth variant.
        Bitmap(int width, int height, int depth, int channel, BitmapFormat format, Core::Memory::TLSFSlab* slab = nullptr) : Width(width), Height(height), Depth(depth), Channel(channel), Format(format), Slab(slab)
        {
            Alloc((size_t) (width * height * depth * channel * BytePerChannel(format)));
        }

        /// @brief Allocate and copy from data. slab parameter routes the buffer allocation.
        Bitmap(int width, int height, int channel, BitmapFormat format, const void* data, Core::Memory::TLSFSlab* slab = nullptr) : Width(width), Height(height), Channel(channel), Format(format), Slab(slab)
        {
            size_t sz = (size_t) (width * height * channel * BytePerChannel(format));
            AllocNoZero(sz);
            if (data && Buffer)
            {
                ZENGINE_VALIDATE_ASSERT(Helpers::secure_memcpy(Buffer, BufferSize, data, BufferSize) == Helpers::MEMORY_OP_SUCCESS, "Bitmap: memcpy from source data failed")
            }
        }

        ~Bitmap()
        {
            Free();
        }

        Bitmap(const Bitmap&)            = delete;
        Bitmap& operator=(const Bitmap&) = delete;

        Bitmap(Bitmap&& o) noexcept : Width(o.Width), Height(o.Height), Depth(o.Depth), Channel(o.Channel), Type(o.Type), Format(o.Format), Buffer(o.Buffer), BufferSize(o.BufferSize), Slab(o.Slab)
        {
            o.Buffer     = nullptr;
            o.BufferSize = 0;
            o.Slab       = nullptr;
        }

        Bitmap& operator=(Bitmap&& o) noexcept
        {
            if (this != &o)
            {
                Free();
                Width        = o.Width;
                Height       = o.Height;
                Depth        = o.Depth;
                Channel      = o.Channel;
                Type         = o.Type;
                Format       = o.Format;
                Buffer       = o.Buffer;
                BufferSize   = o.BufferSize;
                Slab         = o.Slab;

                o.Buffer     = nullptr;
                o.BufferSize = 0;
                o.Slab       = nullptr;
            }
            return *this;
        }

        void SetPixel(int x, int y, const ZEngine::Core::Maths::Vec4f& pixel)
        {
            if (Format == BitmapFormat::UNSIGNED_BYTE)
            {
                const int ofs = Channel * (y * Width + x);
                if (Channel > 0)
                    Buffer[ofs + 0] = uint8_t(pixel.x * 255.0f);
                if (Channel > 1)
                    Buffer[ofs + 1] = uint8_t(pixel.y * 255.0f);
                if (Channel > 2)
                    Buffer[ofs + 2] = uint8_t(pixel.z * 255.0f);
                if (Channel > 3)
                    Buffer[ofs + 3] = uint8_t(pixel.w * 255.0f);
            }
            else if (Format == BitmapFormat::FLOAT)
            {
                const int ofs  = Channel * (y * Width + x);
                float*    data = reinterpret_cast<float*>(Buffer);
                if (Channel > 0)
                    data[ofs + 0] = pixel.x;
                if (Channel > 1)
                    data[ofs + 1] = pixel.y;
                if (Channel > 2)
                    data[ofs + 2] = pixel.z;
                if (Channel > 3)
                    data[ofs + 3] = pixel.w;
            }
        }

        ZEngine::Core::Maths::Vec4f GetPixel(int x, int y) const
        {
            if (Format == BitmapFormat::UNSIGNED_BYTE)
            {
                const int ofs = Channel * (y * Width + x);
                return ZEngine::Core::Maths::Vec4f(Channel > 0 ? float(Buffer[ofs + 0]) / 255.0f : 0.0f, Channel > 1 ? float(Buffer[ofs + 1]) / 255.0f : 0.0f, Channel > 2 ? float(Buffer[ofs + 2]) / 255.0f : 0.0f, Channel > 3 ? float(Buffer[ofs + 3]) / 255.0f : 0.0f);
            }
            else if (Format == BitmapFormat::FLOAT)
            {
                const int    ofs  = Channel * (y * Width + x);
                const float* data = reinterpret_cast<const float*>(Buffer);
                return ZEngine::Core::Maths::Vec4f(Channel > 0 ? data[ofs + 0] : 0.0f, Channel > 1 ? data[ofs + 1] : 0.0f, Channel > 2 ? data[ofs + 2] : 0.0f, Channel > 3 ? data[ofs + 3] : 0.0f);
            }
            return ZEngine::Core::Maths::Vec4f();
        }

        inline static int BytePerChannel(BitmapFormat format)
        {
            if (format == BitmapFormat::UNSIGNED_BYTE)
                return 1;
            if (format == BitmapFormat::FLOAT)
                return 4;
            return 0;
        }

        /// @brief slab is forwarded to all internal Bitmap allocations within this call.
        inline static Bitmap EquirectangularMapToVerticalCross(const Bitmap& input_map, Core::Memory::TLSFSlab* slab = nullptr)
        {
            if (input_map.Type != BitmapType::TEXTURE_2D)
                return Bitmap();

            const int                         face_size = input_map.Width / 4;
            const int                         width     = face_size * 3;
            const int                         height    = face_size * 4;

            Bitmap                            vertical_cross(width, height, input_map.Channel, input_map.Format, slab);

            const ZEngine::Core::Maths::IVec2 face_offsets[] = {
                ZEngine::Core::Maths::IVec2{    face_size, face_size * 3},
                ZEngine::Core::Maths::IVec2{            0,     face_size},
                ZEngine::Core::Maths::IVec2{    face_size,     face_size},
                ZEngine::Core::Maths::IVec2{face_size * 2,     face_size},
                ZEngine::Core::Maths::IVec2{    face_size,             0},
                ZEngine::Core::Maths::IVec2{    face_size, face_size * 2}
            };

            const int clamped_width  = input_map.Width - 1;
            const int clamped_height = input_map.Height - 1;

            for (int face = 0; face < 6; ++face)
            {
                for (int i = 0; i < face_size; ++i)
                {
                    for (int j = 0; j < face_size; ++j)
                    {
                        const ZEngine::Core::Maths::Vec3f P     = BitmapPixel::FaceCoordToXYZ(i, j, face, face_size);
                        const float                       R     = hypot(P.x, P.y);
                        const float                       theta = atan2(P.y, P.x);
                        const float                       phi   = atan2(P.z, R);

                        const float                       Uf    = float(2.0f * face_size * (theta + ZEngine::Core::Maths::PI<float>) / ZEngine::Core::Maths::PI<float>);
                        const float                       Vf    = float(2.0f * face_size * (ZEngine::Core::Maths::PI<float> / 2.0f - phi) / ZEngine::Core::Maths::PI<float>);

                        const int                         U1    = ZEngine::Core::Maths::clamp(int(floor(Uf)), 0, clamped_width);
                        const int                         V1    = ZEngine::Core::Maths::clamp(int(floor(Vf)), 0, clamped_height);
                        const int                         U2    = ZEngine::Core::Maths::clamp(U1 + 1, 0, clamped_width);
                        const int                         V2    = ZEngine::Core::Maths::clamp(V1 + 1, 0, clamped_height);

                        const float                       s     = Uf - U1;
                        const float                       t     = Vf - V1;

                        const ZEngine::Core::Maths::Vec4f A     = input_map.GetPixel(U1, V1);
                        const ZEngine::Core::Maths::Vec4f B     = input_map.GetPixel(U2, V1);
                        const ZEngine::Core::Maths::Vec4f C     = input_map.GetPixel(U1, V2);
                        const ZEngine::Core::Maths::Vec4f D     = input_map.GetPixel(U2, V2);

                        const ZEngine::Core::Maths::Vec4f color = A * (1 - s) * (1 - t) + B * (s) * (1 - t) + C * (1 - s) * t + D * (s) * (t);
                        vertical_cross.SetPixel(i + face_offsets[face].x, j + face_offsets[face].y, color);
                    }
                }
            }
            return vertical_cross;
        }

        /// @brief slab is forwarded to the cubemap buffer allocation.
        inline static Bitmap VerticalCrossToCubemap(const Bitmap& input_map, Core::Memory::TLSFSlab* slab = nullptr)
        {
            const int face_width  = input_map.Width / 3;
            const int face_height = input_map.Height / 4;

            Bitmap    cubemap(face_width, face_height, 6, input_map.Channel, input_map.Format, slab);
            cubemap.Type               = CUBE;

            const uint8_t* source      = input_map.Buffer;
            uint8_t*       destination = cubemap.Buffer;
            int            pixel_size  = cubemap.Channel * BytePerChannel(cubemap.Format);

            const int      RIGHT_FACE  = 0;
            const int      LEFT_FACE   = 1;
            const int      UP_FACE     = 2;
            const int      DOWN_FACE   = 3;
            const int      FRONT_FACE  = 4;
            const int      BACK_FACE   = 5;

            for (int face = 0; face < 6; ++face)
            {
                for (int j = 0; j < face_height; ++j)
                {
                    for (int i = 0; i < face_width; ++i)
                    {
                        int pixel_pos_x = 0;
                        int pixel_pos_y = 0;

                        switch (face)
                        {
                            case RIGHT_FACE:
                                pixel_pos_x = i;
                                pixel_pos_y = face_height + j;
                                break;
                            case LEFT_FACE:
                                pixel_pos_x = 2 * face_width + i;
                                pixel_pos_y = 1 * face_height + j;
                                break;
                            case UP_FACE:
                                pixel_pos_x = 2 * face_width - (i + 1);
                                pixel_pos_y = 1 * face_height - (j + 1);
                                break;
                            case DOWN_FACE:
                                pixel_pos_x = 2 * face_width - (i + 1);
                                pixel_pos_y = 3 * face_height - (j + 1);
                                break;
                            case FRONT_FACE:
                                pixel_pos_x = 2 * face_width - (i + 1);
                                pixel_pos_y = input_map.Height - (j + 1);
                                break;
                            case BACK_FACE:
                                pixel_pos_x = face_width + i;
                                pixel_pos_y = face_height + j;
                                break;
                        }
                        ZENGINE_VALIDATE_ASSERT(Helpers::secure_memcpy(destination, pixel_size, source + (pixel_pos_y * input_map.Width + pixel_pos_x) * pixel_size, pixel_size) == Helpers::MEMORY_OP_SUCCESS, "Bitmap: pixel copy failed in VerticalCrossToCubemap")
                        destination += pixel_size;
                    }
                }
            }
            return cubemap;
        }

        int                     Width      = 0;
        int                     Height     = 0;
        int                     Depth      = 1;
        int                     Channel    = 3;
        BitmapType              Type       = BitmapType::TEXTURE_2D;
        BitmapFormat            Format     = BitmapFormat::UNSIGNED_BYTE;
        uint8_t*                Buffer     = nullptr; ///< Pixel data. Owned by this Bitmap.
        size_t                  BufferSize = 0;       ///< Size of Buffer in bytes.
        Core::Memory::TLSFSlab* Slab       = nullptr; ///< Non-null when Buffer is slab-backed.

    private:
        void Alloc(size_t n)
        {
            BufferSize = n;
            if (Slab)
            {
                Buffer = static_cast<uint8_t*>(Slab->Alloc(n));
                Helpers::secure_memset(Buffer, 0, n, n);
            }
            else
            {
                Buffer = new uint8_t[n]();
            }
        }

        void AllocNoZero(size_t n)
        {
            BufferSize = n;
            Buffer     = Slab ? static_cast<uint8_t*>(Slab->Alloc(n)) : new uint8_t[n];
        }

        void Free()
        {
            if (Buffer)
            {
                if (Slab)
                    Slab->Free(Buffer);
                else
                    delete[] Buffer;
                Buffer     = nullptr;
                BufferSize = 0;
                Slab       = nullptr;
            }
        }
    };
} // namespace ZEngine::Rendering::Buffers
