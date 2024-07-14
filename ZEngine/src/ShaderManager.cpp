#include <pch.h>
#include <Managers/ShaderManager.h>
#include <uuid.h>

namespace ZEngine::Managers
{
    std::unordered_map<std::string, Ref<Rendering::Shaders::Shader>> ShaderManager::m_shader_mappings = {};

    Ref<Rendering::Shaders::Shader> ShaderManager::Get(ZEngine::Rendering::Specifications::ShaderSpecification spec)
    {
        spec.VertexFilename   = GetVertexFilename(spec.Name);
        spec.FragmentFilename = GetFragmentFilename(spec.Name);

        auto& shader = m_shader_mappings[spec.Name];
        if (!shader)
        {
            shader                       = ZEngine::Rendering::Shaders::Shader::Create(spec);
            m_shader_mappings[spec.Name] = shader;
        }
        return shader;
    }

    const std::string_view ShaderManager::base_dir = "Shaders/Cache/";

    const std::unordered_map<std::string, std::pair<std::string, std::string>> ShaderManager::m_shader_path = {
        {"color", {fmt::format("{}final_color_vertex.spv", base_dir), fmt::format("{}final_color_fragment.spv", base_dir)}},
        {"g_buffer", {fmt::format("{}g_buffer_vertex.spv", base_dir), fmt::format("{}g_buffer_fragment.spv", base_dir)}},
        {"imgui", {fmt::format("{}imgui_vertex.spv", base_dir), fmt::format("{}imgui_fragment.spv", base_dir)}},
        {"infinite_grid", {fmt::format("{}infinite_grid_vertex.spv", base_dir), fmt::format("{}infinite_grid_fragment.spv", base_dir)}},
        {"skybox", {fmt::format("{}skybox_vertex.spv", base_dir), fmt::format("{}skybox_fragment.spv", base_dir)}},
        {"depth_prepass_scene", {fmt::format("{}depth_prepass_scene_vertex.spv", base_dir), ""}},
        {"deferred_lighting", {fmt::format("{}deferred_lighting_vertex.spv", base_dir), fmt::format("{}deferred_lighting_fragment.spv", base_dir)}}};

    const std::string ShaderManager::GetFragmentFilename(const std::string_view key)
    {
        auto it = m_shader_path.find(key.data());
        if (it == m_shader_path.end())
        {
            return "";
        }
        return it->second.second;
    }

    const std::string ShaderManager::GetVertexFilename(const std::string_view key)
    {
        auto it = m_shader_path.find(key.data());
        if (it == m_shader_path.end())
        {
            return "";
        }
        return it->second.first;
    }

    ShaderManager::ShaderManager() : IAssetManager()
    {
        this->m_suffix = "_shader";
    }

    Ref<Rendering::Shaders::Shader> ShaderManager::Add(const char* name, const char* filename)
    {

        Ref<Rendering::Shaders::Shader> shader;
        shader.reset(Rendering::Shaders::CreateShader(filename, true));

        std::random_device                                     rd;
        std::ranlux48_base                                     generator(rd());
        uuids::basic_uuid_random_generator<std::ranlux48_base> gen(&generator);

        uuids::uuid const guid   = gen();
        const auto        key    = uuids::to_string(guid) + m_suffix;
        const auto        result = IManager::Add(key, std::move(shader));

        assert(result.has_value() == true);
        return result->get();
    }

    Ref<Rendering::Shaders::Shader> ShaderManager::Load(const char* filename)
    {
        std::filesystem::path p(filename);
        const auto            name = p.stem();
        return Add(reinterpret_cast<const char*>(name.u8string().c_str()), filename);
    }
} // namespace ZEngine::Managers
