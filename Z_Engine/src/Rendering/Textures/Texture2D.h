#pragma once

#include "Texture.h"

namespace Z_Engine::Rendering::Textures {

	class Texture2D : public Texture {
	public:
		Texture2D(const char * path);
		~Texture2D();

		void Bind(int slot = 0) override;
		void Unbind(int slot = 0) override;
	};
}
