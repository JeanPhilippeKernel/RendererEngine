#include <ZEngine/Core/Maths/Vec.h>
#include <ZEngine/Rendering/Buffers/Bitmap.h>
#include <gtest/gtest.h>
#include <stb/stb_image.h>
#include <stb/stb_image_write.h>
#include <cmath>
#include <filesystem>
#include <vector>

using namespace ZEngine::Rendering::Buffers;

constexpr float epsilon = 1e-2;

static bool approximatelyEqual(float a, float b, float eps)
{
    return fabs(a - b) <= eps * fmax(1.0f, fmax(fabs(a), fabs(b)));
}

TEST(BitmapTest, GetOrSetPixel)
{
    ZEngine::Core::Maths::Vec4f p(0.5, 0.5, 0.8, 0.0);

    Bitmap bitmap = Bitmap::Create(100, 100, 1, 3, BitmapFormat::UnsignedByte, BitmapType::Texture2D);
    bitmap.SetPixel(0, 0, p);

    auto pp = bitmap.GetPixel(0, 0);

    EXPECT_TRUE(approximatelyEqual(pp.x, p.x, epsilon));
    EXPECT_TRUE(approximatelyEqual(pp.y, p.y, epsilon));
    EXPECT_TRUE(approximatelyEqual(pp.z, p.z, epsilon));
}

TEST(BitmapTest, TestVerticalCross)
{
    int          width = 0, height = 0, channel = 0;
    const float* image_data = stbi_loadf("piazza_bologni_1k.hdr", &width, &height, &channel, 3);

    Bitmap in             = Bitmap::FromData(width, height, 1, channel, BitmapFormat::Float, BitmapType::Texture2D, image_data);
    Bitmap vertical_cross = BitmapConvert::EquirectToCross(in);
    Bitmap cubemap        = BitmapConvert::CrossToCubemap(vertical_cross);
    stbi_image_free((void*) image_data);

    stbi_write_hdr("screenshot.hdr", vertical_cross.Width, vertical_cross.Height, vertical_cross.Channel, (const float*) vertical_cross.Buffer);
    stbi_write_hdr("screenshot2.hdr", cubemap.Width, cubemap.Height, cubemap.Channel, (const float*) cubemap.Buffer);

    auto current_path = std::filesystem::current_path().string();

    EXPECT_TRUE(std::filesystem::exists(current_path + "/screenshot.hdr"));
    EXPECT_TRUE(std::filesystem::exists(current_path + "/screenshot2.hdr"));
}

TEST(BitmapTest, TestVerticalCross2)
{
    int          width = 0, height = 0, channel = 0;
    const float* image_data = stbi_loadf("piazza_bologni_1k.hdr", &width, &height, &channel, 3);

    std::vector<float> image_buffer_32bit;

    if (image_data)
    {
        const int total_pixel = width * height;
        image_buffer_32bit.resize(total_pixel * 4);

        auto source      = image_data;
        auto destination = image_buffer_32bit.data();
        for (int i = 0; i != total_pixel; i++)
        {
            *destination++ = *source++;
            *destination++ = *source++;
            *destination++ = *source++;
            *destination++ = 1.0f;
        }
    }
    stbi_image_free((void*) image_data);

    Bitmap in             = Bitmap::FromData(width, height, 1, 4, BitmapFormat::Float, BitmapType::Texture2D, image_buffer_32bit.data());
    Bitmap vertical_cross = BitmapConvert::EquirectToCross(in);
    Bitmap cubemap        = BitmapConvert::CrossToCubemap(vertical_cross);

    stbi_write_hdr("screenshot3.hdr", vertical_cross.Width, vertical_cross.Height, vertical_cross.Channel, (const float*) vertical_cross.Buffer);
    stbi_write_hdr("screenshot4.hdr", cubemap.Width, cubemap.Height, cubemap.Channel, (const float*) cubemap.Buffer);
    stbi_write_png("screenshot5.png", cubemap.Width, cubemap.Height, cubemap.Channel, cubemap.Buffer, 0);

    auto current_path = std::filesystem::current_path().string();

    EXPECT_TRUE(std::filesystem::exists(current_path + "/screenshot3.hdr"));
    EXPECT_TRUE(std::filesystem::exists(current_path + "/screenshot4.hdr"));
}
