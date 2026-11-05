#pragma once
#include <ZEngine/Core/Containers/Array.h>
#include <ZEngine/Core/Containers/FixedArray.h>
#include <ZEngine/Core/Containers/Strings.h>
#include <ZEngine/Core/VFS/VFSError.h>

namespace ZEngine::Core::Serialization
{
    enum class JSONValueKind : uint8_t
    {
        Invalid = 0,
        Object,
        Array,
        String,
        Number,
        Boolean,
        Null,
    };

    struct JSONReadLimits
    {
        uint32_t MaxDepth       = 16;
        size_t   MaxStringBytes = 4096;
    };

    // Allocation-free, schema-directed JSON reader. Input and decoded string
    // buffers are caller-owned. Object/array loops consume their closing token:
    // BeginObject(); while (Member(buffer, &key)) { read one value; } Finish().
    // Member/Element return false on either end or failure; Finish/Error must be
    // checked. A value cannot be skipped accidentally. Schema readers reject
    // unknown/duplicate decoded keys before reading their values; no DOM exists.
    class JSONReader
    {
    public:
        explicit JSONReader(Containers::ArrayView<const uint8_t> input, JSONReadLimits limits = {});
        // Reuse the inline parser stack without constructing a second reader.
        // Existing decoded strings belong to caller buffers, not this reader.
        void                        Reset(Containers::ArrayView<const uint8_t> input, JSONReadLimits limits = {});
        // Non-consuming token dispatch for schema-independent validation. This
        // does not validate/skip a value or change the container's read state.
        [[nodiscard]] JSONValueKind PeekValueKind() const noexcept;
        [[nodiscard]] bool          BeginObject();
        [[nodiscard]] bool          BeginArray();
        [[nodiscard]] bool          Member(Containers::ArrayView<char> buffer, Containers::StringView* key);
        [[nodiscard]] bool          Element();
        [[nodiscard]] bool          String(Containers::ArrayView<char> buffer, Containers::StringView* value);
        // Borrowed exact JSON number token, validated but not converted/rounded.
        [[nodiscard]] bool          Number(Containers::StringView* value);
        // Locale-independent checked binary32 conversion; rejects non-finite or
        // unrepresentable values and preserves output on conversion failure.
        [[nodiscard]] bool          Float32(float* value);
        [[nodiscard]] bool          UInt64(uint64_t* value);
        [[nodiscard]] bool          Int64(int64_t* value);
        [[nodiscard]] bool          Boolean(bool* value);
        [[nodiscard]] bool          Null();
        [[nodiscard]] bool          Finish();
        [[nodiscard]] VFS::VFSError Error() const noexcept
        {
            return m_error;
        }
        [[nodiscard]] size_t Offset() const noexcept
        {
            return m_position;
        }

    private:
        enum class ContainerKind : uint8_t
        {
            Object = 0,
            Array  = 1,
        };
        struct Frame
        {
            ContainerKind Kind          = ContainerKind::Object;
            bool          HasEntry      = false;
            bool          AwaitingValue = false;
        };

        bool BeforeValue();
        bool Begin(ContainerKind kind, char token);
        bool Next(ContainerKind kind, char closing);
        bool DecodeString(Containers::ArrayView<char> buffer, Containers::StringView* value);
        template <typename Integer>
        bool                                 ReadInteger(Integer* value);
        bool                                 HexQuad(uint32_t* value);
        bool                                 Literal(cstring text, size_t length);
        bool                                 Delimiter() const;
        bool                                 Reject(VFS::VFSError error = VFS::VFSError::Corrupted);
        void                                 Whitespace();
        bool                                 Consume(char token);
        bool                                 At(char token) const;

        Containers::ArrayView<const uint8_t> m_input         = {};
        JSONReadLimits                       m_limits        = {};
        Containers::FixedArray<Frame, 16>    m_stack         = {};
        size_t                               m_position      = 0;
        uint32_t                             m_depth         = 0;
        bool                                 m_root_consumed = false;
        VFS::VFSError                        m_error         = VFS::VFSError::OK;
    };
} // namespace ZEngine::Core::Serialization
