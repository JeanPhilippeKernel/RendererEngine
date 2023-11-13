#pragma once
#include <vector>
#include <glm/glm.hpp>
#include <ZEngineDef.h>
#include <Maths/Math.h>
#include <Rendering/Materials/ShaderMaterial.h>
#include <Rendering/Geometries/IGeometry.h>

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

    struct gpuvec4
    {
        float x, y, z, w;

        gpuvec4() = default;
        explicit gpuvec4(float v) : x(v), y(v), z(v), w(v) {}
        gpuvec4(float a, float b, float c, float d) : x(a), y(b), z(c), w(d) {}
        explicit gpuvec4(const glm::vec4& v) : x(v.x), y(v.y), z(v.z), w(v.w) {}
    };

    struct MeshMaterial
    {
        gpuvec4   AmbientColor{1.0f};
        gpuvec4   EmissiveColor{0.0f};
        gpuvec4   AlbedoColor{1.0f};
        gpuvec4   DiffuseColor{1.0f};
        gpuvec4   RoughnessColor{1.0f};
        float     TransparencyFactor{1.0f};
        float     MetallicFactor{0.0f};
        float     AlphaTest{1.0f};
        uint64_t  EmissiveTextureMap{0xFFFFFFFF};
        uint64_t  AlbedoTextureMap{0xFFFFFFFF};
        uint64_t  NormalTextureMap{0xFFFFFFFF};
        uint64_t  OpacityTextureMap{0xFFFFFFFF};
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
