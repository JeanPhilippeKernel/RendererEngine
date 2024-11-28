#define TINYOBJLOADER_IMPLEMENTATION

#include <pch.h>
#include <AssimpImporter.h>
#include <Core/Coroutine.h>
#include <Helpers/ThreadPool.h>
#include <TInyobjImporter.h>
#include <fmt/format.h>

namespace Tetragrama::Importers
{
    TInyobjImporter::TInyobjImporter() = default;

    TInyobjImporter::~TInyobjImporter() = default;

    std::future<void> TInyobjImporter::ImportAsync(std::string_view filename, ImportConfiguration config)
    {
        ThreadPoolHelper::Submit([this, path = std::string(filename.data()), config] {
            {
                std::unique_lock l(m_mutex);
                m_is_importing = true;
            }

            tinyobj::ObjReader reader;

            if (!reader.ParseFromFile(path))
            {
                if (m_error_callback)
                {
                    m_error_callback(reader.Error());
                }
            }
            else
            {
                ImporterData import_data = {};
                ExtractMeshes(reader, import_data);
                ExtractMaterials(reader, import_data);
                ExtractTextures(reader, import_data);
                CreateHierarchyScene(reader, import_data);
                /*
                 * Serialization of ImporterData
                 */
                REPORT_LOG("Serializing model...")
                SerializeImporterData(import_data, config);

                if (m_complete_callback)
                {
                    m_complete_callback(std::move(import_data));
                }
            }
            {
                std::unique_lock l(m_mutex);
                m_is_importing = false;
            }
        });
        co_return;
    }

    void TInyobjImporter::ExtractMeshes(const tinyobj::ObjReader& reader, ImporterData& importer_data)
    {
        const auto& attributes = reader.GetAttrib();
        const auto& shapes     = reader.GetShapes();

        if (shapes.empty() || attributes.vertices.empty())
        {
            REPORT_LOG("No shapes or vertices found in the OBJ file.");
            return;
        }

        importer_data.Scene.Meshes.reserve(shapes.size());

        for (const auto& shape : shapes)
        {
            std::unordered_map<VertexData, uint32_t> unique_vertices;
            const size_t                             face_count = shape.mesh.indices.size() / 3;

            size_t vertex_start = importer_data.Scene.Vertices.size() / 8;
            size_t index_start  = importer_data.Scene.Indices.size();

            for (size_t f = 0; f < face_count; ++f)
            {
                for (size_t v = 0; v < 3; ++v)
                {
                    const auto& index = shape.mesh.indices[f * 3 + v];
                    VertexData  vertex{};

                    // Position
                    vertex.px = attributes.vertices[3 * index.vertex_index];
                    vertex.py = attributes.vertices[3 * index.vertex_index + 1];
                    vertex.pz = attributes.vertices[3 * index.vertex_index + 2];

                    // Normal
                    vertex.nx = attributes.normals[3 * index.normal_index];
                    vertex.ny = attributes.normals[3 * index.normal_index + 1];
                    vertex.nz = attributes.normals[3 * index.normal_index + 2];

                    // UV
                    vertex.u = attributes.texcoords[2 * index.texcoord_index];
                    vertex.v = attributes.texcoords[2 * index.texcoord_index + 1];

                    auto     it = unique_vertices.find(vertex);
                    uint32_t vertex_index;

                    if (it != unique_vertices.end())
                    {
                        vertex_index = it->second;
                    }
                    else
                    {
                        vertex_index            = static_cast<uint32_t>(unique_vertices.size());
                        unique_vertices[vertex] = vertex_index;

                        importer_data.Scene.Vertices.insert(
                            importer_data.Scene.Vertices.end(), {vertex.px, vertex.py, vertex.pz, vertex.nx, vertex.ny, vertex.nz, vertex.u, vertex.v});
                    }

                    importer_data.Scene.Indices.push_back(importer_data.VertexOffset + vertex_index);
                }
            }

            // Create mesh entry
            MeshVNext& mesh           = importer_data.Scene.Meshes.emplace_back();
            mesh.VertexCount          = static_cast<uint32_t>(unique_vertices.size());
            mesh.VertexOffset         = importer_data.VertexOffset;
            mesh.VertexUnitStreamSize = sizeof(float) * (3 + 3 + 2);
            mesh.StreamOffset         = mesh.VertexUnitStreamSize * mesh.VertexOffset;
            mesh.IndexOffset          = importer_data.IndexOffset;
            mesh.IndexCount           = static_cast<uint32_t>(shape.mesh.indices.size());
            mesh.IndexUnitStreamSize  = sizeof(uint32_t);
            mesh.IndexStreamOffset    = mesh.IndexUnitStreamSize * mesh.IndexOffset;
            mesh.TotalByteSize        = (mesh.VertexCount * mesh.VertexUnitStreamSize) + (mesh.IndexCount * mesh.IndexUnitStreamSize);

            importer_data.VertexOffset += mesh.VertexCount;
            importer_data.IndexOffset += mesh.IndexCount;
        }
    }

    void TInyobjImporter::ExtractMaterials(const tinyobj::ObjReader& reader, ImporterData& importer_data)
    {
        static constexpr float OPAQUENESS_THRESHOLD = 0.05f;

        auto& materials = reader.GetMaterials();

        // Handle case where no materials exist
        if (materials.empty())
        {
            MeshMaterial& default_material = importer_data.Scene.Materials.emplace_back();
            default_material.AlbedoColor   = ZEngine::Rendering::gpuvec4{0.7f, 0.7f, 0.7f, 1.0f};
            default_material.AmbientColor  = ZEngine::Rendering::gpuvec4{0.2f, 0.2f, 0.2f, 1.0f};
            default_material.SpecularColor = ZEngine::Rendering::gpuvec4{0.5f, 0.5f, 0.5f, 1.0f};
            default_material.EmissiveColor = ZEngine::Rendering::gpuvec4{0.0f, 0.0f, 0.0f, 1.0f};
            default_material.Factors       = ZEngine::Rendering::gpuvec4{0.0f, 0.0f, 0.0f, 0.0f};
            importer_data.Scene.MaterialNames.push_back("<default material>");
            return;
        }

        const uint32_t number_of_materials = static_cast<uint32_t>(materials.size());
        importer_data.Scene.Materials.reserve(number_of_materials);
        importer_data.Scene.MaterialNames.reserve(number_of_materials);

        auto toVec4 = [](const float* rgb, float alpha = 1.0f) -> ZEngine::Rendering::gpuvec4 {
            return ZEngine::Rendering::gpuvec4{std::clamp(rgb[0], 0.0f, 1.0f), std::clamp(rgb[1], 0.0f, 1.0f), std::clamp(rgb[2], 0.0f, 1.0f), std::clamp(alpha, 0.0f, 1.0f)};
        };

        for (uint32_t mat_idx = 0; mat_idx < number_of_materials; ++mat_idx)
        {
            const auto&   obj_material = materials[mat_idx];
            MeshMaterial& material     = importer_data.Scene.Materials.emplace_back();

            importer_data.Scene.MaterialNames.push_back(obj_material.name.empty() ? fmt::format("<unnamed material {}>", mat_idx) : obj_material.name);

            material.AmbientColor  = toVec4(obj_material.ambient);
            material.AlbedoColor   = toVec4(obj_material.diffuse);
            material.SpecularColor = toVec4(obj_material.specular);
            material.EmissiveColor = toVec4(obj_material.emission);

            float opacity      = obj_material.dissolve;
            material.Factors.x = (opacity < 1.0f) ? std::clamp(1.0f - opacity, 0.0f, 1.0f) : 0.0f;

            float transmission = std::max({obj_material.transmittance[0], obj_material.transmittance[1], obj_material.transmittance[2]});
            if (transmission > 0.0f)
            {
                material.Factors.x = std::clamp(transmission, 0.0f, 1.0f);
                material.Factors.z = 0.5f;
            }

            material.Factors.y = std::clamp(obj_material.shininess / 1000.0f, 0.0f, 1.0f);
            material.Factors.w = static_cast<float>(obj_material.illum) / 10.0f;

            // Initialize texture indices
            material.AlbedoMap   = 0xFFFFFFFF;
            material.NormalMap   = 0xFFFFFFFF;
            material.SpecularMap = 0xFFFFFFFF;
            material.EmissiveMap = 0xFFFFFFFF;
            material.OpacityMap  = 0xFFFFFFFF;
        }
    }

    void TInyobjImporter::ExtractTextures(const tinyobj::ObjReader& reader, ImporterData& importer_data)
    {
        auto& materials = reader.GetMaterials();
        if (materials.empty())
        {
            REPORT_LOG("No materials found for texture extraction.");
            return;
        }

        const uint32_t number_of_materials = static_cast<uint32_t>(materials.size());
        REPORT_LOG(fmt::format("Processing textures for {} materials", number_of_materials).c_str());

        auto processTexturePath = [this, &importer_data](const std::string& texname, const std::string& type) -> uint32_t {
            if (texname.empty())
            {
                REPORT_LOG(fmt::format("No {} texture specified.", type));
                return 0xFFFFFFFF;
            }

            std::string normalized_path = texname;
            std::replace(normalized_path.begin(), normalized_path.end(), '\\', '/');

            return GenerateFileIndex(importer_data.Scene.Files, normalized_path);
        };

        for (uint32_t mat_idx = 0; mat_idx < number_of_materials; ++mat_idx)
        {
            const auto& obj_material = materials[mat_idx];
            auto&       material     = importer_data.Scene.Materials[mat_idx];

            material.AlbedoMap   = processTexturePath(obj_material.diffuse_texname, "albedo");
            material.SpecularMap = processTexturePath(obj_material.specular_texname, "specular");
            material.EmissiveMap = processTexturePath(obj_material.emissive_texname, "emissive");

            material.NormalMap = !obj_material.bump_texname.empty()           ? processTexturePath(obj_material.bump_texname, "bump")
                                 : !obj_material.normal_texname.empty()       ? processTexturePath(obj_material.normal_texname, "normal")
                                 : !obj_material.displacement_texname.empty() ? processTexturePath(obj_material.displacement_texname, "displacement as normal")
                                                                              : 0xFFFFFFFF;

            material.OpacityMap = processTexturePath(obj_material.alpha_texname, "opacity");
        }
    }

    int TInyobjImporter::GenerateFileIndex(std::vector<std::string>& data, std::string_view filename)
    {
        auto find = std::find(std::begin(data), std::end(data), filename);
        if (find != std::end(data))
        {
            return std::distance(std::begin(data), find);
        }

        data.push_back(filename.data());
        return (data.size() - 1);
    }

    void TInyobjImporter::CreateHierarchyScene(const tinyobj::ObjReader& reader, ImporterData& importer_data)
    {
        auto& shapes = reader.GetShapes();

        if (shapes.empty())
        {
            return;
        }

        // Create root node ??
        auto root_id                           = SceneRawData::AddNode(&importer_data.Scene, -1, 0);
        importer_data.Scene.NodeNames[root_id] = importer_data.Scene.Names.size();
        importer_data.Scene.Names.push_back("Root");
        importer_data.Scene.GlobalTransformCollection[root_id] = glm::mat4(1.0f);
        importer_data.Scene.LocalTransformCollection[root_id]  = glm::mat4(1.0f);

        // Create model node ??
        auto model_node_id                           = SceneRawData::AddNode(&importer_data.Scene, root_id, 1);
        importer_data.Scene.NodeNames[model_node_id] = importer_data.Scene.Names.size();
        importer_data.Scene.Names.push_back("model_Mesh1_Model");
        importer_data.Scene.GlobalTransformCollection[model_node_id] = glm::mat4(1.0f);
        importer_data.Scene.LocalTransformCollection[model_node_id]  = glm::mat4(1.0f);

        // Collect all unique materials across all shapes
        std::map<int32_t, std::vector<size_t>> material_to_shapes;

        for (size_t shape_idx = 0; shape_idx < shapes.size(); ++shape_idx)
        {
            const auto&       shape = shapes[shape_idx];
            std::set<int32_t> shape_materials;

            for (int32_t mat_id : shape.mesh.material_ids)
            {
                shape_materials.insert(mat_id);
            }

            for (int32_t mat_id : shape_materials)
            {
                material_to_shapes[mat_id].push_back(shape_idx);
            }
        }

        // Create a node for each material
        for (const auto& [material_id, shape_indices] : material_to_shapes)
        {
            auto material_node_id                           = SceneRawData::AddNode(&importer_data.Scene, model_node_id, 2);
            importer_data.Scene.NodeNames[material_node_id] = importer_data.Scene.Names.size();

            std::string material_name = material_id < static_cast<int32_t>(importer_data.Scene.MaterialNames.size()) ? importer_data.Scene.MaterialNames[material_id]
                                                                                                                     : fmt::format("material_{}", material_id);

            importer_data.Scene.Names.push_back(material_name);

            // Create child nodes for each shape using this material
            for (size_t shape_idx : shape_indices)
            {
                auto shape_node_id                           = SceneRawData::AddNode(&importer_data.Scene, material_node_id, 3);
                importer_data.Scene.NodeNames[shape_node_id] = importer_data.Scene.Names.size();
                importer_data.Scene.Names.push_back(fmt::format("{}_{}", material_name, shape_idx));

                importer_data.Scene.NodeMeshes[shape_node_id]    = static_cast<uint32_t>(shape_idx);
                importer_data.Scene.NodeMaterials[shape_node_id] = static_cast<uint32_t>(material_id);

                importer_data.Scene.GlobalTransformCollection[shape_node_id] = glm::mat4(1.0f);
                importer_data.Scene.LocalTransformCollection[shape_node_id]  = glm::mat4(1.0f);
            }
        }
    }
} // namespace Tetragrama::Importers