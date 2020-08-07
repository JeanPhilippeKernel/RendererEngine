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

		void BeginScene(const Ref<Cameras::Camera>& camera) {
			  m_scene.SetCamera(camera);
		}

		void EndScene() {

		}

		template<typename T, typename K>
		void Submit(const Ref<VertexArray<T, K>>& vertex_array) {
			RendererCommand::DrawIndexed(vertex_array);
		}

		template<typename T, typename K>
		void Submit(const Ref<Shader>& shader, const Ref<VertexArray<T, K>>& vertex_array) {
			shader->SetUniform("u_ViewProjectionMat", m_scene.GetCamera()->GetViewProjectionMatrix());
			RendererCommand::DrawIndexed(shader, vertex_array);
		}

		template<typename T, typename K>
		void Submit(const Ref<Shader>& shader, const std::initializer_list<Ref<VertexArray<T, K>>> vertex_array_list) {
			std::vector<Ref<VertexArray<T, K>>> list{vertex_array_list};
			shader->SetUniform("u_ViewProjectionMat", m_scene.GetCamera()->GetViewProjectionMatrix());
			RendererCommand::DrawIndexed(shader, list);
		}


		template<typename T, typename K>
		void Submit(const Ref<Shader>& shader, const Ref<VertexArray<T, K>>& vertex_array, const glm::mat4& transform) {
			shader->SetUniform("u_TransformMat", transform);
			shader->SetUniform("u_ViewProjectionMat", m_scene.GetCamera()->GetViewProjectionMatrix());
			RendererCommand::DrawIndexed(shader, vertex_array);
		}

		template<typename T, typename K>
		void Submit(const Ref<Shader>& shader, const std::initializer_list<Ref<VertexArray<T, K>>> vertex_array_list, const glm::mat4& transform) {
			std::vector<Ref<VertexArray<T, K>>> list{ vertex_array_list };
			shader->SetUniform("u_TransformMat", transform);
			shader->SetUniform("u_ViewProjectionMat", m_scene.GetCamera()->GetViewProjectionMatrix());
			RendererCommand::DrawIndexed(shader, list);
		}

	private:
		GraphicScene m_scene;
	};
}