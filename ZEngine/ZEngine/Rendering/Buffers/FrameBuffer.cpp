#include <pch.h>
#include <Hardwares/VulkanDevice.h>
#include <Rendering/Buffers/Framebuffer.h>

using namespace ZEngine::Helpers;
using namespace ZEngine::Rendering::Specifications;

namespace ZEngine::Rendering::Buffers
{
    FramebufferVNext::FramebufferVNext(Hardwares::VulkanDevice* device, const Specifications::FrameBufferSpecificationVNext& specification)
        : m_device(device), m_specification(specification)
    {
        Create();
    }

    FramebufferVNext::FramebufferVNext(Hardwares::VulkanDevice* device, Specifications::FrameBufferSpecificationVNext&& specification)
        : m_device(device), m_specification(std::move(specification))
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
        std::vector<VkImageView> views = {};
        views.resize(m_specification.RenderTargets.size());

        for (int i = 0; i < views.size(); ++i)
        {
            auto index  = m_specification.RenderTargets[i];
            auto handle = m_device->GlobalTextures->ToHandle(index);
            views[i]    = m_device->GlobalTextures->Access(handle)->ImageBuffer->GetImageViewHandle();
        }
        Handle = m_device->CreateFramebuffer(views, m_specification.Attachment->GetHandle(), m_specification.Width, m_specification.Height, m_specification.Layers);
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
            m_device->EnqueueForDeletion(Rendering::DeviceResourceType::FRAMEBUFFER, Handle);
            Handle = VK_NULL_HANDLE;
        }
    }
} // namespace ZEngine::Rendering::Buffers
