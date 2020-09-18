#pragma once
#include "IMaterial.h"

namespace Z_Engine::Rendering::Materials {
	
	class SimpleMaterial2D : public IMaterial {
	public:
		SimpleMaterial2D();
		~SimpleMaterial2D() = default;

		void UpdateUniforms() override;


	private:
		glm::vec4 m_texture_tint_color;
		int m_texture_tiling_factor;
	};
}