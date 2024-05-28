#include <pch.h>
#include <IAssetImporter.h>
#include <fmt/format.h>

namespace fs = std::filesystem;

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
            std::vector<std::string> source_file_fullnames = {};
            source_file_fullnames.reserve(importer_data.Scene.Files.size());
            std::transform(std::begin(importer_data.Scene.Files), std::end(importer_data.Scene.Files), std::back_inserter(source_file_fullnames), [&config](std::string_view file) {
                return fmt::format("{0}/{1}", config.InputBaseAssetFilePath, file);
            });

            std::vector<std::string> destination_file_fullnames = {};
            destination_file_fullnames.reserve(importer_data.Scene.Files.size());
            std::transform(
                std::begin(importer_data.Scene.Files), std::end(importer_data.Scene.Files), std::back_inserter(destination_file_fullnames), [&config](std::string_view file) {
                return fmt::format("{0}/{1}/{2}", config.OutputTextureFilesPath, config.AssetFilename, file);
            });

            std::string   fullname_path = fmt::format("{0}/{1}.zematerials", config.OutputMaterialsPath, config.AssetFilename);
            std::ofstream out(fullname_path, std::ios::binary | std::ios::trunc);

            if (out.is_open())
            {
                out.seekp(std::ios::beg);

                size_t material_total_count = importer_data.Scene.Materials.size();
                out.write(reinterpret_cast<const char*>(&material_total_count), sizeof(size_t));
                out.write(reinterpret_cast<const char*>(importer_data.Scene.Materials.data()), sizeof(ZEngine::Rendering::Meshes::MeshMaterial) * material_total_count);

                SerializeStringArrayData(out, destination_file_fullnames);
            }
            out.close();

            /*
             * Texture files processing
             *  (1) Ensuring Scene sub-dir is created
             *  (2) Copying files to destination
             */
            for (int i = 0; i < importer_data.Scene.Files.size(); ++i)
            {
                std::string_view source = source_file_fullnames[i];
                std::string_view dest   = destination_file_fullnames[i];

                {
                    std::error_code err;
                    auto            destination_base_dir = fs::path(dest).parent_path();
                    if (!fs::exists(destination_base_dir))
                    {
                        fs::create_directories(destination_base_dir, err);
                    }
                }

                std::ifstream in_file(source.data(), std::ios::binary);
                std::ofstream out_file(dest.data(), std::ios::binary);

                if (!in_file.is_open() || !out_file.is_open())
                {
                    in_file.close();
                    out_file.close();
                    continue;
                }

                out_file << in_file.rdbuf();

                in_file.close();
                out_file.close();
            }

            importer_data.SerializedMaterialsPath = fullname_path;
        }

        if (!config.OutputModelFilePath.empty())
        {
            std::string   fullname_path = fmt::format("{0}/{1}.zemodel", config.OutputModelFilePath, config.AssetFilename);
            std::ofstream out(fullname_path, std::ios::binary | std::ios::trunc);

            if (out.is_open())
            {
                out.seekp(std::ios::beg);

                size_t local_transform_count = importer_data.Scene.LocalTransformCollection.size();
                out.write(reinterpret_cast<const char*>(&local_transform_count), sizeof(size_t));
                out.write(reinterpret_cast<const char*>(importer_data.Scene.LocalTransformCollection.data()), sizeof(glm::mat4) * local_transform_count);

                size_t gobal_transform_count = importer_data.Scene.GlobalTransformCollection.size();
                out.write(reinterpret_cast<const char*>(&gobal_transform_count), sizeof(size_t));
                out.write(reinterpret_cast<const char*>(importer_data.Scene.GlobalTransformCollection.data()), sizeof(glm::mat4) * gobal_transform_count);

                size_t node_hierarchy_count = importer_data.Scene.NodeHierarchyCollection.size();
                out.write(reinterpret_cast<const char*>(&node_hierarchy_count), sizeof(size_t));
                out.write(
                    reinterpret_cast<const char*>(importer_data.Scene.NodeHierarchyCollection.data()),
                    sizeof(ZEngine::Rendering::Scenes::SceneNodeHierarchy) * node_hierarchy_count);

                SerializeStringArrayData(out, importer_data.Scene.Names);
                SerializeStringArrayData(out, importer_data.Scene.MaterialNames);
                SerializeMapData(out, importer_data.Scene.NodeNames);
                SerializeMapData(out, importer_data.Scene.NodeMeshes);
                SerializeMapData(out, importer_data.Scene.NodeMaterials);
            }

            out.close();

            importer_data.SerializedModelPath = fullname_path;
        }
    }

    void IAssetImporter::SerializeMapData(std::ostream& os, const std::unordered_map<uint32_t, uint32_t>& data)
    {
        std::vector<uint32_t> flat_data = {};
        flat_data.reserve(data.size() * 2);
        for (auto d : data)
        {
            flat_data.push_back(d.first);
            flat_data.push_back(d.second);
        }

        size_t data_count = flat_data.size();
        os.write(reinterpret_cast<const char*>(&data_count), sizeof(size_t));
        os.write(reinterpret_cast<const char*>(flat_data.data()), sizeof(uint32_t) * flat_data.size());
    }

    void IAssetImporter::SerializeStringArrayData(std::ostream& os, std::span<std::string> str_view)
    {
        size_t count = str_view.size();
        os.write(reinterpret_cast<const char*>(&count), sizeof(size_t));
        for (std::string_view str : str_view)
        {
            size_t f_count = str.size();
            os.write(reinterpret_cast<const char*>(&f_count), sizeof(size_t));
            os.write(str.data(), f_count + 1);
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

                DeserializeStringArrayData(in, deserialized_data.Scene.Files);
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
                deserialized_data.Scene.LocalTransformCollection.resize(local_transform_count);
                in.read(reinterpret_cast<char*>(deserialized_data.Scene.LocalTransformCollection.data()), sizeof(glm::mat4) * local_transform_count);

                size_t gobal_transform_count;
                in.read(reinterpret_cast<char*>(&gobal_transform_count), sizeof(size_t));
                deserialized_data.Scene.GlobalTransformCollection.resize(gobal_transform_count);
                in.read(reinterpret_cast<char*>(deserialized_data.Scene.GlobalTransformCollection.data()), sizeof(glm::mat4) * gobal_transform_count);

                size_t node_hierarchy_count;
                in.read(reinterpret_cast<char*>(&node_hierarchy_count), sizeof(size_t));
                deserialized_data.Scene.NodeHierarchyCollection.resize(node_hierarchy_count);
                in.read(
                    reinterpret_cast<char*>(deserialized_data.Scene.NodeHierarchyCollection.data()), sizeof(ZEngine::Rendering::Scenes::SceneNodeHierarchy) * node_hierarchy_count);

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

    void IAssetImporter::DeserializeMapData(std::istream& in, std::unordered_map<uint32_t, uint32_t>& data)
    {
        size_t data_count;
        in.read(reinterpret_cast<char*>(&data_count), sizeof(size_t));

        std::vector<uint32_t> flat_data = {};
        flat_data.resize(data_count);
        in.read(reinterpret_cast<char*>(flat_data.data()), sizeof(uint32_t) * data_count);

        for (int i = 0; i < data_count; i += 2)
        {
            data[flat_data[i]] = flat_data[i + 1];
        }
    }

    void IAssetImporter::DeserializeStringArrayData(std::istream& in, std::vector<std::string>& data)
    {
        size_t data_count;
        in.read(reinterpret_cast<char*>(&data_count), sizeof(size_t));

        for (int i = 0; i < data_count; ++i)
        {
            size_t v_count;
            in.read(reinterpret_cast<char*>(&v_count), sizeof(size_t));

            std::string v;
            v.resize(v_count);
            in.read(v.data(), v_count + 1);
            data.push_back(v);
        }
    }
}