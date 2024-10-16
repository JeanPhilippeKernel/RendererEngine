#include <pch.h>
#include <Managers/ShaderManager.h>
#include <uuid.h>

namespace ZEngine::Managers
{
    std::unordered_map<std::string, Ref<Rendering::Shaders::Shader>> ShaderManager::s_shader_mappings = {};

    Ref<Rendering::Shaders::Shader> ShaderManager::Get(ZEngine::Rendering::Specifications::ShaderSpecification& spec)
    {
        auto& shader = s_shader_mappings[spec.Name];
        if (!shader)
        {
            spec.VertexFilename          = GetVertexFilename(spec.Name);
            spec.FragmentFilename        = GetFragmentFilename(spec.Name);
            shader                       = ZEngine::Rendering::Shaders::Shader::Create(spec);
            s_shader_mappings[spec.Name] = shader;
        }
        return shader;
    }

    const std::string_view ShaderManager::s_base_dir = "Shaders/Cache/";

    const std::unordered_map<std::string, std::pair<std::string, std::string>> ShaderManager::s_shader_path = {
        {"color", {fmt::format("{}final_color_vertex.spv", s_base_dir), fmt::format("{}final_color_fragment.spv", s_base_dir)}},
        {"g_buffer", {fmt::format("{}g_buffer_vertex.spv", s_base_dir), fmt::format("{}g_buffer_fragment.spv", s_base_dir)}},
        {"imgui", {fmt::format("{}imgui_vertex.spv", s_base_dir), fmt::format("{}imgui_fragment.spv", s_base_dir)}},
        {"infinite_grid", {fmt::format("{}infinite_grid_vertex.spv", s_base_dir), fmt::format("{}infinite_grid_fragment.spv", s_base_dir)}},
        {"skybox", {fmt::format("{}skybox_vertex.spv", s_base_dir), fmt::format("{}skybox_fragment.spv", s_base_dir)}},
        {"depth_prepass_scene", {fmt::format("{}depth_prepass_scene_vertex.spv", s_base_dir), ""}},
        {"deferred_lighting", {fmt::format("{}deferred_lighting_vertex.spv", s_base_dir), fmt::format("{}deferred_lighting_fragment.spv", s_base_dir)}}};

    const std::string ShaderManager::GetFragmentFilename(std::string_view key)
    {
        auto it = s_shader_path.find(key.data());
        if (it == s_shader_path.end())
        {
            return "";
        }
        return it->second.second;
    }

    const std::string ShaderManager::GetVertexFilename(std::string_view key)
    {
        auto it = s_shader_path.find(key.data());
        if (it == s_shader_path.end())
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
