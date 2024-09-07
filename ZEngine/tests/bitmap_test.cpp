#include <Rendering/Buffers/Bitmap.h>
#include <gtest/gtest.h>
#include <stb/stb_image.h>
#include <stb/stb_image_write.h>
#include <cmath>
#include <filesystem>

using namespace ZEngine::Rendering::Buffers;

constexpr float epsilon = 1e-2;

static bool approximatelyEqual(float a, float b, float epsilon)
{
    return fabs(a - b) <= epsilon * fmax(1.0f, fmax(fabs(a), fabs(b)));
}

TEST(BitmapTest, GetOrSetPixel)
{

    glm::vec4 p(0.5, 0.5, 0.8, 0.0);

    Bitmap bitmap(100, 100, 3, BitmapFormat::UNSIGNED_BYTE);
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

    Bitmap in             = {width, height, channel, BitmapFormat::FLOAT, image_data};
    Bitmap vertical_cross = Bitmap::EquirectangularMapToVerticalCross(in);
    Bitmap cubemap        = Bitmap::VerticalCrossToCubemap(vertical_cross);
    stbi_image_free((void*) image_data);

    stbi_write_hdr("screenshot.hdr", vertical_cross.Width, vertical_cross.Height, vertical_cross.Channel, (const float*) vertical_cross.Buffer.data());
    stbi_write_hdr("screenshot2.hdr", cubemap.Width, cubemap.Height, cubemap.Channel, (const float*) cubemap.Buffer.data());

    auto current_path = std::filesystem::current_path().string();

    EXPECT_TRUE(std::filesystem::exists(current_path + "/screenshot.hdr"));
    EXPECT_TRUE(std::filesystem::exists(current_path + "/screenshot2.hdr"));
}

TEST(BitmapTest, TestVerticalCross2)
{
    int          width = 0, height = 0, channel = 0;
    const float* image_data = stbi_loadf("piazza_bologni_1k.hdr", &width, &height, &channel, 3);

    std::vector<float> image_buffer_32bit = {};

    if (image_data)
    {
        const int total_pixel = width * height;
        image_buffer_32bit.resize(total_pixel * 4);

        auto source_buffer_24      = image_data;
        auto destination_buffer_32 = image_buffer_32bit.data();
        for (int i = 0; i != total_pixel; i++)
        {
            // Copy RGB channels from source to destination
            *destination_buffer_32++ = *source_buffer_24++;
            *destination_buffer_32++ = *source_buffer_24++;
            *destination_buffer_32++ = *source_buffer_24++;

            // Set alpha channel to 1.0f
            *destination_buffer_32++ = 1.0f;
        }
    }
    stbi_image_free((void*) image_data);

    Bitmap in             = {width, height, 4, BitmapFormat::FLOAT, image_buffer_32bit.data()};
    Bitmap vertical_cross = Bitmap::EquirectangularMapToVerticalCross(in);
    Bitmap cubemap        = Bitmap::VerticalCrossToCubemap(vertical_cross);

    stbi_write_hdr("screenshot3.hdr", vertical_cross.Width, vertical_cross.Height, vertical_cross.Channel, (const float*) vertical_cross.Buffer.data());
    stbi_write_hdr("screenshot4.hdr", cubemap.Width, cubemap.Height, cubemap.Channel, (const float*) cubemap.Buffer.data());
    stbi_write_png("screenshot5.png", cubemap.Width, cubemap.Height, cubemap.Channel, cubemap.Buffer.data(), 0);

    auto current_path = std::filesystem::current_path().string();

    EXPECT_TRUE(std::filesystem::exists(current_path + "/screenshot3.hdr"));
    EXPECT_TRUE(std::filesystem::exists(current_path + "/screenshot4.hdr"));
}