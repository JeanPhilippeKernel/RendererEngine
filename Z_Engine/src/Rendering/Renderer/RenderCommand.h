#pragma once
#include <GL/glew.h>
#include <glm/glm.hpp>
#include <memory>

#include "../Buffer/VertexArray.h"
#include "../../Z_EngineDef.h"


using namespace Z_Engine::Rendering::Shaders;

namespace Z_Engine::Rendering::Renderer {
	
	class Z_ENGINE_API RendererCommand {
	public:
		RendererCommand() = delete;
		~RendererCommand() = delete;

		static void SetClearColor(glm::vec4 color) {
			glClearColor(color.r, color.g, color.b, color.a);
		}

		static void Clear() {
			glClear(GL_COLOR_BUFFER_BIT);
		}

		template<typename T, typename K>
		static void DrawIndexed(const std::shared_ptr<Buffer::VertexArray<T, K>>& vertex_array) {
			vertex_array->Bind();
			glDrawElements(GL_TRIANGLES, vertex_array->GetIndexBuffer()->GetDataSize(), GL_UNSIGNED_INT, 0);
		}

		template<typename T, typename K>
		static void DrawIndexed(const std::shared_ptr<Shader>& shader, const std::shared_ptr<Buffer::VertexArray<T, K>>& vertex_array) {
			shader->Bind();
			vertex_array->Bind();
			glDrawElements(GL_TRIANGLES, vertex_array->GetIndexBuffer()->GetDataSize(), GL_UNSIGNED_INT, 0);
		}

		template<typename T, typename K>
		static void DrawIndexed(
			const std::shared_ptr<Shader>& shader, 
			const std::vector<std::shared_ptr<Buffer::VertexArray<T, K>>>& vertex_array_list
		) {
			shader->Bind();
			for(const auto& vertex_array : vertex_array_list) {
				vertex_array->Bind();
				glDrawElements(GL_TRIANGLES, vertex_array->GetIndexBuffer()->GetDataSize(), GL_UNSIGNED_INT, 0);
			}
		}
	};
}
