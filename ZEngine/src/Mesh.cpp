#include <pch.h>
#include <Rendering/Meshes/Mesh.h>

namespace ZEngine::Rendering::Meshes
{
    Mesh::Mesh(const Ref<Geometries::IGeometry>& geometry, const std::vector<Ref<Materials::ShaderMaterial>>& materials) : m_geometry(geometry), m_material_collection(materials) {}

    Mesh::Mesh(Ref<Geometries::IGeometry>&& geometry, std::vector<Ref<Materials::ShaderMaterial>>&& materials)
        : m_geometry(std::move(geometry)), m_material_collection(std::move(materials))
    {
    }

    Ref<Geometries::IGeometry> Mesh::GetGeometry() const
    {
        return m_geometry;
    }

    const std::vector<Ref<Materials::ShaderMaterial>>& Mesh::GetMaterials() const
    {
        return m_material_collection;
    }

    void Mesh::SetGeometry(Ref<Geometries::IGeometry>&& geometry)
    {
        m_geometry = std::move(geometry);
    }

    void Mesh::SetGeometry(const Ref<Geometries::IGeometry>& geometry)
    {
        m_geometry = geometry;
    }

    MeshVNext::MeshVNext(const std::vector<float>& vertices, const std::vector<uint32_t>& indices, uint32_t vertex_count)
        : VertexCount(vertex_count), m_vertex_buffer{}, m_index_buffer{}
    {
        Vertices = vertices;
        Indices  = indices;
    }

    MeshVNext::MeshVNext(std::vector<float>&& vertices, std::vector<uint32_t>&& indices, uint32_t vertex_count)
        : VertexCount(vertex_count), m_vertex_buffer{}, m_index_buffer{}
    {
        Vertices = std::move(vertices);
        Indices  = std::move(indices);
    }

    MeshVNext::~MeshVNext()
    {
    }

    void MeshVNext::Draw(VkCommandBuffer command_buffer)
    {
        m_vertex_buffer.SetData(Vertices);
        m_index_buffer.SetData(Indices);

        VkDeviceSize offsets[]        = {0};
        VkBuffer     vertex_buffers[] = {m_vertex_buffer.GetNativeBufferHandle()};

        vkCmdBindVertexBuffers(command_buffer, 0, 1, vertex_buffers, offsets);
        vkCmdBindIndexBuffer(command_buffer, m_index_buffer.GetNativeBufferHandle(), 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(command_buffer, Indices.size(), 1, 0, 0, 0);
    }

    void MeshVNext::Flush()
    {
        m_vertex_buffer.Dispose();
        m_index_buffer.Dispose();
    }
} // namespace ZEngine::Rendering::Meshes
