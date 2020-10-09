#pragma once
#include <GL/glew.h>
#include <glm/glm.hpp>
#include <memory>

#include "../Shaders/Shader.h"
#include "../Buffers/VertexArray.h"
#include "../../Z_EngineDef.h"

namespace Z_Engine::Rendering::Renderers {
	
	class RendererCommand {
	public:
		RendererCommand()						= delete;
		RendererCommand(const RendererCommand&) = delete;
		~RendererCommand()						= delete;

		static void SetClearColor(glm::vec4 color) {
			glClearColor(color.r, color.g, color.b, color.a);
		}

		static void SetViewport(int x, int y, int width, int height) {
			glViewport(x, y, width, height);
		}

		static void Clear() {
			glClear(GL_COLOR_BUFFER_BIT);
		}

		template<typename T, typename K>
		static void DrawIndexed(const Ref<Buffers::VertexArray<T, K>>& vertex_array) {
			vertex_array->Bind();
			glDrawElements(GL_TRIANGLES, vertex_array->GetIndexBuffer()->GetDataSize(), GL_UNSIGNED_INT, 0);
		}

		template<typename T, typename K>
		static void DrawIndexed(const Ref<Shaders::Shader>& shader, const Ref<Buffers::VertexArray<T, K>>& vertex_array) {
			shader->Bind();
			vertex_array->Bind();
			glDrawElements(GL_TRIANGLES, vertex_array->GetIndexBuffer()->GetDataSize(), GL_UNSIGNED_INT, 0);
		}

		template<typename T, typename K>
		static void DrawIndexed(
			const Ref<Shaders::Shader>& shader, 
			const std::vector<Ref<Buffers::VertexArray<T, K>>>& vertex_array_list
		) {
			shader->Bind();
			for(const auto& vertex_array : vertex_array_list) {
				vertex_array->Bind();
				glDrawElements(GL_TRIANGLES, vertex_array->GetIndexBuffer()->GetDataSize(), GL_UNSIGNED_INT, 0);
			}
		}
	};
}
