#pragma once
#include <ZEngine/Hardwares/VulkanDevice.h>
#include <ZEngine/Rendering/Cameras/Camera.h>
#include <ZEngine/Rendering/Renderers/IRenderer.h>

namespace ZEngine::Rendering::Renderers
{
    struct GraphicRenderer : public IRenderer
    {
        GraphicRenderer();
        ~GraphicRenderer();

        void                      Initialize(Hardwares::VulkanDevicePtr device) override;
        void                      Deinitialize() override;
        Hardwares::CommandBuffer* DrawScene(uint8_t frame_index, uint8_t thread_index, Hardwares::CommandBufferPtr const cb, Cameras::CameraPtr const camera);
        void                      ApplySkyConfig(const Scenes::SkyConfig& sky);
        void                      ApplyGridConfig(const Scenes::GridConfig& cfg);
        /// @brief Returns the stable frame-color texture used by UI viewport widgets.
        Textures::TextureHandle   GetFrameOutput();

    private:
        void                   PublishFrameOutput(Textures::TextureHandle output);

        // The main thread reads this handle to build the next UI payload while
        // the render thread publishes it after compiling the current graph.
        // Sequence-guard the two 64-bit handle fields so readers never observe
        // a mixed index/generation pair.
        PaddedAtomic<uint64_t> m_frame_output_sequence   = {};
        PaddedAtomic<uint64_t> m_frame_output_index      = {.value = UINT64_MAX};
        PaddedAtomic<uint64_t> m_frame_output_generation = {};
    };
    ZDEFINE_PTR(GraphicRenderer);
} // namespace ZEngine::Rendering::Renderers
