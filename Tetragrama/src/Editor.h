#pragma once
#include <ZEngine/Engine.h>
#include <ZEngine/ZEngineDef.h>
#include <ZEngine/Layers/ImguiLayer.h>
#include <Layers/RenderLayer.h>

namespace Tetragrama
{

    class Editor : ZEngine::Core::IInitializable, public std::enable_shared_from_this<Editor>
    {
    public:
        Editor();
        virtual ~Editor();

        void Initialize() override;
        void Run();

        const ZEngine::EngineConfiguration& GetCurrentEngineConfiguration() const;

    private:
        ZEngine::EngineConfiguration               m_engine_configuration;
        ZEngine::Ref<ZEngine::Layers::ImguiLayer>  m_ui_layer;
        ZEngine::Ref<Layers::RenderLayer>          m_render_layer;
    };

} // namespace Tetragrama
