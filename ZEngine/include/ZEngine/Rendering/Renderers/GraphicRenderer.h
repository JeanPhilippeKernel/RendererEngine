#pragma once
#include <queue>
#include <Rendering/Renderers/Storages/GraphicRendererStorage.h>
#include <Rendering/Buffers/FrameBuffers/Framebuffer.h>
#include <Core/IInitializable.h>
#include <Rendering/Meshes/Mesh.h>

namespace ZEngine::Rendering::Renderers {

    class GraphicRenderer : public Core::IInitializable {
    public:
        GraphicRenderer();
        virtual ~GraphicRenderer() = default;

        virtual void AddMesh(Meshes::Mesh& mesh) = 0;

        virtual void AddMesh(Ref<Meshes::Mesh>& mesh);
        virtual void AddMesh(std::vector<Meshes::Mesh>& meshes);
        virtual void AddMesh(std::vector<Ref<Meshes::Mesh>>& meshes);

    public:
        const Ref<Buffers::FrameBuffer>& GetFrameBuffer() const;
        virtual void                     StartScene(const Maths::Matrix4& m_view_projection_matrix);
        virtual void                     EndScene() = 0;

    protected:
        void Submit(const Ref<Storages::GraphicRendererStorage<float, unsigned int>>& graphic_storage);

    protected:
        Maths::Matrix4                                                         m_view_projection_matrix;
        Ref<Buffers::FrameBuffer>                                              m_framebuffer;
        Storages::GraphicRendererStorageType                                   m_storage_type{Storages::GraphicRendererStorageType::GRAPHIC_STORAGE_TYPE_UNDEFINED};
        std::queue<Ref<Storages::GraphicRendererStorage<float, unsigned int>>> m_graphic_storage_list;
    };
} // namespace ZEngine::Rendering::Renderers
