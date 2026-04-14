#pragma once
#include <Rendering/Buffers/Bitmap.h>
#include <ZEngineDef.h>
#include <string>

namespace ZEngine::Importers
{
    struct EnvironmentMapFileHeader
    {
        uint32_t MagicNumber    = 0;
        uint32_t Version        = 0;
        int32_t  FaceWidth      = 0;
        int32_t  FaceHeight     = 0;
        int32_t  Channel        = 0;
        int32_t  LayerCount     = 0;
        uint64_t BufferByteSize = 0;
    };

    struct EnvironmentMapImporter
    {

        static bool WriteToFile(const ZEngine::Rendering::Buffers::Bitmap& cubemap, const char* output_path, std::string& out_error);
        static bool Import(const char* input_filename, const char* output_dir, std::string& out_filepath, std::string& out_error);
        static bool ReadHeader(const char* zenvmap_file, EnvironmentMapFileHeader& out_header);
        static bool Deserialize(const char* zenvmap_file, Rendering::Buffers::Bitmap& out_cubemap);
    };

} // namespace ZEngine::Importers
