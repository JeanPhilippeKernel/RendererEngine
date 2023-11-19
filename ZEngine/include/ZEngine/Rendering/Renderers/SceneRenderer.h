#pragma once
#include <vector>
#include <glm/glm.hpp>
#include <ZEngineDef.h>
#include <Rendering/Scenes/GraphicScene.h>
#include <Rendering/Cameras/Camera.h>
#include <Rendering/Renderers/RenderPasses/RenderPass.h>
#include <Rendering/Buffers/IndirectBuffer.h>

namespace ZEngine::Rendering::Renderers
{
    struct SceneRenderer
    {
        SceneRenderer() = default;
        ~SceneRenderer() = default;

        void Initialize();
        void Deinitialize();

        void StartScene(const glm::vec3& camera_position, const glm::mat4& camera_view, const glm::mat4& camera_projection);
        void StartScene(const glm::vec4& camera_position, const glm::mat4& camera_view, const glm::mat4& camera_projection);
        void RenderScene(const Ref<Rendering::Scenes::SceneRawData>& scene_data);
        void EndScene();

        void Tick();

    private:
        Pools::CommandPool* m_command_pool{nullptr};
        glm::vec4           m_camera_position{1.0f};
        glm::mat4           m_camera_view{1.0f};
        glm::mat4           m_camera_projection{1.0f};
        /*
         * Scene Data Per Frame
         */
        Ref<Buffers::UniformBufferSet>                  m_UB_Camera;
        Ref<Buffers::StorageBufferSet>                  m_SBVertex;
        Ref<Buffers::StorageBufferSet>                  m_SBIndex;
        Ref<Buffers::StorageBufferSet>                  m_SBDrawData;
        Ref<Buffers::StorageBufferSet>                  m_SBTransform;
        Ref<Buffers::StorageBufferSet>                  m_SBMaterialData;
        std::vector<Ref<Buffers::IndirectBuffer>>       m_indirect_buffer;
        std::vector<Ref<Rendering::Textures::Texture>>  m_global_texture_buffer_collection;
        /*
         * Passes
         */
        Ref<RenderPasses::RenderPass> m_final_color_output_pass;

    private:
        uint32_t              m_last_uploaded_buffer_image_count{0};
        std::vector<uint32_t> m_last_drawn_vertices_count;
        std::vector<uint32_t> m_last_drawn_index_count;
    };
} // namespace ZEngine::Rendering::Renderers