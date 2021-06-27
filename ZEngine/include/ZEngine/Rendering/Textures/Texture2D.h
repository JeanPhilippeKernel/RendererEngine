#pragma once

#include <Rendering/Textures/Texture.h>

namespace ZEngine::Rendering::Textures {

	class Texture2D : public Texture {
	public:
		Texture2D(const char * path);
		Texture2D(unsigned int width, unsigned int height);

		~Texture2D();

		void Bind(int slot = 0) const override;
		void Unbind(int slot = 0) const override;

		void SetData(const void * data) override;
		void SetData(float r, float g, float b, float a) override;
	};
}
