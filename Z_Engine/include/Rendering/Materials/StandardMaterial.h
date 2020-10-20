#pragma once
#include "ShaderMaterial.h"


namespace Z_Engine::Rendering::Materials {
	
	class StandardMaterial : public ShaderMaterial {
	public:
		StandardMaterial();
		virtual ~StandardMaterial() =  default;

		virtual void SetAttributes() override;
	};
} 
