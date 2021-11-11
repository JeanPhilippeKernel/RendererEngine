#pragma once
#include <ZEngine/ZEngineDef.h>
#include <ZEngine/Engine.h>
#include <ZEngine/Layers/ImguiLayer.h>

namespace Tetragrama {

	class Editor : ZEngine::Core::IInitializable {
	public:
		Editor();
		virtual ~Editor();

		void Initialize() override;
        void Run();

	private:
		ZEngine::Ref<ZEngine::Layers::ImguiLayer>	m_ui_layer{ nullptr };
		ZEngine::Scope<ZEngine::Engine>				m_engine{ nullptr };
	};

}
