#pragma once
#include <vk_mem_alloc.h>
#include <vulkan/vulkan.h>

/*
 * ^^^^ Headers above are not candidates for sorting by clang-format ^^^^^
 */
#include <Hardwares/VulkanLayer.h>
#include <Helpers/HandleManager.h>
#include <Helpers/MemoryOperations.h>
#include <Helpers/ThreadSafeQueue.h>
#include <Primitives/Fence.h>
#include <Primitives/Semaphore.h>
#include <Rendering/Pools/CommandPool.h>
#include <Rendering/Primitives/ImageMemoryBarrier.h>
#include <Rendering/ResourceTypes.h>
#include <Rendering/Specifications/TextureSpecification.h>
#include <Rendering/Textures/Texture.h>
#include <map>
#include <vector>

namespace ZEngine::Windows
{
    class CoreWindow;
}

namespace ZEngine::Rendering::Renderers::RenderPasses
{
    struct RenderPass;
    struct Attachment;
} // namespace ZEngine::Rendering::Renderers::RenderPasses

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
    /*
     * GPU Device
     */
    struct VulkanDevice;

    struct BufferView
    {
        uint8_t       FrameIndex = std::numeric_limits<uint8_t>::max();
        VkBuffer      Handle{VK_NULL_HANDLE};
        VmaAllocation Allocation{nullptr};

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

    struct IGraphicBuffer : public Helpers::RefCounted
    {
        IGraphicBuffer()
        {
            m_last_byte_size = m_byte_size;
        }

        IGraphicBuffer(Hardwares::VulkanDevice* device) : m_device(device)
        {
            m_last_byte_size = m_byte_size;
        }

        virtual ~IGraphicBuffer() = default;

        virtual inline size_t GetByteSize() const
        {
            return m_byte_size;
        }

        virtual bool HasBeenResized() const
        {
            return m_last_byte_size != m_byte_size;
        }

        virtual void* GetNativeBufferHandle() const = 0;

    protected:
        size_t                   m_byte_size{0};
        size_t                   m_last_byte_size{0};
        Hardwares::VulkanDevice* m_device{nullptr};
    };

    template <typename T, typename = std::enable_if_t<std::is_base_of_v<IGraphicBuffer, T>>>
    struct IBufferSet : public Helpers::RefCounted
    {
        IBufferSet(Hardwares::VulkanDevice* device, uint32_t count = 0)
        {
            for (int i = 0; i < count; ++i)
            {
                m_set.emplace_back(device);
            }
        }

        ~IBufferSet()
        {
            Dispose();
        }

        T& operator[](uint32_t index)
        {
            ZENGINE_VALIDATE_ASSERT(index < m_set.size(), "Index out of range")
            return m_set[index];
        }

        T& At(uint32_t index)
        {
            ZENGINE_VALIDATE_ASSERT(index < m_set.size(), "Index out of range")
            return m_set[index];
        }

        template <typename K>
        void SetData(uint32_t index, std::span<const K> data)
        {
            ZENGINE_VALIDATE_ASSERT(index < m_set.size(), "Index out of range")

            if (std::is_same_v<T, IndexBuffer> || std::is_same_v<T, VertexBuffer> || std::is_same_v<T, StorageBuffer>)
            {
                m_set[index].template SetData<K>(data);
            }
        }

        const std::vector<T>& Data() const
        {
            return m_set;
        }

        std::vector<T>& Data()
        {
            return m_set;
        }

        void Dispose() {}

    protected:
        std::vector<T> m_set;
    };

    class VertexBuffer : public IGraphicBuffer
    {
    public:
        explicit VertexBuffer(Hardwares::VulkanDevice* device) : IGraphicBuffer(device) {}

        void SetData(const void* data, size_t byte_size);

        template <typename T>
        inline void SetData(std::span<const T> content)
        {
            SetData(content.data(), content.size_bytes());
        }

        ~VertexBuffer()
        {
            CleanUpMemory();
        }

        void* GetNativeBufferHandle() const override
        {
            return reinterpret_cast<void*>(m_vertex_buffer.Handle);
        }

        const VkDescriptorBufferInfo& GetDescriptorBufferInfo()
        {
            m_buffer_info = VkDescriptorBufferInfo{.buffer = m_vertex_buffer.Handle, .offset = 0, .range = this->m_byte_size};
            return m_buffer_info;
        }

        void Dispose()
        {
            CleanUpMemory();
        }

    private:
        void CleanUpMemory();

    private:
        BufferView             m_vertex_buffer;
        VkDescriptorBufferInfo m_buffer_info{};
    };

    using VertexBufferSet       = IBufferSet<VertexBuffer>;
    using VertexBufferSetRef    = Helpers::Ref<VertexBufferSet>;
    using VertexBufferSetHandle = Helpers::Handle<VertexBufferSetRef>;

    template <>
    inline void VertexBufferSet::Dispose()
    {
        for (auto& buffer : m_set)
        {
            buffer.Dispose();
        }
    }

    class StorageBuffer : public IGraphicBuffer
    {
    public:
        explicit StorageBuffer(Hardwares::VulkanDevice* device) : IGraphicBuffer(device) {}

        void SetData(const void* data, uint32_t offset, size_t byte_size);

        template <typename T>
        inline void SetData(std::span<const T> content)
        {
            SetData(content.data(), 0, content.size_bytes());
        }

        ~StorageBuffer()
        {
            CleanUpMemory();
        }

        void* GetNativeBufferHandle() const
        {
            return reinterpret_cast<void*>(m_storage_buffer.Handle);
        }

        const VkDescriptorBufferInfo& GetDescriptorBufferInfo()
        {
            m_buffer_info = VkDescriptorBufferInfo{.buffer = m_storage_buffer.Handle, .offset = 0, .range = this->m_byte_size};
            return m_buffer_info;
        }

        void Dispose()
        {
            CleanUpMemory();
        }

    private:
        void CleanUpMemory();

    private:
        BufferView             m_storage_buffer;
        VkDescriptorBufferInfo m_buffer_info{};
    };

    using StorageBufferSet       = IBufferSet<StorageBuffer>;
    using StorageBufferSetRef    = Helpers::Ref<StorageBufferSet>;
    using StorageBufferSetHandle = Helpers::Handle<StorageBufferSetRef>;

    template <>
    inline void StorageBufferSet::Dispose()
    {
        for (auto& buffer : m_set)
        {
            buffer.Dispose();
        }
    }

    class IndexBuffer : public IGraphicBuffer
    {
    public:
        IndexBuffer(Hardwares::VulkanDevice* device) : IGraphicBuffer(device) {}

        void SetData(const void* data, size_t byte_size);

        template <typename T>
        inline void SetData(std::span<const T> content)
        {
            SetData(content.data(), content.size_bytes());
        }

        ~IndexBuffer()
        {
            CleanUpMemory();
        }

        void* GetNativeBufferHandle() const override
        {
            return reinterpret_cast<void*>(m_index_buffer.Handle);
        }

        const VkDescriptorBufferInfo& GetDescriptorBufferInfo()
        {
            m_buffer_info = VkDescriptorBufferInfo{.buffer = m_index_buffer.Handle, .offset = 0, .range = this->m_byte_size};
            return m_buffer_info;
        }

        void Dispose()
        {
            CleanUpMemory();
        }

    private:
        void CleanUpMemory();

    private:
        BufferView             m_index_buffer;
        VkDescriptorBufferInfo m_buffer_info{};
    };

    using IndexBufferSet       = IBufferSet<IndexBuffer>;
    using IndexBufferSetRef    = Helpers::Ref<IndexBufferSet>;
    using IndexBufferSetHandle = Helpers::Handle<IndexBufferSetRef>;

    template <>
    inline void IndexBufferSet::Dispose()
    {
        for (auto& buffer : m_set)
        {
            buffer.Dispose();
        }
    }

    class IndirectBuffer : public IGraphicBuffer
    {
    public:
        explicit IndirectBuffer(Hardwares::VulkanDevice* device) : IGraphicBuffer(device) {}

        void SetData(const VkDrawIndirectCommand* data, size_t byte_size);

        template <typename T>
        inline void SetData(std::span<const T> content)
        {
            SetData(content.data(), content.size_bytes());
        }

        uint32_t GetCommandCount() const
        {
            return m_command_count;
        }

        ~IndirectBuffer()
        {
            CleanUpMemory();
        }

        void* GetNativeBufferHandle() const override
        {
            return reinterpret_cast<void*>(m_indirect_buffer.Handle);
        }

        void Dispose()
        {
            CleanUpMemory();
        }

    private:
        void CleanUpMemory();

    private:
        uint32_t   m_command_count{0};
        BufferView m_indirect_buffer;
    };

    using IndirectBufferSet       = IBufferSet<IndirectBuffer>;
    using IndirectBufferSetRef    = Helpers::Ref<IndirectBufferSet>;
    using IndirectBufferSetHandle = Helpers::Handle<IndirectBufferSetRef>;

    template <>
    template <>
    inline void IndirectBufferSet::SetData<VkDrawIndirectCommand>(uint32_t index, std::span<const VkDrawIndirectCommand> data)
    {
        ZENGINE_VALIDATE_ASSERT(index < m_set.size(), "Index out of range")
        m_set[index].SetData(data);
    }

    template <>
    inline void IndirectBufferSet::Dispose()
    {
        for (auto& buffer : m_set)
        {
            buffer.Dispose();
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
            this->m_device    = rhs.m_device;
            this->m_byte_size = rhs.m_byte_size;

            std::swap(this->m_uniform_buffer, rhs.m_uniform_buffer);
            std::swap(this->m_uniform_buffer_mapped, rhs.m_uniform_buffer_mapped);

            rhs.m_byte_size             = 0;
            rhs.m_uniform_buffer_mapped = false;
            rhs.m_uniform_buffer        = {};
        }

        explicit UniformBuffer(UniformBuffer&& rhs) noexcept
        {
            this->m_device    = rhs.m_device;
            this->m_byte_size = rhs.m_byte_size;

            std::swap(this->m_uniform_buffer, rhs.m_uniform_buffer);
            std::swap(this->m_uniform_buffer_mapped, rhs.m_uniform_buffer_mapped);

            rhs.m_byte_size             = 0;
            rhs.m_uniform_buffer_mapped = false;
            rhs.m_uniform_buffer        = {};
        }

        UniformBuffer& operator=(const UniformBuffer& rhs) = delete;

        UniformBuffer& operator=(UniformBuffer& rhs)
        {
            if (this == &rhs)
            {
                return *this;
            }

            this->m_byte_size = rhs.m_byte_size;

            std::swap(this->m_uniform_buffer, rhs.m_uniform_buffer);
            std::swap(this->m_uniform_buffer_mapped, rhs.m_uniform_buffer_mapped);

            rhs.m_byte_size             = 0;
            rhs.m_uniform_buffer_mapped = false;
            rhs.m_uniform_buffer        = {};

            return *this;
        }

        UniformBuffer& operator=(UniformBuffer&& rhs) noexcept
        {
            if (this == &rhs)
            {
                return *this;
            }

            this->m_byte_size = rhs.m_byte_size;

            std::swap(this->m_uniform_buffer, rhs.m_uniform_buffer);
            std::swap(this->m_uniform_buffer_mapped, rhs.m_uniform_buffer_mapped);

            rhs.m_byte_size             = 0;
            rhs.m_uniform_buffer_mapped = false;
            rhs.m_uniform_buffer        = {};

            return *this;
        }

        void SetData(const void* data, size_t byte_size);

        template <typename T>
        inline void SetData(const std::vector<T>& content)
        {
            size_t byte_size = sizeof(T) * content.size();
            this->SetData(content.data(), byte_size);
        }

        void* GetNativeBufferHandle() const override
        {
            return reinterpret_cast<void*>(m_uniform_buffer.Handle);
        }

        void Dispose()
        {
            CleanUpMemory();
        }

        ~UniformBuffer()
        {
            CleanUpMemory();
        }

        const VkDescriptorBufferInfo& GetDescriptorBufferInfo()
        {
            m_buffer_info = VkDescriptorBufferInfo{.buffer = m_uniform_buffer.Handle, .offset = 0, .range = m_byte_size};
            return m_buffer_info;
        }

    private:
        void CleanUpMemory();

    private:
        bool                   m_uniform_buffer_mapped{false};
        BufferView             m_uniform_buffer{};
        VkDescriptorBufferInfo m_buffer_info{};
    };

    using UniformBufferSet       = IBufferSet<UniformBuffer>;
    using UniformBufferSetRef    = Helpers::Ref<UniformBufferSet>;
    using UniformBufferSetHandle = Helpers::Handle<UniformBufferSetRef>;

    template <>
    inline void UniformBufferSet::Dispose()
    {
        for (auto& buffer : m_set)
        {
            buffer.Dispose();
        }
    }

    struct Image2DBuffer : public Helpers::RefCounted
    {
        Image2DBuffer(VulkanDevice* device, const Rendering::Specifications::Image2DBufferSpecification& spec);
        ~Image2DBuffer();

        BufferImage&           GetBuffer();
        const BufferImage&     GetBuffer() const;
        VkImageView            GetImageViewHandle() const;
        VkImage                GetHandle() const;
        VkSampler              GetSampler() const;
        void                   Dispose();
        VkDescriptorImageInfo& GetDescriptorImageInfo();

    private:
        uint32_t              m_width{1};
        uint32_t              m_height{1};
        BufferImage           m_buffer_image;
        VkDescriptorImageInfo m_image_info;
        VulkanDevice*         m_device{nullptr};
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

    struct CommandBuffer : public Helpers::RefCounted
    {
        CommandBuffer(Hardwares::VulkanDevice* device, VkCommandPool command_pool, Rendering::QueueType type, bool one_time);
        ~CommandBuffer();

        Rendering::QueueType              QueueType;
        Hardwares::VulkanDevice*          Device = nullptr;

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
        void                              SetSignalFence(const Helpers::Ref<Rendering::Primitives::Fence>& semaphore);
        void                              SetSignalSemaphore(const Helpers::Ref<Rendering::Primitives::Semaphore>& semaphore);
        Rendering::Primitives::Semaphore* GetSignalSemaphore() const;
        Rendering::Primitives::Fence*     GetSignalFence();
        void                              ClearColor(float r, float g, float b, float a);
        void                              ClearDepth(float depth_color, uint32_t stencil);
        void                              BeginRenderPass(const Helpers::Ref<Rendering::Renderers::RenderPasses::RenderPass>&, VkFramebuffer framebuffer);
        void                              EndRenderPass();
        void                              BindDescriptorSets(uint32_t frame_index = 0);
        void                              BindDescriptorSet(const VkDescriptorSet& descriptor);
        void                              DrawIndirect(const Hardwares::IndirectBuffer& buffer);
        void                              DrawIndexedIndirect(const Hardwares::IndirectBuffer& buffer, uint32_t count);
        void                              DrawIndexed(uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex, int32_t vertexOffset, uint32_t firstInstance);
        void                              Draw(uint32_t vertex_count, uint32_t instance_count, uint32_t first_index, uint32_t first_instance);
        void                              TransitionImageLayout(const Rendering::Primitives::ImageMemoryBarrier& image_barrier);
        void                              CopyBufferToImage(const Hardwares::BufferView& source, Hardwares::BufferImage& destination, uint32_t width, uint32_t height, uint32_t layer_count, VkImageLayout new_layout);
        void                              BindVertexBuffer(const Hardwares::VertexBuffer& buffer);
        void                              BindIndexBuffer(const Hardwares::IndexBuffer& buffer, VkIndexType type);
        void                              SetScissor(const VkRect2D& scissor);
        void                              PushConstants(VkShaderStageFlags stage_flags, uint32_t offset, uint32_t size, const void* data);

    private:
        std::atomic_uint8_t                                              m_command_buffer_state{CommanBufferState::Idle};
        VkCommandBuffer                                                  m_command_buffer{VK_NULL_HANDLE};
        VkCommandPool                                                    m_command_pool{VK_NULL_HANDLE};
        VkClearValue                                                     m_clear_value[2] = {0};
        Helpers::Ref<Rendering::Primitives::Fence>                       m_signal_fence;
        Helpers::Ref<Rendering::Primitives::Semaphore>                   m_signal_semaphore;
        Helpers::WeakRef<Rendering::Renderers::RenderPasses::RenderPass> m_active_render_pass;
    };

    struct CommandBufferManager
    {
        void                                                     Initialize(VulkanDevice* device, uint8_t swapchain_image_count = 3, int thread_count = 1);
        void                                                     Deinitialize();
        CommandBuffer*                                           GetCommandBuffer(uint8_t frame_index, bool begin = true);
        CommandBuffer*                                           GetInstantCommandBuffer(Rendering::QueueType type, uint8_t frame_index, bool begin = true);
        void                                                     EndInstantCommandBuffer(CommandBuffer* const buffer, VulkanDevice* const device, int wait_flag = 0);
        Rendering::Pools::CommandPool*                           GetCommandPool(Rendering::QueueType type, uint8_t frame_index);
        int                                                      GetPoolFromIndex(Rendering::QueueType type, uint8_t index);
        void                                                     ResetPool(int frame_index);

        VulkanDevice*                                            Device                  = nullptr;
        const int                                                MaxBufferPerPool        = 4;
        std::vector<Helpers::Ref<Rendering::Pools::CommandPool>> CommandPools            = {};
        std::vector<Helpers::Ref<Rendering::Pools::CommandPool>> TransferCommandPools    = {};
        std::vector<Helpers::Ref<CommandBuffer>>                 CommandBuffers          = {};
        std::vector<Helpers::Ref<CommandBuffer>>                 TransferCommandBuffers  = {};
        int                                                      TotalCommandBufferCount = 0;

    private:
        int                                            m_total_pool_count{0};
        std::condition_variable                        m_cond;
        std::atomic_bool                               m_executing_instant_command{false};
        std::mutex                                     m_instant_command_mutex;
        Helpers::Ref<Rendering::Primitives::Semaphore> m_instant_semaphore;
        Helpers::Ref<Rendering::Primitives::Fence>     m_instant_fence;
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
        bool                                                         HasSeperateTransfertQueueFamily    = false;
        uint32_t                                                     SwapchainImageIndex                = std::numeric_limits<uint8_t>::max();
        uint32_t                                                     CurrentFrameIndex                  = std::numeric_limits<uint8_t>::max();
        uint32_t                                                     PreviousFrameIndex                 = std::numeric_limits<uint8_t>::max();
        uint32_t                                                     SwapchainImageCount                = 3;
        uint32_t                                                     SwapchainImageWidth                = std::numeric_limits<uint32_t>::max();
        uint32_t                                                     SwapchainImageHeight               = std::numeric_limits<uint32_t>::max();
        uint32_t                                                     GraphicFamilyIndex                 = std::numeric_limits<uint32_t>::max();
        uint32_t                                                     TransferFamilyIndex                = std::numeric_limits<uint32_t>::max();
        uint32_t                                                     EnqueuedCommandbufferIndex         = 0;
        uint32_t                                                     WriteDescriptorSetIndex            = 0;
        VkInstance                                                   Instance                           = VK_NULL_HANDLE;
        VkSurfaceKHR                                                 Surface                            = VK_NULL_HANDLE;
        VkSurfaceFormatKHR                                           SurfaceFormat                      = {};
        VkPresentModeKHR                                             PresentMode                        = {};
        VkPhysicalDeviceProperties                                   PhysicalDeviceProperties           = {};
        VkDevice                                                     LogicalDevice                      = VK_NULL_HANDLE;
        VkPhysicalDevice                                             PhysicalDevice                     = VK_NULL_HANDLE;
        VkPhysicalDeviceFeatures                                     PhysicalDeviceFeature              = {};
        VkPhysicalDeviceMemoryProperties                             PhysicalDeviceMemoryProperties     = {};
        VkSwapchainKHR                                               SwapchainHandle                    = VK_NULL_HANDLE;
        VmaAllocator                                                 VmaAllocator                       = nullptr;
        const std::string_view                                       ApplicationName                    = "Tetragrama";
        const std::string_view                                       EngineName                         = "ZEngine";
        Helpers::Ref<Rendering::Renderers::RenderPasses::Attachment> SwapchainAttachment                = {};
        std::vector<VkImageView>                                     SwapchainImageViews                = {};
        std::vector<VkFramebuffer>                                   SwapchainFramebuffers              = {};
        std::vector<Helpers::Ref<Rendering::Primitives::Semaphore>>  SwapchainAcquiredSemaphores        = {};
        std::vector<Helpers::Ref<Rendering::Primitives::Semaphore>>  SwapchainRenderCompleteSemaphores  = {};
        std::vector<Helpers::Ref<Rendering::Primitives::Fence>>      SwapchainSignalFences              = {};
        std::vector<CommandBuffer*>                                  EnqueuedCommandbuffers             = {};
        std::set<WriteDescriptorSetRequestKey>                       WriteBindlessDescriptorSetRequests = {};
        Helpers::Ref<Rendering::Textures::TextureHandleManager>      GlobalTextures                     = Helpers::CreateRef<Rendering::Textures::TextureHandleManager>(600);
        Helpers::ThreadSafeQueue<Rendering::Textures::TextureHandle> TextureHandleToUpdates             = {};
        Helpers::HandleManager<VertexBufferSetRef>                   VertexBufferSetManager             = {300};
        Helpers::HandleManager<StorageBufferSetRef>                  StorageBufferSetManager            = {300};
        Helpers::HandleManager<IndirectBufferSetRef>                 IndirectBufferSetManager           = {300};
        Helpers::HandleManager<IndexBufferSetRef>                    IndexBufferSetManager              = {300};
        Helpers::HandleManager<UniformBufferSetRef>                  UniformBufferSetManager            = {300};
        std::atomic_bool                                             RunningDirtyCollector              = true;
        std::atomic_uint                                             IdleFrameCount                     = 0;
        std::atomic_uint                                             IdleFrameThreshold                 = SwapchainImageCount * 3;
        std::condition_variable                                      DirtyCollectorCond                 = {};
        std::mutex                                                   DirtyMutex                         = {};
        Windows::CoreWindow*                                         CurrentWindow                      = nullptr;

        void                                                         Initialize(const Helpers::Ref<Windows::CoreWindow>& window);
        void                                                         Deinitialize();
        void                                                         Update();
        void                                                         Dispose();
        bool                                                         QueueSubmit(const VkPipelineStageFlags wait_stage_flag, CommandBuffer* const command_buffer, Rendering::Primitives::Semaphore* const signal_semaphore = nullptr, Rendering::Primitives::Fence* const fence = nullptr);
        void                                                         EnqueueForDeletion(Rendering::DeviceResourceType resource_type, void* const resource_handle);
        void                                                         EnqueueForDeletion(Rendering::DeviceResourceType resource_type, DirtyResource resource);
        void                                                         EnqueueBufferForDeletion(BufferView& buffer);
        void                                                         EnqueueBufferImageForDeletion(BufferImage& buffer);
        QueueView                                                    GetQueue(Rendering::QueueType type);
        void                                                         QueueWait(Rendering::QueueType type);
        void                                                         QueueWaitAll();
        void                                                         MapAndCopyToMemory(BufferView& buffer, size_t data_size, const void* data);
        BufferView                                                   CreateBuffer(VkDeviceSize byte_size, VkBufferUsageFlags buffer_usage, VmaAllocationCreateFlags vma_create_flags = 0);
        void                                                         CopyBuffer(const BufferView& source, const BufferView& destination, VkDeviceSize byte_size);
        BufferImage                                                  CreateImage(uint32_t width, uint32_t height, VkImageType image_type, VkImageViewType image_view_type, VkFormat image_format, VkImageTiling image_tiling, VkImageLayout image_initial_layout, VkImageUsageFlags image_usage, VkSharingMode image_sharing_mode, VkSampleCountFlagBits image_sample_count, VkMemoryPropertyFlags requested_properties, VkImageAspectFlagBits image_aspect_flag, uint32_t layer_count = 1U, VkImageCreateFlags image_create_flag_bit = 0);
        VkSampler                                                    CreateImageSampler();
        VkFormat                                                     FindSupportedFormat(const std::vector<VkFormat>& format_collection, VkImageTiling image_tiling, VkFormatFeatureFlags feature_flags);
        VkFormat                                                     FindDepthFormat();
        VkImageView                                                  CreateImageView(VkImage image, VkFormat image_format, VkImageViewType image_view_type, VkImageAspectFlagBits image_aspect_flag, uint32_t layer_count = 1U);
        VkFramebuffer                                                CreateFramebuffer(const std::vector<VkImageView>& attachments, const VkRenderPass& render_pass, uint32_t width, uint32_t height, uint32_t layer_number = 1);
        VertexBufferSetHandle                                        CreateVertexBufferSet();
        StorageBufferSetHandle                                       CreateStorageBufferSet();
        IndirectBufferSetHandle                                      CreateIndirectBufferSet();
        IndexBufferSetHandle                                         CreateIndexBufferSet();
        UniformBufferSetHandle                                       CreateUniformBufferSet();
        void                                                         CreateSwapchain();
        void                                                         ResizeSwapchain();
        void                                                         DisposeSwapchain();
        void                                                         NewFrame();
        void                                                         Present();
        void                                                         IncrementFrameImageCount();
        CommandBuffer*                                               GetCommandBuffer(bool begin = true);
        CommandBuffer*                                               GetInstantCommandBuffer(Rendering::QueueType type, bool begin = true);
        void                                                         EnqueueInstantCommandBuffer(CommandBuffer* const buffer, int wait_flag = 0);
        void                                                         EnqueueCommandBuffer(CommandBuffer* const buffer);
        void                                                         DirtyCollector();

    private:
        VulkanLayer                             m_layer{};
        CommandBufferManager                    m_buffer_manager{};
        std::map<Rendering::QueueType, VkQueue> m_queue_map{};
        Helpers::HandleManager<DirtyResource>   m_dirty_resources{300};
        Helpers::HandleManager<BufferView>      m_dirty_buffers{500};
        Helpers::HandleManager<BufferImage>     m_dirty_buffer_images{500};
        VkDebugUtilsMessengerEXT                m_debug_messenger{VK_NULL_HANDLE};
        PFN_vkCreateDebugUtilsMessengerEXT      __createDebugMessengerPtr{VK_NULL_HANDLE};
        PFN_vkDestroyDebugUtilsMessengerEXT     __destroyDebugMessengerPtr{VK_NULL_HANDLE};
        void                                    __cleanupDirtyResource();
        void                                    __cleanupBufferDirtyResource();
        void                                    __cleanupBufferImageDirtyResource();
        static VKAPI_ATTR VkBool32 VKAPI_CALL   __debugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity, VkDebugUtilsMessageTypeFlagsEXT messageType, const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData, void* pUserData);
    };
} // namespace ZEngine::Hardwares

namespace ZEngine::Helpers
{
    template <>
    inline void HandleManager<Hardwares::VertexBufferSetRef>::Dispose()
    {
        for (size_t i = 0; i < m_count; ++i)
        {
            if (m_data[i].Data)
            {
                m_data[i].Data->Dispose();
            }
        }
    }

    template <>
    inline void HandleManager<Hardwares::StorageBufferSetRef>::Dispose()
    {
        for (size_t i = 0; i < m_count; ++i)
        {
            if (m_data[i].Data)
            {
                m_data[i].Data->Dispose();
            }
        }
    }

    template <>
    inline void HandleManager<Hardwares::IndirectBufferSetRef>::Dispose()
    {
        for (size_t i = 0; i < m_count; ++i)
        {
            if (m_data[i].Data)
            {
                m_data[i].Data->Dispose();
            }
        }
    }

    template <>
    inline void HandleManager<Hardwares::IndexBufferSetRef>::Dispose()
    {
        for (size_t i = 0; i < m_count; ++i)
        {
            if (m_data[i].Data)
            {
                m_data[i].Data->Dispose();
            }
        }
    }

    template <>
    inline void HandleManager<Hardwares::UniformBufferSetRef>::Dispose()
    {
        for (size_t i = 0; i < m_count; ++i)
        {
            if (m_data[i].Data)
            {
                m_data[i].Data->Dispose();
            }
        }
    }
} // namespace ZEngine::Helpers
