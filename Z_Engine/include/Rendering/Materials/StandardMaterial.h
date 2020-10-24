#pragma once
#include "ShaderMaterial.h"


namespace Z_Engine::Rendering::Materials {
	
	class StandardMaterial : public ShaderMaterial {
	public:
		StandardMaterial();
		virtual ~StandardMaterial() =  default;
	
		virtual void SetAttributes() override;

		void SetTileFactor(float value);
		void SetTintColor(const glm::vec4& value);

	private: 
		float		m_tile_factor;
		glm::vec4	m_tint_color;

	};
} 
