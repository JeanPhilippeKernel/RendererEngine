#pragma once

#include "Texture.h"

namespace Z_Engine::Rendering::Textures {

	class Texture2D : public Texture {
	public:
		Texture2D(const char * path);
		Texture2D(unsigned int width, unsigned int height);

		~Texture2D();

		void Bind(int slot = 0) const override;
		void Unbind(int slot = 0) const override;

		void SetImageData(const void * data) override;
		void SetImageData(float r, float g, float b, float a) override;
	};
}
