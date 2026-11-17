#pragma once
#include <ZEngine/Cook/CookObjectReader.h>
#include <ZEngine/Core/Memory/AllocationRoute.h>
#include <ZEngine/Importers/AssetByteSource.h>
#include <atomic>
#include <span>

namespace ZEngine::Managers
{
    /// Private verified bytes for one cooked object. The allocation route must
    /// outlive this owner and every borrower of Bytes(). This synchronous helper
    /// belongs on a load worker; it does not open source assets or publish a scene.
    class CookedObjectSnapshot final
    {
    public:
        CookedObjectSnapshot() = default;
        ~CookedObjectSnapshot();
        CookedObjectSnapshot(const CookedObjectSnapshot&)            = delete;
        CookedObjectSnapshot& operator=(const CookedObjectSnapshot&) = delete;
        CookedObjectSnapshot(CookedObjectSnapshot&& other) noexcept;
        CookedObjectSnapshot&                           operator=(CookedObjectSnapshot&& other) noexcept;

        /// Copies and hashes the exact bytes read from one open handle. Path,
        /// size, EOF, digest and checked-close must all succeed before output
        /// changes. max_bytes is an admitted per-object bound; the route owns
        /// allocation accounting. An already populated output is refused so
        /// existing borrowed views cannot be invalidated implicitly.
        [[nodiscard]] static Core::VFS::VFSResult<void> Capture(const Cook::CookObjectReadFns& reader, const Cook::CookObjectDescriptor& object, Core::Containers::StringView extension, Core::Memory::IAllocationRoute& route, size_t max_bytes, CookedObjectSnapshot* output, const std::atomic<bool>* cancelled = nullptr);

        void                                            Reset() noexcept;
        [[nodiscard]] std::span<const uint8_t>          Bytes() const noexcept
        {
            return {m_bytes, m_size};
        }
        [[nodiscard]] const Cook::CookObjectDescriptor& Descriptor() const noexcept
        {
            return m_descriptor;
        }
        /// Borrowed reader; moving/resetting this owner invalidates its context.
        [[nodiscard]] Importers::AssetCodec::AssetByteSource Source() const noexcept;

    private:
        Core::Memory::IAllocationRoute* m_route      = nullptr;
        uint8_t*                        m_bytes      = nullptr;
        size_t                          m_size       = 0;
        Cook::CookObjectDescriptor      m_descriptor = {};
    };
} // namespace ZEngine::Managers
