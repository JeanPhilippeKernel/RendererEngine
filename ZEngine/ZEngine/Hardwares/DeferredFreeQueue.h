#pragma once
#include <ZEngine/Core/Memory/GpuAllocator.h>
#include <ZEngine/Rendering/ResourceTypes.h>

namespace ZEngine::Hardwares
{
    struct DeferredFreeEntry
    {
        enum class Kind : uint8_t
        {
            Buffer   = 0,
            Image    = 1,
            VkHandle = 2,
        };
        Kind     EntryKind     = Kind::VkHandle;
        uint64_t TimelineValue = 0;
        union Payload
        {
            Core::Memory::BufferView  Buffer;
            Core::Memory::BufferImage Image;
            struct
            {
                void*                         Handle;
                Rendering::DeviceResourceType Type;
                void*                         Extra; // used for DESCRIPTORSET pool pointer
            } Vk;

            Payload() : Vk{} {}
            ~Payload() {}
        } Data;
    };

    struct DeferredFreeQueue
    {
        static constexpr uint32_t kCapacity          = 2048;

        DeferredFreeEntry         Entries[kCapacity] = {};
        uint32_t                  Head               = 0;
        uint32_t                  Tail               = 0;

        void                      Enqueue(DeferredFreeEntry entry)
        {
            Entries[Tail] = entry;
            Tail          = (Tail + 1) % kCapacity;
        }

        void Drain(Core::Memory::GpuAllocator* alloc, VkDevice device, uint64_t completed_timeline_value)
        {
            while (Head != Tail && Entries[Head].TimelineValue <= completed_timeline_value)
            {
                DeferredFreeEntry& e = Entries[Head];

                switch (e.EntryKind)
                {
                    case DeferredFreeEntry::Kind::Buffer:
                        alloc->FreeBuffer(e.Data.Buffer);
                        break;

                    case DeferredFreeEntry::Kind::Image:
                        alloc->FreeImage(e.Data.Image, device);
                        break;

                    case DeferredFreeEntry::Kind::VkHandle:
                        switch (e.Data.Vk.Type)
                        {
                            case Rendering::DeviceResourceType::SAMPLER:
                                // vkDestroySampler(device, reinterpret_cast<VkSampler>(e.Data.Vk.Handle), nullptr);
                                break;
                            case Rendering::DeviceResourceType::FRAMEBUFFER:
                                vkDestroyFramebuffer(device, reinterpret_cast<VkFramebuffer>(e.Data.Vk.Handle), nullptr);
                                break;
                            case Rendering::DeviceResourceType::IMAGEVIEW:
                                vkDestroyImageView(device, reinterpret_cast<VkImageView>(e.Data.Vk.Handle), nullptr);
                                break;
                            case Rendering::DeviceResourceType::RENDERPASS:
                                vkDestroyRenderPass(device, reinterpret_cast<VkRenderPass>(e.Data.Vk.Handle), nullptr);
                                break;
                            case Rendering::DeviceResourceType::PIPELINE_LAYOUT:
                                vkDestroyPipelineLayout(device, reinterpret_cast<VkPipelineLayout>(e.Data.Vk.Handle), nullptr);
                                break;
                            case Rendering::DeviceResourceType::PIPELINE:
                                vkDestroyPipeline(device, reinterpret_cast<VkPipeline>(e.Data.Vk.Handle), nullptr);
                                break;
                            case Rendering::DeviceResourceType::DESCRIPTORSETLAYOUT:
                                vkDestroyDescriptorSetLayout(device, reinterpret_cast<VkDescriptorSetLayout>(e.Data.Vk.Handle), nullptr);
                                break;
                            case Rendering::DeviceResourceType::DESCRIPTORPOOL:
                                vkDestroyDescriptorPool(device, reinterpret_cast<VkDescriptorPool>(e.Data.Vk.Handle), nullptr);
                                break;
                            case Rendering::DeviceResourceType::SEMAPHORE:
                                vkDestroySemaphore(device, reinterpret_cast<VkSemaphore>(e.Data.Vk.Handle), nullptr);
                                break;
                            case Rendering::DeviceResourceType::FENCE:
                                vkDestroyFence(device, reinterpret_cast<VkFence>(e.Data.Vk.Handle), nullptr);
                                break;
                            case Rendering::DeviceResourceType::DESCRIPTORSET:
                            {
                                auto ds = reinterpret_cast<VkDescriptorSet>(e.Data.Vk.Handle);
                                vkFreeDescriptorSets(device, reinterpret_cast<VkDescriptorPool>(e.Data.Vk.Extra), 1, &ds);
                                break;
                            }
                            case Rendering::DeviceResourceType::BUFFER:
                            case Rendering::DeviceResourceType::BUFFERMEMORY:
                            case Rendering::DeviceResourceType::IMAGE:
                            case Rendering::DeviceResourceType::RESOURCE_COUNT:
                                break;
                        }
                        break;
                }

                e    = {};
                Head = (Head + 1) % kCapacity;
            }
        }
    };
} // namespace ZEngine::Hardwares