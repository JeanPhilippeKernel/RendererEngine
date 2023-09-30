#include <pch.h>
#include <Rendering/Renderers/SceneRenderer.h>
#include <Rendering/Renderers/GraphicRenderer.h>
#include <Rendering/Specifications/GraphicRendererPipelineSpecification.h>

using namespace ZEngine::Rendering::Specifications;

namespace ZEngine::Rendering::Renderers
{
    SceneRenderer::SceneRenderer(Ref<Swapchain> swapchain) : m_swapchain_ptr(swapchain)
    {
    }

    void SceneRenderer::Initialize()
    {
        if (auto swapchain = m_swapchain_ptr.lock())
        {
            m_command_pool = Hardwares::VulkanDevice::CreateCommandPool(QueueType::GRAPHIC_QUEUE, swapchain->GetIdentifier(), true);

            auto frame_count         = swapchain->GetImageCount();
            auto current_frame_index = swapchain->GetCurrentFrameIndex();

            m_UBOCamera_colletion.resize(frame_count, CreateRef<Buffers::UniformBuffer>());
            m_SBVertex_colletion.resize(frame_count, CreateRef<Buffers::StorageBuffer>());
            m_SBIndex_colletion.resize(frame_count, CreateRef<Buffers::StorageBuffer>());
            m_SBTransform_colletion.resize(frame_count, CreateRef<Buffers::StorageBuffer>());
            m_SBDrawData_colletion.resize(frame_count, CreateRef<Buffers::StorageBuffer>());
            m_draw_indirect_command_collection.resize(frame_count);
            {
                Specifications::GraphicRendererPipelineSpecification pipeline_spec = {};
                pipeline_spec.DebugName                                            = "Standard-Pipeline";
                pipeline_spec.TargetFrameBufferSpecification                       = GraphicRenderer::GetRenderTarget(RenderTarget::FRAME_OUTPUT);
                /*
                 * Todo : Should be moved to Shader specification...
                 */
                pipeline_spec.VertexShaderFilename   = "Shaders/Cache/final_color_vertex.spv";
                pipeline_spec.FragmentShaderFilename = "Shaders/Cache/final_color_fragment.spv";
                pipeline_spec.LayoutBindingMap[0]    = {.Binding = 0, .Count = 1, .DescriptorType = DescriptorType::UNIFORM_BUFFER, .Flags = ShaderStageFlags::VERTEX};
                pipeline_spec.LayoutBindingMap[1]    = {.Binding = 1, .Count = 1, .DescriptorType = DescriptorType::STORAGE_BUFFER, .Flags = ShaderStageFlags::VERTEX};
                pipeline_spec.LayoutBindingMap[2]    = {.Binding = 2, .Count = 1, .DescriptorType = DescriptorType::STORAGE_BUFFER, .Flags = ShaderStageFlags::VERTEX};
                pipeline_spec.LayoutBindingMap[3]    = {.Binding = 3, .Count = 1, .DescriptorType = DescriptorType::STORAGE_BUFFER, .Flags = ShaderStageFlags::VERTEX};
                pipeline_spec.LayoutBindingMap[5]    = {.Binding = 5, .Count = 1, .DescriptorType = DescriptorType::STORAGE_BUFFER, .Flags = ShaderStageFlags::VERTEX};

                RenderPasses::RenderPassSpecification color_pass = {};
                color_pass.DebugName                             = "Final-Color-Attachment";
                color_pass.Pipeline                              = Pipelines::GraphicPipeline::Create(pipeline_spec);
                m_final_color_output_pass                        = RenderPasses::RenderPass::Create(color_pass);

                m_final_color_output_pass->SetInput("UBO_Camera", m_UBOCamera_colletion[current_frame_index]);
                m_final_color_output_pass->SetInput("SB_Vertex", m_SBVertex_colletion[current_frame_index]);
                m_final_color_output_pass->SetInput("SB_Index", m_SBIndex_colletion[current_frame_index]);
                m_final_color_output_pass->SetInput("SB_DrawData", m_SBDrawData_colletion[current_frame_index]);
                m_final_color_output_pass->SetInput("SB_Transform", m_SBTransform_colletion[current_frame_index]);
                m_final_color_output_pass->Bake();
            }
        }
    }

    void SceneRenderer::Deinitialize()
    {
        m_final_color_output_pass->Dispose();

        for (auto& buffer : m_UBOCamera_colletion)
        {
            buffer->Dispose();
        }

        for (auto& buffer : m_SBVertex_colletion)
        {
            buffer->Dispose();
        }

        for (auto& buffer : m_SBIndex_colletion)
        {
            buffer->Dispose();
        }

        for (auto& buffer : m_SBDrawData_colletion)
        {
            buffer->Dispose();
        }

        for (auto& buffer : m_SBTransform_colletion)
        {
            buffer->Dispose();
        }

        Hardwares::VulkanDevice::DisposeCommandPool(m_command_pool);
    }

    void SceneRenderer::StartScene(const glm::vec4& camera_position, const glm::mat4& camera_view, const glm::mat4& camera_projection)
    {
        m_camera_position   = camera_position;
        m_camera_view       = camera_view;
        m_camera_projection = camera_projection;
    }

    void SceneRenderer::StartScene(const glm::vec3& camera_position, const glm::mat4& camera_view, const glm::mat4& camera_projection)
    {
        this->StartScene(glm::vec4(camera_position, 1.0f), camera_view, camera_projection);
    }

    void SceneRenderer::RenderScene(const Ref<Rendering::Scenes::SceneRawData>& scene_data)
    {
        if (auto swapchain = m_swapchain_ptr.lock())
        {
            auto current_frame_index = swapchain->GetCurrentFrameIndex();

            std::vector<glm::mat4> tranform_collection  = {};
            std::vector<DrawData>  draw_data_collection = {};

            int         drawDataIndex    = 0;
            const auto& sceneNodeMeshMap = scene_data->SceneNodeMeshMap;
            for (const auto& sceneNodeMeshPair : sceneNodeMeshMap)
            {
                /*
                 * Composing DrawData
                 */
                DrawData& draw_data = draw_data_collection.emplace_back(DrawData{.Index = drawDataIndex++});
                tranform_collection.emplace_back(scene_data->GlobalTransformCollection[sceneNodeMeshPair.first]);
                draw_data.TransformIndex = tranform_collection.size() - 1;
                draw_data.VertexOffset   = sceneNodeMeshPair.second.VertexOffset;
                draw_data.IndexOffset    = sceneNodeMeshPair.second.IndexOffset;
                draw_data.VertexCount    = sceneNodeMeshPair.second.VertexCount;
            }
            /*
             * Uploading Geometry data
             */
            m_SBVertex_colletion[current_frame_index]->SetData(scene_data->Vertices);
            m_SBIndex_colletion[current_frame_index]->SetData(scene_data->Indices);
            m_SBTransform_colletion[current_frame_index]->SetData(tranform_collection);
            /*
             * Uploading Drawing data
             */
            m_SBDrawData_colletion[current_frame_index]->SetData(draw_data_collection);

            // Todo: Can multithreaded
            auto ubo_camera_data = Contracts::UBOCameraLayout{.position = m_camera_position, .View = m_camera_view, .Projection = m_camera_projection};
            m_UBOCamera_colletion[current_frame_index]->SetData(&ubo_camera_data, sizeof(Contracts::UBOCameraLayout));
            /*
             * Uploading Indirect Commands
             */
            auto& draw_indirect_commmand = m_draw_indirect_command_collection[current_frame_index];
            if (!draw_indirect_commmand.empty())
            {
                draw_indirect_commmand.clear();
                draw_indirect_commmand.shrink_to_fit();
            }
            draw_indirect_commmand = {};
            draw_indirect_commmand.resize(draw_data_collection.size());

            for (uint32_t i = 0; i < draw_indirect_commmand.size(); ++i)
            {
                draw_indirect_commmand[i] = {.vertexCount = draw_data_collection[i].VertexCount, .instanceCount = 1, .firstVertex = 0, .firstInstance = i};
            }
        }
    }

    void SceneRenderer::EndScene()
    {
        if (auto swapchain = m_swapchain_ptr.lock())
        {
            auto current_frame_index = swapchain->GetCurrentFrameIndex();

            auto command_buffer = m_command_pool->GetCurrentCommmandBuffer();

            command_buffer->Begin();
            {
                GraphicRenderer::BeginRenderPass(command_buffer, m_final_color_output_pass);
                GraphicRenderer::RenderGeometry(command_buffer, m_draw_indirect_command_collection[current_frame_index]);
                GraphicRenderer::EndRenderPass(command_buffer);
            }
            command_buffer->End();
            command_buffer->Submit();

        }
    }

    void SceneRenderer::Tick()
    {
        m_command_pool->Tick();
    }
} // namespace ZEngine::Rendering::Renderers
