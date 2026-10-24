#pragma once
#include <ZEngine/Core/Memory/Allocator.h>
#include <ZEngine/Rendering/Renderers/Base/RenderPass.h>
#include <ZEngine/Rendering/Renderers/RenderGraph.h>
#include <ZEngine/Rendering/Scenes/RenderScene.h>

namespace ZEngine::Rendering::Renderers
{
    struct GridPushConstantData
    {
        Core::Containers::FixedArray<float, 4>    OriginAndLogSpacing = {};
        Core::Containers::FixedArray<float, 4>    Orientation         = {};
        Core::Containers::FixedArray<float, 4>    ColorThin           = {};
        Core::Containers::FixedArray<float, 4>    ColorThick          = {};
        Core::Containers::FixedArray<float, 4>    ColorUAxis          = {};
        Core::Containers::FixedArray<float, 4>    ColorVAxis          = {};
        /// log2(fade radius), fade exponent, line width, maximum LOD.
        Core::Containers::FixedArray<float, 4>    Style               = {};
        /// Major cadence, reverse-Z flag, label policy, reserved zero.
        Core::Containers::FixedArray<uint32_t, 4> Controls            = {};
    };
    static_assert(sizeof(GridPushConstantData) == 128, "Grid constants must match GLSL and Vulkan's minimum push-constant budget");
    static_assert(offsetof(GridPushConstantData, Orientation) == 16 && offsetof(GridPushConstantData, Style) == 96 && offsetof(GridPushConstantData, Controls) == 112);

    struct GridPass final : public IRenderGraphCallbackPass
    {
        GridPass();
        /// Invalid/disabled snapshots retire the pass; they never draw the last valid plane.
        void                                 SetConfig(const Scenes::SceneGridRenderSnapshot& config);
        void                                 SetCameraDepthConvention(bool reverse_z);
        /// Pure, bounded packing. Invalid input preserves the entire output.
        [[nodiscard]] static bool            Pack(const Scenes::SceneGridRenderSnapshot& config, bool reverse_z, GridPushConstantData& output);
        Specifications::GraphicsPipelineDesc BuildGraphicsPipelineDescription(Core::Memory::ArenaAllocator* arena) const override;
        bool                                 Register(Hardwares::VulkanDevicePtr device, cstring name, const RenderGraphFrameContext& context, RenderGraphResourceBuilderPtr builder, RenderGraphResourceInspectorPtr inspector) override;
        void                                 Prepare(Hardwares::VulkanDevicePtr device, Scenes::SceneDataPtr scene, RenderGraphResourceInspectorPtr inspector, RenderPasses::RenderPass* pass) override;
        void                                 Execute(Hardwares::VulkanDevicePtr device, RenderGraphResourceInspectorPtr inspector, Scenes::SceneDataPtr scene, RenderPasses::RenderPass* pass, Buffers::FramebufferVNext* framebuffer, Hardwares::CommandBufferPtr command_buffer) override;
        bool                                 RecordDraw(Hardwares::VulkanDevicePtr device, RenderGraphResourceInspectorPtr inspector, Scenes::SceneDataPtr scene, RenderPasses::RenderPass* pass, Buffers::FramebufferVNext* framebuffer, Hardwares::CommandBufferPtr command_buffer) override;
        bool                                 SupportsSecondaryRecording() const override
        {
            return true;
        }

    private:
        GridPushConstantData m_push      = {};
        bool                 m_enabled   = false;
        bool                 m_reverse_z = false;
    };
} // namespace ZEngine::Rendering::Renderers
