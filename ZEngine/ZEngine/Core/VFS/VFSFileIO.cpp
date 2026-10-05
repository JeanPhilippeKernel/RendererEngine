#include <ZEngine/Core/VFS/VFSFileIO.h>
#include <nlohmann/json.hpp>
#include <uuid.h>
#include <algorithm>
#include <cstring>
#include <istream>
#include <random>

namespace ZEngine::Core::VFS
{
    namespace
    {
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
                    return traits_type::eof();
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
            IVFSFile* m_file;
            uint64_t  m_size;
            uint64_t  m_offset = 0;
            char      m_buffer[4096];
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
                    RootObject = true;
                ++Depth;
                UUIDKey = false;
                return true;
            }
            bool key(string_t& value) override
            {
                UUIDKey = Depth == 1 && value == "uuid";
                if (UUIDKey)
                    UUID = {};
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
            return VFSResult<uuids::uuid>::Fail(VFSError::Unsupported);
        auto open = ctx.Open(path, VFSOpenFlags::Read);
        if (open.Failed())
            return VFSResult<uuids::uuid>::Fail(open.Error());
        auto* file   = open.Value();
        auto  result = [&]() -> VFSResult<uuids::uuid> {
            if (mesh)
            {
                uint8_t header[24];
                auto    read = file->ReadAll({header, sizeof(header)});
                if (read.Failed())
                    return VFSResult<uuids::uuid>::Fail(read.Error());
                if (read.Value() != sizeof(header))
                    return VFSResult<uuids::uuid>::Fail(VFSError::Corrupted);
                uint32_t    magic, version;
                uuids::uuid id;
                std::memcpy(&magic, header, 4);
                std::memcpy(&version, header + 4, 4);
                std::memcpy(&id, header + 8, 16);
                if (magic != ZEMESH_MAGIC || version != ASSET_FILE_VERSION || id.is_nil())
                    return VFSResult<uuids::uuid>::Fail(VFSError::Corrupted);
                return VFSResult<uuids::uuid>::Ok(id);
            }
            auto size = file->Size();
            if (size.Failed())
                return VFSResult<uuids::uuid>::Fail(size.Error());
            if (size.Value() > MATERIAL_IDENTITY_MAX_FILE_SIZE)
                return VFSResult<uuids::uuid>::Fail(VFSError::SizeLimitExceeded);
            VFSReadBuffer      buffer(file, size.Value());
            std::istream       input(&buffer);
            MaterialUUIDReader reader;
            const bool         valid = nlohmann::json::sax_parse(input, &reader);
            if (buffer.Error != VFSError::OK)
                return VFSResult<uuids::uuid>::Fail(buffer.Error);
            if (!valid || !reader.RootObject || reader.UUID.is_nil())
                return VFSResult<uuids::uuid>::Fail(VFSError::Corrupted);
            return VFSResult<uuids::uuid>::Ok(reader.UUID);
        }();
        ctx.Close(file);
        return result;
    }

    VFSResult<void> WriteFileAtomically(IVFSContext& ctx, const VFSPath& path, Containers::ArrayView<const uint8_t> data)
    {
        if (!path.IsValid() || path.IsRoot())
            return VFSResult<void>::Fail(VFSError::InvalidPath);

        std::random_device                                     random;
        uuids::basic_uuid_random_generator<std::random_device> generator(random);
        const auto                                             temporary = path.Parent().Append(("." + uuids::to_string(generator()) + ".tmp").c_str());
        if (temporary.Failed())
            return VFSResult<void>::Fail(temporary.Error());
        const auto& tmp_path = temporary.Value();
        auto        fail     = [&](VFSError error) {
            (void) ctx.Remove(tmp_path);
            return VFSResult<void>::Fail(error);
        };
        auto open = ctx.Open(tmp_path, VFSOpenFlags::Write | VFSOpenFlags::Create | VFSOpenFlags::Truncate);
        if (open.Failed())
            return fail(open.Error());
        auto* file  = open.Value();
        auto  write = file->Write(data, 0);
        auto  flush = file->Flush();
        auto  close = file->Close();
        ctx.Close(file);
        if (write.Failed())
            return fail(write.Error());
        if (write.Value() != data.size())
            return fail(VFSError::IOError);
        if (flush.Failed())
            return fail(flush.Error());
        if (close.Failed())
            return fail(close.Error());
        auto rename = ctx.Rename(tmp_path, path);
        return rename.Failed() ? fail(rename.Error()) : rename;
    }
} // namespace ZEngine::Core::VFS
