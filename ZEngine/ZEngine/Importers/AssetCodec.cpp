#include <ZEngine/Core/Containers/Array.h>
#include <ZEngine/Core/Containers/UnorderedHashMap.h>
#include <ZEngine/Core/VFS/IVFSFile.h>
#include <ZEngine/Helpers/MemoryOperations.h>
#include <ZEngine/Helpers/SerializerCommonHelper.h>
#include <ZEngine/Importers/AssetCodec.h>
#include <fmt/format.h>
#include <filesystem>
#include <fstream>
#include <string>

using namespace uuids;
using namespace ZEngine::Helpers;
using namespace ZEngine::Core::Containers;
using namespace ZEngine::Core::Maths;

namespace ZEngine::Importers::AssetCodec
{
    AssetImporterOutput SerializeMeshAssetFile(Core::Memory::ArenaAllocator* arena, AssetMesh& mesh, AssetNodeHierarchy& hierarchies, const ImportConfiguration& config)
    {
        AssetImporterOutput output = {};
        if (config.OutputAssetFile.empty())
            return output;

        std::string   fullname_path = fmt::format("{0}{1}{2}{3}{4}", config.OutputWorkingSpacePath.c_str(), PLATFORM_OS_BACKSLASH, config.OutputAssetsPath.c_str(), PLATFORM_OS_BACKSLASH, config.OutputAssetFile.c_str());
        std::ofstream out(fullname_path, std::ios::binary | std::ios::trunc);
        if (!out.is_open())
        {
            out.close();
            return output;
        }

        out.seekp(std::ios::beg);
        WriteBinary(out, ZEMESH_MAGIC);
        WriteBinary(out, ASSET_FILE_VERSION);
        WriteBinary(out, mesh.MeshUUID);
        WriteBinaryArray(out, ArrayView{mesh.Vertices});
        WriteBinaryArray(out, ArrayView{mesh.Indices});
        WriteBinaryArray(out, ArrayView{mesh.SubMeshes});
        WriteBinary(out, hierarchies.NodeHierarchyUUID);
        WriteBinary(out, hierarchies.MeshUUID);
        WriteBinaryArray(out, ArrayView{hierarchies.Hierarchies});
        WriteBinaryArray(out, ArrayView{hierarchies.LocalTransforms});
        WriteBinaryArray(out, ArrayView{hierarchies.GlobalTransforms});

        uint32_t name_count = static_cast<uint32_t>(hierarchies.Names.size());
        WriteBinary(out, name_count);
        for (const auto& name : hierarchies.Names)
            WriteBinaryString(out, name);

        uint32_t mat_name_count = static_cast<uint32_t>(hierarchies.MaterialNames.size());
        WriteBinary(out, mat_name_count);
        for (const auto& name : hierarchies.MaterialNames)
            WriteBinaryString(out, name);

        WriteBinaryHashMap(out, hierarchies.NodeNames);
        WriteBinaryHashMap(out, hierarchies.NodeMeshes);
        WriteBinaryHashMap(out, hierarchies.NodeMaterials);
        out.close();

        output = {.Type = AssetFileType::MESH, .Path = fmt::format("{0}{1}{2}", config.OutputAssetsPath.c_str(), PLATFORM_OS_BACKSLASH, config.OutputAssetFile.c_str()), .RootPath = config.OutputWorkingSpacePath.c_str()};
        return output;
    }

    AssetImporterOutput SerializeMaterialAssetFile(Core::Memory::ArenaAllocator* arena, AssetMaterial& material, const ImportConfiguration& config)
    {
        AssetImporterOutput output             = {};
        std::string         asset_mat_filename = fmt::format("{0}{1}", material.Name.c_str(), ".zematerial");
        std::string         fullname_path      = fmt::format("{0}{1}{2}{3}{4}", config.OutputWorkingSpacePath.c_str(), PLATFORM_OS_BACKSLASH, config.OutputAssetsPath.c_str(), PLATFORM_OS_BACKSLASH, asset_mat_filename.c_str());
        std::ofstream       out(fullname_path, std::ios::binary | std::ios::trunc);
        if (!out.is_open())
        {
            out.close();
            return output;
        }

        out.seekp(std::ios::beg);
        WriteBinary(out, ZEMATERIAL_MAGIC);
        WriteBinary(out, ASSET_FILE_VERSION);
        WriteBinaryString(out, material.Name);
        WriteBinary(out, material.MaterialUUID);
        WriteBinary(out, material.AlbedoTexUUID);
        WriteBinary(out, material.EmissiveTexUUID);
        WriteBinary(out, material.NormalTexUUID);
        WriteBinary(out, material.OpacityTexUUID);
        WriteBinary(out, material.SpecularTexUUID);
        // Texture paths — project-relative VFS paths to extracted image files
        WriteBinaryString(out, material.AlbedoTexPath);
        WriteBinaryString(out, material.EmissiveTexPath);
        WriteBinaryString(out, material.NormalTexPath);
        WriteBinaryString(out, material.OpacityTexPath);
        WriteBinaryString(out, material.SpecularTexPath);
        out.write(reinterpret_cast<const char*>(material.AmbientColor), sizeof(material.AmbientColor));
        out.write(reinterpret_cast<const char*>(material.AlbedoColor), sizeof(material.AlbedoColor));
        out.write(reinterpret_cast<const char*>(material.EmissiveColor), sizeof(material.EmissiveColor));
        out.write(reinterpret_cast<const char*>(material.RoughnessColor), sizeof(material.RoughnessColor));
        out.write(reinterpret_cast<const char*>(material.SpecularColor), sizeof(material.SpecularColor));
        out.write(reinterpret_cast<const char*>(material.Factors), sizeof(material.Factors));
        out.close();

        output = {.Type = AssetFileType::MATERIAL, .Path = fmt::format("{0}{1}{2}", config.OutputAssetsPath.c_str(), PLATFORM_OS_BACKSLASH, asset_mat_filename.c_str()), .RootPath = config.OutputWorkingSpacePath.c_str()};
        return output;
    }

    AssetImporterOutput SerializeTextureAssetFiles(Core::Memory::ArenaAllocator* arena, ArrayView<AssetTexture> textures, const ImportConfiguration& config)
    {
        AssetImporterOutput output             = {};
        std::string         asset_tex_filename = fmt::format("{0}{1}", config.AssetName.c_str(), ".zetextures");
        std::string         fullname_path      = fmt::format("{0}{1}{2}{3}{4}", config.OutputWorkingSpacePath.c_str(), PLATFORM_OS_BACKSLASH, config.OutputAssetsPath.c_str(), PLATFORM_OS_BACKSLASH, asset_tex_filename.c_str());
        std::ofstream       out(fullname_path, std::ios::binary | std::ios::trunc);
        if (!out.is_open())
        {
            out.close();
            return output;
        }

        out.seekp(std::ios::beg);
        WriteBinary(out, ZETEXTURES_MAGIC);
        WriteBinary(out, ASSET_FILE_VERSION);
        uint32_t texture_count = static_cast<uint32_t>(textures.size());
        WriteBinary(out, texture_count);
        for (uint32_t i = 0; i < texture_count; ++i)
        {
            WriteBinary(out, textures[i].TextureUUID);
            WriteBinaryString(out, textures[i].Path);
        }
        out.close();

        output = {.Type = AssetFileType::TEXTURES, .Path = fmt::format("{0}{1}{2}", config.OutputAssetsPath.c_str(), PLATFORM_OS_BACKSLASH, asset_tex_filename.c_str()), .RootPath = config.OutputWorkingSpacePath.c_str()};
        return output;
    }

    void DeserializeMeshAssetFile(Core::Memory::ArenaAllocator* arena, const char* asset_file, AssetMesh& mesh, AssetNodeHierarchy& hierarchies)
    {
        if (!secure_strlen(asset_file))
            return;
        std::ifstream in(asset_file, std::ios::binary);
        if (!in.is_open())
        {
            in.close();
            return;
        }

        in.seekg(std::ios::beg);
        uint32_t meshf_magic, meshf_version;
        ReadBinary(in, meshf_magic);
        ReadBinary(in, meshf_version);
        if (meshf_magic != ZEMESH_MAGIC && meshf_version != ASSET_FILE_VERSION)
        {
            in.close();
            return;
        }

        ReadBinary(in, mesh.MeshUUID);
        ReadBinaryArray(arena, in, mesh.Vertices);
        ReadBinaryArray(arena, in, mesh.Indices);
        ReadBinaryArray(arena, in, mesh.SubMeshes);
        ReadBinary(in, hierarchies.NodeHierarchyUUID);
        ReadBinary(in, hierarchies.MeshUUID);
        ReadBinaryArray(arena, in, hierarchies.Hierarchies);
        ReadBinaryArray(arena, in, hierarchies.LocalTransforms);
        ReadBinaryArray(arena, in, hierarchies.GlobalTransforms);

        uint32_t name_count = 0;
        ReadBinary(in, name_count);
        hierarchies.Names.init(arena, name_count, name_count);
        for (uint32_t i = 0; i < name_count; ++i)
            ReadBinaryString(arena, in, hierarchies.Names[i]);

        uint32_t mat_name_count = 0;
        ReadBinary(in, mat_name_count);
        hierarchies.MaterialNames.init(arena, mat_name_count, mat_name_count);
        for (uint32_t i = 0; i < mat_name_count; ++i)
            ReadBinaryString(arena, in, hierarchies.MaterialNames[i]);

        ReadHashMap(arena, in, hierarchies.NodeNames);
        ReadHashMap(arena, in, hierarchies.NodeMeshes);
        ReadHashMap(arena, in, hierarchies.NodeMaterials);
        in.close();
    }

    void DeserializeMaterialAssetFile(Core::Memory::ArenaAllocator* arena, const char* asset_file, AssetMaterial& material)
    {
        if (!secure_strlen(asset_file))
            return;
        std::ifstream in(asset_file, std::ios::binary);
        if (!in.is_open())
        {
            in.close();
            return;
        }

        in.seekg(std::ios::beg);
        uint32_t scene_magic, scene_version;
        ReadBinary(in, scene_magic);
        ReadBinary(in, scene_version);
        if (scene_magic != ZEMATERIAL_MAGIC && scene_version != ASSET_FILE_VERSION)
        {
            in.close();
            return;
        }

        ReadBinaryString(arena, in, material.Name);
        ReadBinary(in, material.MaterialUUID);
        ReadBinary(in, material.AlbedoTexUUID);
        ReadBinary(in, material.EmissiveTexUUID);
        ReadBinary(in, material.NormalTexUUID);
        ReadBinary(in, material.OpacityTexUUID);
        ReadBinary(in, material.SpecularTexUUID);
        ReadBinaryString(arena, in, material.AlbedoTexPath);
        ReadBinaryString(arena, in, material.EmissiveTexPath);
        ReadBinaryString(arena, in, material.NormalTexPath);
        ReadBinaryString(arena, in, material.OpacityTexPath);
        ReadBinaryString(arena, in, material.SpecularTexPath);
        in.read(reinterpret_cast<char*>(material.AmbientColor), sizeof(material.AmbientColor));
        in.read(reinterpret_cast<char*>(material.AlbedoColor), sizeof(material.AlbedoColor));
        in.read(reinterpret_cast<char*>(material.EmissiveColor), sizeof(material.EmissiveColor));
        in.read(reinterpret_cast<char*>(material.RoughnessColor), sizeof(material.RoughnessColor));
        in.read(reinterpret_cast<char*>(material.SpecularColor), sizeof(material.SpecularColor));
        in.read(reinterpret_cast<char*>(material.Factors), sizeof(material.Factors));
        in.close();
    }

    void DeserializeTextureAssetFile(Core::Memory::ArenaAllocator* arena, const char* asset_file, Core::Containers::Array<AssetTexture>& textures)
    {
        if (!secure_strlen(asset_file))
            return;
        std::ifstream in(asset_file, std::ios::binary);
        if (!in.is_open())
        {
            in.close();
            return;
        }

        in.seekg(std::ios::beg);
        uint32_t scene_magic, scene_version;
        ReadBinary(in, scene_magic);
        ReadBinary(in, scene_version);
        if (scene_magic != ZETEXTURES_MAGIC && scene_version != ASSET_FILE_VERSION)
        {
            in.close();
            return;
        }

        uint32_t texture_count = 0;
        ReadBinary(in, texture_count);
        textures.init(arena, texture_count, texture_count);
        for (uint32_t i = 0; i < texture_count; ++i)
        {
            ReadBinary(in, textures[i].TextureUUID);
            ReadBinaryString(arena, in, textures[i].Path);
        }
        in.close();
    }

    bool ReadAssetMeshFileHeader(const char* asset_file, AssetMeshFileHeader& header)
    {
        if (!secure_strlen(asset_file))
            return false;
        std::ifstream in(asset_file, std::ios::binary);
        if (!in.is_open())
        {
            in.close();
            return false;
        }

        in.seekg(std::ios::beg);
        ReadBinary(in, header.MagicNumber);
        ReadBinary(in, header.Version);
        if (header.MagicNumber != ZEMESH_MAGIC && header.Version != ASSET_FILE_VERSION)
        {
            in.close();
            return false;
        }
        ReadBinary(in, header.Id);
        in.close();
        return true;
    }

    AssetImporterOutput SerializeEnvironmentMapFile(const Rendering::Buffers::Bitmap& cubemap, const ImportConfiguration& config)
    {
        AssetImporterOutput output = {};
        if (config.OutputAssetFile.empty())
            return output;

        std::string     dir_path      = fmt::format("{0}{1}{2}", config.OutputWorkingSpacePath.c_str(), PLATFORM_OS_BACKSLASH, config.OutputAssetsPath.c_str());
        std::string     fullname_path = fmt::format("{0}{1}{2}", dir_path, PLATFORM_OS_BACKSLASH, config.OutputAssetFile.c_str());

        std::error_code ec;
        std::filesystem::create_directories(dir_path, ec);
        std::ofstream out(fullname_path, std::ios::binary | std::ios::trunc);
        if (!out.is_open())
            return output;

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
        out.close();

        output = {.Type = AssetFileType::ENVIRONMENT_MAP, .Path = fullname_path, .RootPath = config.OutputWorkingSpacePath.c_str()};
        return output;
    }

    bool DeserializeEnvironmentMapFile(const char* zenvmap_file, Rendering::Buffers::Bitmap& out_cubemap)
    {
        std::ifstream in(zenvmap_file, std::ios::binary);
        if (!in.is_open())
            return false;

        EnvironmentMapFileHeader header{};
        in.read(reinterpret_cast<char*>(&header), sizeof(header));
        if (!in.good() || header.MagicNumber != ZENVMAP_MAGIC)
            return false;

        out_cubemap      = Rendering::Buffers::Bitmap(header.FaceWidth, header.FaceHeight, header.LayerCount, header.Channel, Rendering::Buffers::BitmapFormat::FLOAT);
        out_cubemap.Type = Rendering::Buffers::BitmapType::CUBE;
        in.read(reinterpret_cast<char*>(out_cubemap.Buffer.data()), static_cast<std::streamsize>(header.BufferByteSize));
        return in.good();
    }

    bool ReadEnvironmentMapFileHeader(const char* zenvmap_file, EnvironmentMapFileHeader& out_header)
    {
        std::ifstream in(zenvmap_file, std::ios::binary);
        if (!in.is_open())
            return false;
        in.read(reinterpret_cast<char*>(&out_header), sizeof(EnvironmentMapFileHeader));
        return in.good() && (out_header.MagicNumber == ZENVMAP_MAGIC);
    }

    Core::VFS::VFSResult<void> SerializeEnvironmentMapFileVFS(Core::VFS::IVFSContext& ctx, const Core::VFS::VFSPath& out_path, const Rendering::Buffers::Bitmap& cubemap)
    {
        // Build .tmp path for atomic write
        char        tmp_buf[MAX_FILE_PATH_COUNT] = {};
        const char* raw                          = out_path.CStr();
        size_t      len                          = secure_strlen(raw);
        size_t      copy                         = len < MAX_FILE_PATH_COUNT - 5 ? len : MAX_FILE_PATH_COUNT - 5;
        secure_memcpy(tmp_buf, MAX_FILE_PATH_COUNT, raw, copy);
        const char tmp_suffix[] = ".tmp";
        secure_memcpy(tmp_buf + copy, MAX_FILE_PATH_COUNT - copy, tmp_suffix, sizeof(tmp_suffix));
        Core::VFS::VFSPath tmp_path    = Core::VFS::VFSPath::Parse(tmp_buf).Value();

        auto               open_result = ctx.Open(tmp_path, Core::VFS::VFSOpenFlags::Write);
        if (open_result.Failed())
            return Core::VFS::VFSResult<void>::Fail(open_result.Error());

        Core::VFS::IVFSFile*     file = open_result.Value();

        EnvironmentMapFileHeader header{
            .MagicNumber    = ZENVMAP_MAGIC,
            .Version        = ASSET_FILE_VERSION,
            .FaceWidth      = cubemap.Width,
            .FaceHeight     = cubemap.Height,
            .Channel        = cubemap.Channel,
            .LayerCount     = cubemap.Depth,
            .BufferByteSize = static_cast<uint64_t>(cubemap.Buffer.size()),
        };

        const auto* hdr_bytes  = reinterpret_cast<const uint8_t*>(&header);
        auto        w1         = file->Write({hdr_bytes, sizeof(header)}, 0);
        const auto* data_bytes = reinterpret_cast<const uint8_t*>(cubemap.Buffer.data());
        auto        w2         = file->Write({data_bytes, cubemap.Buffer.size()}, sizeof(header));
        auto        flush      = file->Flush();
        file->Close();
        ctx.Close(file);

        if (w1.Failed())
            return Core::VFS::VFSResult<void>::Fail(w1.Error());
        if (w2.Failed())
            return Core::VFS::VFSResult<void>::Fail(w2.Error());
        if (flush.Failed())
            return Core::VFS::VFSResult<void>::Fail(flush.Error());

        return ctx.Rename(tmp_path, out_path);
    }

} // namespace ZEngine::Importers::AssetCodec
