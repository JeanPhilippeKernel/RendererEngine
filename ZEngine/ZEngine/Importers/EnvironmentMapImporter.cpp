#include <EnvironmentMapImporter.h>
#include <Helpers/MemoryOperations.h>
#include <ZEngineDef.h>
#include <fmt/format.h>

// stb_image implementation is defined once in AsyncResourceLoader.cpp.
// Only include the header here.
#include <stb/stb_image.h>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

using namespace ZEngine::Rendering::Buffers;

namespace ZEngine::Importers
{

    bool EnvironmentMapImporter::WriteToFile(const Bitmap& cubemap, const char* output_path, std::string& out_error)
    {
        std::ofstream out(output_path, std::ios::binary | std::ios::trunc);
        if (!out.is_open())
        {
            out_error = fmt::format("Failed to open output file for writing: {}", output_path);
            return false;
        }

        EnvironmentMapFileHeader header{
            .MagicNumber    = ZENVMAP_MAGIC,
            .Version        = ASSET_FILE_VERSION,
            .FaceWidth      = cubemap.Width,
            .FaceHeight     = cubemap.Height,
            .Channel        = cubemap.Channel,
            .LayerCount     = cubemap.Depth,
            .BufferByteSize = static_cast<uint64_t>(cubemap.Buffer.size()),
        };

        out.write(reinterpret_cast<const char*>(&header), sizeof(header));
        out.write(reinterpret_cast<const char*>(cubemap.Buffer.data()), static_cast<std::streamsize>(cubemap.Buffer.size()));

        if (!out.good())
        {
            out_error = fmt::format("Write error while serializing environment map: {}", output_path);
            return false;
        }

        out.close();
        return true;
    }

    bool EnvironmentMapImporter::Import(const char* input_filename, const char* output_dir, std::string& out_filepath, std::string& out_error)
    {
        std::error_code ec;
        fs::create_directories(output_dir, ec);
        if (ec)
        {
            out_error = fmt::format("Failed to create output directory '{}': {}", output_dir, ec.message());
            return false;
        }

        int width = 0, height = 0, channel = 0;
        stbi_set_flip_vertically_on_load(1);
        const float* image_data = stbi_loadf(input_filename, &width, &height, &channel, 4);
        if (!image_data)
        {
            out_error = fmt::format("stbi_loadf failed to decode '{}': {}", input_filename, stbi_failure_reason());
            return false;
        }

        Bitmap equirect = {width, height, 4, BitmapFormat::FLOAT, image_data};
        stbi_image_free(const_cast<float*>(image_data));

        Bitmap vertical_cross = Bitmap::EquirectangularMapToVerticalCross(equirect);
        Bitmap cubemap        = Bitmap::VerticalCrossToCubemap(vertical_cross);

        auto   asset_name     = fs::path(input_filename).filename().replace_extension().string();
        auto   output_file    = fmt::format("{}{}{}.zenvmap", output_dir, PLATFORM_OS_BACKSLASH, asset_name.c_str());

        if (!WriteToFile(cubemap, output_file.c_str(), out_error))
        {
            return false;
        }

        out_filepath = std::move(output_file);
        return true;
    }

    bool EnvironmentMapImporter::ReadHeader(const char* zenvmap_file, EnvironmentMapFileHeader& out_header)
    {
        std::ifstream input(zenvmap_file, std::ios::binary);
        if (!input.is_open())
        {
            return false;
        }

        input.read(reinterpret_cast<char*>(&out_header), sizeof(EnvironmentMapFileHeader));

        return input.good() && (out_header.MagicNumber == ZENVMAP_MAGIC);
    }

    bool EnvironmentMapImporter::Deserialize(const char* zenvmap_file, Bitmap& out_cubemap)
    {
        std::ifstream input(zenvmap_file, std::ios::binary);
        if (!input.is_open())
        {
            return false;
        }

        EnvironmentMapFileHeader header{};
        input.read(reinterpret_cast<char*>(&header), sizeof(header));

        if (!input.good() || header.MagicNumber != ZENVMAP_MAGIC)
        {
            return false;
        }

        out_cubemap      = Bitmap(header.FaceWidth, header.FaceHeight, header.LayerCount, header.Channel, BitmapFormat::FLOAT);
        out_cubemap.Type = BitmapType::CUBE;

        input.read(reinterpret_cast<char*>(out_cubemap.Buffer.data()), static_cast<std::streamsize>(header.BufferByteSize));

        return input.good();
    }

} // namespace ZEngine::Importers
