#pragma once
#include <vector>
#include <ZEngineDef.h>
#include <Maths/Math.h>
#include <Rendering/Materials/ShaderMaterial.h>
#include <Rendering/Geometries/IGeometry.h>

#include <Rendering/Buffers/VertexBuffer.h>
#include <Rendering/Buffers/IndexBuffer.h>
#include <Rendering/Buffers/UniformBuffer.h>
#include <Rendering/Renderers/Contracts/RendererDataContract.h>

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
        MeshVNext()  = default;
        ~MeshVNext() = default;

        uint32_t VertexCount{0};
        uint32_t IndexCount{0};
        uint32_t VertexOffset{0};
        uint32_t IndexOffset{0};
        uint32_t StreamOffset{0};
        uint32_t IndexStreamOffset{0};
        uint32_t VertexUnitStreamSize{0};
        uint32_t IndexUnitStreamSize{0};
        uint32_t TotalByteSize{0};
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
