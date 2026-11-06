#include <ZEngine/Core/VFS/VFSFileIO.h>
#include <ZEngine/Helpers/MemoryOperations.h>
#include <nlohmann/json.hpp>
#include <uuid.h>
#include <algorithm>
#include <chrono>
#include <cstring>
#include <istream>
#if defined(_WIN32)
#include <process.h>
#else
#include <unistd.h>
#endif

namespace ZEngine::Core::VFS
{
    namespace
    {
        PaddedAtomic<uint64_t> TemporarySequence = {};

        VFSResult<VFSPath>     TemporarySibling(const VFSPath& path, cstring name)
        {
            return path.Parent().Append(name);
        }

        VFSResult<VFSRelativePath> TemporarySibling(const VFSRelativePath& path, cstring name)
        {
            return path.Sibling({name, 41});
        }

        bool ValidAtomicPath(const VFSPath& path)
        {
            return path.IsValid() && !path.IsRoot();
        }

        bool ValidAtomicPath(const VFSRelativePath& path)
        {
            return path.IsValid();
        }

        template <typename Path>
        Containers::StringView AtomicDirectory(const Path& path)
        {
            size_t bytes = path.Length();
            while (bytes && path.CStr()[bytes - 1] != '/')
            {
                --bytes;
            }
            return {path.CStr(), bytes};
        }

        void TemporaryName(char (&name)[42])
        {
            // No heap, random-engine state, or persisted-name convention. Exclusive
            // creation, not uniqueness guessing, establishes ownership below.
#if defined(_WIN32)
            const auto process = static_cast<uint32_t>(_getpid());
#else
            const auto process = static_cast<uint32_t>(getpid());
#endif
            const auto tick      = static_cast<uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
            const auto sequence  = TemporarySequence.value.fetch_add(1, std::memory_order_relaxed) ^ (static_cast<uint64_t>(process) << 32);
            uint8_t    bytes[16] = {};
            for (size_t i = 0; i < 8; ++i)
            {
                bytes[i]     = static_cast<uint8_t>(tick >> (8 * i));
                bytes[8 + i] = static_cast<uint8_t>(sequence >> (8 * i));
            }
            // Keep the previous fixed sibling-length budget used by import path
            // preflight, without allocating a UUID string or a random generator.
            name[0]      = '.';
            auto* cursor = uuids::to_string<char>(uuids::uuid(bytes, bytes + sizeof(bytes)), name + 1);
            *cursor++    = '.';
            *cursor++    = 't';
            *cursor++    = 'm';
            *cursor++    = 'p';
            *cursor      = '\0';
        }

        // Retain only a 4 KB input chunk and the UUID, even for large materials.
        class VFSReadBuffer : public std::streambuf
        {
        public:
            VFSReadBuffer(IVFSFile* file, uint64_t size) : m_file(file), m_size(size) {}
            VFSError Error = VFSError::OK;

        protected:
            int_type underflow() override
            {
                if (m_offset == m_size)
                {
                    return traits_type::eof();
                }
                const size_t count = static_cast<size_t>(std::min<uint64_t>(sizeof(m_buffer), m_size - m_offset));
                auto         read  = m_file->Read({reinterpret_cast<uint8_t*>(m_buffer), count}, m_offset);
                if (read.Failed() || read.Value() == 0 || read.Value() > count)
                {
                    Error = read.Failed() ? read.Error() : VFSError::IOError;
                    return traits_type::eof();
                }
                m_offset += read.Value();
                setg(m_buffer, m_buffer, m_buffer + read.Value());
                return traits_type::to_int_type(*gptr());
            }

        private:
            IVFSFile* m_file         = nullptr;
            uint64_t  m_size         = 0;
            uint64_t  m_offset       = 0;
            char      m_buffer[4096] = {};
        };

        struct MaterialUUIDReader : nlohmann::json_sax<nlohmann::json>
        {
            uuids::uuid UUID{};
            int         Depth      = 0;
            bool        RootObject = false;
            bool        UUIDKey    = false;
            bool        null() override
            {
                UUIDKey = false;
                return true;
            }
            bool boolean(bool) override
            {
                return null();
            }
            bool number_integer(number_integer_t) override
            {
                return null();
            }
            bool number_unsigned(number_unsigned_t) override
            {
                return null();
            }
            bool number_float(number_float_t, const string_t&) override
            {
                return null();
            }
            bool string(string_t& value) override
            {
                if (UUIDKey)
                {
                    auto parsed = uuids::uuid::from_string(value);
                    UUID        = parsed.has_value() ? *parsed : uuids::uuid{};
                }
                UUIDKey = false;
                return true;
            }
            bool binary(binary_t&) override
            {
                return null();
            }
            bool start_object(std::size_t) override
            {
                if (Depth == 0)
                {
                    RootObject = true;
                }
                ++Depth;
                UUIDKey = false;
                return true;
            }
            bool key(string_t& value) override
            {
                UUIDKey = Depth == 1 && value == "uuid";
                if (UUIDKey)
                {
                    UUID = {};
                }
                return true;
            }
            bool end_object() override
            {
                --Depth;
                UUIDKey = false;
                return true;
            }
            bool start_array(std::size_t) override
            {
                ++Depth;
                UUIDKey = false;
                return true;
            }
            bool end_array() override
            {
                --Depth;
                UUIDKey = false;
                return true;
            }
            bool parse_error(std::size_t, const std::string&, const nlohmann::detail::exception&) override
            {
                return false;
            }
        };
    } // namespace

    VFSResult<uuids::uuid> ReadEmbeddedAssetUUID(IVFSContext& ctx, const VFSPath& path)
    {
        const bool mesh = path.Extension().Equals(".zemesh");
        if (!mesh && !path.Extension().Equals(".zematerial"))
        {
            return VFSResult<uuids::uuid>::Fail(VFSError::Unsupported);
        }
        auto open = ctx.Open(path, VFSOpenFlags::Read);
        if (open.Failed())
        {
            return VFSResult<uuids::uuid>::Fail(open.Error());
        }
        auto* file   = open.Value();
        auto  result = [&]() -> VFSResult<uuids::uuid> {
            if (mesh)
            {
                uint8_t header[24] = {};
                auto    read       = file->ReadAll({header, sizeof(header)});
                if (read.Failed())
                {
                    return VFSResult<uuids::uuid>::Fail(read.Error());
                }
                if (read.Value() != sizeof(header))
                {
                    return VFSResult<uuids::uuid>::Fail(VFSError::Corrupted);
                }
                uint32_t    magic = 0, version = 0;
                uuids::uuid id = {};
                if (Helpers::secure_memcpy(&magic, sizeof(magic), header, sizeof(magic)) != Helpers::MEMORY_OP_SUCCESS || Helpers::secure_memcpy(&version, sizeof(version), header + 4, sizeof(version)) != Helpers::MEMORY_OP_SUCCESS || Helpers::secure_memcpy(&id, sizeof(id), header + 8, 16) != Helpers::MEMORY_OP_SUCCESS)
                {
                    return VFSResult<uuids::uuid>::Fail(VFSError::Corrupted);
                }
                if (magic != ZEMESH_MAGIC || version != ASSET_FILE_VERSION || id.is_nil())
                {
                    return VFSResult<uuids::uuid>::Fail(VFSError::Corrupted);
                }
                return VFSResult<uuids::uuid>::Ok(id);
            }
            auto size = file->Size();
            if (size.Failed())
            {
                return VFSResult<uuids::uuid>::Fail(size.Error());
            }
            if (size.Value() > MATERIAL_IDENTITY_MAX_FILE_SIZE)
            {
                return VFSResult<uuids::uuid>::Fail(VFSError::SizeLimitExceeded);
            }
            VFSReadBuffer      buffer(file, size.Value());
            std::istream       input(&buffer);
            MaterialUUIDReader reader;
            const bool         valid = nlohmann::json::sax_parse(input, &reader);
            if (buffer.Error != VFSError::OK)
            {
                return VFSResult<uuids::uuid>::Fail(buffer.Error);
            }
            if (!valid || !reader.RootObject || reader.UUID.is_nil())
            {
                return VFSResult<uuids::uuid>::Fail(VFSError::Corrupted);
            }
            return VFSResult<uuids::uuid>::Ok(reader.UUID);
        }();
        const auto close = ctx.CloseChecked(file);
        return result.Succeeded() && close.Failed() ? VFSResult<uuids::uuid>::Fail(close.Error()) : result;
    }

    VFSResult<void> CreatePrivateScratchDirectory(VFSScratchRootLease& root, VFSRelativePath* out)
    {
        if (!out || !root.IsValid())
        {
            return VFSResult<void>::Fail(VFSError::InvalidPath);
        }
        for (uint32_t attempt = 0; attempt < 8; ++attempt)
        {
            char name[42] = {};
            TemporaryName(name);
            const auto path = VFSRelativePath::Parse(name);
            if (path.Failed())
            {
                return VFSResult<void>::Fail(path.Error());
            }
            const auto created = root.CreateDirExclusive(path.Value());
            if (created.Succeeded())
            {
                *out = path.Value();
                return VFSResult<void>::Ok();
            }
            if (created.Error() != VFSError::AlreadyExists)
            {
                return created;
            }
        }
        return VFSResult<void>::Fail(VFSError::AlreadyExists);
    }

    template <typename Context, typename Path>
    VFSAtomicFileWriter<Context, Path>::~VFSAtomicFileWriter()
    {
        (void) Reset();
    }

    template <typename Context, typename Path>
    VFSResult<void> VFSAtomicFileWriter<Context, Path>::Open(Context& ctx, const Path& sibling_of)
    {
        if (m_context || !ValidAtomicPath(sibling_of))
        {
            return VFSResult<void>::Fail(VFSError::InvalidPath);
        }
        for (uint32_t attempt = 0; attempt < 8; ++attempt)
        {
            char name[42] = {};
            TemporaryName(name);
            const auto temporary = TemporarySibling(sibling_of, name);
            if (temporary.Failed())
            {
                return VFSResult<void>::Fail(temporary.Error());
            }
            const auto open = ctx.Open(temporary.Value(), VFSOpenFlags::Write | VFSOpenFlags::Create | VFSOpenFlags::Exclusive);
            if (open.Failed())
            {
                if (open.Error() == VFSError::AlreadyExists)
                {
                    continue;
                }
                // No ownership was acquired: never delete this name on open failure.
                return VFSResult<void>::Fail(open.Error());
            }
            if (!open.Value())
            {
                return VFSResult<void>::Fail(VFSError::IOError);
            }
            m_context   = &ctx;
            m_file      = open.Value();
            m_temporary = temporary.Value();
            m_state     = State::Writing;
            m_bytes     = 0;
            m_error = m_cleanup_error = VFSError::OK;
            return VFSResult<void>::Ok();
        }
        return VFSResult<void>::Fail(VFSError::AlreadyExists);
    }

    template <typename Context, typename Path>
    VFSResult<void> VFSAtomicFileWriter<Context, Path>::Write(Containers::ArrayView<const uint8_t> data)
    {
        if (m_error != VFSError::OK)
        {
            return VFSResult<void>::Fail(m_error);
        }
        if (m_state != State::Writing || !m_file)
        {
            return VFSResult<void>::Fail(VFSError::InvalidPath);
        }
        const auto fail = [&](VFSError error) {
            m_error = error;
            m_state = State::Failed;
            return VFSResult<void>::Fail(error);
        };
        if (data.size() && !data.data())
        {
            return fail(VFSError::IOError);
        }
        if (data.size() > UINT64_MAX - m_bytes)
        {
            return fail(VFSError::SizeLimitExceeded);
        }
        size_t offset = 0;
        while (offset < data.size())
        {
            const auto count = std::min<size_t>(ZKilo(64), data.size() - offset);
            const auto write = m_file->Write({data.data() + offset, count}, m_bytes);
            if (write.Failed() || write.Value() == 0 || write.Value() > count)
            {
                return fail(write.Failed() ? write.Error() : VFSError::IOError);
            }
            offset  += write.Value();
            m_bytes += write.Value();
        }
        return VFSResult<void>::Ok();
    }

    template <typename Context, typename Path>
    VFSResult<void> VFSAtomicFileWriter<Context, Path>::Seal()
    {
        if (!m_file)
        {
            return m_error != VFSError::OK ? VFSResult<void>::Fail(m_error) : m_state == State::Sealed ? VFSResult<void>::Ok() : VFSResult<void>::Fail(VFSError::InvalidPath);
        }
        if (m_error == VFSError::OK)
        {
            m_error = m_file->Flush().Error();
        }
        const auto close = m_context->CloseChecked(m_file);
        m_file           = nullptr;
        if (close.Failed())
        {
            m_cleanup_error = close.Error();
        }
        if (m_error == VFSError::OK)
        {
            m_error = close.Error();
        }
        m_state = m_error == VFSError::OK ? State::Sealed : State::Failed;
        return m_error == VFSError::OK ? VFSResult<void>::Ok() : VFSResult<void>::Fail(m_error);
    }

    template <typename Context, typename Path>
    VFSResult<void> VFSAtomicFileWriter<Context, Path>::Commit(const Path& destination, AtomicWriteMode mode)
    {
        if (m_error != VFSError::OK)
        {
            return VFSResult<void>::Fail(m_error);
        }
        if (m_state != State::Sealed || !ValidAtomicPath(destination) || destination == m_temporary || AtomicDirectory(destination) != AtomicDirectory(m_temporary))
        {
            return VFSResult<void>::Fail(VFSError::InvalidPath);
        }
        if (mode != AtomicWriteMode::Replace && mode != AtomicWriteMode::NoReplace)
        {
            return VFSResult<void>::Fail(VFSError::Unsupported);
        }
        const auto rename = mode == AtomicWriteMode::NoReplace ? m_context->RenameNoReplace(m_temporary, destination) : m_context->Rename(m_temporary, destination);
        if (rename.Succeeded())
        {
            m_state = State::Committed;
        }
        return rename;
    }

    template <typename Context, typename Path>
    VFSResult<void> VFSAtomicFileWriter<Context, Path>::Reset() noexcept
    {
        if (!m_context)
        {
            return m_cleanup_error == VFSError::OK ? VFSResult<void>::Ok() : VFSResult<void>::Fail(m_cleanup_error);
        }
        VFSError error = m_cleanup_error;
        if (m_file)
        {
            const auto close = m_context->CloseChecked(m_file);
            if (error == VFSError::OK)
            {
                error = close.Error();
            }
        }
        if (m_state != State::Committed)
        {
            const auto removed = m_context->Remove(m_temporary);
            if (error == VFSError::OK)
            {
                error = removed.Error();
            }
        }
        m_context       = nullptr;
        m_file          = nullptr;
        m_temporary     = {};
        m_bytes         = 0;
        m_state         = State::Empty;
        m_error         = VFSError::OK;
        m_cleanup_error = error;
        return error == VFSError::OK ? VFSResult<void>::Ok() : VFSResult<void>::Fail(error);
    }

    template class VFSAtomicFileWriter<IVFSContext, VFSPath>;
    template class VFSAtomicFileWriter<VFSPublicationRootLease, VFSRelativePath>;

    template <typename Context, typename Path>
    static VFSResult<void> WriteFileAtomicallyImpl(Context& ctx, const Path& path, Containers::ArrayView<const uint8_t> data, AtomicWriteMode mode)
    {
        if (!ValidAtomicPath(path))
        {
            return VFSResult<void>::Fail(VFSError::InvalidPath);
        }
        if (mode != AtomicWriteMode::Replace && mode != AtomicWriteMode::NoReplace)
        {
            return VFSResult<void>::Fail(VFSError::Unsupported);
        }
        if (data.size() && !data.data())
        {
            return VFSResult<void>::Fail(VFSError::IOError);
        }
        VFSAtomicFileWriter<Context, Path> writer = {};
        auto                               result = writer.Open(ctx, path);
        if (result.Failed())
        {
            return result;
        }
        result = writer.Write(data);
        if (result.Succeeded())
        {
            result = writer.Seal();
        }
        if (result.Succeeded())
        {
            result = writer.Commit(path, mode);
        }
        (void) writer.Reset();
        return result;
    }

    VFSResult<void> WriteFileAtomically(IVFSContext& ctx, const VFSPath& path, Containers::ArrayView<const uint8_t> data, AtomicWriteMode mode)
    {
        if (path.IsRoot())
        {
            return VFSResult<void>::Fail(VFSError::InvalidPath);
        }
        return WriteFileAtomicallyImpl(ctx, path, data, mode);
    }

    VFSResult<void> WriteFileAtomically(VFSPublicationRootLease& root, const VFSRelativePath& path, Containers::ArrayView<const uint8_t> data, AtomicWriteMode mode)
    {
        return WriteFileAtomicallyImpl(root, path, data, mode);
    }
} // namespace ZEngine::Core::VFS
