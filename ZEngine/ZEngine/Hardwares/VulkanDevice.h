#pragma once
#include <vk_mem_alloc.h>
#include <vulkan/vulkan.h>
// clang-format off
#include <Hardwares/DeviceSwapchain.h>
#include <Hardwares/VulkanLayer.h>
#include <Helpers/HandleManager.h>
#include <Helpers/MemoryOperations.h>
#include <Helpers/ThreadSafeQueue.h>
#include <Rendering/Primitives/Fence.h>
#include <Rendering/Primitives/Semaphore.h>
#include <Rendering/Pools/CommandPool.h>
#include <Rendering/Primitives/ImageMemoryBarrier.h>
#include <Rendering/ResourceTypes.h>
#include <Rendering/Specifications/ShaderSpecification.h>
#include <Rendering/Specifications/RenderPassSpecification.h>
#include <Rendering/Textures/Texture.h>
#include <Core/Containers/Array.h>
#include <Core/Containers/UnorderedHashMap.h>
#include <Core/Containers/Strings.h>
#include <Core/Memory/Allocator.h>
#include <AsyncResourceLoader.h>
#include <set>
#include <unordered_set>
#include <limits>
#include <cstdint>
// clang-format on

namespace ZEngine::Windows
{
    class CoreWindow;
}

namespace ZEngine::Rendering::Renderers::RenderPasses
{
    struct RenderPass;
    struct Attachment;
} // namespace ZEngine::Rendering::Renderers::RenderPasses

namespace ZEngine::Rendering::Shaders
{
    struct Shader;
}

namespace ZEngine::Rendering::Renderers::Pipelines
{
    struct GraphicPipeline;
}

namespace ZEngine::Hardwares
{
    struct WriteDescriptorSetRequestKey;
    struct WriteDescriptorSetRequest;
    struct CommandBufferManager;
    struct AsyncGPUOperation;
    struct AsyncGPUOperationHandle;
    /*
     * Vertex | Index | Uniform | Storage Buffers
     */
    struct BufferView;
    struct BufferImage;
    struct IGraphicBuffer;
    class StorageBuffer;
    class VertexBuffer;
    class IndexBuffer;
    class UniformBuffer;
    /*
     * GPU Device
     */
    struct VulkanDevice;

    enum BufferType : uint8_t
    {
        UNKNOWN = 0,
        VERTEX,
        INDEX,
        UNIFORM,
        STORAGE,
        INDIRECT
    };
    struct BufferView
    {
        uint8_t       FrameIndex = std::numeric_limits<uint8_t>::max();
        BufferType    Type       = BufferType::UNKNOWN;
        VkBuffer      Handle     = VK_NULL_HANDLE;
        VmaAllocation Allocation = nullptr;
        // clang-format off
        operator bool() const
        {
            return (Handle != VK_NULL_HANDLE);
        }
        // clang-format on
    };

    struct BufferImage
    {
        uint8_t       FrameIndex{std::numeric_limits<uint8_t>::max()};
        VkImage       Handle{VK_NULL_HANDLE};
        VkImageView   ViewHandle{VK_NULL_HANDLE};
        VkSampler     Sampler{VK_NULL_HANDLE};
        VmaAllocation Allocation{nullptr};
        // clang-format off
        operator bool() const
        {
            return (Handle != VK_NULL_HANDLE);
        }
        // clang-format on
    };

    struct IGraphicBuffer
    {
        IGraphicBuffer() {}
        IGraphicBuffer(Hardwares::VulkanDevice* device) : m_device(device) {}
        virtual ~IGraphicBuffer();

        virtual BufferView CreateBuffer() = 0;

        virtual void       Allocate(uint64_t size, const char* debug_name);
        virtual void       Clear(uint8_t frame_index, uint8_t thread_index);
        virtual void       ClearRange(uint8_t frame_index, uint8_t thread_index, uint8_t value, uint32_t offset, size_t size);
        virtual void       UploadRange(uint8_t frame_index, uint8_t thread_index, const void* data, uint32_t offset, size_t size);
        virtual void       Upload(uint8_t frame_index, uint8_t thread_index, const void* data, size_t size);

        virtual void       Write(uint8_t frame_index, uint8_t thread_index, const void* data, size_t byte_size);

        template <typename T>
        inline void Write(uint8_t frame_index, uint8_t thread_index, Core::Containers::ArrayView<T> content)
        {
            Write(frame_index, thread_index, content.data(), content.size_bytes());
        }

        template <typename T>
        inline void Upload(uint8_t frame_index, uint8_t thread_index, Core::Containers::ArrayView<T> content)
        {
            Upload(frame_index, thread_index, content.data(), content.size_bytes());
        }

        virtual void                          CleanUpMemory();
        virtual void*                         GetNativeBufferHandle() const;
        virtual const VkDescriptorBufferInfo& GetDescriptorBufferInfo();
        virtual void                          Dispose();

        uint64_t                              m_total_size     = 0;
        uint64_t                              m_current_offset = 0;
        Hardwares::VulkanDevice*              m_device         = nullptr;
        BufferView                            Buffer           = {};
        VkDescriptorBufferInfo                BufferInfo       = {};
    };

    template <typename T /*, typename = std::enable_if_t<std::is_base_of_v<IGraphicBuffer, T>> */>
    struct IBufferSet
    {
        Core::Containers::Array<T> set = {};

        T&                         operator[](uint32_t index)
        {
            ZENGINE_VALIDATE_ASSERT(index < set.size(), "Index out of range")
            return set[index];
        }

        T& At(uint32_t index)
        {
            ZENGINE_VALIDATE_ASSERT(index < set.size(), "Index out of range")
            return set[index];
        }

        template <typename K>
        void SetData(uint32_t index, Core::Containers::ArrayView<K> data)
        {
            ZENGINE_VALIDATE_ASSERT(index < set.size(), "Index out of range")

            T& entry = set[index];
            entry->template Upload<K>(data);
        }

        void Dispose() {}
    };

    struct VertexBuffer : public IGraphicBuffer
    {
        explicit VertexBuffer(Hardwares::VulkanDevice* device) : IGraphicBuffer(device) {}

        virtual BufferView CreateBuffer() override;

        virtual ~VertexBuffer() {}
    };

    using VertexBufferSet       = IBufferSet<VertexBuffer*>;
    using VertexBufferSetHandle = Helpers::Handle<VertexBufferSet>;

    template <>
    inline void VertexBufferSet::Dispose()
    {
        for (auto buffer : set)
        {
            if (buffer)
            {
                buffer->Dispose();
            }
        }
    }

    struct StorageBuffer : public IGraphicBuffer
    {
        explicit StorageBuffer(Hardwares::VulkanDevice* device) : IGraphicBuffer(device) {}

        virtual BufferView CreateBuffer() override;

        virtual ~StorageBuffer() {}
    };

    using StorageBufferSet       = IBufferSet<StorageBuffer*>;
    using StorageBufferSetHandle = Helpers::Handle<StorageBufferSet>;

    template <>
    inline void StorageBufferSet::Dispose()
    {
        for (auto buffer : set)
        {
            if (buffer)
            {
                buffer->Dispose();
            }
        }
    }

    struct IndexBuffer : public IGraphicBuffer
    {
        IndexBuffer(Hardwares::VulkanDevice* device) : IGraphicBuffer(device) {}

        virtual BufferView CreateBuffer() override;

        virtual ~IndexBuffer() {}
    };

    using IndexBufferSet       = IBufferSet<IndexBuffer*>;
    using IndexBufferSetHandle = Helpers::Handle<IndexBufferSet>;

    template <>
    inline void IndexBufferSet::Dispose()
    {
        for (auto buffer : set)
        {
            if (buffer)
            {
                buffer->Dispose();
            }
        }
    }

    struct IndirectBuffer : public IGraphicBuffer
    {
        explicit IndirectBuffer(Hardwares::VulkanDevice* device) : IGraphicBuffer(device) {}

        uint32_t           CommandCount = 0;

        virtual BufferView CreateBuffer() override;
        virtual void       CleanUpMemory() override;
        virtual void       Upload(uint8_t frame_index, uint8_t thread_index, const VkDrawIndirectCommand* data, size_t byte_size);

        virtual void       Write(uint8_t frame_index, uint8_t thread_index, const void* data, size_t byte_size) override;

        inline void        Write(uint8_t frame_index, uint8_t thread_index, Core::Containers::ArrayView<VkDrawIndirectCommand> content)
        {
            Write(frame_index, thread_index, content.data(), content.size_bytes());
        }

        virtual ~IndirectBuffer() {}
    };

    using IndirectBufferSet       = IBufferSet<IndirectBuffer*>;
    using IndirectBufferSetHandle = Helpers::Handle<IndirectBufferSet>;

    template <>
    inline void IndirectBufferSet::Dispose()
    {
        for (auto buffer : set)
        {
            if (buffer)
            {
                buffer->Dispose();
            }
        }
    }

    class UniformBuffer : public IGraphicBuffer
    {
    public:
        explicit UniformBuffer() : IGraphicBuffer(nullptr) {}
        explicit UniformBuffer(Hardwares::VulkanDevice* device) : IGraphicBuffer(device) {}

        explicit UniformBuffer(const UniformBuffer& rhs) = delete;

        explicit UniformBuffer(UniformBuffer& rhs)
        {
            this->m_device     = rhs.m_device;
            this->m_total_size = rhs.m_total_size;

            std::swap(this->Buffer, rhs.Buffer);
            std::swap(this->m_uniform_buffer_mapped, rhs.m_uniform_buffer_mapped);

            rhs.m_total_size            = 0;
            rhs.m_uniform_buffer_mapped = false;
            rhs.Buffer                  = {};
        }

        explicit UniformBuffer(UniformBuffer&& rhs) noexcept
        {
            this->m_device     = rhs.m_device;
            this->m_total_size = rhs.m_total_size;

            std::swap(this->Buffer, rhs.Buffer);
            std::swap(this->m_uniform_buffer_mapped, rhs.m_uniform_buffer_mapped);

            rhs.m_total_size            = 0;
            rhs.m_uniform_buffer_mapped = false;
            rhs.Buffer                  = {};
        }

        UniformBuffer& operator=(const UniformBuffer& rhs) = delete;

        UniformBuffer& operator=(UniformBuffer& rhs)
        {
            if (this == &rhs)
            {
                return *this;
            }

            this->m_total_size = rhs.m_total_size;
            this->m_device     = rhs.m_device;

            std::swap(this->Buffer, rhs.Buffer);
            std::swap(this->m_uniform_buffer_mapped, rhs.m_uniform_buffer_mapped);

            rhs.m_total_size            = 0;
            rhs.m_uniform_buffer_mapped = false;
            rhs.Buffer                  = {};

            return *this;
        }

        UniformBuffer& operator=(UniformBuffer&& rhs) noexcept
        {
            if (this == &rhs)
            {
                return *this;
            }

            this->m_total_size = rhs.m_total_size;
            this->m_device     = rhs.m_device;

            std::swap(this->Buffer, rhs.Buffer);
            std::swap(this->m_uniform_buffer_mapped, rhs.m_uniform_buffer_mapped);

            rhs.m_total_size            = 0;
            rhs.m_uniform_buffer_mapped = false;
            rhs.Buffer                  = {};

            return *this;
        }

        virtual void       Allocate(uint64_t byte_size, const char* debug_name) override;
        virtual BufferView CreateBuffer() override;
        virtual void       CleanUpMemory() override;

        virtual ~UniformBuffer() {}

    private:
        bool m_uniform_buffer_mapped{false};
    };

    using UniformBufferSet       = IBufferSet<UniformBuffer*>;
    using UniformBufferSetHandle = Helpers::Handle<UniformBufferSet>;

    template <>
    inline void UniformBufferSet::Dispose()
    {
        for (auto buffer : set)
        {
            if (buffer)
            {
                buffer->Dispose();
            }
        }
    }

    struct Image2DBuffer
    {
        Image2DBuffer() = default;
        ~Image2DBuffer();

        Rendering::Specifications::ImageLayout                Layout        = Rendering::Specifications::ImageLayout::UNDEFINED;
        Rendering::Specifications::Image2DBufferSpecification Specification = {};
        VulkanDevice*                                         Device        = nullptr;

        void                                                  Construct(VulkanDevice* device);

        BufferImage&                                          GetBuffer();
        const BufferImage&                                    GetBuffer() const;
        VkImageView                                           GetImageViewHandle() const;
        VkImage                                               GetHandle() const;
        VkSampler                                             GetSampler() const;
        void                                                  Dispose();
        VkDescriptorImageInfo&                                GetDescriptorImageInfo();

    private:
        BufferImage           m_buffer_image;
        VkDescriptorImageInfo m_image_info;
    };

    struct DirtyResource
    {
        uint32_t                      FrameIndex = UINT32_MAX;
        void*                         Handle     = nullptr;
        void*                         Data1      = nullptr;
        Rendering::DeviceResourceType Type;
    };

    struct QueueView
    {
        uint32_t FamilyIndex{0xFFFFFFFF};
        VkQueue  Handle{VK_NULL_HANDLE};
    };

    /*
     * Command Buffer definition
     */
    enum CommandBufferState : uint8_t
    {
        Idle = 0,
        Recording,
        Executable,
        Pending,
        Invalid
    };
    enum CommandBufferType : uint8_t
    {
        Primary = 0,
        Secondary
    };

    struct CommandBuffer
    {
        CommandBuffer(Hardwares::VulkanDevice* device, VkCommandPool command_pool, Rendering::QueueType type, bool one_time);
        ~CommandBuffer();

        Rendering::QueueType              QueueType;
        CommandBufferType                 BufferType = CommandBufferType::Primary;
        Hardwares::VulkanDevice*          Device     = nullptr;
        Core::Memory::ArenaAllocator      LocalArena = {};

        void                              Create();
        void                              Free();
        VkCommandBuffer                   GetHandle() const;
        void                              Begin();
        void                              BeginSecondary(Rendering::Renderers::RenderPasses::RenderPass* const render_pass, VkFramebuffer framebuffer);
        void                              End();
        bool                              Completed();
        bool                              IsExecutable();
        bool                              IsRecording();
        CommandBufferState                GetState() const;
        void                              ResetState();
        void                              SetState(const CommandBufferState& state);
        void                              SetSignalFence(Rendering::Primitives::Fence* const semaphore);
        void                              SetSignalSemaphore(Rendering::Primitives::Semaphore* const semaphore);
        Rendering::Primitives::Semaphore* GetSignalSemaphore() const;
        Rendering::Primitives::Fence*     GetSignalFence();
        void                              ClearColor(float r, float g, float b, float a);
        void                              ClearDepth(float depth_color, uint32_t stencil);
        void                              BeginRenderPass(Rendering::Renderers::RenderPasses::RenderPass* const, VkFramebuffer framebuffer, bool is_content_secondary_command_buffer);
        void                              EndRenderPass();
        void                              BindDescriptorSets(uint32_t frame_index = 0);
        void                              BindDescriptorSet(const VkDescriptorSet& descriptor);
        void                              BindPipeline(Rendering::Specifications::PipelineBindPoint bind_point, Rendering::Renderers::Pipelines::GraphicPipeline* const pipeline);
        void                              DrawIndirect(const Hardwares::IndirectBuffer& buffer);
        void                              DrawIndexedIndirect(const Hardwares::IndirectBuffer& buffer, uint32_t count);
        void                              DrawIndexed(uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex, int32_t vertexOffset, uint32_t firstInstance);
        void                              Draw(uint32_t vertex_count, uint32_t instance_count, uint32_t first_index, uint32_t first_instance);
        void                              TransitionImageLayout(const Rendering::Primitives::ImageMemoryBarrier& image_barrier);
        void                              CopyBufferToImage(const Hardwares::BufferView& source, Hardwares::BufferImage& destination, uint32_t width, uint32_t height, uint32_t layer_count, VkImageLayout new_layout);
        void                              BindVertexBuffer(Hardwares::VertexBuffer& buffer);
        void                              BindIndexBuffer(const Hardwares::IndexBuffer& buffer, VkIndexType type);
        void                              SetScissor(uint32_t w, uint32_t h, int32_t x = 0, int32_t y = 0);
        void                              SetViewport(uint32_t w, uint32_t h, float x = 0.0f, float y = 0.0f, float min_depth = 0.0f, float max_depth = 1.0f);
        void                              PushConstants(VkShaderStageFlags stage_flags, uint32_t offset, uint32_t size, const void* data);
        void                              ExecuteSecondaryCommandBuffers(Core::Containers::ArrayView<CommandBuffer> buffers);

    private:
        std::atomic_uint8_t m_command_buffer_state{CommandBufferState::Idle};
        VkCommandBuffer     m_command_buffer{VK_NULL_HANDLE};
        VkCommandPool       m_command_pool{VK_NULL_HANDLE};
        VkClearValue        m_clear_value[2] = {0};
        ZRawPtr(Rendering::Primitives::Fence) m_signal_fence;
        ZRawPtr(Rendering::Primitives::Semaphore) m_signal_semaphore;
        ZRawPtr(Rendering::Renderers::RenderPasses::RenderPass) m_active_render_pass;
    };

    ZDEFINE_PTR(CommandBuffer);

    struct CommandBufferManager
    {
        struct InstantCommandBufferInfo;

        void                                                            Initialize(VulkanDevice* device, uint32_t image_count = 0, uint8_t override_thread_count = 0);
        void                                                            Deinitialize();
        CommandBuffer*                                                  GetCommandBuffer(Rendering::QueueType type, uint8_t frame_index, uint8_t thread_index, uint8_t buffer_per_pool_index, bool begin = true);
        CommandBuffer*                                                  GetInstantCommandBuffer(Rendering::QueueType type, uint8_t frame_index, uint8_t thread_index, uint32_t buffer_per_pool_index, bool begin = true);
        Rendering::Pools::CommandPool*                                  GetCommandPool(Rendering::QueueType type, uint8_t frame_index, uint8_t thread_index);
        Rendering::Pools::CommandPool*                                  GetInstantCommandPool(Rendering::QueueType type, uint8_t frame_index, uint8_t thread_index);
        void                                                            ResetPool(uint8_t frame_index, uint8_t thread_index);
        void                                                            IncreaseBuffers();
        void                                                            EnqueueBuffer(CommandBufferPtr const buffer);
        void                                                            EndEnqueuedBuffers();
        void                                                            ResetEnqueuedBufferIndex();
        bool                                                            IsInitialized() const;

        uint32_t                                                        TotalCommandBufferCount        = 0;
        uint32_t                                                        TotalInstantCommandBufferCount = 0;
        uint32_t                                                        TotalPoolCount                 = 0;
        uint32_t                                                        TotalThreadCount               = 0;
        uint32_t                                                        EnqueuedCommandBufferIndex     = 0;
        const uint32_t                                                  MaxBufferPerPool               = 4;
        VulkanDevice*                                                   Device                         = nullptr;

        Core::Containers::Array<Rendering::Pools::CommandPool*>         InstantGraphicsPools           = {};
        Core::Containers::Array<Rendering::Pools::CommandPool*>         InstantTransferPools           = {};
        Core::Containers::Array<CommandBuffer*>                         InstantGraphicsCommandBuffers  = {};
        Core::Containers::Array<CommandBuffer*>                         InstantTransferCommandBuffers  = {};

        Core::Containers::Array<ZRawPtr(Rendering::Pools::CommandPool)> CommandPools                   = {};
        Core::Containers::Array<ZRawPtr(Rendering::Pools::CommandPool)> TransferCommandPools           = {};
        Core::Containers::Array<ZRawPtr(CommandBuffer)>                 CommandBuffers                 = {};
        Core::Containers::Array<ZRawPtr(CommandBuffer)>                 TransferCommandBuffers         = {};
        Core::Containers::Array<CommandBuffer*>                         EnqueuedCommandBuffers         = {};

    private:
        bool m_is_initialized = false;
    };
    ZDEFINE_PTR(CommandBufferManager);

    struct WriteDescriptorSetRequestKey
    {
        uint32_t        Binding = 0;
        VkDescriptorSet DstSet  = VK_NULL_HANDLE;

        bool            operator<(const WriteDescriptorSetRequestKey& other) const
        {
            if (Binding != other.Binding)
                return Binding < other.Binding;
            return DstSet < other.DstSet;
        }
    };

    struct WriteDescriptorSetRequest
    {
        bool             Updated = false;
        int              Handle;
        uint32_t         FrameIndex;
        VkDescriptorSet  DstSet;
        uint32_t         Binding;
        uint32_t         DstArrayElement;
        uint32_t         DescriptorCount;
        VkDescriptorType DescriptorType;
    };

    /*
     * Async GPU operation handle and definition
     */
    struct AsyncGPUOperationHandle
    {
        uint32_t                          StageFlags  = 0;
        uint64_t                          SignalValue = 0;
        Rendering::Primitives::Semaphore* Timeline    = nullptr;
    };

    struct AsyncGPUOperation
    {
        uint64_t                          NextValue    = 0;
        Rendering::Primitives::Semaphore* Timeline     = nullptr;
        Core::Containers::Array<uint64_t> RetireValues = {};

        void                              Initialize(VulkanDevice* device, uint32_t total_buffer_count);
    };
    /*
     *  Device definition
     */
    struct VulkanDevice
    {
        bool                                                                                                                         HasSeperateTransfertQueueFamily             = false;
        bool                                                                                                                         PhysicalDeviceSupportSampledImageBindless   = false;
        bool                                                                                                                         PhysicalDeviceSupportStorageBufferBindless  = false;
        bool                                                                                                                         PhysicalDeviceSupportTimelineSemaphore      = false;
        const char*                                                                                                                  ApplicationName                             = "Tetragrama";
        const char*                                                                                                                  EngineName                                  = "ZEngine";
        uint32_t                                                                                                                     WorkerThreadCount                           = 1;
        uint32_t                                                                                                                     GraphicFamilyIndex                          = std::numeric_limits<uint32_t>::max();
        uint32_t                                                                                                                     TransferFamilyIndex                         = std::numeric_limits<uint32_t>::max();

        uint32_t                                                                                                                     WriteDescriptorSetIndex                     = 0;
        uint32_t                                                                                                                     MaxGlobalTexture                            = 600;
        VkInstance                                                                                                                   Instance                                    = VK_NULL_HANDLE;
        VkSurfaceKHR                                                                                                                 Surface                                     = VK_NULL_HANDLE;
        VkSurfaceFormatKHR                                                                                                           SurfaceFormat                               = {};
        VkPresentModeKHR                                                                                                             PresentMode                                 = {};
        VkPhysicalDeviceProperties2                                                                                                  PhysicalDeviceProperties                    = {};
        VkPhysicalDeviceVulkan12Properties                                                                                           PhysicalDeviceVulkan12Properties            = {};
        VkDevice                                                                                                                     LogicalDevice                               = VK_NULL_HANDLE;
        VkPhysicalDevice                                                                                                             PhysicalDevice                              = VK_NULL_HANDLE;
        VkPhysicalDeviceFeatures2                                                                                                    PhysicalDeviceFeature                       = {};
        VkPhysicalDeviceMemoryProperties                                                                                             PhysicalDeviceMemoryProperties              = {};
        VkSampler                                                                                                                    GlobalLinearWrapSampler                     = VK_NULL_HANDLE;
        VkDescriptorPool                                                                                                             GlobalDescriptorPoolHandle                  = VK_NULL_HANDLE;
        VmaAllocator                                                                                                                 VmaAllocatorValue                           = nullptr;
        VkDescriptorImageInfo                                                                                                        GlobalLinearWrapSamplerImageInfo            = {};
        CommandBufferManagerPtr                                                                                                      CommandBufferMgr                            = {};
        DeviceSwapchainPtr                                                                                                           SwapchainPtr                                = {};
        Core::Containers::Array<VkFormat>                                                                                            DefaultDepthFormats                         = {};

        Core::Containers::UnorderedHashMap<const char*, Helpers::Handle<Rendering::Shaders::Shader>>                                 ShaderCaches                                = {};
        Core::Containers::UnorderedHashMap<uint32_t, Core::Containers::Array<VkDescriptorSet>>                                       ShaderReservedDescriptorSetMap              = {}; //<set, vec<descriptorSet>>
        Core::Containers::UnorderedHashMap<uint32_t, VkDescriptorSetLayout>                                                          ShaderReservedDescriptorSetLayoutMap        = {}; // <set, layout>
        Core::Containers::UnorderedHashMap<uint32_t, Core::Containers::Array<Rendering::Specifications::LayoutBindingSpecification>> ShaderReservedLayoutBindingSpecificationMap = {};
        std::set<WriteDescriptorSetRequestKey>                                                                                       WriteBindlessDescriptorSetRequests          = {};
        std::unordered_set<uint32_t>                                                                                                 ShaderReservedBindingSets                   = {};
        Rendering::Textures::TextureHandleManager                                                                                    GlobalTextures                              = {};
        Helpers::HandleManager<Image2DBuffer>                                                                                        Image2DBufferManager                        = {};
        Helpers::ThreadSafeQueue<Rendering::Textures::TextureHandle>                                                                 TextureHandleToUpdates                      = {};
        Helpers::ThreadSafeQueue<Rendering::Textures::TextureHandle>                                                                 TextureHandleToDispose                      = {};
        Helpers::ThreadSafeQueue<AsyncGPUOperationHandle>                                                                            AsyncGPUOperations                          = {};
        Helpers::HandleManager<Rendering::Shaders::Shader>                                                                           ShaderManager                               = {};
        Helpers::HandleManager<VertexBufferSet>                                                                                      VertexBufferSetManager                      = {};
        Helpers::HandleManager<StorageBufferSet>                                                                                     StorageBufferSetManager                     = {};
        Helpers::HandleManager<IndirectBufferSet>                                                                                    IndirectBufferSetManager                    = {};
        Helpers::HandleManager<IndexBufferSet>                                                                                       IndexBufferSetManager                       = {};
        Helpers::HandleManager<UniformBufferSet>                                                                                     UniformBufferSetManager                     = {};
        Helpers::HandleManager<DirtyResource>                                                                                        DirtyResources                              = {};
        Helpers::HandleManager<BufferView>                                                                                           DirtyBuffers                                = {};
        Helpers::HandleManager<BufferImage>                                                                                          DirtyBufferImages                           = {};
        std::atomic_bool                                                                                                             RunningDirtyCollector                       = {};
        std::mutex                                                                                                                   Mutex                                       = {};
        Windows::CoreWindow*                                                                                                         CurrentWindow                               = nullptr;
        ZEngine::Core::Memory::ArenaAllocator*                                                                                       Arena                                       = nullptr;
        AsyncResourceLoaderPtr                                                                                                       AsyncResLoader                              = nullptr;

        void                                                                                                                         Initialize(ZEngine::Core::Memory::ArenaAllocator* arena, Windows::CoreWindow* const window, uint32_t worker_thread_count);
        void                                                                                                                         Deinitialize();
        void                                                                                                                         Dispose();
        void                                                                                                                         QueueSubmit(CommandBuffer* const command_buffer, Rendering::Primitives::Semaphore* const signal_semaphore, uint32_t wait_flag, uint64_t signal_value, uint64_t wait_value, Rendering::Primitives::Semaphore* const wait_timeline);
        bool                                                                                                                         QueueSubmit(const VkPipelineStageFlags wait_stage_flag, CommandBuffer* const command_buffer, Rendering::Primitives::Semaphore* const signal_semaphore = nullptr, Rendering::Primitives::Fence* const fence = nullptr);
        void                                                                                                                         EnqueueAsyncGPUOperation(const AsyncGPUOperationHandle& handle);
        void                                                                                                                         EnqueueForDeletion(Rendering::DeviceResourceType resource_type, void* const resource_handle);
        void                                                                                                                         EnqueueForDeletion(Rendering::DeviceResourceType resource_type, DirtyResource resource);
        void                                                                                                                         EnqueueBufferForDeletion(BufferView& buffer);
        void                                                                                                                         EnqueueBufferImageForDeletion(BufferImage& buffer);
        QueueView                                                                                                                    GetQueue(Rendering::QueueType type);
        void                                                                                                                         QueueWait(Rendering::QueueType type);
        void                                                                                                                         QueueWaitAll();
        void                                                                                                                         MapAndCopyToMemory(BufferView& buffer, size_t data_size, const void* data);
        BufferView                                                                                                                   CreateBuffer(VkDeviceSize byte_size, VkBufferUsageFlags buffer_usage, VmaAllocationCreateFlags vma_create_flags = 0);
        VkPipelineStageFlags                                                                                                         CopyBuffer(CommandBuffer* const command_buffer, const BufferView& source, const BufferView& destination, VkDeviceSize byte_size, VkDeviceSize src_buffer_offset = 0u, VkDeviceSize dst_buffer_offset = 0u);
        BufferImage                                     CreateImage(uint32_t width, uint32_t height, VkImageType image_type, VkImageViewType image_view_type, VkFormat image_format, VkImageTiling image_tiling, VkImageLayout image_initial_layout, VkImageUsageFlags image_usage, VkSharingMode image_sharing_mode, VkSampleCountFlagBits image_sample_count, VkMemoryPropertyFlags requested_properties, VkImageAspectFlagBits image_aspect_flag, uint32_t layer_count = 1U, VkImageCreateFlags image_create_flag_bit = 0);
        VkFormat                                        FindSupportedFormat(Core::Containers::ArrayView<VkFormat> format_collection, VkImageTiling image_tiling, VkFormatFeatureFlags feature_flags);
        VkFormat                                        FindDepthFormat();
        VkImageView                                     CreateImageView(VkImage image, VkFormat image_format, VkImageViewType image_view_type, VkImageAspectFlagBits image_aspect_flag, uint32_t layer_count = 1U);
        VkFramebuffer                                   CreateFramebuffer(Core::Containers::ArrayView<VkImageView> attachments, const VkRenderPass& render_pass, uint32_t width, uint32_t height, uint32_t layer_number = 1);
        VertexBufferSetHandle                           CreateVertexBufferSet();
        StorageBufferSetHandle                          CreateStorageBufferSet();
        IndirectBufferSetHandle                         CreateIndirectBufferSet();
        IndexBufferSetHandle                            CreateIndexBufferSet();
        UniformBufferSetHandle                          CreateUniformBufferSet();
        void                                            DirtyCollector();

        Helpers::Handle<Rendering::Shaders::Shader>     CompileShader(Rendering::Specifications::ShaderSpecification& spec);

        Rendering::Textures::TextureHandle              CreateTexture(uint32_t width, uint32_t height);
        Rendering::Textures::TextureHandle              CreateTexture(uint32_t width, uint32_t height, float r = 255, float g = 255, float b = 255, float a = 255);
        Rendering::Textures::TextureHandle              CreateTexture(const Rendering::Specifications::TextureSpecification& spec);
        void                                            WriteTextureData(CommandBufferPtr command_buf, const Rendering::Textures::TextureHandle& handle, const void* data);

        Rendering::Renderers::RenderPasses::RenderPass* CreateRenderPass(const Rendering::Specifications::RenderPassSpecification& spec);

    private:
        VulkanLayer                                                       m_layer     = {};
        Core::Containers::UnorderedHashMap<Rendering::QueueType, VkQueue> m_queue_map = {};
        VkDebugUtilsMessengerEXT                                          m_debug_messenger{VK_NULL_HANDLE};
        PFN_vkCreateDebugUtilsMessengerEXT                                __createDebugMessengerPtr{VK_NULL_HANDLE};
        PFN_vkDestroyDebugUtilsMessengerEXT                               __destroyDebugMessengerPtr{VK_NULL_HANDLE};
        void                                                              __cleanupDirtyResource();
        void                                                              __cleanupBufferDirtyResource();
        void                                                              __cleanupBufferImageDirtyResource();
        static VKAPI_ATTR VkBool32 VKAPI_CALL                             __debugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity, VkDebugUtilsMessageTypeFlagsEXT messageType, const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData, void* pUserData);
    };

    ZDEFINE_PTR(VulkanDevice);
} // namespace ZEngine::Hardwares

namespace ZEngine::Helpers
{
    template <>
    inline void HandleManager<Hardwares::VertexBufferSet>::Dispose()
    {
        for (size_t i = 0; i < m_count; ++i)
        {
            m_memory[i].Dispose();
        }
    }

    template <>
    inline void HandleManager<Hardwares::StorageBufferSet>::Dispose()
    {
        for (size_t i = 0; i < m_count; ++i)
        {
            m_memory[i].Dispose();
        }
    }

    template <>
    inline void HandleManager<Hardwares::IndirectBufferSet>::Dispose()
    {
        for (size_t i = 0; i < m_count; ++i)
        {
            m_memory[i].Dispose();
        }
    }

    template <>
    inline void HandleManager<Hardwares::IndexBufferSet>::Dispose()
    {
        for (size_t i = 0; i < m_count; ++i)
        {
            m_memory[i].Dispose();
        }
    }

    template <>
    inline void HandleManager<Hardwares::UniformBufferSet>::Dispose()
    {
        for (size_t i = 0; i < m_count; ++i)
        {
            m_memory[i].Dispose();
        }
    }

    template <>
    inline void HandleManager<Hardwares::Image2DBuffer>::Dispose()
    {
        for (size_t i = 0; i < m_count; ++i)
        {
            m_memory[i].Dispose();
        }
    }
} // namespace ZEngine::Helpers
