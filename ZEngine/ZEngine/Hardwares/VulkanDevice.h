#pragma once
#include <vk_mem_alloc.h>
#include <vulkan/vulkan.h>
// clang-format off
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
#include <Core/Containers/HashMap.h>
#include <Core/Containers/Strings.h>
#include <Core/Memory/Allocator.h>
#include <AsyncResourceLoader.h>
#include <set>
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

namespace ZEngine::Hardwares
{
    struct WriteDescriptorSetRequestKey;
    struct WriteDescriptorSetRequest;
    struct CommandBufferManager;
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

        operator bool() const
        {
            return (Handle != VK_NULL_HANDLE);
        }
    };

    struct BufferImage
    {
        uint8_t       FrameIndex{std::numeric_limits<uint8_t>::max()};
        VkImage       Handle{VK_NULL_HANDLE};
        VkImageView   ViewHandle{VK_NULL_HANDLE};
        VkSampler     Sampler{VK_NULL_HANDLE};
        VmaAllocation Allocation{nullptr};

        operator bool() const
        {
            return (Handle != VK_NULL_HANDLE);
        }
    };

    struct IGraphicBuffer
    {
        IGraphicBuffer() {}
        IGraphicBuffer(Hardwares::VulkanDevice* device) : m_device(device) {}
        virtual ~IGraphicBuffer();

        virtual BufferView CreateBuffer() = 0;

        virtual void       Allocate(uint64_t size, const char* debug_name);
        virtual void       Clear();
        virtual void       ClearRange(uint8_t value, uint32_t offset, size_t size);
        virtual void       UploadRange(const void* data, uint32_t offset, size_t size);
        virtual void       Upload(const void* data, size_t size);

        virtual void       Write(const void* data, size_t byte_size);

        template <typename T>
        inline void Write(Core::Containers::ArrayView<T> content)
        {
            Write(content.data(), content.size_bytes());
        }

        template <typename T>
        inline void Upload(Core::Containers::ArrayView<T> content)
        {
            Upload(content.data(), content.size_bytes());
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
        virtual void       Upload(const VkDrawIndirectCommand* data, size_t byte_size);

        virtual void       Write(const void* data, size_t byte_size) override;

        inline void        Write(Core::Containers::ArrayView<VkDrawIndirectCommand> content)
        {
            Write(content.data(), content.size_bytes());
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
    enum CommanBufferState : uint8_t
    {
        Idle = 0,
        Recording,
        Executable,
        Pending,
        Invalid
    };

    struct CommandBuffer
    {
        CommandBuffer(Hardwares::VulkanDevice* device, VkCommandPool command_pool, Rendering::QueueType type, bool one_time);
        ~CommandBuffer();

        Rendering::QueueType              QueueType;
        Hardwares::VulkanDevice*          Device     = nullptr;
        Core::Memory::ArenaAllocator      LocalArena = {};

        void                              Create();
        void                              Free();
        VkCommandBuffer                   GetHandle() const;
        void                              Begin();
        void                              End();
        bool                              Completed();
        bool                              IsExecutable();
        bool                              IsRecording();
        CommanBufferState                 GetState() const;
        void                              ResetState();
        void                              SetState(const CommanBufferState& state);
        void                              SetSignalFence(Rendering::Primitives::Fence* const semaphore);
        void                              SetSignalSemaphore(Rendering::Primitives::Semaphore* const semaphore);
        Rendering::Primitives::Semaphore* GetSignalSemaphore() const;
        Rendering::Primitives::Fence*     GetSignalFence();
        void                              ClearColor(float r, float g, float b, float a);
        void                              ClearDepth(float depth_color, uint32_t stencil);
        void                              BeginRenderPass(Rendering::Renderers::RenderPasses::RenderPass* const, VkFramebuffer framebuffer);
        void                              EndRenderPass();
        void                              BindDescriptorSets(uint32_t frame_index = 0);
        void                              BindDescriptorSet(const VkDescriptorSet& descriptor);
        void                              DrawIndirect(const Hardwares::IndirectBuffer& buffer);
        void                              DrawIndexedIndirect(const Hardwares::IndirectBuffer& buffer, uint32_t count);
        void                              DrawIndexed(uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex, int32_t vertexOffset, uint32_t firstInstance);
        void                              Draw(uint32_t vertex_count, uint32_t instance_count, uint32_t first_index, uint32_t first_instance);
        void                              TransitionImageLayout(const Rendering::Primitives::ImageMemoryBarrier& image_barrier);
        void                              CopyBufferToImage(const Hardwares::BufferView& source, Hardwares::BufferImage& destination, uint32_t width, uint32_t height, uint32_t layer_count, VkImageLayout new_layout);
        void                              BindVertexBuffer(Hardwares::VertexBuffer& buffer);
        void                              BindIndexBuffer(const Hardwares::IndexBuffer& buffer, VkIndexType type);
        void                              SetScissor(const VkRect2D& scissor);
        void                              PushConstants(VkShaderStageFlags stage_flags, uint32_t offset, uint32_t size, const void* data);

    private:
        std::atomic_uint8_t m_command_buffer_state{CommanBufferState::Idle};
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
        void                                                            Initialize(VulkanDevice* device, uint8_t swapchain_image_count = 3, int thread_count = 1);
        void                                                            Deinitialize();
        CommandBuffer*                                                  GetCommandBuffer(uint8_t frame_index, bool begin = true);
        CommandBuffer*                                                  GetInstantCommandBuffer(Rendering::QueueType type, uint8_t frame_index, bool begin = true);
        void                                                            EndInstantCommandBuffer(CommandBuffer* const buffer, VulkanDevice* const device, int wait_flag = -1);
        Rendering::Pools::CommandPool*                                  GetCommandPool(Rendering::QueueType type, uint8_t frame_index);
        int                                                             GetPoolFromIndex(Rendering::QueueType type, uint8_t index);
        void                                                            ResetPool(int frame_index);

        VulkanDevice*                                                   Device                  = nullptr;
        const int                                                       MaxBufferPerPool        = 4;
        Core::Containers::Array<ZRawPtr(Rendering::Pools::CommandPool)> CommandPools            = {};
        Core::Containers::Array<ZRawPtr(Rendering::Pools::CommandPool)> TransferCommandPools    = {};
        Core::Containers::Array<ZRawPtr(CommandBuffer)>                 CommandBuffers          = {};
        Core::Containers::Array<ZRawPtr(CommandBuffer)>                 TransferCommandBuffers  = {};
        int                                                             TotalCommandBufferCount = 0;

    private:
        int                     m_total_pool_count{0};
        std::condition_variable m_cond;
        std::atomic_bool        m_executing_instant_command{false};
        std::mutex              m_instant_command_mutex;
        ZRawPtr(Rendering::Primitives::Semaphore) m_instant_semaphore;
        ZRawPtr(Rendering::Primitives::Fence) m_instant_fence;
    };

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
     *  Device definition
     */
    struct VulkanDevice
    {
        bool                                                                                HasSeperateTransfertQueueFamily                = false;
        bool                                                                                PhysicalDeviceSupportSampledImageBindless = false;
        bool PhysicalDeviceSupportStorageBufferBindless = false;
        const char*                                                                         ApplicationName                                = "Tetragrama";
        const char*                                                                         EngineName                                     = "ZEngine";
        uint32_t                                                                            SwapchainImageCount                            = 3;
        uint32_t                                                                            SwapchainImageIndex                            = std::numeric_limits<uint8_t>::max();
        uint32_t                                                                            CurrentFrameIndex                              = std::numeric_limits<uint8_t>::max();
        uint32_t                                                                            PreviousFrameIndex                             = std::numeric_limits<uint8_t>::max();
        uint32_t                                                                            SwapchainImageWidth                            = std::numeric_limits<uint32_t>::max();
        uint32_t                                                                            SwapchainImageHeight                           = std::numeric_limits<uint32_t>::max();
        uint32_t                                                                            GraphicFamilyIndex                             = std::numeric_limits<uint32_t>::max();
        uint32_t                                                                            TransferFamilyIndex                            = std::numeric_limits<uint32_t>::max();
        uint32_t                                                                            EnqueuedCommandbufferIndex                     = 0;
        uint32_t                                                                            WriteDescriptorSetIndex                        = 0;
        VkInstance                                                                          Instance                                       = VK_NULL_HANDLE;
        VkSurfaceKHR                                                                        Surface                                        = VK_NULL_HANDLE;
        VkSurfaceFormatKHR                                                                  SurfaceFormat                                  = {};
        VkPresentModeKHR                                                                    PresentMode                                    = {};
        VkPhysicalDeviceProperties                                                          PhysicalDeviceProperties                       = {};
        VkPhysicalDeviceDescriptorIndexingProperties                                        PhysicalDeviceDescriptorIndexingProperties     = {};
        VkDevice                                                                            LogicalDevice                                  = VK_NULL_HANDLE;
        VkPhysicalDevice                                                                    PhysicalDevice                                 = VK_NULL_HANDLE;
        VkPhysicalDeviceFeatures2                                                           PhysicalDeviceFeature                          = {};
        VkPhysicalDeviceMemoryProperties                                                    PhysicalDeviceMemoryProperties                 = {};
        VkSwapchainKHR                                                                      SwapchainHandle                                = VK_NULL_HANDLE;
        VmaAllocator                                                                        VmaAllocatorValue                              = nullptr;
        Core::Containers::Array<VkFormat>                                                   DefaultDepthFormats                            = {};
        Rendering::Renderers::RenderPasses::Attachment*                                     SwapchainAttachment                            = {};
        Core::Containers::Array<VkImageView>                                                SwapchainImageViews                            = {};
        Core::Containers::Array<VkFramebuffer>                                              SwapchainFramebuffers                          = {};
        Core::Containers::Array<Rendering::Primitives::Semaphore*>                          SwapchainAcquiredSemaphores                    = {};
        Core::Containers::Array<Rendering::Primitives::Semaphore*>                          SwapchainRenderCompleteSemaphores              = {};
        Core::Containers::Array<Rendering::Primitives::Fence*>                              SwapchainSignalFences                          = {};
        Core::Containers::Array<CommandBuffer*>                                             EnqueuedCommandbuffers                         = {};
        Core::Containers::HashMap<const char*, Helpers::Handle<Rendering::Shaders::Shader>> ShaderCaches                                   = {};
        std::set<WriteDescriptorSetRequestKey>                                              WriteBindlessDescriptorSetRequests             = {};
        Rendering::Textures::TextureHandleManager                                           GlobalTextures                                 = {};
        Helpers::HandleManager<Image2DBuffer>                                               Image2DBufferManager                           = {};
        Helpers::ThreadSafeQueue<Rendering::Textures::TextureHandle>                        TextureHandleToUpdates                         = {};
        Helpers::ThreadSafeQueue<Rendering::Textures::TextureHandle>                        TextureHandleToDispose                         = {};
        Helpers::HandleManager<Rendering::Shaders::Shader>                                  ShaderManager                                  = {};
        Helpers::HandleManager<VertexBufferSet>                                             VertexBufferSetManager                         = {};
        Helpers::HandleManager<StorageBufferSet>                                            StorageBufferSetManager                        = {};
        Helpers::HandleManager<IndirectBufferSet>                                           IndirectBufferSetManager                       = {};
        Helpers::HandleManager<IndexBufferSet>                                              IndexBufferSetManager                          = {};
        Helpers::HandleManager<UniformBufferSet>                                            UniformBufferSetManager                        = {};
        Helpers::HandleManager<DirtyResource>                                               DirtyResources                                 = {};
        Helpers::HandleManager<BufferView>                                                  DirtyBuffers                                   = {};
        Helpers::HandleManager<BufferImage>                                                 DirtyBufferImages                              = {};
        std::atomic_bool                                                                    RunningDirtyCollector                          = true;
        std::atomic_uint                                                                    IdleFrameCount                                 = 0;
        std::atomic_uint                                                                    IdleFrameThreshold                             = SwapchainImageCount * 3 * 3;
        std::condition_variable                                                             DirtyCollectorCond                             = {};
        std::mutex                                                                          DirtyMutex                                     = {};
        std::mutex                                                                          Mutex                                          = {};
        Windows::CoreWindow*                                                                CurrentWindow                                  = nullptr;
        ZEngine::Core::Memory::ArenaAllocator*                                              Arena                                          = nullptr;
        AsyncResourceLoaderPtr                                                              AsyncResLoader                                 = nullptr;

        void                                                                                Initialize(ZEngine::Core::Memory::ArenaAllocator* arena, Windows::CoreWindow* const window);
        void                                                                                Deinitialize();
        void                                                                                Update();
        void                                                                                Dispose();
        bool                                                                                QueueSubmit(const VkPipelineStageFlags wait_stage_flag, CommandBuffer* const command_buffer, Rendering::Primitives::Semaphore* const signal_semaphore = nullptr, Rendering::Primitives::Fence* const fence = nullptr);
        void                                                                                EnqueueForDeletion(Rendering::DeviceResourceType resource_type, void* const resource_handle);
        void                                                                                EnqueueForDeletion(Rendering::DeviceResourceType resource_type, DirtyResource resource);
        void                                                                                EnqueueBufferForDeletion(BufferView& buffer);
        void                                                                                EnqueueBufferImageForDeletion(BufferImage& buffer);
        QueueView                                                                           GetQueue(Rendering::QueueType type);
        void                                                                                QueueWait(Rendering::QueueType type);
        void                                                                                QueueWaitAll();
        void                                                                                MapAndCopyToMemory(BufferView& buffer, size_t data_size, const void* data);
        BufferView                                                                          CreateBuffer(VkDeviceSize byte_size, VkBufferUsageFlags buffer_usage, VmaAllocationCreateFlags vma_create_flags = 0);
        void                                                                                CopyBuffer(const BufferView& source, const BufferView& destination, VkDeviceSize byte_size, VkDeviceSize src_buffer_offset = 0u, VkDeviceSize dst_buffer_offset = 0u);
        BufferImage                                                                         CreateImage(uint32_t width, uint32_t height, VkImageType image_type, VkImageViewType image_view_type, VkFormat image_format, VkImageTiling image_tiling, VkImageLayout image_initial_layout, VkImageUsageFlags image_usage, VkSharingMode image_sharing_mode, VkSampleCountFlagBits image_sample_count, VkMemoryPropertyFlags requested_properties, VkImageAspectFlagBits image_aspect_flag, uint32_t layer_count = 1U, VkImageCreateFlags image_create_flag_bit = 0);
        VkSampler                                                                           CreateImageSampler();
        VkFormat                                                                            FindSupportedFormat(Core::Containers::ArrayView<VkFormat> format_collection, VkImageTiling image_tiling, VkFormatFeatureFlags feature_flags);
        VkFormat                                                                            FindDepthFormat();
        VkImageView                                                                         CreateImageView(VkImage image, VkFormat image_format, VkImageViewType image_view_type, VkImageAspectFlagBits image_aspect_flag, uint32_t layer_count = 1U);
        VkFramebuffer                                                                       CreateFramebuffer(Core::Containers::ArrayView<VkImageView> attachments, const VkRenderPass& render_pass, uint32_t width, uint32_t height, uint32_t layer_number = 1);
        VertexBufferSetHandle                                                               CreateVertexBufferSet();
        StorageBufferSetHandle                                                              CreateStorageBufferSet();
        IndirectBufferSetHandle                                                             CreateIndirectBufferSet();
        IndexBufferSetHandle                                                                CreateIndexBufferSet();
        UniformBufferSetHandle                                                              CreateUniformBufferSet();
        void                                                                                CreateSwapchain();
        void                                                                                ResizeSwapchain();
        void                                                                                DisposeSwapchain();
        void                                                                                NewFrame();
        void                                                                                Present();
        void                                                                                IncrementFrameImageCount();
        CommandBuffer*                                                                      GetCommandBuffer(bool begin = true);
        CommandBuffer*                                                                      GetInstantCommandBuffer(Rendering::QueueType type, bool begin = true);
        void                                                                                EnqueueInstantCommandBuffer(CommandBuffer* const buffer, int wait_flag = -1);
        void                                                                                EnqueueCommandBuffer(CommandBuffer* const buffer);
        void                                                                                DirtyCollector();

        Helpers::Handle<Rendering::Shaders::Shader>                                         CompileShader(Rendering::Specifications::ShaderSpecification& spec);

        Rendering::Textures::TextureHandle                                                  CreateTexture(uint32_t width, uint32_t height);
        Rendering::Textures::TextureHandle                                                  CreateTexture(uint32_t width, uint32_t height, float r = 255, float g = 255, float b = 255, float a = 255);
        Rendering::Textures::TextureHandle                                                  CreateTexture(const Rendering::Specifications::TextureSpecification& spec);
        void                                                                                WriteTextureData(CommandBufferPtr command_buf, const Rendering::Textures::TextureHandle& handle, const void* data);

        Rendering::Renderers::RenderPasses::RenderPass*                                     CreateRenderPass(const Rendering::Specifications::RenderPassSpecification& spec);

    private:
        VulkanLayer                                              m_layer          = {};
        CommandBufferManager                                     m_buffer_manager = {};
        Core::Containers::HashMap<Rendering::QueueType, VkQueue> m_queue_map      = {};
        VkDebugUtilsMessengerEXT                                 m_debug_messenger{VK_NULL_HANDLE};
        PFN_vkCreateDebugUtilsMessengerEXT                       __createDebugMessengerPtr{VK_NULL_HANDLE};
        PFN_vkDestroyDebugUtilsMessengerEXT                      __destroyDebugMessengerPtr{VK_NULL_HANDLE};
        void                                                     __cleanupDirtyResource();
        void                                                     __cleanupBufferDirtyResource();
        void                                                     __cleanupBufferImageDirtyResource();
        static VKAPI_ATTR VkBool32 VKAPI_CALL                    __debugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity, VkDebugUtilsMessageTypeFlagsEXT messageType, const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData, void* pUserData);
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
