#include <ZEngine/Helpers/MemoryOperations.h>
#include <ZEngine/Managers/CookedObjectSnapshot.h>
#include <algorithm>
#include <limits>
#include <utility>

namespace ZEngine::Managers
{
    namespace VFS = Core::VFS;

    namespace
    {
        bool IsCancelled(const std::atomic<bool>* cancelled)
        {
            return cancelled && cancelled->load(std::memory_order_acquire);
        }

        VFS::VFSResult<void> CaptureBytes(VFS::IVFSFile& file, const Cook::CookObjectDescriptor& object, uint8_t* bytes, const std::atomic<bool>* cancelled)
        {
            const auto before = file.Stat();
            if (before.Failed())
            {
                return VFS::VFSResult<void>::Fail(before.Error());
            }
            if (before.Value().IsDirectory || before.Value().SizeBytes != object.Size)
            {
                return VFS::VFSResult<void>::Fail(VFS::VFSError::Corrupted);
            }
            Core::Hash::SHA256 hash       = {};
            constexpr size_t   read_slice = ZKilo(64);
            for (uint64_t offset = 0; offset < object.Size;)
            {
                if (IsCancelled(cancelled))
                {
                    return VFS::VFSResult<void>::Fail(VFS::VFSError::Cancelled);
                }
                const auto count = static_cast<size_t>(std::min<uint64_t>(read_slice, object.Size - offset));
                const auto read  = file.Read({bytes + static_cast<size_t>(offset), count}, offset);
                if (read.Failed() || !read.Value() || read.Value() > count)
                {
                    return VFS::VFSResult<void>::Fail(read.Failed() ? read.Error() : VFS::VFSError::Corrupted);
                }
                const auto hashed = hash.Update({bytes + static_cast<size_t>(offset), read.Value()});
                if (hashed.Failed())
                {
                    return hashed;
                }
                offset += read.Value();
            }
            if (IsCancelled(cancelled))
            {
                return VFS::VFSResult<void>::Fail(VFS::VFSError::Cancelled);
            }
            uint8_t    trailing = 0;
            const auto eof      = file.Read({&trailing, 1}, object.Size);
            if (eof.Failed() || eof.Value())
            {
                return VFS::VFSResult<void>::Fail(eof.Failed() ? eof.Error() : VFS::VFSError::Corrupted);
            }
            const auto after = file.Stat();
            if (after.Failed())
            {
                return VFS::VFSResult<void>::Fail(after.Error());
            }
            if (after.Value().IsDirectory || after.Value().SizeBytes != object.Size)
            {
                return VFS::VFSResult<void>::Fail(VFS::VFSError::Corrupted);
            }
            Cook::Digest256 digest    = {};
            const auto      finalized = hash.Finalize(&digest);
            if (finalized.Failed())
            {
                return finalized;
            }
            return digest == object.Digest ? VFS::VFSResult<void>::Ok() : VFS::VFSResult<void>::Fail(VFS::VFSError::Corrupted);
        }
    } // namespace

    CookedObjectSnapshot::~CookedObjectSnapshot()
    {
        Reset();
    }

    CookedObjectSnapshot::CookedObjectSnapshot(CookedObjectSnapshot&& other) noexcept : m_route(std::exchange(other.m_route, nullptr)), m_bytes(std::exchange(other.m_bytes, nullptr)), m_size(std::exchange(other.m_size, 0)), m_descriptor(std::exchange(other.m_descriptor, {})) {}

    CookedObjectSnapshot& CookedObjectSnapshot::operator=(CookedObjectSnapshot&& other) noexcept
    {
        if (this != &other)
        {
            Reset();
            m_route      = std::exchange(other.m_route, nullptr);
            m_bytes      = std::exchange(other.m_bytes, nullptr);
            m_size       = std::exchange(other.m_size, 0);
            m_descriptor = std::exchange(other.m_descriptor, {});
        }
        return *this;
    }

    void CookedObjectSnapshot::Reset() noexcept
    {
        if (m_route)
        {
            m_route->Free(m_bytes);
        }
        m_route      = nullptr;
        m_bytes      = nullptr;
        m_size       = 0;
        m_descriptor = {};
    }

    Importers::AssetCodec::AssetByteSource CookedObjectSnapshot::Source() const noexcept
    {
        return {
            const_cast<CookedObjectSnapshot*>(this),
            [](void* context, Core::Containers::ArrayView<uint8_t> destination, uint64_t offset) {
                const auto& owner = *static_cast<const CookedObjectSnapshot*>(context);
                if ((destination.size() && !destination.data()) || offset > owner.m_size)
                {
                    return VFS::VFSResult<size_t>::Fail(VFS::VFSError::InvalidPath);
                }
                const size_t count = std::min(destination.size(), owner.m_size - static_cast<size_t>(offset));
                if (count && Helpers::secure_memcpy(destination.data(), destination.size(), owner.m_bytes + static_cast<size_t>(offset), count) != Helpers::MEMORY_OP_SUCCESS)
                {
                    return VFS::VFSResult<size_t>::Fail(VFS::VFSError::IOError);
                }
                return VFS::VFSResult<size_t>::Ok(count);
            },
            m_size};
    }

    VFS::VFSResult<void> CookedObjectSnapshot::Capture(const Cook::CookObjectReadFns& reader, const Cook::CookObjectDescriptor& object, Core::Containers::StringView extension, Core::Memory::IAllocationRoute& route, size_t max_bytes, CookedObjectSnapshot* output, const std::atomic<bool>* cancelled)
    {
        if (!output || output->m_bytes || !reader.Context || !reader.Open || !reader.CloseChecked || !max_bytes)
        {
            return VFS::VFSResult<void>::Fail(VFS::VFSError::InvalidPath);
        }
        if (!Cook::IsCookObjectDescriptorValid(object, extension))
        {
            return VFS::VFSResult<void>::Fail(VFS::VFSError::Corrupted);
        }
        if (object.Size > max_bytes || object.Size > std::numeric_limits<size_t>::max())
        {
            return VFS::VFSResult<void>::Fail(VFS::VFSError::SizeLimitExceeded);
        }
        if (IsCancelled(cancelled))
        {
            return VFS::VFSResult<void>::Fail(VFS::VFSError::Cancelled);
        }
        Core::Memory::WorkingMemoryCharge fixed = {};
        if (!fixed.TryAcquire(route.WorkingMemoryBudget(), Core::Hash::SHA256::FixedWorkingBytes() + sizeof(fixed)))
        {
            return VFS::VFSResult<void>::Fail(VFS::VFSError::SizeLimitExceeded);
        }
        const auto opened = reader.Open(reader.Context, object.Path);
        if (opened.Failed())
        {
            return VFS::VFSResult<void>::Fail(opened.Error());
        }
        if (!opened.Value())
        {
            return VFS::VFSResult<void>::Fail(VFS::VFSError::IOError);
        }
        CookedObjectSnapshot candidate = {};
        candidate.m_route              = &route;
        candidate.m_size               = static_cast<size_t>(object.Size);
        candidate.m_descriptor         = object;
        candidate.m_bytes              = static_cast<uint8_t*>(route.Allocate(candidate.m_size));
        const auto captured            = candidate.m_bytes ? CaptureBytes(*opened.Value(), object, candidate.m_bytes, cancelled) : VFS::VFSResult<void>::Fail(VFS::VFSError::OutOfMemory);
        const auto closed              = reader.CloseChecked(reader.Context, opened.Value());
        if (captured.Failed() || closed.Failed() || IsCancelled(cancelled))
        {
            return VFS::VFSResult<void>::Fail(captured.Failed() ? captured.Error() : closed.Failed() ? closed.Error() : VFS::VFSError::Cancelled);
        }
        *output = std::move(candidate);
        return VFS::VFSResult<void>::Ok();
    }
} // namespace ZEngine::Managers
