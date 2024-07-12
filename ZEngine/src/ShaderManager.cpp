#include <pch.h>
#include <Managers/ShaderManager.h>
#include <uuid.h>

namespace ZEngine::Managers
{
    std::unordered_map<std::string, Ref<Rendering::Shaders::Shader>> ShaderManager::m_shaderMappings = {};

    Ref<Rendering::Shaders::Shader> ShaderManager::Get(ZEngine::Rendering::Specifications::ShaderSpecification spec)
    {
        spec.VertexFilename = GetVertexFilename(spec.Name);
        spec.FragmentFilename = GetFragmentFilename(spec.Name);

        auto& shader = m_shaderMappings[spec.Name];
        if (!shader)
        {
            shader = ZEngine::Rendering::Shaders::Shader::Create(spec);
            m_shaderMappings[spec.Name] = shader;
        }
        return shader;
    }

    const std::string ShaderManager::base_dir = "Shaders/Cache/";

    const std::unordered_map<std::string, std::pair<std::string, std::string>> ShaderManager::m_shaderPath = {

        {"color", {base_dir + "final_color_vertex.spv", base_dir + "final_color_fragment.spv"}},
        {"g_buffer", {base_dir + "g_buffer_vertex.spv", base_dir + "g_buffer_fragment.spv"}},
        {"imgui", {base_dir + "imgui_vertex.spv", base_dir + "imgui_fragment.spv"}},
        {"infinite_grid", {base_dir + "infinite_grid_vertex.spv", base_dir + "infinite_grid_fragment.spv"}},
        {"skybox", {base_dir + "skybox_vertex.spv", base_dir + "skybox_fragment.spv"}},
        {"depth_prepass_scene", {base_dir + "depth_prepass_scene_vertex.spv", ""}}};

    const std::string ShaderManager::GetFragmentFilename(std::string_view key)
    {
        auto it = m_shaderPath.find(key.data());
        if (it == m_shaderPath.end())
        {
            return "";
        }
        return it->second.second;
    }

    const std::string ShaderManager::GetVertexFilename(std::string_view key)
    {
        auto it = m_shaderPath.find(key.data());
        if (it == m_shaderPath.end())
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

        std::random_device rd;
        std::ranlux48_base generator(rd());
        uuids::basic_uuid_random_generator<std::ranlux48_base> gen(&generator);

        uuids::uuid const guid = gen();
        const auto key = uuids::to_string(guid) + m_suffix;
        const auto result = IManager::Add(key, std::move(shader));

        assert(result.has_value() == true);
        return result->get();
    }

    Ref<Rendering::Shaders::Shader> ShaderManager::Load(const char* filename)
    {
        std::filesystem::path p(filename);
        const auto name = p.stem();
        return Add(reinterpret_cast<const char*>(name.u8string().c_str()), filename);
    }
} // namespace ZEngine::Managers
