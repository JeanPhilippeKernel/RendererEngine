#pragma once
#include <array>
#include "../../../dependencies/glm/glm.hpp"

#include "IVertex.h"

namespace Z_Engine::Rendering::Renderers::Storages {

	class GraphicVertex : public IVertex {
	public:
		explicit GraphicVertex();
		explicit GraphicVertex(
			float index,
			const glm::vec3& position, 
			const glm::vec4& color, 
			float texture_slot_id = 0,  
			const glm::vec2& texture_coord = {0.0f, 0.0f}
		);
		
		
		~GraphicVertex() = default;

		glm::vec3 GetPosition()			const;
		glm::vec4 GetColor()			const;
		glm::vec2 GetTextureCoord()		const;
		
		float GetIndex()				const;
		float GetTextureSlotId()		const;
		

		void SetPosition(const glm::vec3& value);
		void SetColor(const glm::vec4& value);
		void SetTextureCoord(const glm::vec2& value);
		
		void SetIndex(float value); 
		void SetTextureSlotId(float value);
		
		void ApplyMatrixToPosition(const glm::mat4& matrix);

		const std::array<float, 11>& GetData() const { return m_buffer; }

	private:
		void _UpdateBuffer();

	private:
		std::array<float, 11> m_buffer;
	};
}
