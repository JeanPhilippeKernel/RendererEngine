#pragma once
#include <ZEngine/Maths/Math.h>
#include <vector>
#include <vulkan/vulkan.h>
#include <Rendering/Cameras/Camera.h>
#include <Rendering/Meshes/Mesh.h>

namespace ZEngine::Rendering::Renderers::Contracts
{
    struct UBOCameraLayout
    {
        alignas(16) Maths::Vector4 position   = Maths::Vector4(1.0f);
        alignas(16) Maths::Matrix4 View       = Maths::Matrix4(1.0f);
        alignas(16) Maths::Matrix4 Projection = Maths::Matrix4(1.0f);
    };

    struct UBOModelLayout
    {
        alignas(16) Maths::Matrix4 Model = Maths::Matrix4(1.0f);
    };

    struct GraphicSceneLayout
    {
        uint32_t              FrameIndex;
        VkQueue               GraphicQueue;
        Ref<Cameras::Camera>  SceneCamera;
        std::vector<uint32_t> MeshCollectionIdentifiers;
        void*                 GraphicScenePtr{nullptr};
    };

    struct FramebufferViewLayout
    {
        uint32_t FrameId{0xff};
        VkSampler Sampler{VK_NULL_HANDLE};
        VkImageView View{VK_NULL_HANDLE};
        VkDescriptorSet Handle{VK_NULL_HANDLE};
    };
} // namespace ZEngine::Rendering::Renderers::Contracts
