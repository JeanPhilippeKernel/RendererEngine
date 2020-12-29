#pragma once
#include "ShaderMaterial.h"


namespace Z_Engine::Rendering::Materials {
	
	class StandardMaterial : public ShaderMaterial {
	public:
		explicit StandardMaterial();
		virtual ~StandardMaterial() =  default;
	
		unsigned int GetHashCode() override;

		void SetTileFactor(float value);
		void SetTintColor(const glm::vec4& value);

		void Apply() override;

	private: 
		float		m_tile_factor;
		glm::vec4	m_tint_color;
	};
} 
