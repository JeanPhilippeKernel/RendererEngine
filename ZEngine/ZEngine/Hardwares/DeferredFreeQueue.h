#pragma once
#include <ZEngine/Core/Memory/GpuAllocator.h>
#include <ZEngine/Rendering/ResourceTypes.h>
#include <ZEngine/ZEngineDef.h>
#include <mutex>
#include <utility>

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
        uint64_t Reservation   = 0;
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
        static constexpr uint32_t kCapacity = 2048;

        struct Statistics
        {
            uint32_t Capacity  = kCapacity - 1;
            uint32_t Reserved  = 0;
            uint32_t Queued    = 0;
            uint32_t Draining  = 0;
            uint32_t HighWater = 0;
        };

        [[nodiscard]] Statistics GetStatistics() const
        {
            std::lock_guard lock(Mutex);
            const auto      queued = (Tail + kCapacity - Head) % kCapacity;
            return {.Reserved = Occupied - queued - Draining, .Queued = queued, .Draining = Draining, .HighWater = HighWater};
        }

        // A resource reserves its future disposal slot before native creation.
        // Destruction therefore cannot discover a full queue after losing its owner.
        class Lease
        {
        public:
            Lease() = default;
            Lease(DeferredFreeQueue* owner, uint64_t token) : Owner(owner), Token(token) {}
            ~Lease()
            {
                if (Owner)
                {
                    Owner->CancelReservation(Token);
                }
            }
            Lease(const Lease&)            = delete;
            Lease& operator=(const Lease&) = delete;
            Lease(Lease&& other) noexcept : Owner(std::exchange(other.Owner, nullptr)), Token(std::exchange(other.Token, 0)) {}
            Lease& operator=(Lease&& other) noexcept
            {
                if (this != &other)
                {
                    if (Owner)
                    {
                        Owner->CancelReservation(Token);
                    }
                    Owner = std::exchange(other.Owner, nullptr);
                    Token = std::exchange(other.Token, 0);
                }
                return *this;
            }
            explicit operator bool() const
            {
                return Token != 0;
            }
            uint64_t Detach()
            {
                Owner = nullptr;
                return std::exchange(Token, 0);
            }
            void Publish(Rendering::DeviceResourceType type, void* handle)
            {
                ZENGINE_VALIDATE_ASSERT(Owner && Token && handle, "Invalid native retirement publication")
                Owner->PublishReservation(Token, type, handle);
                Detach();
            }

        private:
            DeferredFreeQueue* Owner = nullptr;
            uint64_t           Token = 0;
        };

        [[nodiscard]] Lease Reserve()
        {
            std::lock_guard lock(Mutex);
            return Lease(this, ReserveLocked());
        }

        template <typename Handle, typename Create>
        VkResult CreateNative(Rendering::DeviceResourceType type, Handle* handle, Create&& create, bool keep_partial = false)
        {
            *handle         = VK_NULL_HANDLE;
            auto retirement = Reserve();
            if (!retirement)
            {
                return VK_ERROR_OUT_OF_HOST_MEMORY;
            }
            const auto result = create(handle);
            if ((result == VK_SUCCESS || keep_partial) && *handle)
            {
                retirement.Publish(type, reinterpret_cast<void*>(*handle));
            }
            return result;
        }

        void CancelReservation(uint64_t token)
        {
            if (!token)
            {
                return;
            }
            std::lock_guard lock(Mutex);
            const auto      index = static_cast<uint32_t>(token);
            ZENGINE_VALIDATE_ASSERT(index < kCapacity && Generations[index] == token >> 32, "Invalid native retirement reservation")
            // Native destruction during Drain returns the credit only after all
            // native frees complete; the drain owns that transition.
            if (States[index] == State::Draining)
            {
                return;
            }
            ZENGINE_VALIDATE_ASSERT(States[index] == State::Reserved, "Retirement reservation is not privately owned")
            ReleaseLocked(index);
        }

        [[nodiscard]] bool TryEnqueue(DeferredFreeEntry entry)
        {
            std::lock_guard lock(Mutex);
            auto            token = entry.Reservation;
            if (!token && entry.EntryKind == DeferredFreeEntry::Kind::VkHandle)
            {
                token = FindNativeLocked(entry.Data.Vk.Type, entry.Data.Vk.Handle);
            }
            if (!token && entry.EntryKind == DeferredFreeEntry::Kind::Buffer)
            {
                token = entry.Data.Buffer.RetirementReservation;
            }
            if (!token && entry.EntryKind == DeferredFreeEntry::Kind::Image)
            {
                token = entry.Data.Image.RetirementReservation;
            }
            if (!token)
            {
                token = ReserveLocked();
            }
            if (!token)
            {
                return false;
            }
            const auto index = static_cast<uint32_t>(token);
            if (index >= kCapacity || Generations[index] != token >> 32 || States[index] != State::Reserved)
            {
                return false;
            }
            entry.Reservation   = token;
            Entries[index]      = entry;
            States[index]       = State::Queued;
            QueuedIndices[Tail] = index;
            Tail                = (Tail + 1) % kCapacity;
            return true;
        }

        // Diagnostic cursors; concurrent callers use Empty(), not these fields.
        uint32_t           Head = 0;
        uint32_t           Tail = 0;

        [[nodiscard]] bool Empty() const
        {
            std::lock_guard lock(Mutex);
            return Head == Tail && Draining == 0;
        }

        // Only for objects never exposed/submitted, or after established idle.
        // Serialize key removal with reuse; release credit after the native free.
        template <typename Handle, typename Destroy>
        void DestroyNative(Rendering::DeviceResourceType type, Handle handle, Destroy&& destroy)
        {
            std::lock_guard lock(Mutex);
            const auto      token = FindNativeLocked(type, reinterpret_cast<void*>(handle));
            destroy();
            if (token)
            {
                ReleaseLocked(static_cast<uint32_t>(token));
            }
        }

        void Enqueue(DeferredFreeEntry entry)
        {
            ZENGINE_VALIDATE_ASSERT(TryEnqueue(entry), "Native resource has no admitted retirement capacity")
        }

        void Drain(Core::Memory::GpuAllocator* alloc, VkDevice device, uint64_t completed_timeline_value)
        {
            std::lock_guard drain_lock(DrainMutex);
            for (;;)
            {
                DeferredFreeEntry e     = {};
                uint32_t          index = UINT32_MAX;
                {
                    std::lock_guard lock(Mutex);
                    if (Head == Tail || Entries[QueuedIndices[Head]].TimelineValue > completed_timeline_value)
                    {
                        break;
                    }
                    index         = QueuedIndices[Head];
                    e             = Entries[index];
                    States[index] = State::Draining;
                    ++Draining;
                    Head = (Head + 1) % kCapacity;
                }

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
                                vkDestroySampler(device, reinterpret_cast<VkSampler>(e.Data.Vk.Handle), nullptr);
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
                            case Rendering::DeviceResourceType::SHADERMODULE:
                                vkDestroyShaderModule(device, reinterpret_cast<VkShaderModule>(e.Data.Vk.Handle), nullptr);
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
                            case Rendering::DeviceResourceType::QUERYPOOL:
                                vkDestroyQueryPool(device, reinterpret_cast<VkQueryPool>(e.Data.Vk.Handle), nullptr);
                                break;
                            case Rendering::DeviceResourceType::BUFFER:
                            case Rendering::DeviceResourceType::BUFFERMEMORY:
                            case Rendering::DeviceResourceType::IMAGE:
                            case Rendering::DeviceResourceType::RESOURCE_COUNT:
                                break;
                        }
                        break;
                }

                {
                    std::lock_guard lock(Mutex);
                    Entries[index] = {};
                    --Draining;
                    ReleaseLocked(index);
                }
            }
        }

    private:
        enum class State : uint8_t
        {
            Free = 0,
            Reserved,
            Queued,
            Draining
        };
        DeferredFreeEntry  Entries[kCapacity]       = {};
        uint32_t           QueuedIndices[kCapacity] = {};
        uint32_t           NextFree[kCapacity]      = {};
        uint32_t           Generations[kCapacity]   = {};
        State              States[kCapacity]        = {};
        uint32_t           FreeHead                 = UINT32_MAX;
        bool               Initialized              = false;
        uint32_t           Draining                 = 0;
        uint32_t           Occupied                 = 0;
        uint32_t           HighWater                = 0;
        mutable std::mutex Mutex;
        std::mutex         DrainMutex;

        uint64_t           FindNativeLocked(Rendering::DeviceResourceType type, void* handle) const
        {
            if (!handle)
            {
                return 0;
            }
            for (uint32_t index = 0; index + 1 < kCapacity; ++index)
            {
                const auto& entry = Entries[index];
                if (States[index] == State::Reserved && entry.EntryKind == DeferredFreeEntry::Kind::VkHandle && entry.Data.Vk.Handle == handle && entry.Data.Vk.Type == type)
                {
                    return (static_cast<uint64_t>(Generations[index]) << 32) | index;
                }
            }
            return 0;
        }

        void PublishReservation(uint64_t token, Rendering::DeviceResourceType type, void* handle)
        {
            std::lock_guard lock(Mutex);
            const auto      index = static_cast<uint32_t>(token);
            ZENGINE_VALIDATE_ASSERT(index < kCapacity && Generations[index] == token >> 32 && States[index] == State::Reserved, "Invalid native retirement publication")
            Entries[index].Data.Vk = {handle, type, nullptr};
        }

        uint64_t ReserveLocked()
        {
            if (!Initialized)
            {
                // Keep the ring's sentinel slot so Head == Tail means empty.
                for (uint32_t index = 0; index + 1 < kCapacity; ++index)
                {
                    NextFree[index] = index + 2 < kCapacity ? index + 1 : UINT32_MAX;
                }
                FreeHead    = 0;
                Initialized = true;
            }
            if (FreeHead == UINT32_MAX)
            {
                return 0;
            }
            const auto index = FreeHead;
            FreeHead         = NextFree[index];
            States[index]    = State::Reserved;
            ++Generations[index];
            ++Occupied;
            if (Occupied > HighWater)
            {
                HighWater = Occupied;
            }
            return (static_cast<uint64_t>(Generations[index]) << 32) | index;
        }

        void ReleaseLocked(uint32_t index)
        {
            --Occupied;
            Entries[index] = {};
            States[index]  = State::Free;
            if (Generations[index] != UINT32_MAX)
            {
                NextFree[index] = FreeHead;
                FreeHead        = index;
            }
        }
    };
} // namespace ZEngine::Hardwares
