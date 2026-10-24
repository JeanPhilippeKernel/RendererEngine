#include <ZEngine/Core/Containers/Array.h>
#include <ZEngine/Core/Containers/UnorderedHashMap.h>
#include <ZEngine/Core/VFS/IVFSFile.h>
#include <ZEngine/Core/VFS/Meta/MetaFileIO.h>
#include <ZEngine/Helpers/MemoryOperations.h>
#include <ZEngine/Helpers/SerializerCommonHelper.h>
#include <ZEngine/Importers/AssetCodec.h>
#include <ZEngine/Logging/LoggerDefinition.h>
#include <fmt/format.h>
#include <nlohmann/json.hpp>
#include <uuid.h>
#include <algorithm>
#include <bit>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <new>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

using namespace uuids;
using namespace ZEngine::Helpers;
using namespace ZEngine::Core::Containers;
using namespace ZEngine::Core::Maths;
using namespace ZEngine::Core::VFS;
using ZEngine::Core::VFS::VFSError;
using ZEngine::Core::VFS::VFSPath;
using ZEngine::Core::VFS::VFSResult;

namespace ZEngine::Importers::AssetCodec
{
    namespace
    {
        bool HasExactEnvironmentMapFileSize(std::ifstream& input, const EnvironmentMapFileHeader& header)
        {
            input.seekg(0, std::ios::end);
            if (!input.good())
                return false;

            const std::streamoff expected_file_size = static_cast<std::streamoff>(header.HeaderByteSize) + static_cast<std::streamoff>(header.BufferByteSize);
            return input.tellg() == expected_file_size;
        }
    } // namespace

    VFSResult<VFSPath> MakeOutputPath(const char* directory, const char* filename)
    {
        if (!filename || !filename[0])
            return VFSResult<VFSPath>::Fail(VFSError::InvalidPath);
        auto dir = VFSPath::Parse(directory);
        if (dir.Failed())
            return VFSResult<VFSPath>::Fail(dir.Error());
        auto path = dir.Value().Append(filename);
        if (path.Failed())
            return path;
        if (path.Value().Parent() != dir.Value())
            return VFSResult<VFSPath>::Fail(VFSError::InvalidPath);
        // Cooked assets also receive a .meta sidecar.
        constexpr size_t metadata_suffix_size = sizeof(".meta") - 1;
        if (path.Value().Length() + metadata_suffix_size >= MAX_FILE_PATH_COUNT)
            return VFSResult<VFSPath>::Fail(VFSError::InvalidPath);
        // Account for the unique temporary sibling, even when the destination
        // has a shorter name. Check without generating a UUID or touching VFS.
        auto temporary = dir.Value().Append(".00000000-0000-0000-0000-000000000000.tmp");
        if (temporary.Failed())
            return temporary;
        return path;
    }

    VFSResult<VFSPath> ValidateImportConfiguration(const ImportConfiguration& config)
    {
        if (!config.VFS)
            return VFSResult<VFSPath>::Fail(VFSError::Unsupported);
        auto mesh_path = MakeOutputPath(config.OutputAssetsPath.c_str(), config.OutputAssetFile.c_str());
        if (mesh_path.Failed())
            return mesh_path;
        if (config.Options.ImportMaterials)
        {
            auto materials = MakeOutputPath(config.OutputMaterialPath.c_str(), config.AssetName.c_str());
            if (materials.Failed())
                return materials;
            if (config.Options.ImportTextures)
            {
                auto textures = MakeOutputPath(config.OutputTextureFilesPath.c_str(), config.AssetName.c_str());
                if (textures.Failed())
                    return textures;
            }
        }
        return mesh_path;
    }

    static VFSResult<VFSPath> PrepareOutputPath(Core::VFS::IVFSContext* vfs, VFSResult<VFSPath> path)
    {
        if (!vfs)
            return VFSResult<VFSPath>::Fail(VFSError::Unsupported);
        if (path.Failed())
            return path;
        const auto dir    = path.Value().Parent();
        auto       create = vfs->CreateDir(dir);
        if (create.Failed() && create.Error() != VFSError::AlreadyExists)
            return VFSResult<VFSPath>::Fail(create.Error());
        return path;
    }

    void ReportPublishedArtifact(const ImportConfiguration& config, AssetFileType type, const VFSPath& path)
    {
        if (config.OnArtifactPublished && path.IsValid())
            config.OnArtifactPublished(config.ArtifactContext, type, path.CStr());
    }

    VFSResult<void> CopyTextureFiles(ArrayView<AssetTexture> textures, const ImportConfiguration& config)
    {
        if (textures.size() == 0)
            return VFSResult<void>::Ok();
        if (!config.VFS)
            return VFSResult<void>::Fail(VFSError::Unsupported);
        auto directory = MakeOutputPath(config.OutputTextureFilesPath.c_str(), config.AssetName.c_str());
        if (directory.Failed())
            return VFSResult<void>::Fail(directory.Error());
        auto create = config.VFS->CreateDir(directory.Value());
        if (create.Failed() && create.Error() != VFSError::AlreadyExists)
            return create;
        for (size_t index = 0; index < textures.size(); ++index)
        {
            auto& texture = textures[index];
            if (texture.Path.empty())
                return VFSResult<void>::Fail(VFSError::InvalidPath);
            const std::filesystem::path source_path(texture.Path.c_str());
            const auto                  source = source_path.is_absolute() ? source_path : std::filesystem::path(config.InputBaseAssetFilePath.empty() ? "" : config.InputBaseAssetFilePath.c_str()) / source_path;
            std::ifstream               input(source, std::ios::binary | std::ios::ate);
            if (!input.is_open())
            {
                ZENGINE_CORE_ERROR("Texture not found: {}", source.string())
                return VFSResult<void>::Fail(VFSError::NotFound);
            }
            const auto size = input.tellg();
            if (size <= 0)
                return VFSResult<void>::Fail(VFSError::IOError);
            std::vector<uint8_t> bytes(static_cast<size_t>(size));
            input.seekg(0, std::ios::beg);
            if (!input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(size)))
                return VFSResult<void>::Fail(VFSError::IOError);
            // Source basenames are not unique (e.g. two folders with albedo.png).
            const auto output_name = fmt::format("{}_{}{}", source.stem().string(), index, source.extension().string());
            auto       destination = MakeOutputPath(directory.Value().CStr(), output_name.c_str());
            if (destination.Failed())
                return VFSResult<void>::Fail(destination.Error());
            auto write = Core::VFS::WriteFileAtomically(*config.VFS, destination.Value(), {bytes.data(), bytes.size()});
            if (write.Failed())
            {
                ZENGINE_CORE_ERROR("Failed to copy texture '{}' to '{}' (VFS error {})", source.string(), destination.Value().CStr(), static_cast<uint32_t>(write.Error()))
                return write;
            }
            ReportPublishedArtifact(config, AssetFileType::TEXTURES, destination.Value());
            texture.Path.clear();
            texture.Path.append(destination.Value().CStr());
        }
        return VFSResult<void>::Ok();
    }

    // Write data atomically via VFS: open .tmp, write, flush, close, rename to out_path.
    static VFSResult<void> WriteVFS(Core::VFS::IVFSContext* vfs, const VFSPath& out_path, const std::string& data)
    {
        if (!vfs)
            return VFSResult<void>::Fail(VFSError::Unsupported);
        const auto* bytes = reinterpret_cast<const uint8_t*>(data.data());
        return Core::VFS::WriteFileAtomically(*vfs, out_path, {bytes, data.size()});
    }

    VFSResult<void> RestoreAssetUUID(Core::VFS::IVFSContext& ctx, const VFSPath& path, uuids::uuid& id)
    {
        auto meta = Core::VFS::MetaFileIO::Read(ctx, path);
        if (meta.Succeeded())
            id = meta.Value().AssetUUID;
        else if (meta.Error() != VFSError::NotFound && meta.Error() != VFSError::Corrupted)
            return VFSResult<void>::Fail(meta.Error());
        return VFSResult<void>::Ok();
    }

    VFSResult<AssetImporterOutput> SerializeMeshAssetFile(Core::Memory::ArenaAllocator* /*arena*/, AssetMesh& mesh, AssetNodeHierarchy& hierarchies, const ImportConfiguration& config)
    {
        auto path = PrepareOutputPath(config.VFS, MakeOutputPath(config.OutputAssetsPath.c_str(), config.OutputAssetFile.c_str()));
        if (path.Failed())
            return VFSResult<AssetImporterOutput>::Fail(path.Error());
        const auto&        mesh_path = path.Value();

        std::ostringstream buf(std::ios::binary);
        WriteBinary(buf, ZEMESH_MAGIC);
        WriteBinary(buf, ASSET_FILE_VERSION);
        WriteBinary(buf, mesh.MeshUUID);
        WriteBinaryArray(buf, ArrayView{mesh.Vertices});
        WriteBinaryArray(buf, ArrayView{mesh.Indices});
        WriteBinaryArray(buf, ArrayView{mesh.SubMeshes});
        WriteBinary(buf, hierarchies.NodeHierarchyUUID);
        WriteBinary(buf, hierarchies.MeshUUID);
        WriteBinaryArray(buf, ArrayView{hierarchies.Hierarchies});
        WriteBinaryArray(buf, ArrayView{hierarchies.LocalTransforms});
        WriteBinaryArray(buf, ArrayView{hierarchies.GlobalTransforms});

        uint32_t name_count = static_cast<uint32_t>(hierarchies.Names.size());
        WriteBinary(buf, name_count);
        for (const auto& name : hierarchies.Names)
            WriteBinaryString(buf, name);

        uint32_t mat_name_count = static_cast<uint32_t>(hierarchies.MaterialNames.size());
        WriteBinary(buf, mat_name_count);
        for (const auto& name : hierarchies.MaterialNames)
            WriteBinaryString(buf, name);

        WriteBinaryHashMap(buf, hierarchies.NodeNames);
        WriteBinaryHashMap(buf, hierarchies.NodeMeshes);
        WriteBinaryHashMap(buf, hierarchies.NodeMaterials);

        auto write = WriteVFS(config.VFS, mesh_path, buf.str());
        if (write.Failed())
            return VFSResult<AssetImporterOutput>::Fail(write.Error());

        ReportPublishedArtifact(config, AssetFileType::MESH, mesh_path);

        return VFSResult<AssetImporterOutput>::Ok({.Type = AssetFileType::MESH, .Path = mesh_path.CStr(), .RootPath = config.OutputWorkingSpacePath.empty() ? "" : config.OutputWorkingSpacePath.c_str()});
    }

    std::string MaterialOutputFilename(const AssetMaterial& material, size_t index)
    {
        // Names are optional/non-unique in source formats. The source index keeps
        // duplicate and unnamed materials distinct and stable across recooks.
        return fmt::format("{}_{}.zematerial", material.Name.empty() ? "material" : material.Name.c_str(), index);
    }

    VFSResult<VFSPath> MaterialOutputPath(const AssetMaterial& material, const ImportConfiguration& config, size_t index)
    {
        auto directory = MakeOutputPath(config.OutputMaterialPath.c_str(), config.AssetName.c_str());
        if (directory.Failed())
            return directory;
        return MakeOutputPath(directory.Value().CStr(), MaterialOutputFilename(material, index).c_str());
    }

    VFSResult<void> SynchronizeTextureMetadata(Core::Memory::ArenaAllocator* arena, ArrayView<AssetTexture> textures, ArrayView<AssetMaterial> materials, const ImportConfiguration& config, const char* importer)
    {
        if (!config.VFS)
            return VFSResult<void>::Fail(VFSError::Unsupported);
        for (size_t t = 0; t < textures.size(); ++t)
        {
            auto& texture = textures[t];
            auto  path    = VFSPath::Parse(texture.Path.c_str());
            if (path.Failed())
                return VFSResult<void>::Fail(path.Error());
            auto hash = Core::VFS::MetaFileIO::ComputeHash(*config.VFS, path.Value());
            if (hash.Failed())
                return VFSResult<void>::Fail(hash.Error());
            auto meta = Core::VFS::MetaFileIO::GetOrCreate(*config.VFS, path.Value(), importer, hash.Value());
            if (meta.Failed())
                return VFSResult<void>::Fail(meta.Error());

            const auto original_uuid = texture.TextureUUID;
            texture.TextureUUID      = meta.Value().AssetUUID;
            for (size_t m = 0; m < materials.size(); ++m)
            {
                auto& material = materials[m];
                auto  sync     = [&](uuids::uuid& id, String& destination) {
                    if (!original_uuid.is_nil() && id == original_uuid)
                    {
                        id = texture.TextureUUID;
                        destination.init(arena, texture.Path.c_str());
                    }
                };
                sync(material.AlbedoTexUUID, material.AlbedoTexPath);
                sync(material.EmissiveTexUUID, material.EmissiveTexPath);
                sync(material.NormalTexUUID, material.NormalTexPath);
                sync(material.OpacityTexUUID, material.OpacityTexPath);
                sync(material.SpecularTexUUID, material.SpecularTexPath);
            }
        }
        return VFSResult<void>::Ok();
    }

    VFSResult<AssetImporterOutput> SerializeMaterialAssetFile(Core::Memory::ArenaAllocator* /*arena*/, AssetMaterial& material, const ImportConfiguration& config, size_t material_index)
    {
        auto path = PrepareOutputPath(config.VFS, MaterialOutputPath(material, config, material_index));
        if (path.Failed())
            return VFSResult<AssetImporterOutput>::Fail(path.Error());
        const auto& mat_path = path.Value();

        auto        tex_obj  = [](const uuids::uuid& uuid, const Core::Containers::String& path) {
            nlohmann::json t;
            t["uuid"] = uuids::to_string(uuid);
            t["path"] = path.empty() ? "" : path.c_str();
            return t;
        };

        nlohmann::json j;
        j["name"]                 = material.Name.empty() ? "" : material.Name.c_str();
        j["uuid"]                 = uuids::to_string(material.MaterialUUID);
        j["textures"]["albedo"]   = tex_obj(material.AlbedoTexUUID, material.AlbedoTexPath);
        j["textures"]["emissive"] = tex_obj(material.EmissiveTexUUID, material.EmissiveTexPath);
        j["textures"]["normal"]   = tex_obj(material.NormalTexUUID, material.NormalTexPath);
        j["textures"]["opacity"]  = tex_obj(material.OpacityTexUUID, material.OpacityTexPath);
        j["textures"]["specular"] = tex_obj(material.SpecularTexUUID, material.SpecularTexPath);
        j["ambient_color"]        = nlohmann::json::array({material.AmbientColor[0], material.AmbientColor[1], material.AmbientColor[2], material.AmbientColor[3]});
        j["albedo_color"]         = nlohmann::json::array({material.AlbedoColor[0], material.AlbedoColor[1], material.AlbedoColor[2], material.AlbedoColor[3]});
        j["emissive_color"]       = nlohmann::json::array({material.EmissiveColor[0], material.EmissiveColor[1], material.EmissiveColor[2], material.EmissiveColor[3]});
        j["roughness_color"]      = nlohmann::json::array({material.RoughnessColor[0], material.RoughnessColor[1], material.RoughnessColor[2], material.RoughnessColor[3]});
        j["specular_color"]       = nlohmann::json::array({material.SpecularColor[0], material.SpecularColor[1], material.SpecularColor[2], material.SpecularColor[3]});
        j["factors"]              = nlohmann::json::array({material.Factors[0], material.Factors[1], material.Factors[2], material.Factors[3]});

        auto write                = WriteVFS(config.VFS, mat_path, j.dump(4));
        if (write.Failed())
            return VFSResult<AssetImporterOutput>::Fail(write.Error());

        ReportPublishedArtifact(config, AssetFileType::MATERIAL, mat_path);

        return VFSResult<AssetImporterOutput>::Ok({.Type = AssetFileType::MATERIAL, .Path = mat_path.CStr(), .RootPath = config.OutputWorkingSpacePath.empty() ? "" : config.OutputWorkingSpacePath.c_str()});
    }

    VFSResult<AssetImporterOutput> SerializeTextureAssetFiles(Core::Memory::ArenaAllocator* /*arena*/, ArrayView<AssetTexture> textures, const ImportConfiguration& config)
    {
        if (config.AssetName.empty())
            return VFSResult<AssetImporterOutput>::Fail(VFSError::InvalidPath);
        std::string asset_tex_filename = fmt::format("{}.zetextures", config.AssetName.c_str());
        auto        path               = PrepareOutputPath(config.VFS, MakeOutputPath(config.OutputAssetsPath.c_str(), asset_tex_filename.c_str()));
        if (path.Failed())
            return VFSResult<AssetImporterOutput>::Fail(path.Error());
        const auto&        tex_path = path.Value();

        std::ostringstream buf(std::ios::binary);
        WriteBinary(buf, ZETEXTURES_MAGIC);
        WriteBinary(buf, ASSET_FILE_VERSION);
        uint32_t texture_count = static_cast<uint32_t>(textures.size());
        WriteBinary(buf, texture_count);
        for (uint32_t i = 0; i < texture_count; ++i)
        {
            WriteBinary(buf, textures[i].TextureUUID);
            WriteBinaryString(buf, textures[i].Path);
        }

        auto write = WriteVFS(config.VFS, tex_path, buf.str());
        if (write.Failed())
            return VFSResult<AssetImporterOutput>::Fail(write.Error());

        ReportPublishedArtifact(config, AssetFileType::TEXTURES, tex_path);

        return VFSResult<AssetImporterOutput>::Ok({.Type = AssetFileType::TEXTURES, .Path = tex_path.CStr(), .RootPath = config.OutputWorkingSpacePath.empty() ? "" : config.OutputWorkingSpacePath.c_str()});
    }

    void DeserializeMeshAssetFile(Core::Memory::ArenaAllocator* arena, const char* asset_file, AssetMesh& mesh, AssetNodeHierarchy& hierarchies)
    {
        const auto result = ReadMeshAssetFile(arena, asset_file, &mesh, &hierarchies);
        if (result.Failed())
            ZENGINE_CORE_WARN("[AssetCodec] Mesh read failed (VFS error {}): {}", static_cast<uint32_t>(result.Error()), asset_file ? asset_file : "<null>")
    }

    void DeserializeMaterialAssetFile(Core::Memory::ArenaAllocator* arena, const char* asset_file, AssetMaterial& material)
    {
        if (!secure_strlen(asset_file))
            return;
        std::ifstream in(asset_file);
        if (!in.is_open())
            return;

        std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        in.close();

        auto j = nlohmann::json::parse(content, nullptr, /*allow_exceptions=*/false);
        if (j.is_discarded())
            return;

        auto read_str = [&](const char* key, Core::Containers::String& out) {
            if (j.contains(key) && j[key].is_string())
                out.init(arena, j[key].get<std::string>().c_str());
        };
        auto read_uuid = [&](const nlohmann::json& node, const char* key, uuids::uuid& out) {
            if (node.contains(key) && node[key].is_string())
            {
                auto parsed = uuids::uuid::from_string(node[key].get<std::string>());
                if (parsed.has_value())
                    out = parsed.value();
            }
        };
        auto read_color = [&](const char* key, float* dst, int n) {
            if (j.contains(key) && j[key].is_array())
            {
                const auto& arr = j[key];
                for (int i = 0; i < n && i < (int) arr.size(); ++i)
                    if (arr[i].is_number())
                        dst[i] = arr[i].get<float>();
            }
        };
        auto read_tex = [&](const char* slot, uuids::uuid& uuid_out, Core::Containers::String& path_out) {
            if (!j.contains("textures") || !j["textures"].contains(slot))
                return;
            const auto& t = j["textures"][slot];
            read_uuid(t, "uuid", uuid_out);
            if (t.contains("path") && t["path"].is_string())
            {
                auto s = t["path"].get<std::string>();
                if (!s.empty())
                    path_out.init(arena, s.c_str());
            }
        };

        read_str("name", material.Name);
        read_uuid(j, "uuid", material.MaterialUUID);
        read_tex("albedo", material.AlbedoTexUUID, material.AlbedoTexPath);
        read_tex("emissive", material.EmissiveTexUUID, material.EmissiveTexPath);
        read_tex("normal", material.NormalTexUUID, material.NormalTexPath);
        read_tex("opacity", material.OpacityTexUUID, material.OpacityTexPath);
        read_tex("specular", material.SpecularTexUUID, material.SpecularTexPath);
        read_color("ambient_color", material.AmbientColor, 4);
        read_color("albedo_color", material.AlbedoColor, 4);
        read_color("emissive_color", material.EmissiveColor, 4);
        read_color("roughness_color", material.RoughnessColor, 4);
        read_color("specular_color", material.SpecularColor, 4);
        read_color("factors", material.Factors, 4);
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
        if (!asset_file || !asset_file[0])
            return false;
        std::ifstream in(asset_file, std::ios::binary);
        if (!in.is_open())
        {
            in.close();
            return false;
        }

        AssetMeshFileHeader candidate = {};
        if (!ReadBinary(in, candidate.MagicNumber) || !ReadBinary(in, candidate.Version) || candidate.MagicNumber != ZEMESH_MAGIC || candidate.Version != ASSET_FILE_VERSION || !ReadBinary(in, candidate.Id) || candidate.Id.is_nil())
            return false;
        header = candidate;
        return true;
    }

    uint32_t GetEnvironmentMapFullMipCount(uint32_t face_size)
    {
        uint32_t mip_count = 1;
        while (face_size > 1)
        {
            face_size >>= 1;
            ++mip_count;
        }
        return mip_count;
    }

    bool IsEnvironmentMapFileHeaderValid(const EnvironmentMapFileHeader& header)
    {
        if (header.MagicNumber != ZENVMAP_MAGIC || header.Version != ENVIRONMENT_MAP_FILE_VERSION || header.HeaderByteSize != sizeof(EnvironmentMapFileHeader) || header.ImporterVersion == 0 || header.FaceWidth == 0 || header.FaceWidth != header.FaceHeight || header.FaceWidth > ENVIRONMENT_MAP_MAX_FACE_SIZE || header.Channel != 4 || header.LayerCount != 6 || header.MipCount != GetEnvironmentMapFullMipCount(header.FaceWidth) || header.ColorSpace != static_cast<uint32_t>(EnvironmentMapColorSpace::LinearScene) ||
            header.Orientation != static_cast<uint32_t>(EnvironmentMapOrientation::RendererCanonical) || header.MipPolicy != static_cast<uint32_t>(EnvironmentMapMipPolicy::GenerateOnGpu) || !std::isfinite(header.Exposure) || header.Exposure <= 0.0f)
            return false;

        constexpr uint64_t k_bytes_per_pixel = sizeof(float) * 4;
        const uint64_t     face_pixels       = static_cast<uint64_t>(header.FaceWidth) * static_cast<uint64_t>(header.FaceHeight);
        if (face_pixels > std::numeric_limits<uint64_t>::max() / header.LayerCount || face_pixels * header.LayerCount > std::numeric_limits<uint64_t>::max() / k_bytes_per_pixel)
            return false;
        return header.BufferByteSize == face_pixels * header.LayerCount * k_bytes_per_pixel;
    }

    bool DoesEnvironmentMapHeaderMatchSource(const EnvironmentMapFileHeader& header, uint64_t source_hash)
    {
        return IsEnvironmentMapFileHeaderValid(header) && header.SourceHash == source_hash;
    }

    bool DeserializeEnvironmentMapFile(const char* zenvmap_file, Rendering::Buffers::Bitmap& out_cubemap)
    {
        std::ifstream in(zenvmap_file, std::ios::binary);
        if (!in.is_open())
            return false;

        EnvironmentMapFileHeader header{};
        in.read(reinterpret_cast<char*>(&header), sizeof(header));
        if (!in.good() || !IsEnvironmentMapFileHeaderValid(header))
            return false;

        if (!HasExactEnvironmentMapFileSize(in, header))
            return false;

        in.seekg(static_cast<std::streamoff>(header.HeaderByteSize), std::ios::beg);
        Rendering::Buffers::Bitmap cubemap = Rendering::Buffers::Bitmap::Create(static_cast<int>(header.FaceWidth), static_cast<int>(header.FaceHeight), static_cast<int>(header.LayerCount), static_cast<int>(header.Channel), Rendering::Buffers::BitmapFormat::Float, Rendering::Buffers::BitmapType::CubeMap);
        if (!cubemap.Buffer)
            return false;
        in.read(reinterpret_cast<char*>(cubemap.Buffer), static_cast<std::streamsize>(header.BufferByteSize));
        if (!in.good())
            return false;

        out_cubemap = std::move(cubemap);
        return true;
    }

    bool ReadEnvironmentMapFileHeader(const char* zenvmap_file, EnvironmentMapFileHeader& out_header)
    {
        std::ifstream in(zenvmap_file, std::ios::binary);
        if (!in.is_open())
            return false;
        in.read(reinterpret_cast<char*>(&out_header), sizeof(EnvironmentMapFileHeader));
        return in.good() && IsEnvironmentMapFileHeaderValid(out_header) && HasExactEnvironmentMapFileSize(in, out_header);
    }

    Core::VFS::VFSResult<void> SerializeEnvironmentMapFileVFS(Core::VFS::IVFSContext& ctx, const Core::VFS::VFSPath& out_path, const Rendering::Buffers::Bitmap& cubemap, const EnvironmentMapCookMetadata& metadata)
    {
        if (cubemap.Type != Rendering::Buffers::BitmapType::CubeMap || cubemap.Width <= 0 || cubemap.Width != cubemap.Height || cubemap.Width > static_cast<int>(ENVIRONMENT_MAP_MAX_FACE_SIZE) || cubemap.Channel != 4 || cubemap.Layers != 6 || cubemap.Format != Rendering::Buffers::BitmapFormat::Float || !cubemap.Buffer || !std::isfinite(metadata.Exposure) || metadata.Exposure <= 0.0f || metadata.ImporterVersion == 0)
            return Core::VFS::VFSResult<void>::Fail(Core::VFS::VFSError::InvalidPath);

        // Build .tmp path for atomic write
        char        tmp_buf[MAX_FILE_PATH_COUNT] = {};
        const char* raw                          = out_path.CStr();
        size_t      len                          = secure_strlen(raw);
        size_t      copy                         = len < MAX_FILE_PATH_COUNT - 5 ? len : MAX_FILE_PATH_COUNT - 5;
        secure_memcpy(tmp_buf, MAX_FILE_PATH_COUNT, raw, copy);
        const char tmp_suffix[] = ".tmp";
        secure_memcpy(tmp_buf + copy, MAX_FILE_PATH_COUNT - copy, tmp_suffix, sizeof(tmp_suffix));
        Core::VFS::VFSPath tmp_path    = Core::VFS::VFSPath::Parse(tmp_buf).Value();

        auto               open_result = ctx.Open(tmp_path, Core::VFS::VFSOpenFlags::Write | Core::VFS::VFSOpenFlags::Create | Core::VFS::VFSOpenFlags::Truncate);
        if (open_result.Failed())
            return Core::VFS::VFSResult<void>::Fail(open_result.Error());

        Core::VFS::IVFSFile*     file = open_result.Value();

        EnvironmentMapFileHeader header{
            .MagicNumber     = ZENVMAP_MAGIC,
            .Version         = ENVIRONMENT_MAP_FILE_VERSION,
            .HeaderByteSize  = sizeof(EnvironmentMapFileHeader),
            .ImporterVersion = metadata.ImporterVersion,
            .SourceHash      = metadata.SourceHash,
            .FaceWidth       = static_cast<uint32_t>(cubemap.Width),
            .FaceHeight      = static_cast<uint32_t>(cubemap.Height),
            .Channel         = static_cast<uint32_t>(cubemap.Channel),
            .LayerCount      = static_cast<uint32_t>(cubemap.Layers),
            .MipCount        = GetEnvironmentMapFullMipCount(static_cast<uint32_t>(cubemap.Width)),
            .ColorSpace      = static_cast<uint32_t>(EnvironmentMapColorSpace::LinearScene),
            .Orientation     = static_cast<uint32_t>(EnvironmentMapOrientation::RendererCanonical),
            .MipPolicy       = static_cast<uint32_t>(EnvironmentMapMipPolicy::GenerateOnGpu),
            .Exposure        = metadata.Exposure,
            .BufferByteSize  = static_cast<uint64_t>(cubemap.BufferSize),
        };
        if (!IsEnvironmentMapFileHeaderValid(header))
            return Core::VFS::VFSResult<void>::Fail(Core::VFS::VFSError::InvalidPath);

        const auto* hdr_bytes  = reinterpret_cast<const uint8_t*>(&header);
        auto        w1         = file->Write({hdr_bytes, sizeof(header)}, 0);
        const auto* data_bytes = reinterpret_cast<const uint8_t*>(cubemap.Buffer);
        auto        w2         = file->Write({data_bytes, cubemap.BufferSize}, sizeof(header));
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

    namespace
    {
        // v1 wrote native structs/counts. Its existing Windows/macOS/Linux 64-bit
        // little-endian layout is explicit here; this is not a portable v2 format.
        static_assert(std::endian::native == std::endian::little && sizeof(size_t) == 8);
        static_assert(sizeof(float) == 4 && std::numeric_limits<float>::is_iec559);
        static_assert(sizeof(uuids::uuid) == 16 && sizeof(AssetSubMesh) == 52);
        static_assert(sizeof(Helpers::NodeHierarchy) == 20 && sizeof(Core::Maths::Mat4f) == 64);

        struct Input
        {
            void* Context                                                  = nullptr;
            VFSResult<size_t> (*Read)(void*, ArrayView<uint8_t>, uint64_t) = nullptr;
            uint64_t Size                                                  = 0;
        };

        struct Reader
        {
            Input               Source        = {};
            ArenaAllocator*     Arena         = nullptr;
            MeshAssetReadLimits Limits        = {};
            uint64_t            Position      = 0;
            size_t              Start         = 0;
            size_t              StringBytes   = 0;
            VFSError            Error         = VFSError::OK;
            uint8_t             Buffer[16384] = {};
            uint64_t            BufferedAt    = 0;
            size_t              BufferedBytes = 0;

            bool                Fail(VFSError error = VFSError::Corrupted)
            {
                if (Error == VFSError::OK)
                    Error = error;
                return false;
            }
            bool Bytes(void* output, size_t bytes)
            {
                if (bytes > Source.Size - Position)
                    return Fail();
                auto* destination = static_cast<uint8_t*>(output);
                while (bytes)
                {
                    if (Position >= BufferedAt && Position - BufferedAt < BufferedBytes)
                    {
                        const size_t at    = size_t(Position - BufferedAt);
                        const size_t count = std::min(bytes, BufferedBytes - at);
                        Helpers::secure_memcpy(destination, bytes, Buffer + at, count);
                        destination += count;
                        bytes       -= count;
                        Position    += count;
                        continue;
                    }
                    if (bytes < sizeof(Buffer))
                    {
                        const size_t count  = size_t(std::min(uint64_t(sizeof(Buffer)), Source.Size - Position));
                        const auto   result = Source.Read(Source.Context, {Buffer, count}, Position);
                        if (result.Failed())
                            return Fail(result.Error());
                        if (!result.Value() || result.Value() > count)
                            return Fail();
                        BufferedAt    = Position;
                        BufferedBytes = result.Value();
                        continue;
                    }
                    // Bound native stream requests even on implementations whose
                    // streamsize is narrower than a complete artifact payload.
                    const size_t count  = std::min(bytes, size_t(ZMega(1ULL)));
                    const auto   result = Source.Read(Source.Context, {destination, count}, Position);
                    if (result.Failed())
                        return Fail(result.Error());
                    if (!result.Value() || result.Value() > count)
                        return Fail();
                    Position    += result.Value();
                    destination += result.Value();
                    bytes       -= result.Value();
                }
                return true;
            }
            template <typename T>
            bool Scalar(T& value)
            {
                static_assert(std::is_trivially_copyable_v<T>);
                return Bytes(&value, sizeof(value));
            }
            bool Budget(size_t bytes, size_t alignment)
            {
                const size_t used    = Arena->m_current_offset - Start;
                const size_t padding = (alignment - Arena->m_current_offset % alignment) % alignment;
                if (used > Limits.MaxDecodedBytes || padding > Limits.MaxDecodedBytes - used || bytes > Limits.MaxDecodedBytes - used - padding)
                    return Fail(VFSError::SizeLimitExceeded);
                return true;
            }
            template <typename T>
            bool ArrayData(Array<T>& array, uint64_t maximum)
            {
                static_assert(std::is_trivially_copyable_v<T>);
                uint64_t count = 0;
                if (!Scalar(count))
                    return false;
                if (count > maximum)
                    return Fail(VFSError::SizeLimitExceeded);
                if (count > (Source.Size - Position) / sizeof(T))
                    return Fail();
                if (!Budget(size_t(count) * sizeof(T), alignof(T)))
                    return false;
                array.m_allocator = Arena;
                if (!array.try_reserve(size_t(count)))
                    return Fail(VFSError::OutOfMemory);
                array.m_size = size_t(count);
                return Bytes(array.data(), array.size() * sizeof(T));
            }
            bool Names(Array<String>& names)
            {
                uint32_t count = 0;
                if (!Scalar(count))
                    return false;
                if (count > Limits.MaxNames)
                    return Fail(VFSError::SizeLimitExceeded);
                if (count > (Source.Size - Position) / sizeof(uint64_t))
                    return Fail();
                if (!Budget(size_t(count) * sizeof(String), alignof(String)))
                    return false;
                names.m_allocator = Arena;
                if (!names.try_reserve(count))
                    return Fail(VFSError::OutOfMemory);
                for (uint32_t i = 0; i < count; ++i)
                {
                    uint64_t length = 0;
                    if (!Scalar(length))
                        return false;
                    if (length > Limits.MaxStringBytes || length + 1 > Limits.MaxStringTotalBytes - StringBytes)
                        return Fail(VFSError::SizeLimitExceeded);
                    StringBytes             += size_t(length) + 1;
                    const size_t wire_bytes  = length ? size_t(length) + 1 : 0;
                    if (wire_bytes > Source.Size - Position)
                        return Fail();
                    if (!Budget(size_t(length) + 1, alignof(char)))
                        return false;
                    auto* data = static_cast<char*>(Arena->AllocateNoZero(size_t(length) + 1, alignof(char)));
                    if (!data)
                        return Fail(VFSError::OutOfMemory);
                    if (!Bytes(data, wire_bytes))
                        return false;
                    if (length && (data[length] != '\0' || std::string_view(data, size_t(length)).find('\0') != std::string_view::npos))
                        return Fail();
                    data[length]      = '\0';
                    auto* name        = new (&names.m_data[i]) String{};
                    name->m_allocator = Arena;
                    name->m_data      = data;
                    name->m_size      = size_t(length);
                    name->m_capacity  = size_t(length) + 1;
                    ++names.m_size;
                }
                return true;
            }
            bool Map(UnorderedHashMap<uint32_t, uint32_t>& map, size_t nodes, size_t values)
            {
                uint64_t count = 0;
                if (!Scalar(count))
                    return false;
                if (count > nodes || count > (Source.Size - Position) / (2 * sizeof(uint32_t)))
                    return Fail();
                size_t capacity = 16;
                while (capacity < count * 2)
                    capacity *= 2;
                if (!Budget(capacity * sizeof(HashEntry<uint32_t, uint32_t>), alignof(HashEntry<uint32_t, uint32_t>)))
                    return false;
                if (!map.try_init(Arena, capacity))
                    return Fail(VFSError::OutOfMemory);
                for (uint64_t i = 0; i < count; ++i)
                {
                    uint32_t key = 0, value = 0;
                    if (!Scalar(key) || !Scalar(value))
                        return false;
                    if (key >= nodes || value >= values || map.contains(key))
                        return Fail();
                    map.insert(key, value);
                }
                return true;
            }
        };

        bool Validate(Reader& reader, const AssetMesh& mesh, const AssetNodeHierarchy& hierarchy)
        {
            if (mesh.Vertices.empty() || mesh.Vertices.size() % 8 || mesh.Indices.empty() || mesh.SubMeshes.empty() || hierarchy.Hierarchies.empty() || hierarchy.LocalTransforms.size() != hierarchy.Hierarchies.size() || hierarchy.GlobalTransforms.size() != hierarchy.Hierarchies.size())
                return reader.Fail();
            for (float value : mesh.Vertices)
                if (!std::isfinite(value))
                    return reader.Fail();
            const size_t vertices   = mesh.Vertices.size() / 8;
            size_t       next_index = 0;
            for (const auto& sub : mesh.SubMeshes)
            {
                // Index ranges partition the stream. Besides preventing hidden
                // unused indices, this bounds validation to O(V + I + S), not
                // repeated scans of attacker-controlled overlapping ranges.
                if (!sub.VertexCount || !sub.IndexCount || sub.IndexCount % 3 || sub.VertexOffset > vertices || sub.VertexCount > vertices - sub.VertexOffset || sub.IndexOffset != next_index || sub.IndexCount > mesh.Indices.size() - next_index || sub.VertexUnitStreamSize != 8 * sizeof(float) || sub.IndexUnitStreamSize != sizeof(uint32_t))
                    return reader.Fail();
                for (size_t i = next_index; i < next_index + sub.IndexCount; ++i)
                    if (mesh.Indices[i] >= sub.VertexCount)
                        return reader.Fail();
                next_index += sub.IndexCount;
            }
            if (next_index != mesh.Indices.size())
                return reader.Fail();

            const auto count = hierarchy.Hierarchies.size();
            for (size_t i = 0; i < count; ++i)
            {
                const auto& node        = hierarchy.Hierarchies[i];
                const auto  index_valid = [&](int index) { return index == -1 || (index >= 0 && size_t(index) < count && size_t(index) != i); };
                if (!index_valid(node.Parent) || !index_valid(node.FirstChild) || !index_valid(node.LastChild) || !index_valid(node.RightSibling) || node.DepthLevel < 0 || size_t(node.DepthLevel) >= count)
                    return reader.Fail();
                if (node.Parent == -1)
                {
                    if (node.DepthLevel != 0 || node.RightSibling != -1)
                        return reader.Fail();
                }
                else
                {
                    const int depth = hierarchy.Hierarchies[node.Parent].DepthLevel;
                    if (depth < 0 || size_t(depth) >= count || node.DepthLevel != depth + 1)
                        return reader.Fail();
                }
                for (const auto* matrix : {&hierarchy.LocalTransforms[i], &hierarchy.GlobalTransforms[i]})
                    for (const auto& column : matrix->m_data)
                        for (float value : column)
                            if (!std::isfinite(value))
                                return reader.Fail();
            }
            // Child/sibling consistency in O(N), with one byte per node of
            // temporary state. Parent-depth validation above already excludes
            // cycles without recursive DFS or walking every ancestor chain.
            if (!reader.Budget(count, alignof(uint8_t)))
                return false;
            ArenaScratch temporary(reader.Arena);
            auto*        seen = static_cast<uint8_t*>(temporary.Arena()->Allocate(count, alignof(uint8_t)));
            if (!seen)
                return reader.Fail(VFSError::OutOfMemory);
            for (size_t i = 0; i < count; ++i)
            {
                int last = -1;
                for (int child = hierarchy.Hierarchies[i].FirstChild; child != -1; child = hierarchy.Hierarchies[child].RightSibling)
                {
                    if (seen[child] || hierarchy.Hierarchies[child].Parent != int(i))
                        return reader.Fail();
                    seen[child] = 1;
                    last        = child;
                }
                if (hierarchy.Hierarchies[i].LastChild != -1 && hierarchy.Hierarchies[i].LastChild != last)
                    return reader.Fail();
            }
            for (size_t i = 0; i < count; ++i)
                if ((hierarchy.Hierarchies[i].Parent != -1) != (seen[i] != 0))
                    return reader.Fail();
            return true;
        }

        VFSResult<void> Decode(Input input, ArenaAllocator* arena, AssetMesh* out_mesh, AssetNodeHierarchy* out_hierarchy, const uuids::uuid& expected_uuid, const MeshAssetReadLimits& limits)
        {
            if (!arena || !arena->m_memory || !out_mesh || !out_hierarchy || !input.Read || arena->m_current_offset > arena->m_total_size)
                return VFSResult<void>::Fail(VFSError::OutOfMemory);
            if (input.Size > limits.MaxFileBytes)
                return VFSResult<void>::Fail(VFSError::SizeLimitExceeded);
            ArenaCheckpoint    transaction(arena);
            Reader             reader{.Source = input, .Arena = arena, .Limits = limits, .Start = arena->m_current_offset};
            uint32_t           magic = 0, version = 0;
            AssetMesh          mesh      = {};
            AssetNodeHierarchy hierarchy = {};
            if (!reader.Scalar(magic) || !reader.Scalar(version) || magic != ZEMESH_MAGIC || version != ASSET_FILE_VERSION || !reader.Scalar(mesh.MeshUUID) || mesh.MeshUUID.is_nil() || (!expected_uuid.is_nil() && mesh.MeshUUID != expected_uuid))
                return VFSResult<void>::Fail(reader.Error == VFSError::OK ? VFSError::Corrupted : reader.Error);
            if (!reader.ArrayData(mesh.Vertices, uint64_t(limits.MaxVertices) * 8) || !reader.ArrayData(mesh.Indices, limits.MaxIndices) || !reader.ArrayData(mesh.SubMeshes, limits.MaxSubMeshes) || !reader.Scalar(hierarchy.NodeHierarchyUUID) || !reader.Scalar(hierarchy.MeshUUID))
                return VFSResult<void>::Fail(reader.Error);
            if (hierarchy.NodeHierarchyUUID.is_nil() || hierarchy.NodeHierarchyUUID == mesh.MeshUUID || hierarchy.MeshUUID != mesh.MeshUUID)
                return VFSResult<void>::Fail(VFSError::Corrupted);
            if (!reader.ArrayData(hierarchy.Hierarchies, limits.MaxNodes) || !reader.ArrayData(hierarchy.LocalTransforms, limits.MaxNodes) || !reader.ArrayData(hierarchy.GlobalTransforms, limits.MaxNodes) || !reader.Names(hierarchy.Names) || !reader.Names(hierarchy.MaterialNames) || !reader.Map(hierarchy.NodeNames, hierarchy.Hierarchies.size(), hierarchy.Names.size()) || !reader.Map(hierarchy.NodeMeshes, hierarchy.Hierarchies.size(), mesh.SubMeshes.size()) || !reader.Map(hierarchy.NodeMaterials, hierarchy.Hierarchies.size(), hierarchy.MaterialNames.size()) ||
                !Validate(reader, mesh, hierarchy))
                return VFSResult<void>::Fail(reader.Error);
            if (reader.Position != input.Size)
                return VFSResult<void>::Fail(VFSError::Corrupted);
            *out_mesh      = std::move(mesh);
            *out_hierarchy = std::move(hierarchy);
            transaction.Commit();
            return VFSResult<void>::Ok();
        }
    } // namespace

    VFSResult<void> ReadMeshAssetFileVFS(ArenaAllocator* arena, IVFSContext& vfs, const VFSPath& path, AssetMesh* mesh, AssetNodeHierarchy* hierarchy, const uuids::uuid& expected_uuid, const MeshAssetReadLimits& limits)
    {
        const auto opened = vfs.Open(path, VFSOpenFlags::Read);
        if (opened.Failed())
            return VFSResult<void>::Fail(opened.Error());
        struct Lease
        {
            IVFSContext* VFS  = nullptr;
            IVFSFile*    File = nullptr;
            ~Lease()
            {
                VFS->Close(File);
            }
        } lease{&vfs, opened.Value()};
        const auto size = lease.File->Size();
        if (size.Failed())
            return VFSResult<void>::Fail(size.Error());
        Input input = {.Context = lease.File, .Read = [](void* file, ArrayView<uint8_t> bytes, uint64_t offset) { return static_cast<IVFSFile*>(file)->Read(bytes, offset); }, .Size = size.Value()};
        return Decode(input, arena, mesh, hierarchy, expected_uuid, limits);
    }

    VFSResult<void> ReadMeshAssetFile(ArenaAllocator* arena, const char* path, AssetMesh* mesh, AssetNodeHierarchy* hierarchy, const uuids::uuid& expected_uuid, const MeshAssetReadLimits& limits)
    {
        if (!path || !path[0])
            return VFSResult<void>::Fail(VFSError::InvalidPath);
        std::ifstream file(path, std::ios::binary | std::ios::ate);
        if (!file.is_open())
            return VFSResult<void>::Fail(VFSError::NotFound);
        const auto end = file.tellg();
        if (end < 0)
            return VFSResult<void>::Fail(VFSError::IOError);
        Input input = {
            .Context = &file,
            .Read =
                [](void* context, ArrayView<uint8_t> bytes, uint64_t offset) {
                    auto& source = *static_cast<std::ifstream*>(context);
                    source.clear();
                    source.seekg(static_cast<std::streamoff>(offset));
                    if (!source.good())
                        return VFSResult<size_t>::Fail(VFSError::IOError);
                    source.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
                    if (source.bad())
                        return VFSResult<size_t>::Fail(VFSError::IOError);
                    return VFSResult<size_t>::Ok(static_cast<size_t>(source.gcount()));
                },
            .Size = static_cast<uint64_t>(end)};
        return Decode(input, arena, mesh, hierarchy, expected_uuid, limits);
    }

} // namespace ZEngine::Importers::AssetCodec
