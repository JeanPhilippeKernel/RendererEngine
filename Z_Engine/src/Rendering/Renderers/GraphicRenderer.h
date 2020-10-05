#pragma once
#include <memory>

#include "RenderCommand.h"
#include "Storages/GraphicRendererStorage.h"
#include "../Scenes/GraphicScene.h"		

#include "../Buffers/VertexArray.h"
#include "../Cameras/Camera.h"

#include "../../Z_EngineDef.h"
#include "../../Core/IInitializable.h"
#include "../Textures/Texture.h"

#include "../../Managers/ShaderManager.h"
#include "../../Managers/TextureManager.h"


namespace Z_Engine::Rendering::Renderers {
	
	class GraphicRenderer : public Core::IInitializable {
	public:
		GraphicRenderer();
		~GraphicRenderer()	= default;


		void Initialize() override {
			//enable management of transparent background image (RGBA-8)
			glEnable(GL_BLEND);
			glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		}

	protected:
		virtual void BeginScene(const Ref<Cameras::Camera>& camera) {
			  m_scene->SetCamera(camera);
		}

		virtual void EndScene() = 0;

		template<typename T, typename K>
		 void Submit(const Ref<Buffers::VertexArray<T, K>>& vertex_array) {
			RendererCommand::DrawIndexed(vertex_array);
		}

		template<typename T, typename K>
		void Submit(const Ref<Shaders::Shader>& shader, const Ref<Buffers::VertexArray<T, K>>& vertex_array) {
			shader->SetUniform("uniform_viewprojection", m_scene->GetCamera()->GetViewProjectionMatrix());
			RendererCommand::DrawIndexed(shader, vertex_array);
		}

		template<typename T, typename K>
		void Submit(const Ref<Shaders::Shader>& shader, const std::initializer_list<Ref<Buffers::VertexArray<T, K>>> vertex_array_list) {
			std::vector<Ref<Buffers::VertexArray<T, K>>> list{vertex_array_list};
			shader->SetUniform("uniform_viewprojection", m_scene->GetCamera()->GetViewProjectionMatrix());
			RendererCommand::DrawIndexed(shader, list);
		}


		template<typename T, typename K>
		void Submit(const Ref<Shaders::Shader>& shader, const Ref<Buffers::VertexArray<T, K>>& vertex_array, const glm::mat4& transform) {
			shader->SetUniform("uniform_transform", transform);
			shader->SetUniform("uniform_viewprojection", m_scene->GetCamera()->GetViewProjectionMatrix());
			RendererCommand::DrawIndexed(shader, vertex_array);
		}

		template<typename T, typename K>
		void Submit(const Ref<Shaders::Shader>& shader, const std::initializer_list<Ref<Buffers::VertexArray<T, K>>> vertex_array_list, const glm::mat4& transform) {
			std::vector<Ref<Buffers::VertexArray<T, K>>> list{ vertex_array_list };
			shader->SetUniform("uniform_transform", transform);
			shader->SetUniform("uniform_viewprojection", m_scene->GetCamera()->GetViewProjectionMatrix());
			RendererCommand::DrawIndexed(shader, list);
		}


		template<typename T, typename K>
		void Submit(const Ref<Shaders::Shader>& shader, const Ref<Buffers::VertexArray<T, K>>& vertex_array, const glm::mat4& transform, const Ref<Textures::Texture>& texture) {
			shader->SetUniform("uniform_transform", transform);
			texture->Bind();
			shader->SetUniform("uniform_texture", 0); //0 means GL_TEXTURE0
			shader->SetUniform("uniform_tex_tint_color", glm::vec4(1.0f));
			shader->SetUniform("uniform_tex_tiling_factor", 1.0f);
			shader->SetUniform("uniform_viewprojection", m_scene->GetCamera()->GetViewProjectionMatrix());
			RendererCommand::DrawIndexed(shader, vertex_array);

			texture->Unbind();
		}

		template<typename T, typename K>
		 void Submit(const Ref<Shaders::Shader>& shader, const Ref<Buffers::VertexArray<T, K>>& vertex_array, const glm::mat4& transform, const Ref<Textures::Texture>& texture, const glm::vec4& tint_color, float tiling_factor) {
			shader->SetUniform("uniform_transform", transform);
			texture->Bind();
			shader->SetUniform("uniform_texture", 0); //0 means GL_TEXTURE0
			shader->SetUniform("uniform_tex_tint_color", tint_color);
			shader->SetUniform("uniform_tex_tiling_factor", tiling_factor); 
			shader->SetUniform("uniform_viewprojection", m_scene->GetCamera()->GetViewProjectionMatrix());
			RendererCommand::DrawIndexed(shader, vertex_array);

			texture->Unbind();
		}

		template<typename T, typename K>
		 void Submit(const Ref<Shaders::Shader>& shader, const std::initializer_list<Ref<Buffers::VertexArray<T, K>>> vertex_array_list, const glm::mat4& transform, const Ref<Textures::Texture>& texture) {
			std::vector<Ref<Buffers::VertexArray<T, K>>> list{ vertex_array_list };
			shader->SetUniform("uniform_transform", transform);
			texture->Bind();
			shader->SetUniform("uniform_texture", 0); //0 means GL_TEXTURE0
			shader->SetUniform("uniform_tex_tint_color", glm::vec4(1.0f));
			shader->SetUniform("uniform_tex_tiling_factor", 1.0f);
			shader->SetUniform("uniform_viewprojection", m_scene->GetCamera()->GetViewProjectionMatrix());
			RendererCommand::DrawIndexed(shader, list);

			texture->Unbind();
		}

		template<typename T, typename K>
		 void Submit(const Ref<Shaders::Shader>& shader, const std::initializer_list<Ref<Buffers::VertexArray<T, K>>> vertex_array_list, const glm::mat4& transform, const Ref<Textures::Texture>& texture, const glm::vec4& tint_color, float tiling_factor) {
			std::vector<Ref<Buffers::VertexArray<T, K>>> list{ vertex_array_list };
			shader->SetUniform("uniform_transform", transform);
			texture->Bind();
			shader->SetUniform("uniform_texture", 0); //0 means GL_TEXTURE0
			shader->SetUniform("uniform_tex_tint_color", tint_color);
			shader->SetUniform("uniform_tex_tiling_factor", tiling_factor);
			shader->SetUniform("uniform_viewprojection", m_scene->GetCamera()->GetViewProjectionMatrix());
			RendererCommand::DrawIndexed(shader, list);

			texture->Unbind();
		}

		/*template<typename T, typename K>
		 void Submit(const Ref<Shaders::Shader>& shader, const Ref<Buffers::VertexArray<T, K>>& vertex_array, const glm::mat4& transform, const glm::vec4& color) {
			shader->SetUniform("uniform_transform", transform);
			shader->SetUniform("uniform_viewprojection", m_scene.GetCamera()->GetViewProjectionMatrix());
			RendererCommand::DrawIndexed(shader, vertex_array);
		}*/

		//template<typename T, typename K>
		// void Submit(const Ref<Shaders::Shader>& shader, const std::initializer_list<Ref<Buffers::VertexArray<T, K>>> vertex_array_list, const glm::mat4& transform, const glm::vec4& color) {
		//	std::vector<Ref<Buffers::VertexArray<T, K>>> list{ vertex_array_list };
		//	shader->SetUniform("uniform_transform", transform);
		//	shader->SetUniform("uniform_viewprojection", m_scene.GetCamera()->GetViewProjectionMatrix());
		//	RendererCommand::DrawIndexed(shader, list);
		//}
																 
	protected:
		Ref<Scenes::GraphicScene>									m_scene;
		Ref<Managers::TextureManager>								m_texture_manager;
		Ref<Managers::ShaderManager>								m_shader_manager;

		Ref<Storages::GraphicRendererStorage<float, unsigned int>>	m_graphic_storage;

	};
}