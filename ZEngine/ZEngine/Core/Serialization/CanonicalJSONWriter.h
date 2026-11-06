#pragma once
#include <ZEngine/Core/Containers/Array.h>
#include <ZEngine/Core/Containers/FixedArray.h>
#include <ZEngine/Core/Containers/Strings.h>
#include <ZEngine/Core/VFS/VFSError.h>
#include <uuid.h>

namespace ZEngine::Core::Serialization
{
    using JSONWriteFn = VFS::VFSResult<void> (*)(void*, Containers::ArrayView<const uint8_t>);

    // Allocation-free schema-directed emission primitives. Schema emitters own
    // punctuation and canonical key/array order; no DOM or runtime sort exists.
    // Strings preserve UTF-8 and use lowercase six-byte escapes for C0 controls.
    // Float32 uses nine scientific significant digits with normalized exponents.
    // The sink must consume each complete write or fail. Partial output on error
    // is private staging data, never permission to publish a document.
    // One inline buffer is reused by escaping/numbers/UUID/hex emission and is
    // included in sizeof(writer). Same-writer sink reentry fails before mutation;
    // callbacks may use a different writer. No temporary text buffer is retained.
    class CanonicalJSONWriter
    {
    public:
        explicit CanonicalJSONWriter(JSONWriteFn write, void* context = nullptr) noexcept;
        [[nodiscard]] bool          Raw(Containers::StringView trusted_syntax);
        [[nodiscard]] bool          String(Containers::StringView);
        [[nodiscard]] bool          UInt64(uint64_t);
        [[nodiscard]] bool          Float32(float);
        [[nodiscard]] bool          Boolean(bool);
        [[nodiscard]] bool          UUID(const uuids::uuid&);
        [[nodiscard]] bool          HexString(Containers::ArrayView<const uint8_t>);
        [[nodiscard]] VFS::VFSError Error() const noexcept
        {
            return m_error;
        }

    private:
        bool                             Reject(VFS::VFSError);
        bool                             CanEmit();
        JSONWriteFn                      m_write   = nullptr;
        void*                            m_context = nullptr;
        VFS::VFSError                    m_error   = VFS::VFSError::OK;
        Containers::FixedArray<char, 64> m_scratch = {};
        bool                             m_writing = false;
    };
} // namespace ZEngine::Core::Serialization
