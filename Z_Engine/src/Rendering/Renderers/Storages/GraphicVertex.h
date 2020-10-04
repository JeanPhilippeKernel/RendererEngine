#pragma once
#include <array>
#include <glm/glm.hpp>

#include "IVertex.h"

namespace Z_Engine::Rendering::Renderers::Storages {

	class GraphicVertex : public IVertex {
	public:
		explicit GraphicVertex();
		explicit GraphicVertex(const glm::vec3& position, const glm::vec4& color, const glm::vec2& texture_coord, float texture_id = 0);
		~GraphicVertex() = default;

		glm::vec3 GetPosition()		const;
		glm::vec4 GetColor()		const;
		glm::vec2 GetTextureCoord()	const;
		float GetTextureId()		const;


		void SetPosition(const glm::vec3& value);
		void SetColor(const glm::vec4& value);
		void SetTextureCoord(const glm::vec2& value);
		void SetTextureId(float value);

		const std::array<float, 10>& GetData() const { return m_buffer; }

	private:
		void _UpdateBuffer();

	private:
		std::array<float, 10> m_buffer;
	};
}
