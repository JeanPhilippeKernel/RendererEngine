#pragma once
#include <array>
#include "../../../dependencies/glm/glm.hpp"

#include "IVertex.h"

namespace Z_Engine::Rendering::Renderers::Storages {

	class GraphicVertex : public IVertex {
	public:
		explicit GraphicVertex();
		explicit GraphicVertex(
			const glm::vec3& position, const glm::vec4& color, 
			float texture_id = 0,  const glm::vec2& texture_coord = {0.0f, 0.0f}, 
			const glm::vec4& texture_tint_color = {1.0f, 1.0f, 1.0f, 1.0f}, float texture_tiling_factor = 1.0f
		);
		
		
		~GraphicVertex() = default;

		glm::vec3 GetPosition()			const;
		glm::vec4 GetColor()			const;
		
		float GetTextureId()			const;
		float GetTextureTilingFactor()	const;
		glm::vec2 GetTextureCoord()		const;
		glm::vec4 GetTextureTintColor() const;


		void SetPosition(const glm::vec3& value);
		void SetColor(const glm::vec4& value);
		
		void SetTextureId(float value);
		void SetTextureCoord(const glm::vec2& value);
		void SetTextureTilingFactor(float value);
		void SetTextureTintColor(const glm::vec4& value);


		void ApplyMatrixToPosition(const glm::mat4& matrix);

		const std::array<float, 15>& GetData() const { return m_buffer; }

	private:
		void _UpdateBuffer();

	private:
		std::array<float, 15> m_buffer;
	};
}
