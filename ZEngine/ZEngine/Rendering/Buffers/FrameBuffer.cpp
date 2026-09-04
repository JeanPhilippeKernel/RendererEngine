#include <ZEngine/Hardwares/VulkanDevice.h>
#include <ZEngine/Rendering/Buffers/Framebuffer.h>

using namespace ZEngine::Helpers;
using namespace ZEngine::Rendering::Specifications;

namespace ZEngine::Rendering::Buffers
{
    FramebufferVNext::FramebufferVNext(Hardwares::VulkanDevice* device, Specifications::FrameBufferSpecificationVNext&& specification) : m_device(device), m_specification(std::move(specification))
    {
        Create();
    }

    FramebufferVNext::FramebufferVNext(Hardwares::VulkanDevice* device) : m_device(device) {}

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
            auto img_buf  = m_device->ImageBufferManager.Access(resource->BufferHandle);
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

    void FramebufferVNext::Reset(VkFramebuffer handle, uint32_t width, uint32_t height)
    {
        Handle                 = handle;
        m_specification.Width  = width;
        m_specification.Height = height;
    }

    void FramebufferVNext::Dispose()
    {
        if (Handle)
        {
            // Deferred: Dispose() is called during resize while the render thread
            // may still be recording commands that reference this framebuffer.
            // DeferFree queues the handle for destruction at the next safe drain
            // in RRM::EndFrame or the final VulkanDevice::Dispose drain.
            Hardwares::DeferredFreeEntry e = {};
            e.EntryKind                    = Hardwares::DeferredFreeEntry::Kind::VkHandle;
            e.Data.Vk                      = {Handle, Rendering::DeviceResourceType::FRAMEBUFFER, nullptr};
            m_device->DeferFree(e);
            Handle = VK_NULL_HANDLE;
        }
    }
} // namespace ZEngine::Rendering::Buffers
