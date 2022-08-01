#pragma once
#include <ZEngine/Engine.h>
#include <ZEngine/Layers/ImguiLayer.h>
#include <ZEngine/ZEngineDef.h>

namespace Tetragrama {

    class Editor : ZEngine::Core::IInitializable, public std::enable_shared_from_this<Editor> {
    public:
        Editor();
        virtual ~Editor();

        void Initialize() override;
        void Run();

        ZEngine::Ref<ZEngine::EngineConfiguration> GetCurrentEngineConfiguration() const;

    public:
        void LoggerMessageCallback(std::vector<std::string>) const;

    private:
        ZEngine::Ref<ZEngine::EngineConfiguration> m_engine_configuration;
        ZEngine::Ref<ZEngine::Layers::ImguiLayer>  m_ui_layer;
        ZEngine::Ref<ZEngine::Layers::Layer>       m_render_layer;
        ZEngine::Scope<ZEngine::Engine>            m_engine;
    };

} // namespace Tetragrama
