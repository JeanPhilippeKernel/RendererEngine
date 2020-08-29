#pragma once
#include <memory>

#include "RenderCommand.h"
#include "GraphicRendererStorage.h"
#include "GraphicScene.h"		

#include "../Buffers/VertexArray.h"
#include "../Cameras/Camera.h"

#include "../../Z_EngineDef.h"
#include "../../Core/IInitializable.h"
#include "../Textures/Texture.h"


namespace Z_Engine::Rendering::Renderer {
	
	class Z_ENGINE_API GraphicRenderer : public Core::IInitializable {
	public:
		GraphicRenderer()	= default;
		~GraphicRenderer()	= default;


		void Initialize() override {
			//enable management of transparent background image (RGBA-8)
			glEnable(GL_BLEND);
			glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		}

	protected:
		void BeginScene(const Ref<Cameras::Camera>& camera) {
			  m_scene.SetCamera(camera);
		}

		virtual void EndScene() = 0;

		template<typename T, typename K>
		constexpr void Submit(const Ref<Buffers::VertexArray<T, K>>& vertex_array) {
			RendererCommand::DrawIndexed(vertex_array);
		}

		template<typename T, typename K>
		constexpr void Submit(const Ref<Shaders::Shader>& shader, const Ref<Buffers::VertexArray<T, K>>& vertex_array) {
			shader->SetUniform("uniform_viewprojection", m_scene.GetCamera()->GetViewProjectionMatrix());
			RendererCommand::DrawIndexed(shader, vertex_array);
		}

		template<typename T, typename K>
		constexpr void Submit(const Ref<Shaders::Shader>& shader, const std::initializer_list<Ref<Buffers::VertexArray<T, K>>> vertex_array_list) {
			std::vector<Ref<Buffers::VertexArray<T, K>>> list{vertex_array_list};
			shader->SetUniform("uniform_viewprojection", m_scene.GetCamera()->GetViewProjectionMatrix());
			RendererCommand::DrawIndexed(shader, list);
		}


		template<typename T, typename K>
		constexpr void Submit(const Ref<Shaders::Shader>& shader, const Ref<Buffers::VertexArray<T, K>>& vertex_array, const glm::mat4& transform) {
			shader->SetUniform("uniform_transform", transform);
			shader->SetUniform("uniform_viewprojection", m_scene.GetCamera()->GetViewProjectionMatrix());
			RendererCommand::DrawIndexed(shader, vertex_array);
		}

		template<typename T, typename K>
		constexpr void Submit(const Ref<Shaders::Shader>& shader, const std::initializer_list<Ref<Buffers::VertexArray<T, K>>> vertex_array_list, const glm::mat4& transform) {
			std::vector<Ref<Buffers::VertexArray<T, K>>> list{ vertex_array_list };
			shader->SetUniform("uniform_transform", transform);
			shader->SetUniform("uniform_viewprojection", m_scene.GetCamera()->GetViewProjectionMatrix());
			RendererCommand::DrawIndexed(shader, list);
		}

		template<typename T, typename K>
		constexpr void Submit(const Ref<Shaders::Shader>& shader, const Ref<Buffers::VertexArray<T, K>>& vertex_array, const glm::mat4& transform, const glm::vec4& color) {
			shader->SetUniform("uniform_transform", transform);
			shader->SetUniform("uniform_color", color);
			shader->SetUniform("uniform_viewprojection", m_scene.GetCamera()->GetViewProjectionMatrix());
			RendererCommand::DrawIndexed(shader, vertex_array);
		}

		template<typename T, typename K>
		constexpr void Submit(const Ref<Shaders::Shader>& shader, const std::initializer_list<Ref<Buffers::VertexArray<T, K>>> vertex_array_list, const glm::mat4& transform, const glm::vec4& color) {
			std::vector<Ref<Buffers::VertexArray<T, K>>> list{ vertex_array_list };
			shader->SetUniform("uniform_transform", transform);
			shader->SetUniform("uniform_color", color);
			shader->SetUniform("uniform_viewprojection", m_scene.GetCamera()->GetViewProjectionMatrix());
			RendererCommand::DrawIndexed(shader, list);
		}

		template<typename T, typename K>
		constexpr void Submit(const Ref<Shaders::Shader>& shader, const Ref<Buffers::VertexArray<T, K>>& vertex_array, const glm::mat4& transform, const Ref<Textures::Texture>& texture) {
			shader->SetUniform("uniform_transform", transform);
			texture->Bind();
			shader->SetUniform("uniform_texture", 0);
			shader->SetUniform("uniform_viewprojection", m_scene.GetCamera()->GetViewProjectionMatrix());
			RendererCommand::DrawIndexed(shader, vertex_array);

			texture->Unbind();
		}

		template<typename T, typename K>
		constexpr void Submit(const Ref<Shaders::Shader>& shader, const std::initializer_list<Ref<Buffers::VertexArray<T, K>>> vertex_array_list, const glm::mat4& transform, const Ref<Textures::Texture>& texture) {
			std::vector<Ref<Buffers::VertexArray<T, K>>> list{ vertex_array_list };
			shader->SetUniform("uniform_transform", transform);
			texture->Bind();
			shader->SetUniform("uniform_texture", 0);
			shader->SetUniform("uniform_viewprojection", m_scene.GetCamera()->GetViewProjectionMatrix());
			RendererCommand::DrawIndexed(shader, list);

			texture->Unbind();
		}

																 
	protected:
		GraphicScene m_scene;
	};
}