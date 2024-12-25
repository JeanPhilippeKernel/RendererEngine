#pragma once
#include <Helpers/IntrusivePtr.h>
#include <ZEngineDef.h>
#include <vk_mem_alloc.h>
#include <vulkan/vulkan.h>
#include <limits>
#include <span>
#include <type_traits>
#include <vector>

namespace ZEngine::Hardwares
{
    struct VulkanDevice;
} // namespace ZEngine::Hardwares

namespace ZEngine::Rendering::Buffers
{
    struct BufferView
    {
        uint8_t       FrameIndex{std::numeric_limits<uint8_t>::max()};
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

    class StorageBuffer;
    class VertexBuffer;
    class IndexBuffer;

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

} // namespace ZEngine::Rendering::Buffers
