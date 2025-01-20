#include <pch.h>
#include <Helpers/SerializerCommonHelper.h>
#include <IAssetImporter.h>
#include <ZEngine/Helpers/MemoryOperations.h>
#include <fmt/format.h>

namespace fs = std::filesystem;

using namespace Tetragrama::Helpers;
using namespace ZEngine::Helpers;

namespace Tetragrama::Importers
{
    void IAssetImporter::SerializeImporterData(ImporterData& importer_data, const ImportConfiguration& config)
    {
        importer_data.Name = config.AssetFilename;

        if (!config.OutputMeshFilePath.empty())
        {
            std::string   fullname_path = fmt::format("{0}/{1}.zemeshes", config.OutputMeshFilePath, config.AssetFilename);
            std::ofstream out(fullname_path, std::ios::binary | std::ios::trunc);

            if (out.is_open())
            {
                out.seekp(std::ios::beg);

                size_t mesh_count = importer_data.Scene.Meshes.size();
                out.write(reinterpret_cast<const char*>(&mesh_count), sizeof(size_t));
                out.write(reinterpret_cast<const char*>(importer_data.Scene.Meshes.data()), sizeof(ZEngine::Rendering::Meshes::MeshVNext) * mesh_count);

                size_t indices_count = importer_data.Scene.Indices.size();
                out.write(reinterpret_cast<const char*>(&indices_count), sizeof(size_t));
                out.write(reinterpret_cast<const char*>(importer_data.Scene.Indices.data()), sizeof(uint32_t) * indices_count);

                size_t vertice_count = importer_data.Scene.Vertices.size();
                out.write(reinterpret_cast<const char*>(&vertice_count), sizeof(size_t));
                out.write(reinterpret_cast<const char*>(importer_data.Scene.Vertices.data()), sizeof(float) * vertice_count);
            }
            out.close();

            importer_data.SerializedMeshesPath = fullname_path;
        }

        if (!config.OutputMaterialsPath.empty() && !config.OutputTextureFilesPath.empty())
        {
            /*
             * Normalize file naming
             */
            auto dst_dir            = fmt::format("{0}/{1}", config.OutputTextureFilesPath, config.AssetFilename);

            auto create_base_dir_fn = [](std::string_view filename) -> void {
                auto            base_dir = fs::absolute(filename).parent_path();

                std::error_code err      = {};
                if (!fs::exists(base_dir))
                {
                    fs::create_directories(base_dir, err);
                }
            };

            std::vector<std::string> src_tex_files = {};
            std::vector<std::string> dst_tex_files = {};
            for (auto& mat_file : importer_data.Scene.MaterialFiles)
            {
                if (!std::string_view(mat_file.AlbedoTexture).empty())
                {
                    auto src_file = fmt::format("{0}/{1}", config.InputBaseAssetFilePath, mat_file.AlbedoTexture);
                    auto dst_file = fmt::format("{0}/{1}", dst_dir, mat_file.AlbedoTexture);
                    create_base_dir_fn(dst_file);

                    ZEngine::Helpers::secure_strcpy(mat_file.AlbedoTexture, MAX_FILE_PATH_COUNT, dst_file.c_str());

                    src_tex_files.emplace_back(src_file);
                    dst_tex_files.emplace_back(dst_file);
                }

                if (!std::string_view(mat_file.EmissiveTexture).empty())
                {
                    auto src_file = fmt::format("{0}/{1}", config.InputBaseAssetFilePath, mat_file.EmissiveTexture);
                    auto dst_file = fmt::format("{0}/{1}", dst_dir, mat_file.EmissiveTexture);

                    create_base_dir_fn(dst_file);

                    ZEngine::Helpers::secure_strcpy(mat_file.EmissiveTexture, MAX_FILE_PATH_COUNT, dst_file.c_str());

                    src_tex_files.emplace_back(src_file);
                    dst_tex_files.emplace_back(dst_file);
                }

                if (!std::string_view(mat_file.NormalTexture).empty())
                {
                    auto src_file = fmt::format("{0}/{1}", config.InputBaseAssetFilePath, mat_file.NormalTexture);
                    auto dst_file = fmt::format("{0}/{1}", dst_dir, mat_file.NormalTexture);

                    create_base_dir_fn(dst_file);
                    ZEngine::Helpers::secure_strcpy(mat_file.NormalTexture, MAX_FILE_PATH_COUNT, dst_file.c_str());

                    src_tex_files.emplace_back(src_file);
                    dst_tex_files.emplace_back(dst_file);
                }

                if (!std::string_view(mat_file.OpacityTexture).empty())
                {
                    auto src_file = fmt::format("{0}/{1}", config.InputBaseAssetFilePath, mat_file.OpacityTexture);
                    auto dst_file = fmt::format("{0}/{1}", dst_dir, mat_file.OpacityTexture);

                    create_base_dir_fn(dst_file);

                    ZEngine::Helpers::secure_strcpy(mat_file.OpacityTexture, MAX_FILE_PATH_COUNT, dst_file.c_str());

                    src_tex_files.emplace_back(src_file);
                    dst_tex_files.emplace_back(dst_file);
                }

                if (!std::string_view(mat_file.SpecularTexture).empty())
                {
                    auto src_file = fmt::format("{0}/{1}", config.InputBaseAssetFilePath, mat_file.SpecularTexture);
                    auto dst_file = fmt::format("{0}/{1}", dst_dir, mat_file.SpecularTexture);

                    create_base_dir_fn(dst_file);

                    ZEngine::Helpers::secure_strcpy(mat_file.SpecularTexture, MAX_FILE_PATH_COUNT, dst_file.c_str());

                    src_tex_files.emplace_back(src_file);
                    dst_tex_files.emplace_back(dst_file);
                }
            }
            /*
             * Texture files processing
             *  (1) Ensuring Scene sub-dir is created
             *  (2) Copying files to destination
             */

            ZENGINE_VALIDATE_ASSERT(src_tex_files.size() == dst_tex_files.size(), "source files count can't be diff of destination files count")
            for (int i = 0; i < src_tex_files.size(); ++i)
            {
                auto          src = fs::absolute(src_tex_files[i]);
                auto          dst = fs::absolute(dst_tex_files[i]);

                std::ifstream in(src.c_str(), std::ios::binary);
                std::ofstream out(dst.c_str(), std::ios::binary);

                if (!in.is_open() || !out.is_open())
                {
                    in.close();
                    out.close();
                    continue;
                }

                out << in.rdbuf();

                in.close();
                out.close();
            }

            std::string   fullname_path = fmt::format("{0}/{1}.zematerials", config.OutputMaterialsPath, config.AssetFilename);
            std::ofstream out(fullname_path, std::ios::binary | std::ios::trunc);

            if (out.is_open())
            {
                out.seekp(std::ios::beg);

                size_t material_total_count = importer_data.Scene.Materials.size();
                out.write(reinterpret_cast<const char*>(&material_total_count), sizeof(size_t));
                out.write(reinterpret_cast<const char*>(importer_data.Scene.Materials.data()), sizeof(ZEngine::Rendering::Meshes::MeshMaterial) * material_total_count);

                size_t mat_file_count = importer_data.Scene.MaterialFiles.size();
                out.write(reinterpret_cast<const char*>(&mat_file_count), sizeof(size_t));
                for (auto& mat_file : importer_data.Scene.MaterialFiles)
                {
                    Tetragrama::Helpers::SerializeStringData(out, mat_file.AlbedoTexture);
                    Tetragrama::Helpers::SerializeStringData(out, mat_file.EmissiveTexture);
                    Tetragrama::Helpers::SerializeStringData(out, mat_file.NormalTexture);
                    Tetragrama::Helpers::SerializeStringData(out, mat_file.OpacityTexture);
                    Tetragrama::Helpers::SerializeStringData(out, mat_file.SpecularTexture);
                }
            }
            out.close();

            importer_data.SerializedMaterialsPath = fullname_path;
        }

        if (!config.OutputModelFilePath.empty())
        {
            std::string   fullname_path = fmt::format("{0}/{1}.zemodel", config.OutputModelFilePath, config.AssetFilename);
            std::ofstream out(fullname_path, std::ios::binary | std::ios::trunc);

            if (out.is_open())
            {
                out.seekp(std::ios::beg);

                size_t local_transform_count = importer_data.Scene.LocalTransforms.size();
                out.write(reinterpret_cast<const char*>(&local_transform_count), sizeof(size_t));
                out.write(reinterpret_cast<const char*>(importer_data.Scene.LocalTransforms.data()), sizeof(glm::mat4) * local_transform_count);

                size_t gobal_transform_count = importer_data.Scene.GlobalTransforms.size();
                out.write(reinterpret_cast<const char*>(&gobal_transform_count), sizeof(size_t));
                out.write(reinterpret_cast<const char*>(importer_data.Scene.GlobalTransforms.data()), sizeof(glm::mat4) * gobal_transform_count);

                size_t node_hierarchy_count = importer_data.Scene.NodeHierarchies.size();
                out.write(reinterpret_cast<const char*>(&node_hierarchy_count), sizeof(size_t));
                out.write(reinterpret_cast<const char*>(importer_data.Scene.NodeHierarchies.data()), sizeof(ZEngine::Rendering::Scenes::SceneNodeHierarchy) * node_hierarchy_count);

                Tetragrama::Helpers::SerializeStringArrayData(out, importer_data.Scene.Names);
                Tetragrama::Helpers::SerializeStringArrayData(out, importer_data.Scene.MaterialNames);
                Tetragrama::Helpers::SerializeMapData(out, importer_data.Scene.NodeNames);
                Tetragrama::Helpers::SerializeMapData(out, importer_data.Scene.NodeMeshes);
                Tetragrama::Helpers::SerializeMapData(out, importer_data.Scene.NodeMaterials);
            }

            out.close();

            importer_data.SerializedModelPath = fullname_path;
        }
    }

    ImporterData IAssetImporter::DeserializeImporterData(std::string_view model_path, std::string_view mesh_path, std::string_view material_path)
    {
        ImporterData deserialized_data = {};

        if (!mesh_path.empty())
        {
            std::ifstream in(mesh_path.data(), std::ios::binary);

            if (in.is_open())
            {
                in.seekg(0, std::ios::beg);

                size_t mesh_count;
                in.read(reinterpret_cast<char*>(&mesh_count), sizeof(size_t));
                deserialized_data.Scene.Meshes.resize(mesh_count);
                in.read(reinterpret_cast<char*>(deserialized_data.Scene.Meshes.data()), sizeof(ZEngine::Rendering::Meshes::MeshVNext) * mesh_count);

                size_t indices_count;
                in.read(reinterpret_cast<char*>(&indices_count), sizeof(size_t));
                deserialized_data.Scene.Indices.resize(indices_count);
                in.read(reinterpret_cast<char*>(deserialized_data.Scene.Indices.data()), sizeof(uint32_t) * indices_count);

                size_t vertice_count;
                in.read(reinterpret_cast<char*>(&vertice_count), sizeof(size_t));
                deserialized_data.Scene.Vertices.resize(vertice_count);
                in.read(reinterpret_cast<char*>(deserialized_data.Scene.Vertices.data()), sizeof(float) * vertice_count);
            }
            in.close();
        }

        if (!material_path.empty())
        {
            std::ifstream in(material_path.data(), std::ios::binary);

            if (in.is_open())
            {
                in.seekg(0, std::ios::beg);

                size_t material_total_count;
                in.read(reinterpret_cast<char*>(&material_total_count), sizeof(size_t));
                deserialized_data.Scene.Materials.resize(material_total_count);
                in.read(reinterpret_cast<char*>(deserialized_data.Scene.Materials.data()), sizeof(ZEngine::Rendering::Meshes::MeshMaterial) * material_total_count);

                size_t mat_file_count;
                in.read(reinterpret_cast<char*>(&mat_file_count), sizeof(size_t));
                deserialized_data.Scene.MaterialFiles.resize(mat_file_count);

                std::string textures[5] = {""};
                for (auto& mat_file : deserialized_data.Scene.MaterialFiles)
                {
                    Tetragrama::Helpers::DeserializeStringData(in, textures[0]);
                    Tetragrama::Helpers::DeserializeStringData(in, textures[1]);
                    Tetragrama::Helpers::DeserializeStringData(in, textures[2]);
                    Tetragrama::Helpers::DeserializeStringData(in, textures[3]);
                    Tetragrama::Helpers::DeserializeStringData(in, textures[4]);

                    ZEngine::Helpers::secure_strcpy(mat_file.AlbedoTexture, MAX_FILE_PATH_COUNT, textures[0].c_str());
                    ZEngine::Helpers::secure_strcpy(mat_file.EmissiveTexture, MAX_FILE_PATH_COUNT, textures[1].c_str());
                    ZEngine::Helpers::secure_strcpy(mat_file.NormalTexture, MAX_FILE_PATH_COUNT, textures[2].c_str());
                    ZEngine::Helpers::secure_strcpy(mat_file.OpacityTexture, MAX_FILE_PATH_COUNT, textures[3].c_str());
                    ZEngine::Helpers::secure_strcpy(mat_file.SpecularTexture, MAX_FILE_PATH_COUNT, textures[4].c_str());
                }
            }
            in.close();
        }

        if (!model_path.empty())
        {
            std::ifstream in(model_path.data(), std::ios::binary);
            if (in.is_open())
            {
                in.seekg(0, std::ios::beg);

                size_t local_transform_count;
                in.read(reinterpret_cast<char*>(&local_transform_count), sizeof(size_t));
                deserialized_data.Scene.LocalTransforms.resize(local_transform_count);
                in.read(reinterpret_cast<char*>(deserialized_data.Scene.LocalTransforms.data()), sizeof(glm::mat4) * local_transform_count);

                size_t gobal_transform_count;
                in.read(reinterpret_cast<char*>(&gobal_transform_count), sizeof(size_t));
                deserialized_data.Scene.GlobalTransforms.resize(gobal_transform_count);
                in.read(reinterpret_cast<char*>(deserialized_data.Scene.GlobalTransforms.data()), sizeof(glm::mat4) * gobal_transform_count);

                size_t node_hierarchy_count;
                in.read(reinterpret_cast<char*>(&node_hierarchy_count), sizeof(size_t));
                deserialized_data.Scene.NodeHierarchies.resize(node_hierarchy_count);
                in.read(reinterpret_cast<char*>(deserialized_data.Scene.NodeHierarchies.data()), sizeof(ZEngine::Rendering::Scenes::SceneNodeHierarchy) * node_hierarchy_count);

                DeserializeStringArrayData(in, deserialized_data.Scene.Names);
                DeserializeStringArrayData(in, deserialized_data.Scene.MaterialNames);
                DeserializeMapData(in, deserialized_data.Scene.NodeNames);
                DeserializeMapData(in, deserialized_data.Scene.NodeMeshes);
                DeserializeMapData(in, deserialized_data.Scene.NodeMaterials);
            }
            in.close();
        }

        return deserialized_data;
    }
} // namespace Tetragrama::Importers