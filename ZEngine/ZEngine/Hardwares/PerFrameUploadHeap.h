#pragma once
#include <ZEngine/Core/Memory/GpuAllocator.h>
#include <ZEngine/Helpers/MemoryOperations.h>
#include <ZEngine/ZEngineDef.h>

namespace ZEngine::Hardwares
{
    struct PerFrameHeapAlloc
    {
        uint32_t Offset;
        uint32_t Size;
    };

    struct PerFrameUploadHeap
    {
        static constexpr uint32_t kCapacity  = static_cast<uint32_t>(Core::Memory::UniformBytes); // 64 MB per frame

        VkBuffer                  Handle     = VK_NULL_HANDLE;
        VmaAllocation             Allocation = nullptr;
        void*                     MappedPtr  = nullptr;
        uint32_t                  WritePos   = 0;
        bool                      Coherent   = false;

        void                      Initialize(Core::Memory::GpuAllocator* alloc, const char* debug_name)
        {
            Core::Memory::BufferView view = alloc->AllocateBuffer(kCapacity, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT, Core::Memory::GpuMemoryDomain::HostUniform, debug_name);
            Handle                        = view.Handle;
            Allocation                    = view.Allocation;
            MappedPtr                     = nullptr;

            VmaAllocationInfo info        = {};
            vmaGetAllocationInfo(alloc->Allocator, Allocation, &info);
            MappedPtr                       = info.pMappedData;

            VkMemoryPropertyFlags mem_flags = 0;
            vmaGetAllocationMemoryProperties(alloc->Allocator, Allocation, &mem_flags);
            Coherent = (mem_flags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) != 0;
        }

        void Shutdown(Core::Memory::GpuAllocator* alloc)
        {
            Core::Memory::BufferView view = {};
            view.Handle                   = Handle;
            view.Allocation               = Allocation;
            alloc->FreeBuffer(view);
            Handle     = VK_NULL_HANDLE;
            Allocation = nullptr;
            MappedPtr  = nullptr;
        }

        void Reset()
        {
            WritePos = 0;
        }

        PerFrameHeapAlloc Push(const void* data, uint32_t size, uint32_t alignment)
        {
            uint32_t aligned_pos = (WritePos + alignment - 1) & ~(alignment - 1);
            ZENGINE_VALIDATE_ASSERT(aligned_pos + size <= kCapacity, "PerFrameUploadHeap full")
            Helpers::secure_memcpy(reinterpret_cast<uint8_t*>(MappedPtr) + aligned_pos, size, data, size);
            WritePos = aligned_pos + size;
            return {aligned_pos, size};
        }

        void Flush(Core::Memory::GpuAllocator* alloc)
        {
            if (!Coherent && WritePos > 0)
                vmaFlushAllocation(alloc->Allocator, Allocation, 0, WritePos);
        }
    };

} // namespace ZEngine::Hardwares
