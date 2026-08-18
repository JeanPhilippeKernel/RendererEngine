#include <ZEngine/Hardwares/VulkanDevice.h>
#include <ZEngine/Rendering/Buffers/Framebuffer.h>

using namespace ZEngine::Helpers;
using namespace ZEngine::Rendering::Specifications;

namespace ZEngine::Rendering::Buffers
{
    FramebufferVNext::FramebufferVNext(Hardwares::VulkanDevice* device, const Specifications::FrameBufferSpecificationVNext& specification) : m_device(device), m_specification(specification)
    {
        Create();
    }

    FramebufferVNext::FramebufferVNext(Hardwares::VulkanDevice* device, Specifications::FrameBufferSpecificationVNext&& specification) : m_device(device), m_specification(std::move(specification))
    {
        Create();
    }

    FramebufferVNext::~FramebufferVNext()
    {
        Dispose();
    }

    uint32_t FramebufferVNext::GetWidth() const
    {
        return m_specification.Width;
    }

    uint32_t FramebufferVNext::GetHeight() const
    {
        return m_specification.Height;
    }

    FrameBufferSpecificationVNext& FramebufferVNext::GetSpecification()
    {
        return m_specification;
    }

    const FrameBufferSpecificationVNext& FramebufferVNext::GetSpecification() const
    {
        return m_specification;
    }

    void FramebufferVNext::Create()
    {
        auto                                 scratch = ZGetScratch(m_device->Arena);

        size_t                               count   = m_specification.RenderTargets.size();
        Core::Containers::Array<VkImageView> views   = {};
        views.init(scratch.Arena, count, count);

        for (int i = 0; i < count; ++i)
        {
            auto index    = m_specification.RenderTargets[i];
            auto handle   = m_device->GlobalTextures.ToHandle(index);

            auto resource = m_device->GlobalTextures.Access(handle);
            auto img_buf  = m_device->Image2DBufferManager.Access(resource->BufferHandle);
            views[i]      = img_buf->GetImageViewHandle();
        }
        Handle = m_device->CreateFramebuffer(views, m_specification.Attachment->GetHandle(), m_specification.Width, m_specification.Height, m_specification.Layers);

        ZReleaseScratch(scratch);
    }

    void FramebufferVNext::Resize(uint32_t width, uint32_t height)
    {
        m_specification.Width  = width;
        m_specification.Height = height;
        Dispose();
        Create();
    }

    void FramebufferVNext::Dispose()
    {
        if (Handle)
        {
            // Direct destroy: Dispose() is always called at GPU-idle points
            // (after QueueWaitAll at shutdown, after vkDeviceWaitIdle at resize).
            vkDestroyFramebuffer(m_device->LogicalDevice, Handle, nullptr);
            Handle = VK_NULL_HANDLE;
        }
    }
} // namespace ZEngine::Rendering::Buffers
