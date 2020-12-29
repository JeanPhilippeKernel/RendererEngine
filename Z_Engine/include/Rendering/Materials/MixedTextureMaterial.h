#pragma once
#include "ShaderMaterial.h"


namespace Z_Engine::Rendering::Materials {
	
	class MixedTextureMaterial : public ShaderMaterial {
	public:
		explicit MixedTextureMaterial();
		virtual ~MixedTextureMaterial() =  default;
	
		unsigned int GetHashCode() override;

		void Apply() override;

		void SetSecondTexture(Ref<Textures::Texture>& texture);
		void SetSecondTexture(Textures::Texture*  const texture);
		
		void SetInterpolateFactor(float value);

	private: 
		float					m_interpolate_factor;
		Ref<Textures::Texture>	m_second_texture;
	};
} 
