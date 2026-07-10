#include <ZEngine/Core/Containers/Array.h>
#include <ZEngine/Core/Containers/UnorderedHashMap.h>
#include <ZEngine/Helpers/SerializerCommonHelper.h>
#include <ZEngine/Importers/IAssetImporter.h>
#include <fmt/format.h>
#include <filesystem>
#include <fstream>
#include <string>

using namespace uuids;
using namespace ZEngine::Helpers;
using namespace ZEngine::Core::Containers;
using namespace ZEngine::Core::Maths;

namespace ZEngine::Importers
{
    int AddNode(AssetNodeHierarchy& hierarchy, int parent, int depth)
    {
        if (depth < 0)
        {
            return -1;
        }

        int node_id = (int) hierarchy.Hierarchies.size();

        hierarchy.Hierarchies.push({.Parent = parent});
        hierarchy.LocalTransforms.push(Identity<Mat4f>());
        hierarchy.GlobalTransforms.push(Identity<Mat4f>());

        if (parent > -1)
        {
            int first_child = hierarchy.Hierarchies[parent].FirstChild;

            if (first_child == -1)
            {
                hierarchy.Hierarchies[parent].FirstChild = node_id;
            }
            else
            {
                int right_sibling = hierarchy.Hierarchies[first_child].RightSibling;
                if (right_sibling > -1)
                {
                    // iterate nextSibling_ indices
                    for (right_sibling = first_child; hierarchy.Hierarchies[right_sibling].RightSibling != -1; right_sibling = hierarchy.Hierarchies[right_sibling].RightSibling)
                    {
                    }
                    hierarchy.Hierarchies[right_sibling].RightSibling = node_id;
                }
                else
                {
                    hierarchy.Hierarchies[first_child].RightSibling = node_id;
                }
            }
        }
        hierarchy.Hierarchies[node_id].DepthLevel   = depth;
        hierarchy.Hierarchies[node_id].RightSibling = -1;
        hierarchy.Hierarchies[node_id].FirstChild   = -1;

        return node_id;
    }

    void IAssetImporter::Initialize(Core::Memory::ArenaAllocator* arena)
    {
        arena->CreateSubArena(ZMega(350), &Arena);
    }

    void IAssetImporter::SetOnCompleteCallback(on_import_complete_fn callback)
    {
        m_complete_callback = callback;
    }

    void IAssetImporter::SetOnProgressCallback(on_import_progress_fn callback)
    {
        m_progress_callback = callback;
    }

    void IAssetImporter::SetOnErrorCallback(on_import_error_fn callback)
    {
        m_error_callback = callback;
    }

    void IAssetImporter::SetOnLogCallback(on_import_log_fn callback)
    {
        m_log_callback = callback;
    }

    bool IAssetImporter::IsImporting()
    {
        return m_is_importing.load(std::memory_order_acquire);
    }

    AssetImporterOutput IAssetImporter::SerializeMeshAssetFile(Core::Memory::ArenaAllocator* arena, AssetMesh& mesh, AssetNodeHierarchy& hierarchies, const ImportConfiguration& config)
    {
        AssetImporterOutput output = {};

        if (config.OutputAssetFile.empty())
        {
            return output;
        }

        std::string   fullname_path = fmt::format("{0}{1}{2}{3}{4}", config.OutputWorkingSpacePath.c_str(), PLATFORM_OS_BACKSLASH, config.OutputAssetsPath.c_str(), PLATFORM_OS_BACKSLASH, config.OutputAssetFile.c_str());
        std::ofstream out(fullname_path, std::ios::binary | std::ios::trunc);

        if (!out.is_open())
        {
            out.close();
            return output;
        }

        out.seekp(std::ios::beg);
        /*
         *  Magic Headers
         */
        WriteBinary(out, ZEMESH_MAGIC);
        WriteBinary(out, ASSET_FILE_VERSION);
        /*
         * Asset Mesh
         */
        WriteBinary(out, mesh.MeshUUID);
        WriteBinaryArray(out, ArrayView{mesh.Vertices});
        WriteBinaryArray(out, ArrayView{mesh.Indices});
        WriteBinaryArray(out, ArrayView{mesh.SubMeshes});

        /*
         * Asset AssetNodeHierarchy
         */
        WriteBinary(out, hierarchies.NodeHierarchyUUID);
        WriteBinary(out, hierarchies.MeshUUID);
        WriteBinaryArray(out, ArrayView{hierarchies.Hierarchies});
        WriteBinaryArray(out, ArrayView{hierarchies.LocalTransforms});
        WriteBinaryArray(out, ArrayView{hierarchies.GlobalTransforms});

        uint32_t name_count = static_cast<uint32_t>(hierarchies.Names.size());
        WriteBinary(out, name_count);
        for (const auto& name : hierarchies.Names)
        {
            WriteBinaryString(out, name);
        }

        uint32_t material_name_count = static_cast<uint32_t>(hierarchies.MaterialNames.size());
        WriteBinary(out, material_name_count);
        for (const auto& name : hierarchies.MaterialNames)
        {
            WriteBinaryString(out, name);
        }

        WriteBinaryHashMap(out, hierarchies.NodeNames);
        WriteBinaryHashMap(out, hierarchies.NodeMeshes);
        WriteBinaryHashMap(out, hierarchies.NodeMaterials);

        out.close();

        output = {.Type = AssetFileType::MESH, .Path = fmt::format("{0}{1}{2}", config.OutputAssetsPath.c_str(), PLATFORM_OS_BACKSLASH, config.OutputAssetFile.c_str()), .RootPath = config.OutputWorkingSpacePath.c_str()};
        return output;
    }

    AssetImporterOutput IAssetImporter::SerializeMaterialAssetFile(Core::Memory::ArenaAllocator* arena, AssetMaterial& material, const ImportConfiguration& config)
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
        /*
         *  Magic Headers
         */
        WriteBinary(out, ZEMATERIAL_MAGIC);
        WriteBinary(out, ASSET_FILE_VERSION);

        /*
         * Asset Materials
         */
        WriteBinaryString(out, material.Name);
        WriteBinary(out, material.MaterialUUID);
        WriteBinary(out, material.AlbedoTexUUID);
        WriteBinary(out, material.EmissiveTexUUID);
        WriteBinary(out, material.NormalTexUUID);
        WriteBinary(out, material.OpacityTexUUID);
        WriteBinary(out, material.SpecularTexUUID);
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

    AssetImporterOutput IAssetImporter::SerializeTextureAssetFiles(Core::Memory::ArenaAllocator* arena, ArrayView<AssetTexture> textures, const ImportConfiguration& config)
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
        /*
         *  Magic Headers
         */
        WriteBinary(out, ZETEXTURES_MAGIC);
        WriteBinary(out, ASSET_FILE_VERSION);
        /*
         * Asset Textures
         */
        uint32_t texture_count = (uint32_t) textures.size();
        WriteBinary(out, texture_count);
        for (unsigned i = 0; i < texture_count; ++i)
        {
            auto& tex = textures[i];
            WriteBinary(out, tex.TextureUUID);
            WriteBinaryString(out, tex.Path);
        }

        out.close();

        output = {.Type = AssetFileType::TEXTURES, .Path = fmt::format("{0}{1}{2}", config.OutputAssetsPath.c_str(), PLATFORM_OS_BACKSLASH, asset_tex_filename.c_str()), .RootPath = config.OutputWorkingSpacePath.c_str()};
        return output;
    }

    void IAssetImporter::DeserializeMeshAssetFile(Core::Memory::ArenaAllocator* arena, const char* asset_file, AssetMesh& mesh, AssetNodeHierarchy& hierarchies)
    {
        if (!Helpers::secure_strlen(asset_file))
        {
            return;
        }

        std::ifstream in(asset_file, std::ios::binary);

        if (!in.is_open())
        {
            in.close();
            return;
        }

        in.seekg(std::ios::beg);

        uint32_t meshf_magic;
        uint32_t meshf_version;
        ReadBinary(in, meshf_magic);
        ReadBinary(in, meshf_version);

        if (meshf_magic != ZEMESH_MAGIC && meshf_version != ASSET_FILE_VERSION)
        {
            in.close();
            return;
        }

        /*
         * Asset Mesh
         */
        ReadBinary(in, mesh.MeshUUID);
        ReadBinaryArray(arena, in, mesh.Vertices);
        ReadBinaryArray(arena, in, mesh.Indices);
        ReadBinaryArray(arena, in, mesh.SubMeshes);

        /*
         * Asset AssetNodeHierarchy
         */
        ReadBinary(in, hierarchies.NodeHierarchyUUID);
        ReadBinary(in, hierarchies.MeshUUID);
        ReadBinaryArray(arena, in, hierarchies.Hierarchies);
        ReadBinaryArray(arena, in, hierarchies.LocalTransforms);
        ReadBinaryArray(arena, in, hierarchies.GlobalTransforms);

        uint32_t name_count = 0;
        ReadBinary(in, name_count);
        hierarchies.Names.init(arena, name_count, name_count);
        for (int i = 0; i < name_count; ++i)
        {
            auto& name = hierarchies.Names[i];
            ReadBinaryString(arena, in, name);
        }

        uint32_t material_name_count = 0;
        ReadBinary(in, material_name_count);
        hierarchies.MaterialNames.init(arena, material_name_count, material_name_count);
        for (int i = 0; i < material_name_count; ++i)
        {
            auto& name = hierarchies.MaterialNames[i];
            ReadBinaryString(arena, in, name);
        }

        ReadHashMap(arena, in, hierarchies.NodeNames);
        ReadHashMap(arena, in, hierarchies.NodeMeshes);
        ReadHashMap(arena, in, hierarchies.NodeMaterials);

        in.close();
    }

    void IAssetImporter::DeserializeMaterialAssetFile(Core::Memory::ArenaAllocator* arena, const char* asset_file, AssetMaterial& material)
    {
        if (!Helpers::secure_strlen(asset_file))
        {
            return;
        }

        std::ifstream in(asset_file, std::ios::binary);

        if (!in.is_open())
        {
            in.close();
            return;
        }

        in.seekg(std::ios::beg);

        uint32_t scene_magic;
        uint32_t scene_version;
        ReadBinary(in, scene_magic);
        ReadBinary(in, scene_version);

        if (scene_magic != ZEMATERIAL_MAGIC && scene_version != ASSET_FILE_VERSION)
        {
            in.close();
            return;
        }

        /*
         * Asset Materials
         */
        ReadBinaryString(arena, in, material.Name);
        ReadBinary(in, material.MaterialUUID);
        ReadBinary(in, material.AlbedoTexUUID);
        ReadBinary(in, material.EmissiveTexUUID);
        ReadBinary(in, material.NormalTexUUID);
        ReadBinary(in, material.OpacityTexUUID);
        ReadBinary(in, material.SpecularTexUUID);
        in.read(reinterpret_cast<char*>(material.AmbientColor), sizeof(material.AmbientColor));
        in.read(reinterpret_cast<char*>(material.AlbedoColor), sizeof(material.AlbedoColor));
        in.read(reinterpret_cast<char*>(material.EmissiveColor), sizeof(material.EmissiveColor));
        in.read(reinterpret_cast<char*>(material.RoughnessColor), sizeof(material.RoughnessColor));
        in.read(reinterpret_cast<char*>(material.SpecularColor), sizeof(material.SpecularColor));
        in.read(reinterpret_cast<char*>(material.Factors), sizeof(material.Factors));

        in.close();
    }

    void IAssetImporter::DeserializeTextureAssetFile(Core::Memory::ArenaAllocator* arena, const char* asset_file, Core::Containers::Array<AssetTexture>& textures)
    {
        if (!Helpers::secure_strlen(asset_file))
        {
            return;
        }

        std::ifstream in(asset_file, std::ios::binary);

        if (!in.is_open())
        {
            in.close();
            return;
        }

        in.seekg(std::ios::beg);

        uint32_t scene_magic;
        uint32_t scene_version;
        ReadBinary(in, scene_magic);
        ReadBinary(in, scene_version);

        if (scene_magic != ZETEXTURES_MAGIC && scene_version != ASSET_FILE_VERSION)
        {
            in.close();
            return;
        }

        /*
         * Asset Textures
         */
        uint32_t texture_count = 0;
        ReadBinary(in, texture_count);
        textures.init(arena, texture_count, texture_count);
        for (int i = 0; i < texture_count; ++i)
        {
            auto& tex = textures[i];
            ReadBinary(in, tex.TextureUUID);
            ReadBinaryString(arena, in, tex.Path);
        }

        in.close();
    }

    bool IAssetImporter::ReadAssetMeshFileHeader(cstring asset_file, AssetMeshFileHeader& header)
    {
        bool output = false;
        if (!Helpers::secure_strlen(asset_file))
        {
            return output;
        }

        std::ifstream in(asset_file, std::ios::binary);

        if (!in.is_open())
        {
            in.close();
            return output;
        }

        in.seekg(std::ios::beg);

        ReadBinary(in, header.MagicNumber);
        ReadBinary(in, header.Version);

        if (header.MagicNumber != ZEMESH_MAGIC && header.Version != ASSET_FILE_VERSION)
        {
            in.close();
            return output;
        }

        /*
         * Asset Mesh Header
         */
        ReadBinary(in, header.Id);
        output = true;

        in.close();
        return output;
    }

    AssetImporterOutput IAssetImporter::SerializeEnvironmentMapFile(const Rendering::Buffers::Bitmap& cubemap, const ImportConfiguration& config)
    {
        AssetImporterOutput output = {};

        if (config.OutputAssetFile.empty())
        {
            return output;
        }

        std::string     dir_path      = fmt::format("{0}{1}{2}", config.OutputWorkingSpacePath.c_str(), PLATFORM_OS_BACKSLASH, config.OutputAssetsPath.c_str());
        std::string     fullname_path = fmt::format("{0}{1}{2}", dir_path, PLATFORM_OS_BACKSLASH, config.OutputAssetFile.c_str());

        std::error_code ec;
        std::filesystem::create_directories(dir_path, ec);
        std::ofstream out(fullname_path, std::ios::binary | std::ios::trunc);

        if (!out.is_open())
        {
            return output;
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
        out.close();

        output = {.Type = AssetFileType::ENVIRONMENT_MAP, .Path = fullname_path, .RootPath = config.OutputWorkingSpacePath.c_str()};
        return output;
    }

    bool IAssetImporter::DeserializeEnvironmentMapFile(const char* zenvmap_file, Rendering::Buffers::Bitmap& out_cubemap)
    {
        std::ifstream in(zenvmap_file, std::ios::binary);
        if (!in.is_open())
        {
            return false;
        }

        EnvironmentMapFileHeader header{};
        in.read(reinterpret_cast<char*>(&header), sizeof(header));

        if (!in.good() || header.MagicNumber != ZENVMAP_MAGIC)
        {
            return false;
        }

        out_cubemap      = Rendering::Buffers::Bitmap(header.FaceWidth, header.FaceHeight, header.LayerCount, header.Channel, Rendering::Buffers::BitmapFormat::FLOAT);
        out_cubemap.Type = Rendering::Buffers::BitmapType::CUBE;

        in.read(reinterpret_cast<char*>(out_cubemap.Buffer.data()), static_cast<std::streamsize>(header.BufferByteSize));
        return in.good();
    }

    bool IAssetImporter::ReadEnvironmentMapFileHeader(const char* zenvmap_file, EnvironmentMapFileHeader& out_header)
    {
        std::ifstream in(zenvmap_file, std::ios::binary);
        if (!in.is_open())
        {
            return false;
        }

        in.read(reinterpret_cast<char*>(&out_header), sizeof(EnvironmentMapFileHeader));
        return in.good() && (out_header.MagicNumber == ZENVMAP_MAGIC);
    }

} // namespace ZEngine::Importers
