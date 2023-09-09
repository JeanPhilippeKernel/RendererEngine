#pragma once
#include <Rendering/Meshes/Mesh.h>

namespace ZEngine::Rendering::Components
{
    struct MeshComponent
    {
        MeshComponent(uint32_t mesh_id)
        {
            m_mesh_id = mesh_id;
        }

        uint32_t GetMeshID() const
        {
            return m_mesh_id;
        }

    private:
        uint32_t m_mesh_id{0xFFFFFFFF};
    };

} // namespace ZEngine::Rendering::Components
