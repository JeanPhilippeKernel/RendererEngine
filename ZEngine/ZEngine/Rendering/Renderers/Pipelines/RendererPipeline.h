#pragma once
#include <ZEngine/Hardwares/VulkanDevice.h>
#include <ZEngine/Rendering/Renderers/Base/Attachment.h>
#include <ZEngine/Rendering/Renderers/Pipelines/PSOCache.h>
#include <ZEngine/Rendering/Shaders/Shader.h>
#include <ZEngine/Rendering/Specifications/GraphicsPipelineDesc.h>
#include <ZEngine/ZEngineDef.h>
#include <vulkan/vulkan.h>

namespace ZEngine::Rendering::Renderers::Pipelines
{
    struct IPipeline
    {
        using DescriptorReplayCallback                      = bool (*)(void* context);
        using DescriptorSetResolver                         = VkDescriptorSet (*)(void* context, uint32_t set, uint32_t frame);

        virtual ~IPipeline()                                = default;

        Shaders::Shader*            Shader                  = nullptr;
        Hardwares::VulkanDevice*    Device                  = nullptr;
        VkPipeline                  Handle                  = VK_NULL_HANDLE;
        VkPipelineLayout            Layout                  = VK_NULL_HANDLE;
        uint32_t                    BakedShaderGeneration   = UINT32_MAX;
        DescriptorReplayCallback    ReplayDescriptors       = nullptr;
        DescriptorSetResolver       ResolveDescriptorSet    = nullptr;
        void*                       DescriptorReplayContext = nullptr;
        bool                        DescriptorBindingsValid = true;

        virtual VkPipelineBindPoint GetBindPoint() const    = 0;
        virtual void                Bake()
        {
            (void) BakeInternal(false);
        }
        virtual void Dispose() = 0;

        /// @brief Rebuilds this borrowed pipeline after a shader generation change.
        bool         EnsureCurrent()
        {
            if (Shader && (Handle == VK_NULL_HANDLE || BakedShaderGeneration != Shader->Generation))
            {
                Bake();
                DescriptorBindingsValid = Handle != VK_NULL_HANDLE && (!ReplayDescriptors || ReplayDescriptors(DescriptorReplayContext));
            }
            return Handle != VK_NULL_HANDLE && (!Shader || BakedShaderGeneration == Shader->Generation) && DescriptorBindingsValid;
        }

        /// @brief Prepares this exact variant without waiting for pipeline compilation.
        /// Pending/compile failure leaves the current pipeline borrow untouched. No callback
        /// captures this object; it may be disposed while the immutable cache job drains.
        PSOPreparationState PrepareAsync()
        {
            if (!Shader)
            {
                return PSOPreparationState::Failed;
            }
            if (!m_prepared_shader)
            {
                m_prepared_shader = Shader->RetainGeneration();
                if (!m_prepared_shader)
                {
                    return PSOPreparationState::Failed;
                }
            }
            if (m_prepared_shader.GetShader() != Shader || m_prepared_shader.GetGeneration() != Shader->Generation)
            {
                return PSOPreparationState::Failed;
            }
            const auto previous_handle     = Handle;
            const auto previous_layout     = Layout;
            const auto previous_generation = BakedShaderGeneration;
            const auto state               = BakeInternal(true);
            if (state != PSOPreparationState::Ready)
            {
                return state;
            }
            if (Handle != previous_handle || Layout != previous_layout || BakedShaderGeneration != previous_generation)
            {
                DescriptorBindingsValid = Handle != VK_NULL_HANDLE && (!ReplayDescriptors || ReplayDescriptors(DescriptorReplayContext));
            }
            return DescriptorBindingsValid ? PSOPreparationState::Ready : PSOPreparationState::Failed;
        }

        /// @brief Ends the preparation transaction without dropping the PSO borrow.
        /// Deferred shader reload remains queued for VulkanDevice's next safe point.
        void ReleasePreparationGeneration()
        {
            m_prepared_shader.Reset();
        }

    protected:
        virtual PSOPreparationState BakeInternal(bool asynchronous) = 0;

        /// @brief Pins the new exact version before releasing the previous borrow.
        void                        AdoptPreparedPipeline(VkPipeline handle, VkPipelineLayout layout);

    private:
        Shaders::ShaderGenerationLease m_prepared_shader = {};
    };

    struct GraphicPipeline : IPipeline
    {
        Specifications::GraphicsPipelineDesc Description = {};
        RenderPasses::Attachment*            Attachment  = nullptr;

        VkPipelineBindPoint                  GetBindPoint() const override
        {
            return VK_PIPELINE_BIND_POINT_GRAPHICS;
        }
        void Initialize(Hardwares::VulkanDevice* device, Specifications::GraphicsPipelineDesc&& desc, RenderPasses::Attachment* attachment);
        void InitializePrepared(Hardwares::VulkanDevice* device, Specifications::GraphicsPipelineDesc&& desc, RenderPasses::Attachment* attachment, Shaders::Shader* shader);
        void Dispose() override;

    private:
        PSOPreparationState BakeInternal(bool asynchronous) override;
    };

    struct ComputePipeline : IPipeline
    {
        /// @brief Exact byte count each dispatch records through vkCmdPushConstants.
        uint32_t            DeclaredPushConstantSize = 0;

        VkPipelineBindPoint GetBindPoint() const override
        {
            return VK_PIPELINE_BIND_POINT_COMPUTE;
        }
        void Initialize(Hardwares::VulkanDevice* device, cstring shader_name, uint32_t push_constant_size = 0);
        void InitializePrepared(Hardwares::VulkanDevice* device, Shaders::Shader* shader, uint32_t push_constant_size);
        void Dispose() override;

    private:
        PSOPreparationState BakeInternal(bool asynchronous) override;
        /// @brief Releases a stale borrowed PSO after an incompatible shader reload.
        void                InvalidateBakedState();
    };
} // namespace ZEngine::Rendering::Renderers::Pipelines
