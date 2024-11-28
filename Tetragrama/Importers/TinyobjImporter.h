#pragma once

#include <IAssetImporter.h>
#include <tinyobjloader/tiny_obj_loader.h>

using namespace ZEngine::Helpers;
using namespace ZEngine::Rendering::Meshes;
using namespace ZEngine::Rendering::Scenes;

namespace Tetragrama::Importers
{
    class TInyobjImporter : public IAssetImporter
    {
    public:
        TInyobjImporter();
        virtual ~TInyobjImporter();
        std::future<void> ImportAsync(std::string_view filename, ImportConfiguration config = {}) override;

        // To aid in Vertex deduplication
        struct VertexData
        {
            float px, py, pz; // Position
            float nx, ny, nz; // Normal
            float u, v;       // Texture

            bool operator==(const VertexData& other) const noexcept
            {
                return px == other.px && py == other.py && pz == other.pz && nx == other.nx && ny == other.ny && nz == other.nz && u == other.u && v == other.v;
            }
        };

    private:
        void ExtractMeshes(const tinyobj::ObjReader& reader, ImporterData& importer_data);
        void ExtractMaterials(const tinyobj::ObjReader& reader, ImporterData& importer_data);
        void ExtractTextures(const tinyobj::ObjReader& reader, ImporterData& importer_data);
        void CreateHierarchyScene(const tinyobj::ObjReader& reader, ImporterData& importer_data);
        int  GenerateFileIndex(std::vector<std::string>& data, std::string_view filename);
    };
} // namespace Tetragrama::Importers

namespace std
{
    template <>
    struct hash<Tetragrama::Importers::TInyobjImporter::VertexData>
    {
        size_t operator()(const Tetragrama::Importers::TInyobjImporter::VertexData& v) const noexcept
        {
            size_t seed         = 0;
            auto   hash_combine = [&seed](size_t hash) {
                seed ^= hash + 0x9e3779b9 + (seed << 6) + (seed >> 2);
            };

            hash_combine(std::hash<float>{}(v.px));
            hash_combine(std::hash<float>{}(v.py));
            hash_combine(std::hash<float>{}(v.pz));
            hash_combine(std::hash<float>{}(v.nx));
            hash_combine(std::hash<float>{}(v.ny));
            hash_combine(std::hash<float>{}(v.nz));
            hash_combine(std::hash<float>{}(v.u));
            hash_combine(std::hash<float>{}(v.v));

            return seed;
        }
    };
} // namespace std
