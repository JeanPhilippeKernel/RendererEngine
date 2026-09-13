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
        ZENGINE_VALIDATE_ASSERT(arena, "ShaderReader::ReadAsBinary requires an arena")
        auto* vfs      = Engine::GetContext()->VFS;
        auto  path_res = Core::VFS::VFSPath::Parse(filename);
        if (path_res.Failed())
        {
            ZENGINE_CORE_ERROR("====== Shader file : {} — invalid VFS path ======", filename)
            ZENGINE_EXIT_FAILURE()
        }

        auto file_res = vfs->Open(path_res.Value(), Core::VFS::VFSOpenFlags::Read);
        if (file_res.Failed())
        {
            ZENGINE_CORE_ERROR("====== Shader file : {} cannot be opened ======", filename)
            ZENGINE_EXIT_FAILURE()
        }

        auto* file     = file_res.Value();
        auto  size_res = file->Size();
        if (size_res.Failed())
        {
            vfs->Close(file);
            ZENGINE_CORE_ERROR("====== Shader file : {} cannot get size ======", filename)
            ZENGINE_EXIT_FAILURE()
        }

        const uint64_t byte_size = size_res.Value();
        ZENGINE_VALIDATE_ASSERT(byte_size > 0 && byte_size % sizeof(uint32_t) == 0, "Shader binary must contain a non-empty number of 32-bit words")

        Core::Containers::Array<uint32_t> buffer;
        buffer.init(arena, static_cast<size_t>(byte_size / sizeof(uint32_t)), static_cast<size_t>(byte_size / sizeof(uint32_t)));
        Core::Containers::ArrayView<uint8_t> view{reinterpret_cast<uint8_t*>(buffer.data()), byte_size};
        file->ReadAll(view);
        vfs->Close(file);
        return buffer;
    }

    ShaderType ShaderReader::GetShaderType(const std::filesystem::path& path)
    {
        if (path.extension() == ".vert")
            return ShaderType::VERTEX;
        if (path.extension() == ".frag")
            return ShaderType::FRAGMENT;
        if (path.extension() == ".geom")
            return ShaderType::GEOMETRY;
        if (path.extension() == ".comp")
            return ShaderType::COMPUTE;
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
