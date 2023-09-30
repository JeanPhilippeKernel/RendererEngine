#pragma once
#include <vector>
#include <glm/glm.hpp>
#include <ZEngineDef.h>
#include <Rendering/Scenes/GraphicScene.h>
#include <Rendering/Cameras/Camera.h>
#include <Rendering/Renderers/RenderPasses/RenderPass.h>
#include <Rendering/Swapchain.h>

namespace ZEngine::Rendering::Renderers
{
    struct SceneRenderer
    {
        SceneRenderer(Ref<Swapchain> swapchain);
        ~SceneRenderer() = default;

        void Initialize();
        void Deinitialize();

        void StartScene(const glm::vec3& camera_position, const glm::mat4& camera_view, const glm::mat4& camera_projection);
        void StartScene(const glm::vec4& camera_position, const glm::mat4& camera_view, const glm::mat4& camera_projection);
        void RenderScene(const Ref<Rendering::Scenes::SceneRawData>& scene_data);
        void EndScene();

        void Tick();

    private:
        WeakRef<Swapchain>  m_swapchain_ptr;
        Pools::CommandPool* m_command_pool{nullptr};
        glm::vec4           m_camera_position{1.0f};
        glm::mat4           m_camera_view{1.0f};
        glm::mat4           m_camera_projection{1.0f};
        /*
         * Data Per Frame
         */
        std::vector<Ref<Buffers::UniformBuffer>>        m_UBOCamera_colletion;
        std::vector<Ref<Buffers::StorageBuffer>>        m_SBVertex_colletion;
        std::vector<Ref<Buffers::StorageBuffer>>        m_SBIndex_colletion;
        std::vector<Ref<Buffers::StorageBuffer>>        m_SBDrawData_colletion;
        std::vector<Ref<Buffers::StorageBuffer>>        m_SBTransform_colletion;
        std::vector<std::vector<VkDrawIndirectCommand>> m_draw_indirect_command_collection;
        Ref<RenderPasses::RenderPass>                   m_final_color_output_pass;
    };
} // namespace ZEngine::Rendering::Renderers