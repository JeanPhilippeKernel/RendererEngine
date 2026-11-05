#include <ZEngine/Cook/CookAdapter.h>
#include <ZEngine/Cook/CookCallbackStorage.h>
#include <ZEngine/Cook/CookRegistryMemory.h>
#include <ZEngine/Cook/CookServiceMemoryInternal.h>
#include <ZEngine/Cook/SceneCooker.h>
#include <ZEngine/ECS/SceneCompatibility.h>
#include <ZEngine/ECS/SceneComponentSchemaRegistry.h>
#include <algorithm>

namespace ZEngine::Cook
{
    namespace VFS = Core::VFS;

    namespace
    {
        bool AddBacking(CookServiceMemoryRequirements& result, const Core::Memory::ArenaRetainedStorage& backing) noexcept
        {
            if (backing.CommittedBytes > UINT64_MAX - result.StorageBytes)
            {
                return false;
            }
            result.StorageBytes += backing.CommittedBytes;
            if (backing.TrackingBytes > UINT64_MAX - result.StorageBytes || backing.ReservedAddressBytes > UINT64_MAX - result.ReservedAddressBytes)
            {
                return false;
            }
            result.StorageBytes         += backing.TrackingBytes;
            result.ReservedAddressBytes += backing.ReservedAddressBytes;
            return true;
        }
    } // namespace

    VFS::VFSResult<void> Detail::CookNativeServiceMemory::Initialize(const SceneCookContext& context, const Core::Memory::ArenaAllocator* callbacks) noexcept
    {
        if (!context.Schemas || !context.Schemas->IsInitialized() || !context.Adapters || !context.Adapters->IsInitialized() || (context.Migrations && !context.Migrations->IsInitialized()))
        {
            return VFS::VFSResult<void>::Fail(VFS::VFSError::InvalidPath);
        }
        m_arenas                           = {};
        m_arenas[0]                        = context.Schemas->StorageArena();
        m_arenas[1]                        = context.Migrations ? context.Migrations->StorageArena() : nullptr;
        m_arenas[2]                        = callbacks;
        m_facades                          = {};
        const VFS::IVFSContext* contexts[] = {context.Output, context.Scratch};
        for (size_t index = 0; index < m_facades.size(); ++index)
        {
            if (!contexts[index])
            {
                continue;
            }
            if (index && contexts[index] == contexts[0])
            {
                m_facades[index] = m_facades[0];
            }
            else
            {
                const auto storage = contexts[index]->NativeStorage();
                if (storage.Failed())
                {
                    return VFS::VFSResult<void>::Fail(storage.Error());
                }
                m_facades[index] = storage.Value();
            }
            const auto& facade = m_facades[index];
            const auto  begin  = reinterpret_cast<uintptr_t>(facade.Object);
            const auto  object = reinterpret_cast<uintptr_t>(contexts[index]);
            if (!facade.Object || !facade.ObjectBytes || !facade.Arena || facade.ObjectBytes > UINTPTR_MAX - begin || object < begin || object - begin > facade.ObjectBytes || sizeof(*contexts[index]) > facade.ObjectBytes - (object - begin))
            {
                return VFS::VFSResult<void>::Fail(VFS::VFSError::Unsupported);
            }
            if (index && facade.Object == m_facades[0].Object && (facade.ObjectBytes != m_facades[0].ObjectBytes || facade.Arena != m_facades[0].Arena))
            {
                return VFS::VFSResult<void>::Fail(VFS::VFSError::Corrupted);
            }
            m_arenas[index + 3] = facade.Arena;
        }
        m_backing = {};
        for (size_t index = 0; index < m_arenas.size(); ++index)
        {
            if (!m_arenas[index])
            {
                continue;
            }
            if (!m_arenas[index]->RetainedStorage(&m_backing[index]))
            {
                return VFS::VFSResult<void>::Fail(VFS::VFSError::Unsupported);
            }
            for (size_t previous = 0; previous < index; ++previous)
            {
                if (m_arenas[previous] && m_backing[index].Identity == m_backing[previous].Identity && m_backing[index] != m_backing[previous])
                {
                    return VFS::VFSResult<void>::Fail(VFS::VFSError::Corrupted);
                }
            }
        }
        return VFS::VFSResult<void>::Ok();
    }

    bool Detail::CookNativeServiceMemory::ContainsBacking(const void* object, size_t bytes) const noexcept
    {
        for (const auto* arena : m_arenas)
        {
            if (arena && arena->RetainsStorage(object, bytes))
            {
                return true;
            }
        }
        return false;
    }

    bool Detail::CookNativeServiceMemory::Contains(const void* object, size_t bytes) const noexcept
    {
        if (ContainsBacking(object, bytes))
        {
            return true;
        }
        const auto address = reinterpret_cast<uintptr_t>(object);
        for (const auto& facade : m_facades)
        {
            const auto begin = reinterpret_cast<uintptr_t>(facade.Object);
            if (facade.Object && object && bytes && address >= begin && address - begin <= facade.ObjectBytes && bytes <= facade.ObjectBytes - (address - begin))
            {
                return true;
            }
        }
        return false;
    }

    bool Detail::CookNativeServiceMemory::EarlierFamily(size_t index) const noexcept
    {
        for (size_t previous = 0; previous < index; ++previous)
        {
            if (m_arenas[previous] && m_backing[index].Identity == m_backing[previous].Identity)
            {
                return true;
            }
        }
        return false;
    }

    bool Detail::CookNativeServiceMemory::EarlierView(size_t index) const noexcept
    {
        for (size_t previous = 0; previous < index; ++previous)
        {
            if (m_arenas[index] == m_arenas[previous])
            {
                return true;
            }
        }
        return false;
    }

    uint64_t Detail::CookNativeServiceMemory::OwnerBytes(size_t index) const noexcept
    {
        const auto* arena = m_arenas[index];
        const auto* owner = m_backing[index].Owner;
        uint64_t    bytes = 0;
        if (!EarlierFamily(index) && !Contains(owner, sizeof(*owner)))
        {
            bytes += sizeof(*owner);
        }
        if (arena != owner && !EarlierView(index) && !Contains(arena, sizeof(*arena)))
        {
            bytes += sizeof(*arena);
        }
        return bytes;
    }

    VFS::VFSResult<CookServiceMemoryRequirements> Detail::CookNativeServiceMemory::RegistryRequirements(const SceneCookContext& context) const noexcept
    {
        using Result                               = VFS::VFSResult<CookServiceMemoryRequirements>;
        CookServiceMemoryRequirements requirements = {};
        if (!Contains(context.Schemas, sizeof(*context.Schemas)))
        {
            requirements.StateBytes += sizeof(*context.Schemas);
        }
        if (!Contains(context.Adapters, sizeof(*context.Adapters)))
        {
            requirements.StateBytes += sizeof(*context.Adapters);
        }
        if (context.Migrations && !Contains(context.Migrations, sizeof(*context.Migrations)))
        {
            requirements.StateBytes += sizeof(*context.Migrations);
        }
        for (size_t index = 0; index < 2; ++index)
        {
            if (!m_arenas[index])
            {
                continue;
            }
            requirements.StateBytes += OwnerBytes(index);
            if (!EarlierFamily(index) && !AddBacking(requirements, m_backing[index]))
            {
                return Result::Fail(VFS::VFSError::SizeLimitExceeded);
            }
        }
        return Result::Ok(requirements);
    }

    VFS::VFSResult<CookServiceMemoryRequirements> Detail::CookNativeServiceMemory::CallbackRequirements(const void* owner, size_t owner_bytes) const noexcept
    {
        using Result = VFS::VFSResult<CookServiceMemoryRequirements>;
        if (!m_arenas[2])
        {
            return Result::Fail(VFS::VFSError::Unsupported);
        }
        CookServiceMemoryRequirements requirements  = {.StateBytes = Contains(owner, owner_bytes) ? 0 : owner_bytes};
        requirements.StateBytes                    += OwnerBytes(2);
        if (!EarlierFamily(2) && !AddBacking(requirements, m_backing[2]))
        {
            return Result::Fail(VFS::VFSError::SizeLimitExceeded);
        }
        return Result::Ok(requirements);
    }

    VFS::VFSResult<CookServiceMemoryRequirements> Detail::CookNativeServiceMemory::VFSRequirements() const noexcept
    {
        using Result                               = VFS::VFSResult<CookServiceMemoryRequirements>;
        CookServiceMemoryRequirements requirements = {};
        for (size_t index = 0; index < m_facades.size(); ++index)
        {
            const auto& facade = m_facades[index];
            if (!facade.Object)
            {
                continue;
            }
            if (!ContainsBacking(facade.Object, facade.ObjectBytes))
            {
                uint64_t    bytes    = facade.ObjectBytes;
                const auto& previous = m_facades[0];
                if (index && previous.Object && !ContainsBacking(previous.Object, previous.ObjectBytes))
                {
                    const auto begin         = reinterpret_cast<uintptr_t>(facade.Object);
                    const auto earlier       = reinterpret_cast<uintptr_t>(previous.Object);
                    const auto overlap_begin = std::max(begin, earlier);
                    const auto overlap_end   = std::min(begin + facade.ObjectBytes, earlier + previous.ObjectBytes);
                    if (overlap_end > overlap_begin)
                    {
                        bytes -= overlap_end - overlap_begin;
                    }
                }
                if (bytes > UINT64_MAX - requirements.StateBytes)
                {
                    return Result::Fail(VFS::VFSError::SizeLimitExceeded);
                }
                requirements.StateBytes += bytes;
            }
            const size_t view   = index + 3;
            const auto   owners = OwnerBytes(view);
            if (owners > UINT64_MAX - requirements.StateBytes)
            {
                return Result::Fail(VFS::VFSError::SizeLimitExceeded);
            }
            requirements.StateBytes += owners;
            if (!EarlierFamily(view) && !AddBacking(requirements, m_backing[view]))
            {
                return Result::Fail(VFS::VFSError::SizeLimitExceeded);
            }
        }
        return Result::Ok(requirements);
    }

    VFS::VFSResult<CookServiceMemoryRequirements> QueryCookRegistryMemory(const SceneCookContext& context) noexcept
    {
        Detail::CookNativeServiceMemory memory      = {};
        const auto                      initialized = memory.Initialize(context, context.Callbacks ? context.Callbacks->StorageArena() : nullptr);
        return initialized.Succeeded() ? memory.RegistryRequirements(context) : VFS::VFSResult<CookServiceMemoryRequirements>::Fail(initialized.Error());
    }
} // namespace ZEngine::Cook
