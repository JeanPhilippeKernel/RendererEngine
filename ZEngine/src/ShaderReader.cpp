#include <pch.h>
#include <Core/Coroutine.h>
#include <Logging/LoggerDefinition.h>
#include <Rendering/Shaders/ShaderReader.h>

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

    std::vector<uint32_t> ShaderReader::ReadAsBinary(std::string_view filename)
    {
        std::ifstream file_stream = {};
        file_stream.open(filename, std::ifstream::binary | std::ifstream::ate);
        if (!file_stream.is_open())
        {
            ZENGINE_CORE_ERROR("====== Shader file : {} cannot be opened ======", filename.data())
            ZENGINE_EXIT_FAILURE()
        }

        size_t                buffer_size = static_cast<size_t>(file_stream.tellg());
        std::vector<uint32_t> buffer(buffer_size / 4);
        file_stream.seekg(std::ifstream::beg);
        file_stream.read(reinterpret_cast<char*>(buffer.data()), buffer_size);
        file_stream.close();

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
