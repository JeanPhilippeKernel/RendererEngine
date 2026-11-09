#pragma once
#include <ZEngine/Core/VFS/Registry/AssetRegistry.h>
#include <ZEngine/Managers/AssetCacheLease.h>
#include <ZEngine/Rendering/RenderResourceManager.h>
#include <ZEngine/Rendering/Renderers/RenderGraph.h>
#include <mutex>

namespace ZEngine::Rendering::Renderers
{
    /// Bounded editor-only offscreen work. The service borrows this renderer,
    /// never callbacks into editor memory. All GPU recording belongs to the graph.
    struct AssetPreviewPass final : IRenderGraphCallbackPass
    {
        static constexpr uint32_t Capacity            = 16;
        static constexpr uint32_t Extent              = 128;
        static constexpr uint32_t EnvironmentRevision = 1;
        static constexpr uint32_t RendererRevision    = 3;
        static constexpr uint32_t MaxDraws            = 256;
        enum class Result : uint8_t
        {
            Pending = 0,
            Succeeded,
            Failed
        };

        void                      Initialize(Hardwares::VulkanDevice* device);
        Textures::TextureHandle   Request(const Core::VFS::AssetRecordSnapshot& record, uint64_t dependency_hash, const char* cache_path = nullptr, const RenderResourceManager::ThumbnailCacheIdentity* identity = nullptr);
        Result                    Observe(Textures::TextureHandle texture);
        void                      Forget(Textures::TextureHandle texture);
        /// Cancels ownership without freeing a recorded/submitted target early.
        bool                      Discard(Textures::TextureHandle texture);
        void                      BeginFrame();
        void                      EndFrame();
        [[nodiscard]] static bool DependencyFingerprint(const Core::VFS::AssetRecordSnapshot& record, uint64_t* hash, uint64_t* persistent_hash = nullptr, bool* fully_transparent = nullptr, Materials::AlphaInfo* alpha_info = nullptr);
        /// Proves that the preview's alpha test rejects the whole constant surface.
        /// Textured coverage is deliberately unknown; Legacy replaces the factor,
        /// while explicit modes multiply it by sampled base-color alpha.
        [[nodiscard]] static bool IsFullyTransparent(const Meshes::MeshMaterial& material);
        [[nodiscard]] static bool FrameBounds(const Core::Maths::Vec3f& center, float radius, Core::Maths::Mat4f* view_projection);

        bool                      TryBuildGraphicsPipelineDescription(Core::Memory::ArenaAllocator* arena, Specifications::GraphicsPipelineDesc* out) const override;
        bool                      Register(Hardwares::VulkanDevicePtr, cstring, const RenderGraphFrameContext&, RenderGraphResourceBuilderPtr, RenderGraphResourceInspectorPtr) override;
        void                      Prepare(Hardwares::VulkanDevicePtr, Scenes::SceneDataPtr, RenderGraphResourceInspectorPtr, RenderPasses::RenderPass*) override;
        void                      Execute(Hardwares::VulkanDevicePtr, RenderGraphResourceInspectorPtr, Scenes::SceneDataPtr, RenderPasses::RenderPass*, Buffers::FramebufferVNext*, Hardwares::CommandBufferPtr) override;
        bool                      RecordDraw(Hardwares::VulkanDevicePtr, RenderGraphResourceInspectorPtr, Scenes::SceneDataPtr, RenderPasses::RenderPass*, Buffers::FramebufferVNext*, Hardwares::CommandBufferPtr) override;
        bool                      SupportsSecondaryRecording() const override
        {
            return true;
        }
        void Deinitialize(Hardwares::VulkanDevicePtr) override;

        /// A separate graph consumer derives the color-write -> sampled barrier.
        struct PublishPass final : IRenderGraphCallbackPass
        {
            AssetPreviewPass* Owner = nullptr;
            bool              Register(Hardwares::VulkanDevicePtr, cstring, const RenderGraphFrameContext&, RenderGraphResourceBuilderPtr, RenderGraphResourceInspectorPtr) override;
            bool              RequiresRenderPass() const override
            {
                return false;
            }
            RGPassFlags GetPassFlags() const override
            {
                return RGPassFlags::NeverCull;
            }
            void Execute(Hardwares::VulkanDevicePtr, RenderGraphResourceInspectorPtr, Scenes::SceneDataPtr, RenderPasses::RenderPass*, Buffers::FramebufferVNext*, Hardwares::CommandBufferPtr) override;
        } Publish{};

    private:
        friend struct AssetPreviewPassTestAccess;
        enum class State : uint8_t
        {
            Empty = 0,
            Queued,
            Recording,
            Submitted,
            Succeeded,
            Failed
        };
        struct Task
        {
            AssetPreviewPass*                             Owner                          = nullptr;
            Core::VFS::AssetRecordSnapshot                Record                         = {};
            Textures::TextureHandle                       Texture                        = {};
            Managers::AssetPayloadPin                     SourcePin                      = {};
            Primitives::Semaphore*                        Timeline                       = nullptr;
            uint64_t                                      TimelineValue                  = 0;
            uint64_t                                      DependencyHash                 = 0;
            State                                         CurrentState                   = State::Empty;
            uint16_t                                      Attempts                       = 0;
            bool                                          Abandoned                      = false;
            RenderResourceManager::ThumbnailCacheIdentity CacheIdentity                  = {};
            char                                          CachePath[MAX_FILE_PATH_COUNT] = {};
        };
        struct alignas(16) Uniform
        {
            Core::Maths::Mat4f ViewProjection = {};
            Core::Maths::Vec4f CenterRadius   = {0, 0, 0, 1};
            Core::Maths::Vec4f Albedo         = {0.62f, 0.65f, 0.7f, 1};
            Core::Maths::Vec4f Emissive       = {};
            Core::Maths::Vec4f Surface        = {0.6f, 0, 0, 0}; // roughness, metallic, cutoff, procedural-sphere flag
            uint32_t           Maps[8]        = {UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX, 0, 0, 0};
        };
        struct Draw
        {
            uint32_t VertexOffset = 0, IndexOffset = 0, Count = 0;
        };
        static void              OnSubmitted(void*, Primitives::Semaphore*, uint64_t);
        static void              OnCancelled(void*);
        bool                     PrepareTask(Task& task);
        void                     Release(Task& task);
        Task*                    Find(Textures::TextureHandle texture);
        Hardwares::VulkanDevice* m_device = nullptr;
        std::mutex               m_mutex{};
        Task                     m_tasks[Capacity] = {};
        Task*                    m_current         = nullptr;
        uint32_t                 m_cursor          = 0;
        Uniform                  m_uniform         = {};
        Draw                     m_draws[MaxDraws] = {};
        uint32_t                 m_draw_count      = 0;
        uint32_t                 m_uniform_offset  = 0;
        RGResourceHandle         m_color           = {};
        bool                     m_recorded        = false;
        bool                     m_published       = false;
        void*                    m_cache_ticket    = nullptr;
    };
} // namespace ZEngine::Rendering::Renderers
