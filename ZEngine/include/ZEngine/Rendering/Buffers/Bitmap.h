#pragma once
#include <vector>
#include <cmath>
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <Helpers/MemoryOperations.h>
#include <ZEngineDef.h>

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
         * The A and B values are normalized coordinates in the range [-1, 1], calculated from pixel coordinates (i, j) and the face size.
         *
         * Reference: "Real-Time Rendering, Fourth Edition" by Tomas Akenine-Möller, Eric Haines, Naty Hoffman
         */
        static glm::vec3 FaceCoordToXYZ(int i, int j, int face_id, int face_size)
        {
            const float A = 2.0f * float(i) / face_size;
            const float B = 2.0f * float(j) / face_size;

            /*
             * The right face of the cube is mapped to the negative x-axis, so the x-coordinate is set to -1.0f.
             * The y and z coordinates are set based on the normalized pixel coordinates.
             */
            if (face_id == 0)
                return glm::vec3(-1.0f, A - 1.0f, B - 1.0f);

            /*
             * The left face is mapped to the positive x-axis, so the x-coordinate is set to A - 1.0f.
             * The y-coordinate is set to -1.0f, and the z-coordinate is set based on the normalized pixel coordinates.
             */
            if (face_id == 1)
                return glm::vec3(A - 1.0f, -1.0f, 1.0f - B);

            /*
             * The top face is mapped to the positive y-axis, so the y-coordinate is set to A - 1.0f.
             * The x-coordinate is set to 1.0f, and the z-coordinate is set based on the normalized pixel coordinates.
             */
            if (face_id == 2)
                return glm::vec3(1.0f, A - 1.0f, 1.0f - B);

            /*
             * The bottom face is mapped to the negative y-axis, so the y-coordinate is set to 1.0f.
             * The x-coordinate is set to 1.0f - A, and the z-coordinate is set based on the normalized pixel coordinates
             */
            if (face_id == 3)
                return glm::vec3(1.0f - A, 1.0f, 1.0f - B);

            /*
             * The front face is mapped to the positive z-axis, so the z-coordinate is set to 1.0f.
             *The x and y coordinates are set based on the normalized pixel coordinates.
             */
            if (face_id == 4)
                return glm::vec3(B - 1.0f, A - 1.0f, 1.0f);

            /*
             * The back face is mapped to the negative z-axis, so the z-coordinate is set to -1.0f.
             * The x and y coordinates are set based on the normalized pixel coordinates.
             */
            if (face_id == 5)
                return glm::vec3(1.0f - B, A - 1.0f, -1.0f);

            return glm::vec3{};
        }
    };

    struct Bitmap
    {
        Bitmap() = default;
        Bitmap(int width, int height, int channel, BitmapFormat format)
            : Width(width), Height(height), Channel(channel), Format(format), Buffer(width * height * channel * BytePerChannel(format))
        {
        }
        Bitmap(int width, int height, int depth, int channel, BitmapFormat format)
            : Width(width), Height(height), Depth(depth), Channel(channel), Format(format), Buffer(width * height * depth * channel * BytePerChannel(format))
        {
        }
        Bitmap(int width, int height, int channel, BitmapFormat format, const void* data)
            : Width(width), Height(height), Channel(channel), Format(format), Buffer(width * height * channel * BytePerChannel(format))
        {
            if (data)
            {
                ZENGINE_VALIDATE_ASSERT(
                    Helpers::secure_memcpy(Buffer.data(), Buffer.size(), data, Buffer.size()) == Helpers::MEMORY_OP_SUCCESS, "Failed to perform memory copy operation")
            }
        }
        ~Bitmap() = default;

        void SetPixel(int x, int y, const glm::vec4& pixel)
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
                float*    data = reinterpret_cast<float*>(Buffer.data());
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

        glm::vec4 GetPixel(int x, int y) const
        {
            if (Format == BitmapFormat::UNSIGNED_BYTE)
            {
                const int ofs = Channel * (y * Width + x);
                return glm::vec4(
                    Channel > 0 ? float(Buffer[ofs + 0]) / 255.0f : 0.0f,
                    Channel > 1 ? float(Buffer[ofs + 1]) / 255.0f : 0.0f,
                    Channel > 2 ? float(Buffer[ofs + 2]) / 255.0f : 0.0f,
                    Channel > 3 ? float(Buffer[ofs + 3]) / 255.0f : 0.0f);
            }
            else if (Format == BitmapFormat::FLOAT)
            {
                const int    ofs  = Channel * (y * Width + x);
                const float* data = reinterpret_cast<const float*>(Buffer.data());
                return glm::vec4(
                    Channel > 0 ? data[ofs + 0] : 0.0f,
                    Channel > 1 ? data[ofs + 1] : 0.0f,
                    Channel > 2 ? data[ofs + 2] : 0.0f,
                    Channel > 3 ? data[ofs + 3] : 0.0f);
            }

            return glm::vec4();
        }

        inline static int BytePerChannel(BitmapFormat format)
        {
            if (format == BitmapFormat::UNSIGNED_BYTE)
            {
                return 1;
            }
            else if (format == BitmapFormat::FLOAT)
            {
                return 4;
            }
            
            return 0;
        }

        inline static Bitmap EquirectangularMapToVerticalCross(const Bitmap& input_map)
        {
            if (input_map.Type != BitmapType::TEXTURE_2D)
            {
                return Bitmap();
            }

            const int face_size = input_map.Width / 4;

            const int width  = face_size * 3;
            const int height = face_size * 4;

            Bitmap vertical_cross = Bitmap(width, height, input_map.Channel, input_map.Format);

            const glm::ivec2 face_offsets[] = {
                glm::ivec2{face_size, face_size * 3},
                glm::ivec2{0, face_size},
                glm::ivec2{face_size, face_size},
                glm::ivec2{face_size * 2, face_size},
                glm::ivec2{face_size, 0},
                glm::ivec2{face_size, face_size * 2}};

            const int clamped_width  = input_map.Width - 1;
            const int clamped_height = input_map.Height - 1;

            for (int face = 0; face < 6; ++face)
            {
                for (int i = 0; i < face_size; ++i)
                {
                    for (int j = 0; j < face_size; ++j)
                    {
                        const glm::vec3 P     = BitmapPixel::FaceCoordToXYZ(i, j, face, face_size);
                        const float     R     = hypot(P.x, P.y);
                        const float     theta = atan2(P.y, P.x);
                        const float     phi   = atan2(P.z, R);

                        const float Uf = float(2.0f * face_size * (theta + glm::pi<float>()) / glm::pi<float>());
                        const float Vf = float(2.0f * face_size * (glm::pi<float>() / 2.0f - phi) / glm::pi<float>());

                        const int U1 = glm::clamp(int(floor(Uf)), 0, clamped_width);
                        const int V1 = glm::clamp(int(floor(Vf)), 0, clamped_height);
                        const int U2 = glm::clamp(U1 + 1, 0, clamped_width);
                        const int V2 = glm::clamp(V1 + 1, 0, clamped_height);

                        const float s = Uf - U1;
                        const float t = Vf - V1;

                        const glm::vec4 A = input_map.GetPixel(U1, V1);
                        const glm::vec4 B = input_map.GetPixel(U2, V1);
                        const glm::vec4 C = input_map.GetPixel(U1, V2);
                        const glm::vec4 D = input_map.GetPixel(U2, V2);

                        const glm::vec4 color = A * (1 - s) * (1 - t) + B * (s) * (1 - t) + C * (1 - s) * t + D * (s) * (t);
                        vertical_cross.SetPixel(i + face_offsets[face].x, j + face_offsets[face].y, color);
                    }
                }
            }
            return vertical_cross;
        }

        inline static Bitmap VerticalCrossToCubemap(const Bitmap& input_map)
        {
            const int face_width  = input_map.Width / 3;
            const int face_height = input_map.Height / 4;

            Bitmap cubemap = Bitmap(face_width, face_height, 6, input_map.Channel, input_map.Format);
            cubemap.Type   = CUBE;

            const uint8_t* source      = input_map.Buffer.data();
            uint8_t*       destination = cubemap.Buffer.data();
            int            pixel_size  = cubemap.Channel * BytePerChannel(cubemap.Format);

            const int RIGHT_FACE = 0;
            const int LEFT_FACE  = 1;
            const int UP_FACE    = 2;
            const int DOWN_FACE  = 3;
            const int FRONT_FACE = 4;
            const int BACK_FACE  = 5;

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
                            {
                                pixel_pos_x = i;
                                pixel_pos_y = face_height + j;
                                break;
                            }
                            case LEFT_FACE:
                            {
                                pixel_pos_x = 2 * face_width + i;
                                pixel_pos_y = 1 * face_height + j;
                                break;
                            }
                            case UP_FACE:
                            {
                                pixel_pos_x = 2 * face_width - (i + 1);
                                pixel_pos_y = 1 * face_height - (j + 1);
                                break;
                            }
                            case DOWN_FACE:
                            {
                                pixel_pos_x = 2 * face_width - (i + 1);
                                pixel_pos_y = 3 * face_height - (j + 1);
                                break;
                            }
                            case FRONT_FACE:
                            {
                                pixel_pos_x = 2 * face_width - (i + 1);
                                pixel_pos_y = input_map.Height - (j + 1);
                                break;
                            }
                            case BACK_FACE:
                            {
                                pixel_pos_x = face_width + i;
                                pixel_pos_y = face_height + j;
                                break;
                            }
                        }
                        ZENGINE_VALIDATE_ASSERT(
                            Helpers::secure_memcpy(destination, pixel_size, source + (pixel_pos_y * input_map.Width + pixel_pos_x) * pixel_size, pixel_size) ==
                                Helpers::MEMORY_OP_SUCCESS,
                            "Failed to perform memory copy operation")
                        destination += pixel_size;
                    }
                }
            }

            return cubemap;
        }

        int                  Width   = 0;
        int                  Height  = 0;
        int                  Depth   = 1;
        int                  Channel = 3;
        BitmapType           Type    = BitmapType::TEXTURE_2D;
        BitmapFormat         Format  = BitmapFormat::UNSIGNED_BYTE;
        std::vector<uint8_t> Buffer  = {};
    };
} // namespace ZEngine::Rendering::Buffers