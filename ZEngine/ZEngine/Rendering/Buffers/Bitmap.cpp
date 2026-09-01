#include <ZEngine/Core/Maths/MathUtils.h>
#include <ZEngine/Helpers/MemoryOperations.h>
#include <ZEngine/Rendering/Buffers/Bitmap.h>
#include <ZEngine/ZEngineDef.h>
#include <cmath>

namespace ZEngine::Rendering::Buffers
{
    Bitmap::~Bitmap()
    {
        FreeBuffer();
    }

    Bitmap::Bitmap(Bitmap&& o) noexcept : Width(o.Width), Height(o.Height), Layers(o.Layers), Channel(o.Channel), Type(o.Type), Format(o.Format), Buffer(o.Buffer), BufferSize(o.BufferSize), Slab(o.Slab)
    {
        o.Buffer     = nullptr;
        o.BufferSize = 0;
        o.Slab       = nullptr;
    }

    Bitmap& Bitmap::operator=(Bitmap&& o) noexcept
    {
        if (this != &o)
        {
            FreeBuffer();
            Width        = o.Width;
            Height       = o.Height;
            Layers       = o.Layers;
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

    Bitmap Bitmap::Create(int w, int h, int layers, int ch, BitmapFormat fmt, BitmapType type, Core::Memory::TLSFSlab* slab)
    {
        Bitmap b;
        b.Width   = w;
        b.Height  = h;
        b.Layers  = layers;
        b.Channel = ch;
        b.Format  = fmt;
        b.Type    = type;
        b.Slab    = slab;
        b.Alloc(static_cast<size_t>(w) * h * layers * ch * BytePerChannel(fmt));
        return b;
    }

    Bitmap Bitmap::FromData(int w, int h, int layers, int ch, BitmapFormat fmt, BitmapType type, const void* data, Core::Memory::TLSFSlab* slab)
    {
        Bitmap b;
        b.Width   = w;
        b.Height  = h;
        b.Layers  = layers;
        b.Channel = ch;
        b.Format  = fmt;
        b.Type    = type;
        b.Slab    = slab;

        size_t sz = static_cast<size_t>(w) * h * layers * ch * BytePerChannel(fmt);
        if (data)
        {
            b.AllocNoZero(sz);
            if (b.Buffer)
                ZENGINE_VALIDATE_ASSERT(Helpers::secure_memcpy(b.Buffer, b.BufferSize, data, b.BufferSize) == Helpers::MEMORY_OP_SUCCESS, "Bitmap::FromData: memcpy failed")
        }
        else
        {
            b.Alloc(sz);
        }
        return b;
    }

    int Bitmap::BytePerChannel(BitmapFormat fmt)
    {
        switch (fmt)
        {
            case BitmapFormat::UnsignedByte:
                return 1;
            case BitmapFormat::Float:
                return 4;
        }
        return 0;
    }

    void Bitmap::SetPixel(int x, int y, const Core::Maths::Vec4f& pixel)
    {
        if (Format == BitmapFormat::UnsignedByte)
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
        else if (Format == BitmapFormat::Float)
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

    Core::Maths::Vec4f Bitmap::GetPixel(int x, int y) const
    {
        if (Format == BitmapFormat::UnsignedByte)
        {
            const int ofs = Channel * (y * Width + x);
            return Core::Maths::Vec4f(Channel > 0 ? float(Buffer[ofs + 0]) / 255.0f : 0.0f, Channel > 1 ? float(Buffer[ofs + 1]) / 255.0f : 0.0f, Channel > 2 ? float(Buffer[ofs + 2]) / 255.0f : 0.0f, Channel > 3 ? float(Buffer[ofs + 3]) / 255.0f : 0.0f);
        }
        else if (Format == BitmapFormat::Float)
        {
            const int    ofs  = Channel * (y * Width + x);
            const float* data = reinterpret_cast<const float*>(Buffer);
            return Core::Maths::Vec4f(Channel > 0 ? data[ofs + 0] : 0.0f, Channel > 1 ? data[ofs + 1] : 0.0f, Channel > 2 ? data[ofs + 2] : 0.0f, Channel > 3 ? data[ofs + 3] : 0.0f);
        }
        return Core::Maths::Vec4f();
    }

    void Bitmap::Alloc(size_t n)
    {
        if (n == 0)
            return;
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

    void Bitmap::AllocNoZero(size_t n)
    {
        if (n == 0)
            return;
        BufferSize = n;
        Buffer     = Slab ? static_cast<uint8_t*>(Slab->Alloc(n)) : new uint8_t[n];
    }

    void Bitmap::FreeBuffer()
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

    namespace BitmapConvert
    {
        static Core::Maths::Vec3f FaceCoordToXYZ(int i, int j, int face_id, int face_size)
        {
            const float A = 2.0f * float(i) / face_size;
            const float B = 2.0f * float(j) / face_size;

            switch (face_id)
            {
                case 0:
                    return Core::Maths::Vec3f(-1.0f, A - 1.0f, B - 1.0f);
                case 1:
                    return Core::Maths::Vec3f(A - 1.0f, -1.0f, 1.0f - B);
                case 2:
                    return Core::Maths::Vec3f(1.0f, A - 1.0f, 1.0f - B);
                case 3:
                    return Core::Maths::Vec3f(1.0f - A, 1.0f, 1.0f - B);
                case 4:
                    return Core::Maths::Vec3f(B - 1.0f, A - 1.0f, 1.0f);
                case 5:
                    return Core::Maths::Vec3f(1.0f - B, A - 1.0f, -1.0f);
                default:
                    return Core::Maths::Vec3f{};
            }
        }

        Bitmap EquirectToCross(const Bitmap& input, Core::Memory::TLSFSlab* slab)
        {
            if (input.Type != BitmapType::Texture2D)
                return Bitmap();

            const int                face_size      = input.Width / 4;
            Bitmap                   out            = Bitmap::Create(face_size * 3, face_size * 4, 1, input.Channel, input.Format, BitmapType::Texture2D, slab);

            const Core::Maths::IVec2 face_offsets[] = {
                Core::Maths::IVec2{    face_size, face_size * 3},
                Core::Maths::IVec2{            0,     face_size},
                Core::Maths::IVec2{    face_size,     face_size},
                Core::Maths::IVec2{face_size * 2,     face_size},
                Core::Maths::IVec2{    face_size,             0},
                Core::Maths::IVec2{    face_size, face_size * 2}
            };

            const int cw = input.Width - 1;
            const int ch = input.Height - 1;

            for (int face = 0; face < 6; ++face)
            {
                for (int i = 0; i < face_size; ++i)
                {
                    for (int j = 0; j < face_size; ++j)
                    {
                        const Core::Maths::Vec3f P     = FaceCoordToXYZ(i, j, face, face_size);
                        const float              R     = hypot(P.x, P.y);
                        const float              theta = atan2(P.y, P.x);
                        const float              phi   = atan2(P.z, R);
                        const float              Uf    = float(2.0f * face_size * (theta + Core::Maths::PI<float>) / Core::Maths::PI<float>);
                        const float              Vf    = float(2.0f * face_size * (Core::Maths::PI<float> / 2.0f - phi) / Core::Maths::PI<float>);

                        const int                U1    = Core::Maths::clamp(int(floor(Uf)), 0, cw);
                        const int                V1    = Core::Maths::clamp(int(floor(Vf)), 0, ch);
                        const int                U2    = Core::Maths::clamp(U1 + 1, 0, cw);
                        const int                V2    = Core::Maths::clamp(V1 + 1, 0, ch);

                        const float              s     = Uf - U1;
                        const float              t     = Vf - V1;

                        const Core::Maths::Vec4f A     = input.GetPixel(U1, V1);
                        const Core::Maths::Vec4f B     = input.GetPixel(U2, V1);
                        const Core::Maths::Vec4f C     = input.GetPixel(U1, V2);
                        const Core::Maths::Vec4f D     = input.GetPixel(U2, V2);

                        out.SetPixel(i + face_offsets[face].x, j + face_offsets[face].y, A * (1 - s) * (1 - t) + B * s * (1 - t) + C * (1 - s) * t + D * s * t);
                    }
                }
            }
            return out;
        }

        Bitmap CrossToCubemap(const Bitmap& input, Core::Memory::TLSFSlab* slab)
        {
            const int      face_w  = input.Width / 3;
            const int      face_h  = input.Height / 4;
            const int      px_size = input.Channel * Bitmap::BytePerChannel(input.Format);

            Bitmap         out     = Bitmap::Create(face_w, face_h, 6, input.Channel, input.Format, BitmapType::CubeMap, slab);
            const uint8_t* src     = input.Buffer;
            uint8_t*       dst     = out.Buffer;

            for (int face = 0; face < 6; ++face)
            {
                for (int j = 0; j < face_h; ++j)
                {
                    for (int i = 0; i < face_w; ++i)
                    {
                        int px = 0, py = 0;
                        switch (face)
                        {
                            case 0:
                                px = i;
                                py = face_h + j;
                                break; // right
                            case 1:
                                px = 2 * face_w + i;
                                py = face_h + j;
                                break; // left
                            case 2:
                                px = 2 * face_w - (i + 1);
                                py = face_h - (j + 1);
                                break; // up
                            case 3:
                                px = 2 * face_w - (i + 1);
                                py = 3 * face_h - (j + 1);
                                break; // down
                            case 4:
                                px = 2 * face_w - (i + 1);
                                py = input.Height - (j + 1);
                                break; // front
                            case 5:
                                px = face_w + i;
                                py = face_h + j;
                                break; // back
                        }
                        ZENGINE_VALIDATE_ASSERT(Helpers::secure_memcpy(dst, px_size, src + (py * input.Width + px) * px_size, px_size) == Helpers::MEMORY_OP_SUCCESS, "BitmapConvert::CrossToCubemap: pixel copy failed")
                        dst += px_size;
                    }
                }
            }
            return out;
        }
    } // namespace BitmapConvert

} // namespace ZEngine::Rendering::Buffers
