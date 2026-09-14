#pragma once
#include <ZEngine/Hardwares/VulkanDevice.h>
#include <ZEngine/Rendering/Cameras/Camera.h>
#include <ZEngine/Rendering/Renderers/IRenderer.h>
#include <ZEngine/Rendering/Scenes/SkyEnvironment.h>

namespace ZEngine::Rendering::Renderers
{
    struct LightingPass;
    struct SkyboxPass;
} // namespace ZEngine::Rendering::Renderers

namespace ZEngine::Rendering::Renderers
{
    struct GraphicRenderer : public IRenderer
    {
        GraphicRenderer();
        ~GraphicRenderer();

        void                      Initialize(Hardwares::VulkanDevicePtr device) override;
        void                      Deinitialize() override;
        Hardwares::CommandBuffer* DrawScene(uint8_t frame_index, uint8_t thread_index, Hardwares::CommandBufferPtr const cb, const Cameras::CameraFrameData& camera);
        /// @brief Accepts an immutable main-thread sky snapshot carried by the frame mailbox.
        void                      ApplySkyConfig(const Scenes::SkyConfig& sky, uint64_t revision);
        /// @brief Pins the published environment and selects it for all sky consumers this frame.
        void                      BeginSkyFrame();
        void                      ApplyGridConfig(const Scenes::GridConfig& cfg);
        /// @brief Returns the stable frame-color texture used by UI viewport widgets.
        Textures::TextureHandle   GetFrameOutput();

    private:
        void                   PublishFrameOutput(Textures::TextureHandle output);
        void                   StartPendingSkyBake();
        void                   PollSkyBake();
        void                   CollectRetiredSkySnapshots();
        void                   DiscardSkyTexture(Textures::TextureHandle texture);
        static void            OnSkyFrameSubmitted(void* context, Rendering::Primitives::Semaphore* timeline, uint64_t timeline_value);
        static void            OnSkyFrameCancelled(void* context);

        // The main thread reads this handle to build the next UI payload while
        // the render thread publishes it after compiling the current graph.
        // Sequence-guard the two 64-bit handle fields so readers never observe
        // a mixed index/generation pair.
        PaddedAtomic<uint64_t> m_frame_output_sequence   = {};
        PaddedAtomic<uint64_t> m_frame_output_index      = {.value = UINT64_MAX};
        PaddedAtomic<uint64_t> m_frame_output_generation = {};
        Scenes::SkyEnvironment m_sky_environment         = {};
        LightingPass*          m_lighting_pass           = nullptr;
        SkyboxPass*            m_skybox_pass             = nullptr;
    };
    ZDEFINE_PTR(GraphicRenderer);
} // namespace ZEngine::Rendering::Renderers
