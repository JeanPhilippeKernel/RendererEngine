#pragma once
#include <vector>
#include <glm/glm.hpp>

#include "IVertex.h"

namespace Z_Engine::Rendering::Renderers::Storages {

	class GraphicVertex : public IVertex {
	public:
		GraphicVertex();
		GraphicVertex(const glm::vec3& position, const glm::vec4& color, const glm::vec3& texture);
		~GraphicVertex() = default;

		glm::vec3 GetPosition()	const;
		glm::vec4 GetColor()	const;
		glm::vec3 GetTexture()	const;


		void SetPosition(const glm::vec3& value);
		void SetColor(const glm::vec4& value);
		void SetTexture(const glm::vec3& value);

		const std::vector<float>& GetData() const {return m_buffer; }

	private:
		void _UpdateBuffer();

	private:
		std::vector<float> m_buffer;
	};
}
