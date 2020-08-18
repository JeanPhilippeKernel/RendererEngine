#pragma once
#include <memory>

#include "RenderCommand.h"
#include "../Buffers/VertexArray.h"
#include "../Cameras/Camera.h"
#include "GraphicScene.h"																																		 

#include "../../Z_EngineDef.h"
#include "../../Core/IInitializable.h"

namespace Z_Engine::Rendering::Renderer {
	
	class Z_ENGINE_API GraphicRenderer : public Core::IInitializable {
	public:
		GraphicRenderer() =  default;
		~GraphicRenderer() = default;


		void Initialize() override {
			//enable management of transparent background image (RGBA-8)
			glEnable(GL_BLEND);
			glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		}

		void BeginScene(const Ref<Cameras::Camera>& camera) {
			  m_scene.SetCamera(camera);
		}

		void EndScene() {

		}

		template<typename T, typename K>
		constexpr void Submit(const Ref<Buffers::VertexArray<T, K>>& vertex_array) {
			RendererCommand::DrawIndexed(vertex_array);
		}

		template<typename T, typename K>
		constexpr void Submit(const Ref<Shaders::Shader>& shader, const Ref<Buffers::VertexArray<T, K>>& vertex_array) {
			shader->SetUniform("u_ViewProjectionMat", m_scene.GetCamera()->GetViewProjectionMatrix());
			RendererCommand::DrawIndexed(shader, vertex_array);
		}

		template<typename T, typename K>
		constexpr void Submit(const Ref<Shaders::Shader>& shader, const std::initializer_list<Ref<Buffers::VertexArray<T, K>>> vertex_array_list) {
			std::vector<Ref<Buffers::VertexArray<T, K>>> list{vertex_array_list};
			shader->SetUniform("u_ViewProjectionMat", m_scene.GetCamera()->GetViewProjectionMatrix());
			RendererCommand::DrawIndexed(shader, list);
		}


		template<typename T, typename K>
		constexpr void Submit(const Ref<Shaders::Shader>& shader, const Ref<Buffers::VertexArray<T, K>>& vertex_array, const glm::mat4& transform) {
			shader->SetUniform("u_TransformMat", transform);
			shader->SetUniform("u_ViewProjectionMat", m_scene.GetCamera()->GetViewProjectionMatrix());
			RendererCommand::DrawIndexed(shader, vertex_array);
		}

		template<typename T, typename K>
		constexpr void Submit(const Ref<Shaders::Shader>& shader, const std::initializer_list<Ref<Buffers::VertexArray<T, K>>> vertex_array_list, const glm::mat4& transform) {
			std::vector<Ref<Buffers::VertexArray<T, K>>> list{ vertex_array_list };
			shader->SetUniform("u_TransformMat", transform);
			shader->SetUniform("u_ViewProjectionMat", m_scene.GetCamera()->GetViewProjectionMatrix());
			RendererCommand::DrawIndexed(shader, list);
		}

	private:
		GraphicScene m_scene;
	};
}