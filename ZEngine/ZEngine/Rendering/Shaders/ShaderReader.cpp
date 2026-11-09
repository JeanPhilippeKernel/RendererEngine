#include <ZEngine/Core/Containers/Array.h>
#include <ZEngine/Core/Coroutine.h>
#include <ZEngine/Core/VFS/IVFSFile.h>
#include <ZEngine/Core/VFS/VFSPath.h>
#include <ZEngine/Engine.h>
#include <ZEngine/Logging/LoggerDefinition.h>
#include <ZEngine/Rendering/Shaders/ShaderReader.h>

namespace ZEngine::Rendering::Shaders
{

    ShaderReader::ShaderReader() {}

    ShaderReader::~ShaderReader()
    {
        if (m_filestream.is_open())
        {
            m_filestream.close();
        }
    }

    Core::Containers::Array<uint32_t> ShaderReader::ReadAsBinary(Core::Memory::ArenaAllocator* arena, cstring filename)
    {
        Core::Containers::Array<uint32_t> buffer  = {};
        auto*                             context = Engine::GetContext();
        ZENGINE_VALIDATE_ASSERT(context && context->VFS && TryReadAsBinary(*context->VFS, arena, filename, arena ? arena->m_total_size : 0, &buffer), "Failed to read shader binary")
        return buffer;
    }

    bool ShaderReader::TryReadAsBinary(Core::VFS::IVFSContext& vfs, Core::Memory::ArenaAllocator* arena, cstring filename, size_t max_bytes, Core::Containers::Array<uint32_t>* out)
    {
        if (!arena || !out || !filename || !max_bytes)
        {
            return false;
        }
        auto path_res = Core::VFS::VFSPath::Parse(filename);
        if (path_res.Failed())
        {
            return false;
        }

        auto file_res = vfs.Open(path_res.Value(), Core::VFS::VFSOpenFlags::Read);
        if (file_res.Failed())
        {
            return false;
        }

        auto* file     = file_res.Value();
        auto  size_res = file->Size();
        if (size_res.Failed())
        {
            vfs.Close(file);
            return false;
        }

        const uint64_t byte_size = size_res.Value();
        if (byte_size < 5 * sizeof(uint32_t) || byte_size > max_bytes || byte_size % sizeof(uint32_t))
        {
            vfs.Close(file);
            return false;
        }
        Core::Memory::ArenaCheckpoint     checkpoint(arena);
        Core::Containers::Array<uint32_t> buffer = {};
        buffer.m_allocator                       = arena;
        if (!buffer.try_reserve(static_cast<size_t>(byte_size / sizeof(uint32_t))))
        {
            vfs.Close(file);
            return false;
        }
        buffer.m_size = byte_size / sizeof(uint32_t);
        Core::Containers::ArrayView<uint8_t> view{reinterpret_cast<uint8_t*>(buffer.data()), byte_size};
        const auto                           read = file->ReadAll(view);
        vfs.Close(file);
        if (read.Failed() || read.Value() != byte_size || buffer[0] != 0x07230203u || buffer[4] != 0)
        {
            return false;
        }
        *out = std::move(buffer);
        checkpoint.Commit();
        return true;
    }

    ShaderType ShaderReader::GetShaderType(const std::filesystem::path& path)
    {
        if (path.extension() == ".vert")
        {
            return ShaderType::VERTEX;
        }
        if (path.extension() == ".frag")
        {
            return ShaderType::FRAGMENT;
        }
        if (path.extension() == ".geom")
        {
            return ShaderType::GEOMETRY;
        }
        if (path.extension() == ".comp")
        {
            return ShaderType::COMPUTE;
        }
        return ShaderType::UNKNOWN;
    }

    std::future<ShaderOperationResult> ShaderReader::ReadAsync(std::string_view filename)
    {
        std::unique_lock<std::mutex> lock(m_lock);
        std::filesystem::path        filepath(filename);

        m_filestream.open(filename.data(), std::ifstream::in);
        if (!m_filestream.is_open())
        {
            ZENGINE_CORE_ERROR("====== Shader file : {} cannot be opened ======", filename.data())
            co_return ShaderOperationResult::FAILURE;
        }

        std::streamsize size = m_filestream.tellg();
        m_filestream.seekg(0, std::ios::beg);

        std::string buffer;
        buffer.reserve(size);

        buffer.assign((std::istreambuf_iterator<char>(m_filestream)), std::istreambuf_iterator<char>());

        m_shader_info_collection.Source = std::move(buffer);
        m_shader_info_collection.Name   = filepath.stem().string();
        m_shader_info_collection.Type   = GetShaderType(filepath);

        if (m_shader_info_collection.Type == ShaderType::UNKNOWN)
        {
            ZENGINE_CORE_ERROR("====== Shader file : {} unsupported format ======", filename.data())
            co_return ShaderOperationResult::FAILURE;
        }

        ZENGINE_CORE_INFO("====== Shader file : {} read succeeded ======", filename.data())
        m_filestream.close();
        co_return ShaderOperationResult::SUCCESS;
    }

    const ShaderInformation& ShaderReader::GetInformations() const
    {
        return m_shader_info_collection;
    }

    ShaderInformation& ShaderReader::GetInformations()
    {
        return m_shader_info_collection;
    }
} // namespace ZEngine::Rendering::Shaders
