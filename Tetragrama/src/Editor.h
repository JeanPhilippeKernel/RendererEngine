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

    private:
        ZEngine::Ref<ZEngine::Layers::ImguiLayer> m_ui_layer{nullptr};
        ZEngine::Ref<ZEngine::Layers::Layer>      m_rendering_layer{nullptr};
        ZEngine::Scope<ZEngine::Engine>           m_engine{nullptr};
    };

} // namespace Tetragrama
