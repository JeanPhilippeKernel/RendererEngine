#pragma once
#include <ZEngine/Core/Containers/Array.h>
#include <ZEngine/Core/Memory/GpuAllocator.h>
#include <ZEngine/Rendering/Renderers/RenderGraph.h>
#include <ZEngine/Rendering/Renderers/RenderPasses/RenderPass.h>
#include <ZEngine/Rendering/Scenes/RenderScene.h>
#include <ZEngine/ZEngineDef.h>

#define WRITE_BUFFERS_ONCE(frame_index, body)          \
    if (!m_write_once_control.contains(frame_index))   \
    {                                                  \
        body m_write_once_control[frame_index] = true; \
    }

namespace ZEngine::Rendering::Renderers
{

    struct UploadPass : public IRenderGraphCallbackPass
    {
        Core::Containers::Array<float>    SkyboxVertexData = {};
        Core::Containers::Array<float>    GridVertexData   = {};
        Core::Containers::Array<uint16_t> SkyboxIndexData  = {};
        Core::Containers::Array<uint16_t> GridIndexData    = {};

        Core::Memory::BufferView          SkyboxVBHandle   = {};
        Core::Memory::BufferView          GridVBHandle     = {};
        Core::Memory::BufferView          SkyboxIBHandle   = {};
        Core::Memory::BufferView          GridIBHandle     = {};

        Core::Containers::Array<uint8_t>  WriteOnceControl = {};

        virtual void                      Setup(Hardwares::VulkanDevicePtr const device, cstring name, RenderGraphResourceBuilderPtr const res_builder, RenderGraphResourceInspectorPtr res_inspector) override;
        virtual void                      Compile(Hardwares::VulkanDevicePtr const device, Rendering::Scenes::SceneDataPtr const scene, RenderPasses::RenderPassBuilder* pass_builder, RenderGraphResourceInspectorPtr res_inspector, RenderPasses::RenderPass** const output_pass) override;
        virtual void                      Execute(Hardwares::VulkanDevicePtr const device, RenderGraphResourceInspectorPtr res_inspector, Rendering::Scenes::SceneDataPtr const scene, RenderPasses::RenderPass* const pass, Buffers::FramebufferVNext* const framebuffer, Hardwares::CommandBufferPtr const command_buffer) override;
    };

    struct BasePass : public IRenderGraphCallbackPass
    {
        virtual void Setup(Hardwares::VulkanDevicePtr const device, cstring name, RenderGraphResourceBuilderPtr const res_builder, RenderGraphResourceInspectorPtr res_inspector) override;
        virtual void Compile(Hardwares::VulkanDevicePtr const device, Rendering::Scenes::SceneDataPtr const scene, RenderPasses::RenderPassBuilder* pass_builder, RenderGraphResourceInspectorPtr res_inspector, RenderPasses::RenderPass** const output_pass) override;
        virtual void Execute(Hardwares::VulkanDevicePtr const device, RenderGraphResourceInspectorPtr res_inspector, Rendering::Scenes::SceneDataPtr const scene, RenderPasses::RenderPass* const pass, Buffers::FramebufferVNext* const framebuffer, Hardwares::CommandBufferPtr const command_buffer) override;
    };

    struct DepthPrePass : public IRenderGraphCallbackPass
    {
        virtual void Setup(Hardwares::VulkanDevicePtr const device, cstring name, RenderGraphResourceBuilderPtr const res_builder, RenderGraphResourceInspectorPtr res_inspector) override;
        virtual void Compile(Hardwares::VulkanDevicePtr const device, Rendering::Scenes::SceneDataPtr const scene, RenderPasses::RenderPassBuilder* pass_builder, RenderGraphResourceInspectorPtr res_inspector, RenderPasses::RenderPass** const output_pass) override;
        virtual void Execute(Hardwares::VulkanDevicePtr const device, RenderGraphResourceInspectorPtr res_inspector, Rendering::Scenes::SceneDataPtr const scene, RenderPasses::RenderPass* const pass, Buffers::FramebufferVNext* const framebuffer, Hardwares::CommandBufferPtr const command_buffer) override;
    };

    struct SkyboxPass : public IRenderGraphCallbackPass
    {
        // Set before Setup() is called. Empty string = no env map, pass is disabled.
        cstring      EnvMapPath = nullptr;

        virtual void Setup(Hardwares::VulkanDevicePtr const device, cstring name, RenderGraphResourceBuilderPtr const res_builder, RenderGraphResourceInspectorPtr res_inspector) override;
        virtual void Compile(Hardwares::VulkanDevicePtr const device, Rendering::Scenes::SceneDataPtr const scene, RenderPasses::RenderPassBuilder* pass_builder, RenderGraphResourceInspectorPtr res_inspector, RenderPasses::RenderPass** const output_pass) override;
        virtual void Execute(Hardwares::VulkanDevicePtr const device, RenderGraphResourceInspectorPtr res_inspector, Rendering::Scenes::SceneDataPtr const scene, RenderPasses::RenderPass* const pass, Buffers::FramebufferVNext* const framebuffer, Hardwares::CommandBufferPtr const command_buffer) override;

    private:
        Textures::TextureHandle m_env_map = {};
    };

    struct GridPushConstantData
    {
        float ColorThin[4]  = {0.6f, 0.6f, 0.6f, 1.0f};
        float ColorThick[4] = {0.3f, 0.3f, 0.3f, 1.0f};
        float ColorXAxis[4] = {0.9f, 0.2f, 0.2f, 1.0f};
        float ColorZAxis[4] = {0.2f, 0.4f, 1.0f, 1.0f};
        float CellSize      = 0.025f;
        float FadeStrength  = 0.5f;
    };

    struct GridPass : public IRenderGraphCallbackPass
    {
        GridPushConstantData PushData = {};

        virtual void         Setup(Hardwares::VulkanDevicePtr const device, cstring name, RenderGraphResourceBuilderPtr const res_builder, RenderGraphResourceInspectorPtr res_inspector) override;
        virtual void         Compile(Hardwares::VulkanDevicePtr const device, Rendering::Scenes::SceneDataPtr const scene, RenderPasses::RenderPassBuilder* pass_builder, RenderGraphResourceInspectorPtr res_inspector, RenderPasses::RenderPass** const output_pass) override;
        virtual void         Execute(Hardwares::VulkanDevicePtr const device, RenderGraphResourceInspectorPtr res_inspector, Rendering::Scenes::SceneDataPtr const scene, RenderPasses::RenderPass* const pass, Buffers::FramebufferVNext* const framebuffer, Hardwares::CommandBufferPtr const command_buffer) override;
    };

    struct GbufferPass : public IRenderGraphCallbackPass
    {
        virtual void Setup(Hardwares::VulkanDevicePtr const device, cstring name, RenderGraphResourceBuilderPtr const res_builder, RenderGraphResourceInspectorPtr res_inspector) override;
        virtual void Compile(Hardwares::VulkanDevicePtr const device, Rendering::Scenes::SceneDataPtr const scene, RenderPasses::RenderPassBuilder* pass_builder, RenderGraphResourceInspectorPtr res_inspector, RenderPasses::RenderPass** const output_pass) override;
        virtual void Execute(Hardwares::VulkanDevicePtr const device, RenderGraphResourceInspectorPtr res_inspector, Rendering::Scenes::SceneDataPtr const scene, RenderPasses::RenderPass* const pass, Buffers::FramebufferVNext* const framebuffer, Hardwares::CommandBufferPtr const command_buffer) override;
    };

    struct LightingPass : public IRenderGraphCallbackPass
    {
        virtual void Setup(Hardwares::VulkanDevicePtr const device, cstring name, RenderGraphResourceBuilderPtr const res_builder, RenderGraphResourceInspectorPtr res_inspector) override;
        virtual void Compile(Hardwares::VulkanDevicePtr const device, Rendering::Scenes::SceneDataPtr const scene, RenderPasses::RenderPassBuilder* pass_builder, RenderGraphResourceInspectorPtr res_inspector, RenderPasses::RenderPass** const output_pass) override;
        virtual void Execute(Hardwares::VulkanDevicePtr const device, RenderGraphResourceInspectorPtr res_inspector, Rendering::Scenes::SceneDataPtr const scene, RenderPasses::RenderPass* const pass, Buffers::FramebufferVNext* const framebuffer, Hardwares::CommandBufferPtr const command_buffer) override;
    };

} // namespace ZEngine::Rendering::Renderers