#pragma once
#include <vector>
#include <ZEngineDef.h>
#include <Maths/Math.h>
#include <Rendering/Materials/ShaderMaterial.h>
#include <Rendering/Geometries/IGeometry.h>

#include <Rendering/Buffers/VertexBuffer.h>
#include <Rendering/Buffers/IndexBuffer.h>

namespace ZEngine::Rendering::Meshes
{

    enum class MeshType
    {
        CUSTOM = 0,
        CUBE   = 1,
        QUAD   = 2,
        SQUARE = 3
    };

    struct MeshVNext
    {
        MeshVNext(const std::vector<float>& vertices, const std::vector<uint32_t>& indices, uint32_t vertex_count);
        MeshVNext(std::vector<float>&& vertices, std::vector<uint32_t>&& indices, uint32_t vertex_count);
        ~MeshVNext();
        uint32_t              MaterialID{0xFFFFFFFF};
        MeshType              Type{MeshType::CUSTOM};
        uint32_t              VertexCount{0};
        Maths::Matrix4        LocalTransform{Maths::Matrix4(1.0f)};
        uint32_t              VertexOffset{sizeof(Rendering::Renderers::Storages::IVertex)};
        std::vector<float>    Vertices;
        std::vector<uint32_t> Indices;

        void Draw(VkCommandBuffer command_buffer);

        void Flush();

    private:
        Buffers::VertexBuffer<std::vector<float>>   m_vertex_buffer;
        Buffers::IndexBuffer<std::vector<uint32_t>> m_index_buffer;
    };

    /*Need to be deprecated*/
    class Mesh
    {
    public:
        explicit Mesh();
        explicit Mesh(const Ref<Geometries::IGeometry>& geometry, const std::vector<Ref<Materials::ShaderMaterial>>& materials);
        explicit Mesh(Ref<Geometries::IGeometry>&& geometry, std::vector<Ref<Materials::ShaderMaterial>>&& materials);

        virtual ~Mesh() = default;

        void SetGeometry(const Ref<Geometries::IGeometry>& geometry);
        void SetGeometry(Ref<Geometries::IGeometry>&& geometry);

        Ref<Geometries::IGeometry>                         GetGeometry() const;
        const std::vector<Ref<Materials::ShaderMaterial>>& GetMaterials() const;

    private:
        std::vector<Ref<Materials::ShaderMaterial>> m_material_collection;
        Ref<Geometries::IGeometry>                  m_geometry;
    };
} // namespace ZEngine::Rendering::Meshes
