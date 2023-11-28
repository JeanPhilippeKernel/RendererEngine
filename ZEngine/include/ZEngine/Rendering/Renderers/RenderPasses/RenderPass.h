#pragma once
#include <vector>
#include <ZEngineDef.h>
#include <vulkan/vulkan.h>
#include <Rendering/Specifications/RenderPassSpecification.h>
#include <Rendering/Buffers/UniformBuffer.h>
#include <Rendering/Buffers/StorageBuffer.h>
#include <Rendering/Buffers/GraphicBuffer.h>
#include <Rendering/Buffers/Image2DBuffer.h>
#include <Rendering/Textures/Texture.h>

namespace ZEngine::Rendering::Renderers::RenderPasses
{
    enum PassInputType
    {
        UNIFORM_BUFFER_SET,
        STORAGE_BUFFER_SET,
        TEXTURE_ARRAY,
        UNIFORM_BUFFER,
        STORAGE_BUFFER,
        TEXTURE
    };

    union InputData
    {
        void* Data{nullptr};
    };

    struct PassInput
    {
        uint32_t      Set{0};
        uint32_t      Binding{0};
        std::string   DebugName;
        PassInputType Type;
        InputData     Input;
    };

    struct RenderPass
    {
        RenderPass() = default;
        ~RenderPass();

        void Dispose();

        Ref<Pipelines::GraphicPipeline> GetPipeline() const;
        void                            Bake();
        bool                            Verify();
        void                            Update();
        void                            MarkDirty();
        void                            SetInput(std::string_view key_name, const Ref<Rendering::Buffers::UniformBufferSet>& buffer);
        void                            SetInput(std::string_view key_name, const Ref<Rendering::Buffers::StorageBufferSet>& buffer);
        void                            SetInput(std::string_view key_name, const Ref<Textures::TextureArray>& textures);
        void                            SetInput(std::string_view key_name, const Ref<Rendering::Buffers::UniformBuffer>& buffer);
        void                            SetInput(std::string_view key_name, const Ref<Rendering::Buffers::StorageBuffer>& buffer);
        void                            SetInput(std::string_view key_name, const Ref<Textures::Texture>& buffer);

        Ref<Rendering::Buffers::Image2DBuffer> GetOutputColor(uint32_t color_index);
        Ref<Rendering::Buffers::Image2DBuffer> GetOutputDepth();
        void                                   ResizeRenderTarget(uint32_t width, uint32_t height);

        static Ref<RenderPass> Create(const RenderPassSpecification& specification);

    private:
        std::pair<bool, Specifications::LayoutBindingSpecification> ValidateInput(std::string_view key);

    private:
        bool                            m_perform_update{false};
        std::vector<PassInput>          m_input_collection;
        Ref<Pipelines::GraphicPipeline> m_pipeline;
    };
} // namespace ZEngine::Rendering::Renderers::RenderPasses