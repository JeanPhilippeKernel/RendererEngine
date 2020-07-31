#pragma once
#include <memory>

#include "RenderCommand.h"
#include "../Buffer/VertexArray.h"
#include "../Cameras/Camera.h"
#include "GraphicScene.h"

#include "../../Z_EngineDef.h"

using namespace Z_Engine::Rendering::Buffer;

namespace Z_Engine::Rendering::Renderer {
	
	class Z_ENGINE_API GraphicRenderer {
	public:
		GraphicRenderer() =  default;
		~GraphicRenderer() = default;

		void BeginScene(const std::shared_ptr<Cameras::Camera>& camera) {
			  m_scene.SetCamera(camera);
		}

		void EndScene() {

		}

		template<typename T, typename K>
		void Submit(const std::shared_ptr<VertexArray<T, K>>& vertex_array) {
			RendererCommand::DrawIndexed(vertex_array);
		}

		template<typename T, typename K>
		void Submit(const std::shared_ptr<Shader>& shader, const std::shared_ptr<VertexArray<T, K>>& vertex_array) {
			RendererCommand::DrawIndexed(shader, vertex_array);
		}

		template<typename T, typename K>
		void Submit(const std::shared_ptr<Shader>& shader, const std::initializer_list<std::shared_ptr<VertexArray<T, K>>> vertex_array_list) {
			std::vector<std::shared_ptr<VertexArray<T, K>>> list{vertex_array_list};
			RendererCommand::DrawIndexed(shader, list);
		}

	private:
		GraphicScene m_scene;
	};
}